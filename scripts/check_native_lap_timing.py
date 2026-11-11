"""Verify independent dual timing capture and recovery accounting (stage 2)."""
import argparse
import json
from pathlib import Path
import subprocess

from native_build import ROOT, NATIVE_BUILD
from check_native_ghost import identity
from check_native_timing import source_records as crossing_sources


def source_records():
    return sorted([*crossing_sources(), identity(Path(__file__))], key=lambda item: item['path'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', choices=('Release', 'Debug'), default='Release')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    preset = 'windows-debug' if args.configuration == 'Debug' else 'windows-release'
    default = ROOT / 'analysis/lap-timing'
    if args.configuration == 'Debug':
        default /= 'debug'
    output = (args.output or default).resolve()
    output.mkdir(parents=True, exist_ok=True)
    paths = [ROOT / path for path in ('analysis/assets/stunt-car-racer.scr-assets',
             'analysis/captures/race/chip-000000.bin', 'analysis/reference/vesuri/offline-extracted-payload.bin',
             'tests/replays/laps/authored-race-1463.csv', 'tests/replays/laps/authored-race-1463.provenance.json')]
    fixtures = [identity(path) for path in paths]
    before = source_records()
    targets = ('lap_timing_tests', 'native_lap_timing_tests', 'crossing_estimation_tests', 'native_game')
    binaries = {name: NATIVE_BUILD / args.configuration / ('Stunt Car Racer.exe' if name == 'native_game' else name + '.exe') for name in targets}
    receipt = output / 'build-receipt.json'
    if args.skip_build:
        previous = json.loads(receipt.read_text())
        if previous['sources'] != before or previous['executables'] != {name: identity(path) for name, path in binaries.items()}:
            raise RuntimeError('Lap timing build is stale; rerun without --skip-build')
    else:
        commands = [['cmake', '--preset', preset],
                    ['cmake', '--build', '--preset', preset, '--target', *targets, '--parallel', '1', '--', '/nr:false']]
        with (output / 'build.log').open('w') as log:
            for command in commands:
                subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        receipt.write_text(json.dumps({'sources': before, 'executables': {name: identity(path) for name, path in binaries.items()}}, indent=2) + '\n')
    with (output / 'ctest.log').open('w') as log:
        subprocess.run(['ctest', '--preset', preset, '-V', '-R', '^(lap_timing_tests|native_lap_timing_tests|crossing_estimation_tests)$'],
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    results = NATIVE_BUILD / 'test-output' / args.configuration / 'native_lap_timing_tests/results.json'
    checks = json.loads(results.read_text())
    if (checks['status'] != 'passed' or checks['fresh_contexts'] != 16 or checks['captured_completions'] != 12
            or not checks['full_memory_and_rng_isolation'] or not checks['recording_independent']):
        raise RuntimeError('Lap timing acceptance omitted required native or replay coverage')
    if before != source_records() or fixtures != [identity(path) for path in paths]:
        raise RuntimeError('Lap timing sources or fixtures changed during verification')
    executables = {name: identity(path) for name, path in binaries.items()}
    if executables != json.loads(receipt.read_text())['executables']:
        raise RuntimeError('Lap timing executable changed during verification')
    report = {'status': 'passed', 'stage': 2, 'configuration': args.configuration,
              'scope': 'independent live timing, dual completion metadata, confidence and explicit recovery penalties; persistence and UI pending',
              'sources': before, 'fixtures': fixtures, 'executables': executables, 'checks': checks,
              'original_cpu_executed': False, 'artifacts': [identity(results), identity(output / 'ctest.log')]}
    (output / 'check-results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'report': str(output / 'check-results.json'), 'checks': checks}, indent=2))


if __name__ == '__main__':
    main()
