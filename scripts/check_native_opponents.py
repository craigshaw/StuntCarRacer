"""Verify native local opponents, their rendering and complete original race loops."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

from check_driving_replay import verify_replay_result
from import_native_assets import DEFAULT_PACK, ROOT, import_assets


def identity(path):
    path = Path(path).resolve()
    try:
        name = str(path.relative_to(ROOT))
    except ValueError:
        name = str(path)
    return {'path': name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def sources():
    paths = {ROOT / 'CMakeLists.txt', Path(__file__).resolve(),
             ROOT / 'scripts/check_driving_replay.py', ROOT / 'scripts/import_native_assets.py',
             ROOT / 'tests/native_opponent_driving_tests.cpp',
             ROOT / 'tests/native_opponent_session_tests.cpp',
             ROOT / 'tests/race_transitions_oracle.cpp',
             ROOT / 'tests/driving_replay_oracle.cpp', ROOT / 'tests/native_opponent_race_tests.cpp',
             ROOT / 'tests/native_competition_render_tests.cpp'}
    for directory in ('src', 'app'):
        paths.update((ROOT / directory).glob('*.cpp'))
        paths.update((ROOT / directory).glob('*.h'))
    paths.update((ROOT / 'tests').glob('*fixture.h'))
    paths.update((ROOT / 'tests').glob('driving_replay*.h'))
    paths.update((ROOT / 'tests').glob('opponent_*_oracle.cpp'))
    return [identity(path) for path in sorted(paths)]


def run_json(command, destination):
    execution = subprocess.run([str(value) for value in command], cwd=ROOT,
                               capture_output=True, text=True)
    destination.with_suffix('.stdout.txt').write_text(execution.stdout)
    destination.with_suffix('.stderr.txt').write_text(execution.stderr)
    if execution.returncode:
        raise RuntimeError('Opponent verification failed: ' + ' '.join(str(x) for x in command)
                           + '\n' + execution.stderr)
    result = json.loads(execution.stdout)
    if result.get('status') != 'passed' and result.get('mismatches') != 0:
        raise RuntimeError('Opponent verification did not report success')
    if 'mismatches' in result and (type(result['mismatches']) is not int or result['mismatches'] != 0):
        raise RuntimeError('Opponent verification reported nonzero or invalid mismatches')
    destination.write_text(json.dumps(result, indent=2) + '\n')
    return result


def compare_trace(actual, expected):
    with actual.open(newline='') as file:
        native = list(csv.DictReader(file))
    with expected.open(newline='') as file:
        original = list(csv.DictReader(file))
    if len(native) != len(original):
        raise RuntimeError('Opponent trace lengths differ: ' + str(actual))
    for tick, (row, reference) in enumerate(zip(native, original)):
        for key, value in row.items():
            if key != 'original_instructions' and value != reference.get(key):
                raise RuntimeError(f'Opponent trace differs at tick{tick}, field{key}: {actual}')
    return len(native)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, default=DEFAULT_PACK)
    parser.add_argument('--adf', type=Path)
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    pack = args.pack.resolve()
    if args.adf:
        import_assets(args.adf, pack)
    elif not pack.exists():
        import_assets(ROOT / 'Stunt Car Racer (MicroStyle).adf', pack)
    output = ROOT / 'analysis/native-opponents'
    output.mkdir(parents=True, exist_ok=True)
    build = output / 'build'
    component_names = ('opponent_driving_oracle', 'opponent_car_oracle',
                       'opponent_painter_oracle', 'opponent_far_track_oracle',
                       'native_opponent_race_tests', 'native_competition_render_tests')
    names = ('native_opponent_driving_tests', *component_names,
             'driving_replay_oracle', 'opponent_scene_oracle', 'race_transitions_oracle')
    executables = {name: build / 'Release' / (name + '.exe') for name in names}
    receipt = build / 'build-receipt.json'
    records, pack_record = sources(), identity(pack)
    if args.skip_build:
        previous = json.loads(receipt.read_text())
        if previous['sources'] != records or any(previous['executables'][name] != identity(path)
                                               for name, path in executables.items()):
            raise RuntimeError('Opponent build is stale; rerun without --skip-build')
    else:
        commands = [['cmake', '-S', ROOT, '-B', build, '-DSCR_BUILD_ORACLE=ON',
                     '-DMUSASHI_DIR=' + str(ROOT / 'analysis/reference/musashi')],
                    ['cmake', '--build', build, '--config', 'Release', '--target',
                     *names, '--parallel', '4']]
        with (output / 'build.log').open('w') as log:
            for command in commands:
                subprocess.run([str(value) for value in command], cwd=ROOT, check=True,
                               stdout=log, stderr=subprocess.STDOUT)
        receipt.write_text(json.dumps({'sources': records,
                                      'executables': {name: identity(path) for name, path in executables.items()}},
                                     indent=2) + '\n')
    executable_records = {name: identity(path) for name, path in executables.items()}
    integration = run_json([executables['native_opponent_driving_tests'], pack, output / 'integration'],
                           output / 'integration-results.json')
    tracks = integration.get('tracks', [])
    if len(tracks) != 8 or {track.get('track') for track in tracks} != set(range(8)):
        raise RuntimeError('Opponent race coverage must include every original track')
    if any(track.get('moving_ticks', 0) <= 100 or track.get('ticks', 0) <= 0 or track.get('visible_frames', 0) <= 0 for track in tracks):
        raise RuntimeError('Opponent race did not advance its AI')
    idle = integration.get('idle_loss', {})
    if not idle.get('terminal') or idle.get('opponent_lap_counter') != 4:
        raise RuntimeError('The unassisted opponent did not complete its race')
    if integration.get('natural_contact', {}).get('contact_ticks', 0) <= 0:
        raise RuntimeError('Authored digital controls did not exercise natural car contact')
    if integration.get('midrace_opponent_state_injection') is not False:
        raise RuntimeError('Opponent race check must not inject midrace state')
    payload = ROOT / 'analysis/reference/vesuri/offline-extracted-payload.bin'
    leaves = {}
    for name in component_names:
        argument = payload if name in ('native_opponent_race_tests', 'native_competition_render_tests') else pack
        leaves[name] = run_json([executables[name], argument], output / (name + '-results.json'))
    transitions = run_json([executables['race_transitions_oracle'], payload,
                            output / 'integration/track-0/seed.bin'],
                           output / 'race-transitions-original-results.json')
    if transitions.get('total_cases') != 91598 or transitions.get('mismatches') != 0:
        raise RuntimeError('Competitive race exits must pass all91598 original transition cases')
    leaves['race_transitions_oracle'] = transitions
    original_scene = run_json([executables['opponent_scene_oracle'], output / 'integration', '512'],
                              output / 'opponent-scene-original-results.json')
    if original_scene.get('ticks') != 4096 or original_scene.get('command_frames') != 4096:
        raise RuntimeError('Original opponent scene comparison must cover all4096 track frames')
    for field in ('car_calls', 'car_commands', 'line_allocations'):
        if type(original_scene.get(field)) is not int or original_scene[field] <= 0:
            raise RuntimeError('Original opponent scene must exercise ' + field)
    if (original_scene.get('original_renderer_calls_replaced') is not False
            or original_scene.get('original_code_modified') is not False
            or original_scene.get('independent_evolving_native_scene') is not True):
        raise RuntimeError('Opponent scene must execute the unchanged original renderer against independent native state')
    comparisons = []
    scenarios = [output / 'integration' / ('track-' + str(track)) for track in range(8)]
    scenarios.append(output / 'integration/idle-opponent-finish')
    scenarios.append(output / 'integration/natural-car-contact')
    for folder in scenarios:
        checks = run_json([executables['driving_replay_oracle'], folder / 'seed.bin',
                           folder / 'controls.csv', folder / 'original.csv', '--full-loop'],
                          folder / 'original-results.json')
        ticks = compare_trace(folder / 'native.csv', folder / 'original.csv')
        compare_trace(folder / 'scene.csv', folder / 'original.csv')
        verify_replay_result(checks, ticks, full_loop=True)
        comparisons.append({'scenario': str(folder.relative_to(output)), 'ticks': ticks,
                            'results': checks, 'seed': identity(folder / 'seed.bin'),
                            'controls': identity(folder / 'controls.csv'),
                            'native': identity(folder / 'native.csv'),
                            'scene': identity(folder / 'scene.csv'),
                            'original': identity(folder / 'original.csv')})
    if sources() != records or identity(pack) != pack_record:
        raise RuntimeError('Source or asset pack changed during opponent verification')
    if any(identity(executables[name]) != record for name, record in executable_records.items()):
        raise RuntimeError('An opponent verification executable changed during checking')
    report = {'status': 'passed', 'pack': pack_record, 'sources': records,
              'executables': executable_records, 'integration': integration,
              'component_oracles': leaves, 'original_scene': original_scene, 'original_races': comparisons,
              'capture_input': False, 'rom_input': False,
              'runtime_cpu_emulation': False, 'test_only_original_cpu_oracle': True}
    destination = output / 'check-results.json'
    destination.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'ticks': integration['ticks'],
                      'tracks': 8, 'idle_opponent_finish': True, 'original_races': len(comparisons),
                      'original_scene_frames': original_scene['command_frames'],
                      'report': str(destination)}))


if __name__ == '__main__':
    main()
