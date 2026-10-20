"""Verify the original mode chooser, player names, roster and native race route."""
import argparse
import hashlib
import json
from pathlib import Path

from check_native_game import build, compare_race, run_json, sources as game_sources
from import_native_assets import DEFAULT_PACK, PAYLOAD_SHA, ROOT, import_assets


def identity(path):
    path = Path(path).resolve()
    try:
        name = str(path.relative_to(ROOT))
    except ValueError:
        name = str(path)
    return {'path': name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def sources():
    paths = {ROOT / record['path'] for record in game_sources()}
    paths.add(Path(__file__).resolve())
    for name in ('native_opening_flow', 'native_player_name', 'native_opening_render', 'native_opening_session'):
        paths.add(ROOT / 'tests' / (name + '_tests.cpp'))
    return [identity(path) for path in sorted(paths)]


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
    payload = ROOT / 'analysis/reference/vesuri/offline-extracted-payload.bin'
    payload_record, pack_record = identity(payload), identity(pack)
    if payload_record['sha256'] != PAYLOAD_SHA:
        raise RuntimeError('Opening oracle payload does not match the supplied disk')
    output = ROOT / 'analysis/native-opening'
    output.mkdir(parents=True, exist_ok=True)
    binary_dir = output / 'build'
    names = ('native_opening_session_tests', 'native_opening_flow_tests',
             'native_player_name_tests', 'native_opening_render_tests',
             'driving_replay_oracle', 'native_palette_fade_tests')
    executables = {name: binary_dir / 'Release' / (name + '.exe') for name in names}
    records = sources()
    receipt = binary_dir / 'build-receipt.json'
    if args.skip_build:
        old = json.loads(receipt.read_text())
        if old['sources'] != records or old['executables'] != {key: identity(path) for key, path in executables.items()}:
            raise RuntimeError('Opening build is stale; rerun without --skip-build')
    else:
        build([['cmake', '-S', ROOT, '-B', binary_dir, '-DSCR_BUILD_ORACLE=ON',
                '-DMUSASHI_DIR=' + str(ROOT / 'analysis/reference/musashi')],
               ['cmake', '--build', binary_dir, '--config', 'Release', '--target', *names, '--parallel', '4']],
              output / 'build.log')
        receipt.write_text(json.dumps({'sources': records, 'executables': {key: identity(path) for key, path in executables.items()}}, indent=2) + '\n')
    executable_records = {key: identity(path) for key, path in executables.items()}
    integration = run_json([executables['native_opening_session_tests'], pack, output / 'integration'], output / 'integration-results.json')
    if (integration.get('single_player_startups') != 1 or integration.get('local_startups') != 7
            or integration.get('named_race_routes') != 3 or integration.get('entered_names') != 36
            or integration.get('local_player_counts') != list(range(2, 9))
            or integration.get('capture_input') is not False or integration.get('cpu_emulation') is not False):
        raise RuntimeError('Opening session coverage is incomplete')
    original = {}
    for name in ('native_opening_flow', 'native_player_name', 'native_opening_render', 'native_palette_fade'):
        original[name] = run_json([executables[name + '_tests'], payload], output / (name + '-original-results.json'))
    flow, text, pixels = (original[name] for name in ('native_opening_flow', 'native_player_name', 'native_opening_render'))
    if (flow.get('routeCases') != 32 or flow.get('standingsCases') != 896
            or text.get('scanner_cases') != 24576 or text.get('text_loop_cases') != 12300
            or text.get('complete_entry_cases') != 48
            or pixels.get('menu_cases', 0) <= 0 or pixels.get('name_cases', 0) <= 0
            or pixels.get('standings_cases', 0) < 7):
        raise RuntimeError('Original startup comparison omitted declared coverage')
    races = []
    oracle = executables['driving_replay_oracle']
    for name in ('single-practice', 'single-season', 'local-practice'):
        races.append(compare_race(oracle, output / 'integration' / name, output))
    if sum(race['results']['ticks'] for race in races) != integration['ticks']:
        raise RuntimeError('Named opening race traces differ from integration updates')
    if (sources() != records or identity(pack) != pack_record or identity(payload) != payload_record
            or {key: identity(path) for key, path in executables.items()} != executable_records):
        raise RuntimeError('Source, input or executable identity changed during opening verification')
    report = {'status': 'passed', 'pack': pack_record, 'payload': payload_record, 'sources': records,
              'executables': executable_records, 'integration': integration,
              'original_comparisons': original, 'named_races': races,
              'capture_input': False, 'rom_input': False, 'runtime_cpu_emulation': False,
              'test_only_original_cpu_oracle': True}
    destination = output / 'check-results.json'
    destination.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'integration': integration, 'original_pixel_cases': pixels['cases'],
                      'scanner_cases': text['scanner_cases'], 'editing_cases': text['text_loop_cases'],
                      'report': str(destination)}))


if __name__ == '__main__':
    main()
