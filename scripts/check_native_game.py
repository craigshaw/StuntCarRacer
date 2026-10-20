"""Check the original native menus and complete local Practice lifecycle."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

from check_native_startup import sources as startup_sources
from import_native_assets import DEFAULT_PACK, ROOT, import_assets


def identity(path):
    return {'path': str(path.relative_to(ROOT)), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def sources():
    paths = {ROOT / record['path'] for record in startup_sources()}
    paths.add(Path(__file__).resolve())
    paths.add(ROOT / 'tests/native_game_session_tests.cpp')
    for name in ('native_menu_render', 'native_menu_flow', 'native_track_preview', 'native_palette_fade'):
        paths.add(ROOT / 'tests' / (name + '_tests.cpp'))
    return [identity(path) for path in sorted(paths)]


def run_json(command, destination):
    run = subprocess.run([str(item) for item in command], cwd=ROOT, capture_output=True, text=True)
    destination.with_suffix('.stdout.txt').write_text(run.stdout)
    destination.with_suffix('.stderr.txt').write_text(run.stderr)
    if run.returncode:
        raise RuntimeError('Native game check failed: ' + run.stderr)
    result = json.loads(run.stdout)
    if result.get('status') != 'passed' and result.get('mismatches') != 0:
        raise RuntimeError('Native game check did not pass')
    destination.write_text(json.dumps(result, indent=2) + '\n')
    return result


def build(commands, log_path):
    with log_path.open('w') as log:
        for command in commands:
            subprocess.run([str(value) for value in command], cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, default=DEFAULT_PACK)
    parser.add_argument('--adf', type=Path)
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--verify-original', action='store_true')
    args = parser.parse_args()
    pack = args.pack.resolve()
    if args.adf:
        import_assets(args.adf, pack)
    elif not pack.exists():
        import_assets(ROOT / 'Stunt Car Racer (MicroStyle).adf', pack)
    output = ROOT / 'analysis/native-game';output.mkdir(parents=True, exist_ok=True)
    binary_dir = output / 'build'
    executable = binary_dir / 'Release/native_game.exe'
    test = binary_dir / 'Release/native_game_session_tests.exe'
    startup_test = binary_dir / 'Release/native_startup_tests.exe'
    receipt = binary_dir / 'build-receipt.json'
    records = sources()
    executables = {'game': executable, 'test': test, 'startup_test': startup_test}
    if args.skip_build:
        old = json.loads(receipt.read_text())
        if old['sources'] != records or any(old[key] != identity(path) for key, path in executables.items()):
            raise RuntimeError('Native game build is stale; rerun without --skip-build')
    else:
        build([['cmake', '-S', ROOT, '-B', binary_dir, '-DSCR_BUILD_ORACLE=OFF'],
               ['cmake', '--build', binary_dir, '--config', 'Release', '--target',
                'native_game', 'native_game_session_tests', 'native_startup_tests', '--parallel', '4']], output / 'build.log')
        receipt.write_text(json.dumps({'sources': records, **{key: identity(path) for key, path in executables.items()}}, indent=2) + '\n')
    integration = run_json([test, pack, output / 'integration'], output / 'integration-results.json')
    if integration.get('practice_routes') != 7 or integration.get('repeated_races') != 7 or integration.get('return_to_main_menu') is not True:
        raise RuntimeError('Native game Practice route coverage differs')
    startup = run_json([startup_test, pack, output / 'startup'], output / 'startup-results.json')
    if startup.get('ticks') != 3584 or startup.get('left_right_steering_checked') is not True:
        raise RuntimeError('Fresh race physics or steering regression failed')
    original = {}
    if args.verify_original:
        oracle_build = output / 'oracle-build'
        build([['cmake', '-S', ROOT, '-B', oracle_build, '-DSCR_BUILD_ORACLE=ON',
                '-DMUSASHI_DIR=' + str(ROOT / 'analysis/reference/musashi')],
               ['cmake', '--build', oracle_build, '--config', 'Release', '--target',
                'native_menu_render_tests', 'native_menu_flow_tests', 'native_track_preview_tests', 'native_palette_fade_tests',
                'driving_replay_oracle', '--parallel', '4']], output / 'oracle-build.log')
        payload = ROOT / 'analysis/reference/vesuri/offline-extracted-payload.bin'
        for name, argument in (('native_menu_render', payload), ('native_menu_flow', payload), ('native_track_preview', pack), ('native_palette_fade', payload)):
            target = oracle_build / 'Release' / (name + '_tests.exe')
            original[name] = run_json([target, argument], output / (name + '-original-results.json'))
        original['menu_entered_races'] = []
        oracle = oracle_build / 'Release/driving_replay_oracle.exe'
        for ordinal in range(7):
            folder = output / 'integration' / ('route-' + str(ordinal))
            result = run_json([oracle, folder / 'native-initial.bin', folder / 'controls.csv',
                               folder / 'original.csv', '--full-loop'], folder / 'original-results.json')
            if result.get('mismatches') != 0 or result.get('original_code_patched_by_harness') or result.get('native_state_reseeded'):
                raise RuntimeError('Menu-entered original race comparison failed')
            with (folder / 'native.csv').open(newline='') as f:
                actual = list(csv.DictReader(f))
            with (folder / 'original.csv').open(newline='') as f:
                expected = list(csv.DictReader(f))
            if len(actual) != len(expected) or any(any(row[k] != target[k] for k in row if k != 'original_instructions') for row, target in zip(actual, expected)):
                raise RuntimeError('Menu-entered native scene trace differs from original')
            original['menu_entered_races'].append({'ordinal': ordinal, 'results': result,
                'seed': identity(folder / 'native-initial.bin'), 'controls': identity(folder / 'controls.csv'),
                'native': identity(folder / 'native.csv'), 'original': identity(folder / 'original.csv')})
    if sources() != records:
        raise RuntimeError('Sources changed during native game checks')
    report = {'status': 'passed', 'capture_input': False, 'rom_input': False, 'cpu_emulation': False,
              'pack': identity(pack), 'game': identity(executable), 'sources': records,
              'integration': integration, 'startup': startup, 'original_comparisons': original}
    (output / 'check-results.json').write_text(json.dumps(report, indent=2) + '\n')
    if original:
        (output / 'original-check-results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'integration': integration, 'startup': startup,
                      'original_checks': bool(original), 'report': str(output / 'check-results.json')}))


if __name__ == '__main__':
    main()
