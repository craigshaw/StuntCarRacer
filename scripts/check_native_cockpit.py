"""Verify original cockpit pixels, instrument events, wheel sprites and race traces."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from check_driving_replay import verify_replay_result
from import_native_assets import DEFAULT_PACK, ROOT, import_assets
from replay_lap import initial_lap_state, validate_laps


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
             ROOT / 'scripts/replay_lap.py',
             ROOT / 'tests/native_cockpit_instruments_tests.cpp',
             ROOT / 'tests/cockpit_timing_oracle.cpp', ROOT / 'tests/wheel_animation_oracle.cpp',
             ROOT / 'tests/native_cockpit_replay_oracle.cpp',
             ROOT / 'tests/native_startup_tests.cpp', ROOT / 'tests/driving_replay_oracle.cpp',
             ROOT / 'tests/native_opponent_driving_tests.cpp', ROOT / 'tests/driving_replay_native.cpp'}
    for directory in ('src', 'app'):
        paths.update((ROOT / directory).glob('*.cpp'))
        paths.update((ROOT / directory).glob('*.h'))
    paths.update((ROOT / 'tests').glob('*fixture.h'))
    paths.update((ROOT / 'tests').glob('driving_replay*.h'))
    return [identity(path) for path in sorted(paths)]


def run_json(command, destination, requires_comparison=True):
    run = subprocess.run([str(value) for value in command], cwd=ROOT,
                         capture_output=True, text=True)
    destination.with_suffix('.stdout.txt').write_text(run.stdout)
    destination.with_suffix('.stderr.txt').write_text(run.stderr)
    if run.returncode:
        raise RuntimeError('Cockpit verification failed: ' + ' '.join(str(x) for x in command)
                           + '\n' + run.stderr)
    result = json.loads(run.stdout)
    if requires_comparison and result.get('status') != 'passed' and result.get('mismatches') != 0:
        raise RuntimeError('Cockpit verification did not report success')
    if 'mismatches' in result and (type(result['mismatches']) is not int or result['mismatches'] != 0):
        raise RuntimeError('Cockpit verification reported invalid or nonzero mismatches')
    destination.write_text(json.dumps(result, indent=2) + '\n')
    return result


def compare_trace(actual, original):
    with actual.open(newline='') as file:
        native_rows = list(csv.DictReader(file))
    with original.open(newline='') as file:
        original_rows = list(csv.DictReader(file))
    if len(native_rows) != len(original_rows):
        raise RuntimeError('Cockpit gameplay trace lengths differ: ' + str(actual))
    for tick, (native, reference) in enumerate(zip(native_rows, original_rows)):
        for field, value in native.items():
            if field != 'original_instructions' and value != reference.get(field):
                raise RuntimeError(f'Cockpit gameplay trace differs at tick {tick}, field {field}: {actual}')
    return len(native_rows)


def positive(result, fields, name):
    for field in fields:
        if type(result.get(field)) is not int or result[field] <= 0:
            raise RuntimeError(name + ' did not exercise ' + field)


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
    output = ROOT / 'analysis/native-cockpit'
    output.mkdir(parents=True, exist_ok=True)
    build = output / 'build'
    names = ('native_cockpit_instruments_tests', 'wheel_animation_oracle',
             'cockpit_timing_oracle', 'native_cockpit_replay_oracle',
             'native_startup_tests', 'driving_replay_oracle',
             'native_opponent_driving_tests', 'driving_replay_native')
    executables = {name: build / 'Release' / (name + '.exe') for name in names}
    receipt = build / 'build-receipt.json'
    records, pack_record = sources(), identity(pack)
    payload = ROOT / 'analysis/reference/vesuri/offline-extracted-payload.bin'
    payload_record = identity(payload)
    if args.skip_build:
        previous = json.loads(receipt.read_text())
        if previous['sources'] != records or any(previous['executables'][name] != identity(path)
                                               for name, path in executables.items()):
            raise RuntimeError('Cockpit build is stale; rerun without --skip-build')
    else:
        commands = [['cmake', '-S', ROOT, '-B', build, '-DSCR_BUILD_ORACLE=ON',
                     '-DMUSASHI_DIR=' + str(ROOT / 'analysis/reference/musashi')],
                    ['cmake', '--build', build, '--config', 'Release', '--target',
                     *names, '--parallel', '4']]
        with (output / 'build.log').open('w') as log:
            for command in commands:
                subprocess.run([str(value) for value in command], cwd=ROOT, check=True,
                               stdout=log, stderr=subprocess.STDOUT)
        if sources() != records:
            raise RuntimeError('Cockpit sources changed during compilation; rerun after edits finish')
        receipt.write_text(json.dumps({'sources': records,
                                      'executables': {name: identity(path) for name, path in executables.items()}},
                                     indent=2) + '\n')
    executable_records = {name: identity(path) for name, path in executables.items()}
    integration_directory = output / 'integration'
    integration = run_json([executables['native_startup_tests'], pack, integration_directory],
                           output / 'integration-results.json')
    if integration.get('fresh_starts') != 8 or integration.get('ticks') != 4096:
        raise RuntimeError('Cockpit integration must cover all eight original tracks for 512 updates each')
    for field in ('capture_input', 'rom_input', 'cpu_emulation'):
        if integration.get(field) is not False:
            raise RuntimeError('Native cockpit integration unexpectedly requires ' + field)
    seeds = []
    for track in range(8):
        folder = integration_directory / ('track-' + str(track))
        seed = folder / 'seed.bin'
        shutil.copyfile(folder / 'native-initial.bin', seed)
        seeds.extend(['track-' + str(track), seed])
    shutil.copyfile(payload, integration_directory / 'source-payload.bin')
    opponents_directory = output / 'opponents'
    opponents = run_json([executables['native_opponent_driving_tests'], pack, opponents_directory],
                         output / 'opponent-integration-results.json')
    if len(opponents.get('tracks', [])) != 8 or {r.get('track') for r in opponents['tracks']} != set(range(8)):
        raise RuntimeError('Competitive cockpit coverage must include all eight tracks')
    if not opponents.get('idle_loss', {}).get('terminal') or opponents.get('natural_contact', {}).get('contact_ticks', 0) <= 0:
        raise RuntimeError('Competitive cockpit coverage must include an opponent finish and natural car contact')
    if opponents.get('midrace_opponent_state_injection') is not False:
        raise RuntimeError('Competitive cockpit scenarios must not inject midrace state')
    leaves = {}
    for name in ('native_cockpit_instruments_tests', 'wheel_animation_oracle', 'cockpit_timing_oracle'):
        arguments = [payload] if name == 'native_cockpit_instruments_tests' else [payload, *seeds]
        leaves[name] = run_json([executables[name], *arguments], output / (name + '-results.json'))
    pixels = leaves['native_cockpit_instruments_tests']
    if pixels.get('cases', 0) < 12784 or pixels.get('both_buffers_verified') is not True:
        raise RuntimeError('Instrument oracle must compare every declared case and both planar buffers')
    positive(pixels, ('compared_planar_and_state_bytes', 'original_instructions'), 'Instrument oracle')
    wheels = leaves['wheel_animation_oracle']
    if wheels.get('cases', 0) < 549888 or wheels.get('ordered_pointer_writes_compared') is not True:
        raise RuntimeError('Wheel oracle must cover exhaustive animation inputs and ordered sprite pointers')
    positive(wheels, ('sprite_position_cases', 'sprite_pixel_cases', 'pixels_compared',
                      'vblank_prefix_cases', 'carry_cases'), 'Wheel oracle')
    timing = leaves['cockpit_timing_oracle']
    if timing.get('capture_cases') != 8 or timing.get('held_lap_sequence_updates') != 28:
        raise RuntimeError('Timing oracle must cover every fresh seed and the entire completed-lap hold')
    positive(timing, ('hidden_hundredths_calls', 'visible_hundredths_calls', 'snapshots_before_slot_clear',
                      'held_result_transitions', 'leader_icon_calls', 'lap_counter_calls'), 'Timing oracle')
    # The retained capture is a regression fixture only. The sixteen fresh
    # Practice/competitive races above start from locally extracted assets.
    lap_inputs = ROOT / 'tests/replays/laps/authored-race-1463.csv'
    lap_provenance = lap_inputs.with_suffix('.provenance.json')
    provenance = json.loads(lap_provenance.read_text())
    capture_manifest_path = ROOT / 'analysis/captures/race/manifest.json'
    capture_manifest = json.loads(capture_manifest_path.read_text())
    capture_bank = next(bank for bank in capture_manifest['banks'] if bank['tag'] == 'CRAM')
    lap_seed = capture_manifest_path.parent / capture_bank['file']
    if (identity(lap_inputs)['sha256'] != provenance['input_sha256']
            or identity(lap_seed)['sha256'] != provenance['starting_capture']['chip_ram_sha256']
            or identity(lap_seed)['sha256'] != capture_bank['sha256']):
        raise RuntimeError('The authored complete-race regression no longer matches its provenance')
    scenarios = []
    for track in range(8):
        folder = integration_directory / ('track-' + str(track))
        scenarios.append({'name': 'practice-track-' + str(track), 'seed': folder / 'seed.bin',
                          'controls': folder / 'controls.csv', 'maximum': 512, 'preserve_hud': False})
    competitive_folders = [opponents_directory / ('track-' + str(track)) for track in range(8)]
    competitive_folders += [opponents_directory / 'idle-opponent-finish', opponents_directory / 'natural-car-contact']
    for folder in competitive_folders:
        with (folder / 'controls.csv').open(newline='') as file:
            count = sum(1 for _ in csv.DictReader(file))
        scenarios.append({'name': 'competitive-' + folder.name, 'seed': folder / 'seed.bin',
                          'controls': folder / 'controls.csv', 'maximum': count, 'preserve_hud': False})
    scenarios.append({'name': 'captured-three-lap-race', 'seed': lap_seed, 'controls': lap_inputs,
                      'maximum': 1463, 'preserve_hud': True})
    scenario_inputs = [(path, identity(path)) for scenario in scenarios for path in (scenario['seed'], scenario['controls'])]
    scenario_inputs += [(path, identity(path)) for path in (capture_manifest_path, lap_provenance)]
    manifest = output / 'cockpit-scenarios.tsv'
    lines = ['name\tseed\tcontrols\tmaximum\tpreserve_hud']
    for scenario in scenarios:
        fields = [scenario['name'], str(scenario['seed'].resolve()), str(scenario['controls'].resolve()),
                  str(scenario['maximum']), '1' if scenario['preserve_hud'] else '0']
        if any(any(c in field for c in '\t\r\n') for field in fields):
            raise RuntimeError('Cockpit scenario paths must not contain tabs or newlines')
        lines.append('\t'.join(fields))
    manifest.write_text('\n'.join(lines) + '\n')
    original_cockpit = run_json([executables['native_cockpit_replay_oracle'], '--manifest', manifest, payload],
                                output / 'cockpit-replay-original-results.json')
    expected_cockpit_updates = sum(scenario['maximum'] for scenario in scenarios)
    if original_cockpit.get('ticks') != expected_cockpit_updates:
        raise RuntimeError('Original cockpit replay omitted updates from a declared scenario')
    fresh_hud_scenarios = sum(not scenario['preserve_hud'] for scenario in scenarios)
    if (original_cockpit.get('startup_hud_seeds') != fresh_hud_scenarios
            or original_cockpit.get('startup_hud_pixels') != fresh_hud_scenarios * 320 * 32 * 2):
        raise RuntimeError('Fresh native startup HUDs were not compared against original initialization')
    actual_scenarios = original_cockpit.get('scenarios', [])
    if len(actual_scenarios) != len(scenarios):
        raise RuntimeError('Original cockpit replay omitted a declared scenario')
    for expected, actual in zip(scenarios, actual_scenarios):
        if (actual.get('name') != expected['name'] or actual.get('ticks') != expected['maximum']
                or actual.get('preserve_hud') is not expected['preserve_hud']):
            raise RuntimeError('Original cockpit scenario differs from its manifest: ' + expected['name'])
    by_name = {scenario['name']: scenario for scenario in actual_scenarios}
    complete_race = by_name['captured-three-lap-race']
    if complete_race.get('results_calls', 0) < 3 or complete_race.get('lap_counter_calls') != 4:
        raise RuntimeError('Complete-race cockpit must show all four crossings and completed-lap results')
    if by_name['competitive-idle-opponent-finish'].get('results_calls', 0) <= 0:
        raise RuntimeError('Opponent finish did not exercise cockpit result drawing')
    if by_name['competitive-natural-car-contact'].get('fuel_calls', 0) <= 0:
        raise RuntimeError('Natural-contact controls did not exercise boost-fuel instrument updates')
    if (original_cockpit.get('hud_pixels') != expected_cockpit_updates * 320 * 32 * 2
            or original_cockpit.get('sprite_placements') != expected_cockpit_updates * 12):
        raise RuntimeError('Cockpit replay must compare both entire instrument strips and all sprite placements each update')
    positive(original_cockpit, ('hud_pixels', 'wheel_timeline_states', 'sprite_placements',
                                'original_vblank_prefix_calls', 'speed_calls', 'distance_calls',
                                'timer_calls', 'result_calls', 'fuel_calls', 'lap_counter_calls'),
             'Original cockpit replay')
    for field, expected in (('original_drawing_calls_replaced', False),
                            ('original_code_modified', False),
                            ('independent_evolving_native_scene', True)):
        if original_cockpit.get(field) is not expected:
            raise RuntimeError('Original cockpit replay has an invalid ' + field)
    original_races = []
    for track in range(8):
        folder = integration_directory / ('track-' + str(track))
        checks = run_json([executables['driving_replay_oracle'], folder / 'seed.bin',
                           folder / 'controls.csv', folder / 'original.csv', '--full-loop'],
                          folder / 'original-results.json')
        ticks = compare_trace(folder / 'native.csv', folder / 'original.csv')
        verify_replay_result(checks, ticks, full_loop=True)
        original_races.append({'track': track, 'ticks': ticks, 'results': checks,
                               'seed': identity(folder / 'seed.bin'),
                               'controls': identity(folder / 'controls.csv'),
                               'native': identity(folder / 'native.csv'),
                               'original': identity(folder / 'original.csv')})
    for folder in competitive_folders:
        checks = run_json([executables['driving_replay_oracle'], folder / 'seed.bin',
                           folder / 'controls.csv', folder / 'original.csv', '--full-loop'],
                          folder / 'original-results.json')
        ticks = compare_trace(folder / 'native.csv', folder / 'original.csv')
        compare_trace(folder / 'scene.csv', folder / 'original.csv')
        verify_replay_result(checks, ticks, full_loop=True)
        original_races.append({'scenario': 'competitive-' + folder.name, 'ticks': ticks, 'results': checks,
                               'seed': identity(folder / 'seed.bin'), 'controls': identity(folder / 'controls.csv'),
                               'native': identity(folder / 'native.csv'), 'scene': identity(folder / 'scene.csv'),
                               'original': identity(folder / 'original.csv')})
    lap_directory = output / 'captured-three-lap-race'
    lap_directory.mkdir(exist_ok=True)
    lap_original = lap_directory / 'original.csv'
    lap_native = lap_directory / 'native.csv'
    lap_checks = run_json([executables['driving_replay_oracle'], lap_seed, lap_inputs, lap_original, '--full-loop'],
                          lap_directory / 'original-results.json')
    verify_replay_result(lap_checks, 1463, full_loop=True)
    lap_native_checks = run_json([executables['driving_replay_native'], lap_seed, lap_inputs, lap_native, '--full-loop'],
                                 lap_directory / 'native-results.json', requires_comparison=False)
    if lap_native_checks.get('cpu_emulation') is not False or lap_native_checks.get('ticks') != 1463:
        raise RuntimeError('Complete-race native regression did not independently replay all updates')
    if compare_trace(lap_native, lap_original) != 1463:
        raise RuntimeError('Complete-race trace is incomplete')
    lap_validation = validate_laps(lap_original, initial_lap_state(lap_seed.read_bytes()), require_complete_lap=True)
    if lap_validation.get('complete_laps') != 3:
        raise RuntimeError('Cockpit regression must complete all three observed laps')
    captured_regression = {'seed': identity(lap_seed), 'inputs': identity(lap_inputs),
                           'capture_manifest': identity(capture_manifest_path),
                           'input_provenance': identity(lap_provenance), 'original': identity(lap_original),
                           'native': identity(lap_native), 'results': lap_checks,
                           'native_results': lap_native_checks, 'lap_validation': lap_validation,
                           'preserve_existing_hud': True}
    if sources() != records or identity(pack) != pack_record or identity(payload) != payload_record:
        raise RuntimeError('Source or original asset identity changed during cockpit verification')
    if any(identity(executables[name]) != record for name, record in executable_records.items()):
        raise RuntimeError('A cockpit verification executable changed during checking')
    if any(identity(path) != record for path, record in scenario_inputs):
        raise RuntimeError('A cockpit scenario seed or input changed during checking')
    report = {'status': 'passed', 'pack': pack_record, 'payload': payload_record,
              'sources': records, 'executables': executable_records,
              'integration': integration, 'opponent_integration': opponents, 'component_oracles': leaves,
              'original_cockpit': original_cockpit, 'scenario_manifest': identity(manifest),
              'scenarios': [{'name': s['name'], 'seed': identity(s['seed']), 'controls': identity(s['controls']),
                             'updates': s['maximum'], 'preserve_existing_hud': s['preserve_hud']} for s in scenarios],
              'original_races': original_races, 'captured_complete_race_regression': captured_regression,
              'fresh_race_capture_input': False, 'captured_regression_fixture': True,
              'rom_input': False, 'runtime_cpu_emulation': False,
              'test_only_original_cpu_oracle': True}
    destination = output / 'check-results.json'
    destination.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'tracks': 8, 'scenarios': len(scenarios), 'ticks': expected_cockpit_updates,
                      'instrument_cases': pixels['cases'], 'wheel_cases': wheels['cases'],
                      'original_cockpit_updates': original_cockpit['ticks'],
                      'report': str(destination)}))


if __name__ == '__main__':
    main()
