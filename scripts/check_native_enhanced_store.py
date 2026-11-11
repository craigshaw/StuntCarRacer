"""Verify separate Enhanced PB/Hall models, codecs and durable storage (stage 3)."""
import argparse
import json
from pathlib import Path
import subprocess
import time

from native_build import ROOT, NATIVE_BUILD
from check_native_ghost import identity, sources


def source_records():
    return sorted([*sources(), identity(Path(__file__)), identity(ROOT / 'scripts/check_native_hall_store.py')], key=lambda item: item['path'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', choices=('Release', 'Debug'), default='Release')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    preset = 'windows-debug' if args.configuration == 'Debug' else 'windows-release'
    default = ROOT / 'analysis/enhanced-records'
    if args.configuration == 'Debug':
        default /= 'debug'
    output = (args.output or default).resolve()
    output.mkdir(parents=True, exist_ok=True)
    tests = ['enhanced_records_tests', 'enhanced_store_tests', 'hall_of_fame_tests', 'hall_table_codec_tests',
             'hall_ghost_codec_tests', 'shared_hall_storage_tests', 'shared_hall_store_tests',
             'ghost_lap_codec_tests', 'lap_recording_tests', 'lap_timing_tests', 'native_ghost_store_tests']
    fixtures = []
    pack = ROOT / 'analysis/assets/stunt-car-racer.scr-assets'
    if pack.is_file():
        tests.append('native_autosave_tests')
        fixtures.append(identity(pack))
        alternate = ROOT / 'analysis/assets/stunt-car-racer-woodpecker.scr-assets'
        if alternate.is_file():
            fixtures.append(identity(alternate))
    targets = [*tests, 'native_enhanced_store_proof', 'native_hall_store_proof', 'native_game']
    before = source_records()
    executables = {name: NATIVE_BUILD / args.configuration / ('Stunt Car Racer.exe' if name == 'native_game' else name + '.exe') for name in targets}
    receipt = output / 'build-receipt.json'
    if args.skip_build:
        previous = json.loads(receipt.read_text())
        if previous['sources'] != before or previous['executables'] != {name: identity(path) for name, path in executables.items()}:
            raise RuntimeError('Enhanced store build is stale; rerun without --skip-build')
    else:
        with (output / 'build.log').open('w') as log:
            for command in (['cmake', '--preset', preset],
                            ['cmake', '--build', '--preset', preset, '--target', *targets, '--parallel', '1', '--', '/nr:false']):
                subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        receipt.write_text(json.dumps({'sources': before, 'executables': {name: identity(path) for name, path in executables.items()}}, indent=2) + '\n')
    with (output / 'ctest.log').open('w') as log:
        subprocess.run(['ctest', '--preset', preset, '-V', '-R', '^(' + '|'.join(tests) + ')$'], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    checks = NATIVE_BUILD / 'test-output' / args.configuration / 'enhanced_store_tests/results.json'
    storage = json.loads(checks.read_text())
    if (storage['status'] != 'passed' or not storage['timing_namespaces_isolated'] or not storage['private_recovery_and_ties']
            or storage['shared_recovery_points'] != 7 or storage['concurrent_writers'] != 2):
        raise RuntimeError('Enhanced storage acceptance omitted required cases')
    proofs = {}
    for label, target in (('enhanced', 'native_enhanced_store_proof'), ('original', 'native_hall_store_proof')):
        directory = output / (label + '-process-' + str(time.time_ns()))
        with (output / (label + '-process.log')).open('w') as log:
            subprocess.run([str(executables[target]), '--proof', str(directory)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        proof = json.loads((directory / 'results.json').read_text())
        if (proof['status'] != 'passed' or proof['entries'] != 20 or proof['lost_updates'] or proof['retry_duplicates']
                or proof['process_crash_points'] != 7 or not proof['all_references_usable']):
            raise RuntimeError(label + ' process-interruption proof failed')
        proofs[label] = {'root': str(directory), 'checks': proof, 'identity': identity(directory / 'results.json')}
    binaries = {name: identity(path) for name, path in executables.items()}
    if source_records() != before or binaries != json.loads(receipt.read_text())['executables']:
        raise RuntimeError('Enhanced storage source/executable identities changed during verification')
    for fixture in fixtures:
        if identity(ROOT / fixture['path']) != fixture:
            raise RuntimeError('Enhanced storage fixture changed during verification')
    report = {'status': 'passed', 'stage': 3, 'configuration': args.configuration,
              'scope': 'separate Enhanced PB/Hall storage and Original compatibility; session delivery and UI pending',
              'sources': before, 'fixtures': fixtures, 'executables': binaries,
              'storage_checks': storage, 'ctest_targets': tests, 'process_proofs': proofs,
              'real_player_data_used': False, 'power_loss_tested': False, 'real_second_account_tested': False,
              'artifacts': [identity(checks), identity(output / 'ctest.log')]}
    (output / 'check-results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'report': str(output / 'check-results.json'), 'tests': len(tests),
                      'process_crash_points_per_category': 7, 'storage': storage}, indent=2))


if __name__ == '__main__':
    main()
