"""Build/check native Practice startup using only a local ADF data pack."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

from native_build import NATIVE_BUILD, ORACLE_BUILD, build_sources, configure_command
from import_native_assets import DEFAULT_PACK, ROOT, import_assets


def identity(path):
    return {'path': str(path.relative_to(ROOT)), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def sources():
    paths = {ROOT / 'CMakeLists.txt', Path(__file__).resolve(), ROOT / 'scripts/import_native_assets.py',
             ROOT / 'tests/integration/native_startup_tests.cpp'}
    for directory in ('src', 'app', 'tools/replay'):
        paths.update((ROOT / directory).rglob('*.cpp'))
        paths.update((ROOT / directory).rglob('*.h'))
    paths.update((ROOT / 'tests/support').glob('*fixture.h'))
    paths.update((ROOT / 'tests/support').glob('driving_replay*.h'))
    paths.update(build_sources())
    return [identity(path) for path in sorted(paths)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, default=DEFAULT_PACK)
    parser.add_argument('--adf', type=Path, help='Import this original disk before checking')
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--verify-original', action='store_true', help='Also run test-only68000 comparisons from every fresh seed')
    args = parser.parse_args()
    pack = args.pack.resolve()
    if args.adf:
        import_assets(args.adf, pack)
    elif not pack.exists():
        import_assets(ROOT / 'Stunt Car Racer (MicroStyle).adf', pack)
    output = ROOT / 'analysis/native-startup';output.mkdir(parents=True, exist_ok=True)
    build = NATIVE_BUILD;receipt_path = output / 'build-receipt.json'
    viewer = build / 'Release/native_replay_viewer.exe';test = build / 'Release/native_startup_tests.exe'
    records = sources()
    if args.skip_build:
        receipt = json.loads(receipt_path.read_text())
        if receipt['sources'] != records or receipt['viewer'] != identity(viewer) or receipt['test'] != identity(test):
            raise RuntimeError('Native startup build is stale; rerun without --skip-build')
    else:
        commands = [configure_command(oracle=False),
                    ['cmake', '--build', str(build), '--config', 'Release', '--target',
                     'native_replay_viewer', 'native_startup_tests', '--parallel', '4']]
        with (output / 'build.log').open('w') as log:
            for command in commands:
                subprocess.run(command, check=True, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        receipt_path.write_text(json.dumps({'sources': records, 'viewer': identity(viewer), 'test': identity(test)}, indent=2) + '\n')
    run = subprocess.run([str(test), str(pack), str(output / 'integration')], cwd=ROOT, capture_output=True, text=True)
    (output / 'integration-stdout.txt').write_text(run.stdout)
    (output / 'integration-stderr.txt').write_text(run.stderr)
    if run.returncode:
        raise RuntimeError('Fresh startup integration failed: ' + run.stderr)
    result = json.loads(run.stdout)
    if result.get('status') != 'passed' or result.get('capture_input') is not False or result.get('ticks') != 4096 or result.get('left_right_steering_checked') is not True:
        raise RuntimeError('Fresh startup coverage differs from the eight-track regression')
    if sources() != records:
        raise RuntimeError('Sources changed while native startup was being checked')
    original_results = []
    if args.verify_original:
        oracle_build = ORACLE_BUILD
        commands = [configure_command(oracle=True),
                    ['cmake', '--build', str(oracle_build), '--config', 'Release', '--target', 'driving_replay_oracle', '--parallel', '4']]
        with (output / 'oracle-build.log').open('w') as log:
            for command in commands:
                subprocess.run(command, check=True, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        oracle = oracle_build / 'Release/driving_replay_oracle.exe'
        for track in range(8):
            directory = output / 'integration' / ('track-' + str(track))
            seed, controls, trace = directory / 'native-initial.bin', directory / 'controls.csv', directory / 'original.csv'
            execution = subprocess.run([str(oracle), str(seed), str(controls), str(trace), '--full-loop'], cwd=ROOT, check=True, capture_output=True, text=True)
            (directory / 'original-result.json').write_text(execution.stdout)
            checks = json.loads(execution.stdout)
            if checks['mismatches'] != 0 or checks['ticks'] != 512 or checks['original_code_patched_by_harness'] or checks['native_state_reseeded']:
                raise RuntimeError('Fresh original-code replay failed')
            with trace.open(newline='') as f:
                expected = list(csv.DictReader(f))
            with (directory / 'native.csv').open(newline='') as f:
                actual = list(csv.DictReader(f))
            if len(expected) != len(actual) or any(any(row[k] != target[k] for k in row if k != 'original_instructions') for row, target in zip(actual, expected)):
                raise RuntimeError('Fresh scene trace differs from original execution on track' + str(track))
            original_results.append({'track': track, 'ticks': checks['ticks'], 'mismatches': checks['mismatches'],
                'compared_bytes': checks['compared_bytes'], 'restarts': checks['restarts'], 'audio_register_writes': checks['audio_register_writes'],
                'original_instructions': checks['original_instructions'], 'seed': identity(seed), 'controls': identity(controls), 'trace': identity(trace),
                'native_trace': identity(directory / 'native.csv'), 'oracle': identity(oracle)})
    report = {'status': 'passed', 'capture_input': False, 'rom_input': False, 'cpu_emulation': False,
              'pack': identity(pack), 'viewer': identity(viewer), 'test': identity(test),
              'sources': records, 'results': result, 'original_comparisons': original_results}
    (output / 'check-results.json').write_text(json.dumps(report, indent=2) + '\n')
    if args.verify_original:
        (output / 'original-check-results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': 'passed', **result, 'report': str(output / 'check-results.json')}))


if __name__ == '__main__':
    main()
