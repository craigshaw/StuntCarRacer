"""Verify the isolated durable shared Hall store; no player data or integration."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

from native_build import NATIVE_BUILD, ROOT, build_sources

TESTS = ('hall_of_fame_tests', 'hall_table_codec_tests', 'hall_ghost_codec_tests',
         'shared_hall_storage_tests', 'shared_hall_store_tests',
         'ghost_lap_codec_tests', 'lap_recording_tests')
TARGETS = (*TESTS, 'native_hall_store_proof')


def identity(path):
    return {'path': path.relative_to(ROOT).as_posix(),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def sources():
    paths = {*build_sources(), Path(__file__).resolve()}
    for pattern in ('app/records/*', 'app/persistence/*hall*',
                    'app/platform/windows/shared_hall*', 'app/race/ghost_lap_data.h',
                    'app/race/lap_recording.*', 'app/persistence/ghost_lap_codec.*',
                    'app/presentation/ghost_bridge.*', 'app/presentation/presentation_interpolation.*',
                    'tests/support/*hall*', 'tests/support/ghost_lap_fixture.h',
                    'tests/unit/*hall*', 'tests/unit/*lap_recording*', 'tests/unit/ghost_lap_codec_tests.cpp',
                    'tests/integration/shared_hall*', 'tools/replay/native_hall_store_proof.cpp',
                    'src/race/race_timing.*', 'src/state/native_checkpoint_io.h',
                    'src/render/*.h', 'src/track/*.h', 'app/input/drive_input.h'):
        paths.update(ROOT.glob(pattern))
    # The PB regression targets also link the native render/simulation/state libs.
    paths.update(ROOT.glob('src/**/*.h'))
    paths.update(ROOT.glob('src/**/*.cpp'))
    return [identity(path) for path in sorted(paths) if path.is_file()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/hall-of-fame/durable-store')
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    before = sources()
    executables = {name: NATIVE_BUILD / 'Release' / (name + '.exe') for name in TARGETS}
    receipt = output / 'build-receipt.json'
    if args.skip_build:
        old = json.loads(receipt.read_text())
        if old['sources'] != before or old['executables'] != {n: identity(p) for n, p in executables.items()}:
            raise RuntimeError('Durable Hall build is stale; rerun without --skip-build')
    else:
        with (output / 'build.log').open('w') as log:
            for command in (['cmake', '--preset', 'windows-release'],
                            ['cmake', '--build', '--preset', 'windows-release', '--target', *TARGETS,
                             '--parallel', '1', '--', '/nr:false']):
                subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        receipt.write_text(json.dumps({'sources': before, 'executables': {n: identity(p) for n, p in executables.items()}}, indent=2) + '\n')
    with (output / 'ctest.log').open('w') as log:
        subprocess.run(['ctest', '--preset', 'windows-release', '-R', '^(' + '|'.join(TESTS) + ')$', '--output-on-failure'],
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    run = output / ('run-' + str(time.time_ns()))
    with (output / 'proof.log').open('w') as log:
        subprocess.run([str(executables['native_hall_store_proof']), '--proof', str(run)],
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    proof = json.loads((run / 'results.json').read_text())
    if (proof['status'] != 'passed' or proof['entries'] != 20 or proof['lost_updates']
            or proof['retry_duplicates'] or proof['process_crash_points'] != 7 or not proof['all_references_usable']):
        raise RuntimeError('Durable Hall process proof failed')
    after = {n: identity(p) for n, p in executables.items()}
    if sources() != before or json.loads(receipt.read_text())['executables'] != after:
        raise RuntimeError('Durable Hall source or executable identities changed during verification')
    result = {'status': 'passed', 'scope': 'Durable store and owner-neutral ghost codec; not player integration',
              'sources': before, 'executables': after, 'process_proof': proof, 'proof_root': str(run),
              'real_player_data_used': False, 'original_cpu_executed': False,
              'power_loss_tested': False, 'real_second_account_tested': False}
    report = output / 'check-results.json'
    report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'report': str(report), 'process_crash_points': 7}, indent=2))


if __name__ == '__main__':
    main()
