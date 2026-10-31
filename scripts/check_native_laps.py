"""Verify in-memory personal-best recording, lap timing and session isolation."""
import argparse
import json
from pathlib import Path
import subprocess
import time

from native_build import ROOT, NATIVE_BUILD, ORACLE_BUILD
from check_native_ghost import identity, sources


def source_records():
    return sorted([*sources(), identity(Path(__file__))], key=lambda item: item['path'])


def run_json(command, output):
    run = subprocess.run(list(map(str, command)), cwd=ROOT, capture_output=True, text=True)
    output.with_suffix('.stdout.txt').write_text(run.stdout)
    output.with_suffix('.stderr.txt').write_text(run.stderr)
    run.check_returncode()
    result = json.loads(run.stdout)
    if result.get('mismatches') != 0:
        raise RuntimeError('Original-code lap timing check did not report zero mismatches')
    output.write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/lap-recording')
    parser.add_argument('--verify-original', action='store_true')
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    assets = ROOT / 'analysis/assets/stunt-car-racer.scr-assets'
    chip = ROOT / 'analysis/captures/race/chip-000000.bin'
    payload = ROOT / 'analysis/reference/vesuri/offline-extracted-payload.bin'
    controls = ROOT / 'tests/replays/laps/authored-race-1463.csv'
    reference = ROOT / 'analysis/native-cockpit/captured-three-lap-race/original.csv'
    fixture_paths = (assets, chip, payload, controls, reference)
    fixtures = [identity(path) for path in fixture_paths]
    before = source_records()
    targets = ('lap_recording_tests', 'native_lap_recording_tests', 'native_lap_timer_tests',
               'native_career_session_tests', 'native_multiplayer_session_tests', 'native_autosave_tests',
               'presentation_interpolation_tests', 'ghost_bridge_tests', 'native_opponent_driving_tests',
               'native_ghost_proof', 'native_ghost_bridge_proof', 'native_game')
    executables = {name: NATIVE_BUILD / 'Release' / ('Stunt Car Racer.exe' if name == 'native_game' else name + '.exe')
                   for name in targets}
    oracle_targets = ('race_timing_oracle', 'driving_replay_oracle') if args.verify_original else ()
    executables.update({name: ORACLE_BUILD / 'Release' / (name + '.exe') for name in oracle_targets})
    receipt = output / 'build-receipt.json'
    if args.skip_build:
        previous = json.loads(receipt.read_text())
        if previous['sources'] != before or previous['executables'] != {name: identity(path) for name, path in executables.items()}:
            raise RuntimeError('Lap recording build is stale; rerun without --skip-build')
    else:
        commands = [['cmake', '--preset', 'windows-release'],
                    ['cmake', '--build', '--preset', 'windows-release', '--target', *targets, '--parallel', '1', '--', '/nr:false']]
        if oracle_targets:
            commands += [['cmake', '--preset', 'windows-oracle'],
                         ['cmake', '--build', '--preset', 'windows-oracle', '--target', *oracle_targets, '--parallel', '1', '--', '/nr:false']]
        with (output / 'build.log').open('w') as log:
            for command in commands:
                subprocess.run(command, cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT)
        receipt.write_text(json.dumps({'sources': before, 'executables': {name: identity(path) for name, path in executables.items()}}, indent=2) + '\n')
    started = time.perf_counter()
    with (output / 'ctest.log').open('w') as log:
        subprocess.run(['ctest', '--preset', 'windows-release', '-R', '^(' + '|'.join(targets[:-1]) + ')$'],
                       cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT)
    checks = json.loads((NATIVE_BUILD / 'test-output/Release/native_lap_recording_tests/results.json').read_text())
    if not checks['captured_regression_run'] or checks['earned_completed_laps'] != 3 or not checks['authored_competitive_completed_laps']:
        raise RuntimeError('Lap recording verification omitted complete Practice or competitive laps')
    originals = {}
    if args.verify_original:
        originals['timing'] = run_json([executables['race_timing_oracle'], payload, 'lap-recording', chip], output / 'timing-original.json')
        originals['complete_race'] = run_json([executables['driving_replay_oracle'], chip, controls,
                                               output / 'original.csv', '--full-loop'], output / 'race-original.json')
    if source_records() != before or [identity(path) for path in fixture_paths] != fixtures:
        raise RuntimeError('Lap recording sources or fixtures changed during verification')
    result = {'status': 'passed', 'scope': 'in-memory lap recording, ranking and Practice playback timeline',
              'durable_ghost_storage': False, 'player_ghost_controls': False,
              'seconds': time.perf_counter() - started, 'sources': before, 'fixtures': fixtures,
              'executables': {name: identity(path) for name, path in executables.items()},
              'checks': checks, 'original_cpu_executed': args.verify_original, 'original_comparisons': originals}
    (output / 'check-results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'report': str(output / 'check-results.json'), 'checks': checks}, indent=2))


if __name__ == '__main__':
    main()
