"""Verify Hall ranking/codec and an isolated Windows shared-storage proof."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

from native_build import NATIVE_BUILD, ROOT, build_sources


TARGETS = ('hall_of_fame_tests', 'hall_table_codec_tests', 'shared_hall_storage_tests', 'native_hall_storage_proof')


def identity(path):
    path = path.resolve()
    return {'path': path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def sources():
    paths = {*build_sources(), Path(__file__).resolve()}
    paths.update(ROOT / name for name in (
        'app/records/hall_of_fame.h', 'app/records/hall_of_fame.cpp',
        'app/persistence/hall_table_codec.h', 'app/persistence/hall_table_codec.cpp',
        'app/platform/windows/shared_hall_storage.h', 'app/platform/windows/shared_hall_storage.cpp',
        'tools/replay/native_hall_storage_proof.cpp', 'tests/support/hall_fixture.h',
        'tests/unit/hall_of_fame_tests.cpp', 'tests/unit/hall_table_codec_tests.cpp',
        'tests/integration/shared_hall_storage_tests.cpp', 'src/race/race_timing.h',
        'src/race/race_timing.cpp', 'src/state/native_checkpoint_io.h'))
    return [identity(path) for path in sorted(paths)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/hall-of-fame/foundations')
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--collect-account-proof', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    before = sources()
    executables = {name: NATIVE_BUILD / 'Release' / (name + '.exe') for name in TARGETS}
    receipt = output / 'build-receipt.json'
    if args.collect_account_proof or args.skip_build:
        previous = json.loads(receipt.read_text())
        if previous['sources'] != before or previous['executables'] != {name: identity(path) for name, path in executables.items()}:
            raise RuntimeError('Hall foundation build is stale; rerun without --skip-build')
    else:
        with (output / 'build.log').open('w') as log:
            for command in (['cmake', '--preset', 'windows-release'],
                            ['cmake', '--build', '--preset', 'windows-release', '--target', *TARGETS,
                             '--parallel', '1', '--', '/nr:false']):
                subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        receipt.write_text(json.dumps({'sources': before, 'executables': {name: identity(path) for name, path in executables.items()}}, indent=2) + '\n')
    report_path = output / 'check-results.json'
    proof_exe = executables['native_hall_storage_proof']
    if args.collect_account_proof:
        result = json.loads(report_path.read_text())
        account_root = Path(result['account_fixture'])
    else:
        with (output / 'ctest.log').open('w') as log:
            subprocess.run(['ctest', '--preset', 'windows-release', '-R', '^(' + '|'.join(TARGETS[:3]) + ')$'],
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        run = output / ('run-' + str(time.time_ns()))
        process_root = run / 'processes'
        account_root = run / 'accounts'
        # The executable creates only explicitly supplied disposable roots.
        with (output / 'proof.log').open('w') as log:
            for mode, path in (('--proof', process_root), ('--prepare-account', account_root)):
                subprocess.run([str(proof_exe), mode, str(path)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        processes = json.loads((process_root / 'results.json').read_text())
        if processes['status'] != 'passed' or processes['lost_updates'] or not processes['dead_process_lock_released']:
            raise RuntimeError('Shared-storage process proof failed')
        result = {'scope': 'Hall model/codec and disposable Windows storage foundations; not player integration',
                  'sources': before, 'executables': {name: identity(path) for name, path in executables.items()},
                  'process_proof': processes, 'account_fixture': str(account_root),
                  'original_cpu_executed': False, 'real_player_data_used': False}
        command = output / 'run-as-second-user.cmd'
        command.write_text('@echo off\n"' + str(proof_exe) + '" --account-worker "' + str(account_root) + '"\n'
                           'if errorlevel 1 (echo Proof failed. No account changes were made.) else (echo Cross-account proof passed.)\npause\n')
    account_path = account_root / 'account-results.json'
    if account_path.exists():
        account = json.loads(account_path.read_text())
        if (account['status'] != 'passed' or not account['real_second_account_tested'] or account['first_sid'] == account['second_sid']
                or account['executable_sha256'] != identity(proof_exe)['sha256'] or account['entries'] != 20):
            raise RuntimeError('Second-account receipt does not establish the real-account proof')
        result['account_proof'] = account
        result['status'] = 'passed'
        result['first_deliverable_complete'] = True
    else:
        result['status'] = 'pending_real_account_check'
        result['first_deliverable_complete'] = False
    if sources() != before or result['executables'] != {name: identity(path) for name, path in executables.items()}:
        raise RuntimeError('Hall foundation sources/executables changed during verification')
    report_path.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'status': result['status'], 'first_deliverable_complete': result['first_deliverable_complete'],
                      'report': str(report_path), 'second_user_launcher': str(output / 'run-as-second-user.cmd')}, indent=2))


if __name__ == '__main__':
    main()
