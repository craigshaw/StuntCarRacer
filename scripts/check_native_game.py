"""Check native menus, Practice and a complete original local racing season."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

from check_native_startup import sources as startup_sources
from check_driving_replay import verify_replay_result
from import_native_assets import DEFAULT_PACK, ROOT, import_assets


def identity(path):
    return {'path': str(path.relative_to(ROOT)), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def sources():
    paths = {ROOT / record['path'] for record in startup_sources()}
    paths.add(Path(__file__).resolve())
    paths.add(ROOT / 'tests/native_game_session_tests.cpp')
    paths.add(ROOT / 'tests/native_opponent_session_tests.cpp')
    paths.add(ROOT / 'tests/draw_bridge_oracle.cpp')
    paths.add(ROOT / 'tests/draw_bridge_driving_tests.cpp')
    paths.add(ROOT / 'tests/driving_replay_oracle.cpp')
    paths.add(ROOT / 'scripts/check_driving_replay.py')
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


def compare_race(oracle, folder, output, seed_name='native-initial.bin'):
    result = run_json([oracle, folder / seed_name, folder / 'controls.csv',
                       folder / 'original.csv', '--full-loop'], folder / 'original-results.json')
    with (folder / 'native.csv').open(newline='') as f:
        actual = list(csv.DictReader(f))
    with (folder / 'original.csv').open(newline='') as f:
        expected = list(csv.DictReader(f))
    verify_replay_result(result, len(actual), full_loop=True)
    if len(actual) != len(expected) or any(any(row[k] != target[k] for k in row if k != 'original_instructions') for row, target in zip(actual, expected)):
        raise RuntimeError('Native scene trace differs from original in ' + str(folder))
    return {'directory': str(folder.relative_to(output)), 'results': result,
            'seed': identity(folder / seed_name), 'controls': identity(folder / 'controls.csv'),
            'native': identity(folder / 'native.csv'), 'original': identity(folder / 'original.csv'),
            'oracle': identity(oracle)}


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
    bridge_test = binary_dir / 'Release/draw_bridge_driving_tests.exe'
    season_test = binary_dir / 'Release/native_opponent_session_tests.exe'
    receipt = binary_dir / 'build-receipt.json'
    records = sources()
    executables = {'game': executable, 'test': test, 'startup_test': startup_test,
                   'bridge_test': bridge_test, 'season_test': season_test}
    if args.skip_build:
        old = json.loads(receipt.read_text())
        if old['sources'] != records or any(old[key] != identity(path) for key, path in executables.items()):
            raise RuntimeError('Native game build is stale; rerun without --skip-build')
    else:
        build([['cmake', '-S', ROOT, '-B', binary_dir, '-DSCR_BUILD_ORACLE=OFF'],
               ['cmake', '--build', binary_dir, '--config', 'Release', '--target',
                'native_game', 'native_game_session_tests', 'native_startup_tests', 'draw_bridge_driving_tests',
                'native_opponent_session_tests', '--parallel', '4']], output / 'build.log')
        receipt.write_text(json.dumps({'sources': records, **{key: identity(path) for key, path in executables.items()}}, indent=2) + '\n')
    integration = run_json([test, pack, output / 'integration'], output / 'integration-results.json')
    if integration.get('practice_routes') != 8 or integration.get('repeated_races') != 8 or integration.get('return_to_main_menu') is not True:
        raise RuntimeError('Native game Practice route coverage differs')
    startup = run_json([startup_test, pack, output / 'startup'], output / 'startup-results.json')
    if startup.get('ticks') != 4096 or startup.get('left_right_steering_checked') is not True:
        raise RuntimeError('Fresh race physics or steering regression failed')
    bridge = run_json([bridge_test, pack, output / 'bridge-driving'], output / 'bridge-driving-results.json')
    if bridge.get('ticks') != 512 or bridge.get('bridge_occupied_ticks', 0) <= 0 or bridge.get('released_ticks', 0) <= 0:
        raise RuntimeError('Draw Bridge driving did not cover occupancy and release')
    season = run_json([season_test, pack, output / 'season'], output / 'season-results.json')
    if (season.get('human_races') != 4 or season.get('opponent_completed_races') != 1
            or season.get('original_physics_rate') is not True or season.get('return_to_main_menu') is not True):
        raise RuntimeError('The native local season did not complete its original route')
    original = {}
    if args.verify_original:
        oracle_build = output / 'oracle-build'
        build([['cmake', '-S', ROOT, '-B', oracle_build, '-DSCR_BUILD_ORACLE=ON',
                '-DMUSASHI_DIR=' + str(ROOT / 'analysis/reference/musashi')],
               ['cmake', '--build', oracle_build, '--config', 'Release', '--target',
                'native_menu_render_tests', 'native_menu_flow_tests', 'native_track_preview_tests', 'native_palette_fade_tests',
                'driving_replay_oracle', 'draw_bridge_oracle', '--parallel', '4']], output / 'oracle-build.log')
        payload = ROOT / 'analysis/reference/vesuri/offline-extracted-payload.bin'
        for name, argument in (('native_menu_render', payload), ('native_menu_flow', payload), ('native_track_preview', pack), ('native_palette_fade', payload)):
            target = oracle_build / 'Release' / (name + '_tests.exe')
            original[name] = run_json([target, argument], output / (name + '-original-results.json'))
        original['draw_bridge'] = run_json([oracle_build / 'Release/draw_bridge_oracle.exe', payload], output / 'draw-bridge-original-results.json')
        original['menu_entered_races'] = []
        oracle = oracle_build / 'Release/driving_replay_oracle.exe'
        for ordinal in range(8):
            folder = output / 'integration' / ('route-' + str(ordinal))
            original['menu_entered_races'].append({'ordinal': ordinal, **compare_race(oracle, folder, output)})
        original['fresh_races'] = [compare_race(oracle, output / 'startup' / ('track-' + str(track)), output)
                                   for track in range(8)]
        original['bridge_approach'] = compare_race(oracle, output / 'bridge-driving/synthetic-approach', output, 'seed.bin')
        original['season_races'] = [compare_race(oracle, output / 'season' / ('race-' + str(race)), output)
                                    for race in range(4)]
    if sources() != records:
        raise RuntimeError('Sources changed during native game checks')
    report = {'status': 'passed', 'capture_input': False, 'rom_input': False, 'cpu_emulation': False,
              'pack': identity(pack), 'game': identity(executable), 'sources': records,
              'integration': integration, 'startup': startup, 'bridge_driving': bridge,
              'season': season, 'original_comparisons': original}
    (output / 'check-results.json').write_text(json.dumps(report, indent=2) + '\n')
    if original:
        (output / 'original-check-results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'integration': integration, 'startup': startup, 'bridge_driving': bridge, 'season': season,
                      'original_checks': bool(original), 'report': str(output / 'check-results.json')}))


if __name__ == '__main__':
    main()
