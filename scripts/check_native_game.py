"""Check native menus, Practice, single-player and local championships."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

from native_build import NATIVE_BUILD, ORACLE_BUILD, build_sources, configure_command
from check_native_startup import sources as startup_sources
from check_driving_replay import verify_replay_result
from import_native_assets import DEFAULT_PACK, ROOT, import_assets


def identity(path):
    return {'path': str(path.relative_to(ROOT)), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def sources():
    paths = {ROOT / record['path'] for record in startup_sources()}
    paths.add(Path(__file__).resolve())
    paths.add(ROOT / 'tests/integration/native_game_session_tests.cpp')
    paths.add(ROOT / 'tests/integration/native_display_tests.cpp')
    paths.add(ROOT / 'tests/integration/native_launcher_tests.cpp')
    paths.add(ROOT / 'tests/integration/native_navigation_tests.cpp')
    paths.add(ROOT / 'tests/unit/live_drive_tests.cpp')
    paths.add(ROOT / 'tests/integration/native_opening_session_tests.cpp')
    paths.add(ROOT / 'tests/integration/native_opponent_session_tests.cpp')
    paths.add(ROOT / 'tests/integration/native_multiplayer_session_tests.cpp')
    paths.add(ROOT / 'tests/integration/native_career_session_tests.cpp')
    paths.add(ROOT / 'tests/oracle/native_render_audit.cpp')
    paths.add(ROOT / 'tests/oracle/replay_scene_commands_tests.cpp')
    paths.add(ROOT / 'tests/integration/native_save_session_tests.cpp')
    paths.add(ROOT / 'tests/unit/native_save_store_tests.cpp')
    paths.add(ROOT / 'tests/oracle/draw_bridge_oracle.cpp')
    paths.add(ROOT / 'tests/integration/draw_bridge_driving_tests.cpp')
    paths.add(ROOT / 'tests/oracle/driving_replay_oracle.cpp')
    paths.add(ROOT / 'scripts/check_driving_replay.py')
    for name in ('native_menu_render', 'native_menu_flow', 'native_track_preview', 'native_palette_fade',
                 'native_hall_of_fame', 'native_hall_records', 'native_save_state', 'native_save_menu',
                 'native_multiplayer_state', 'native_multiplayer_render', 'native_opponent_race', 'native_competition_render',
                 'native_career_state'):
        paths.add(ROOT / 'tests/oracle' / (name + '_tests.cpp'))
    paths.update(build_sources())
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
    binary_dir = NATIVE_BUILD
    executable = binary_dir / 'Release/Stunt Car Racer.exe'
    test = binary_dir / 'Release/native_game_session_tests.exe'
    opening_test = binary_dir / 'Release/native_opening_session_tests.exe'
    startup_test = binary_dir / 'Release/native_startup_tests.exe'
    bridge_test = binary_dir / 'Release/draw_bridge_driving_tests.exe'
    season_test = binary_dir / 'Release/native_opponent_session_tests.exe'
    multiplayer_test = binary_dir / 'Release/native_multiplayer_session_tests.exe'
    career_test = binary_dir / 'Release/native_career_session_tests.exe'
    save_test = binary_dir / 'Release/native_save_session_tests.exe'
    store_test = binary_dir / 'Release/native_save_store_tests.exe'
    display_test = binary_dir / 'Release/native_display_tests.exe'
    controls_test = binary_dir / 'Release/live_drive_tests.exe'
    launcher_test = binary_dir / 'Release/native_launcher_tests.exe'
    navigation_test = binary_dir / 'Release/native_navigation_tests.exe'
    artwork = identity(ROOT / 'logo.png') if (ROOT / 'logo.png').exists() else None
    receipt = output / 'build-receipt.json'
    records = sources()
    executables = {'game': executable, 'test': test, 'opening_test': opening_test, 'startup_test': startup_test,
                   'bridge_test': bridge_test, 'season_test': season_test, 'multiplayer_test': multiplayer_test,
                   'career_test': career_test, 'save_test': save_test, 'store_test': store_test, 'display_test': display_test, 'controls_test': controls_test, 'launcher_test': launcher_test, 'navigation_test': navigation_test}
    if args.skip_build:
        old = json.loads(receipt.read_text())
        if old['sources'] != records or old.get('artwork') != artwork or any(old.get(key) != identity(path) for key, path in executables.items()):
            raise RuntimeError('Native game build is stale; rerun without --skip-build')
    else:
        build([configure_command(oracle=False),
               ['cmake', '--build', binary_dir, '--config', 'Release', '--target',
                'native_game', 'native_game_session_tests', 'native_opening_session_tests', 'native_startup_tests', 'draw_bridge_driving_tests',
                'native_opponent_session_tests', 'native_multiplayer_session_tests', 'native_career_session_tests',
                'native_save_session_tests', 'native_save_store_tests', 'native_display_tests', 'native_launcher_tests', 'native_navigation_tests', 'live_drive_tests', '--parallel', '4']], output / 'build.log')
        receipt.write_text(json.dumps({'sources': records, 'artwork': artwork, **{key: identity(path) for key, path in executables.items()}}, indent=2) + '\n')
    launcher_args = [launcher_test, pack, output / 'launcher']
    disk = args.adf or ROOT / 'Stunt Car Racer (MicroStyle).adf'
    if disk.exists():
        launcher_args.append(disk)
    disk_fixture = {'filename': disk.name, 'sha256': hashlib.sha256(disk.read_bytes()).hexdigest()} if disk.exists() else None
    launcher = run_json(launcher_args, output / 'launcher-results.json')
    navigation = run_json([navigation_test, pack, output / 'navigation'], output / 'navigation-results.json')
    if (navigation.get('status') != 'passed' or navigation.get('practice_routes') != 8
            or navigation.get('local_player_counts') != list(range(2, 9))
            or any(navigation.get(key) is not True for key in ('responsive_taps', 'repeat_timing', 'back_routes',
                                                            'preview_rng_preserved', 'save_cancellation', 'competition_progression'))):
        raise RuntimeError('Responsive navigation or state-preservation regression failed')
    if (launcher.get('shared_preferences') is not True or launcher.get('launch_overrides_transient') is not True
            or launcher.get('recording_isolation_ticks') != 96 or launcher.get('rendered_states') != 3
            or launcher.get('recording_final_memory_identical') is not True
            or (disk.exists() and launcher.get('supported_import') is not True)):
        raise RuntimeError('Native launcher setup or isolation regression failed')
    integration = run_json([test, pack, output / 'integration'], output / 'integration-results.json')
    if integration.get('practice_routes') != 8 or integration.get('repeated_races') != 8 or integration.get('return_to_main_menu') is not True:
        raise RuntimeError('Native game Practice route coverage differs')
    if integration.get('hall_of_fame_routes') != 16 or integration.get('hall_of_fame_fire_wait_checked') is not True:
        raise RuntimeError('Hall of Fame menu routes or fire-wait coverage differs')
    if integration.get('cockpit_label_checks') != 16:
        raise RuntimeError('First and repeated menu-entered cockpit label coverage differs')
    if (integration.get('throttle_presets') != 2 or integration.get('modern_coasts_on_release') is not True
            or integration.get('preset_switch_and_pause_checked') is not True or integration.get('modern_original_replay_rejected') is not True):
        raise RuntimeError('Native throttle preset behaviour or recording isolation regression failed')
    opening = run_json([opening_test, pack, output / 'opening'], output / 'opening-results.json')
    if (opening.get('named_race_routes') != 3 or opening.get('local_startups') != 7
            or opening.get('entered_names') != 36 or opening.get('blank_retry') is not True
            or opening.get('hall_of_fame_routes') != 8):
        raise RuntimeError('Original startup or player-name entry coverage differs')
    startup = run_json([startup_test, pack, output / 'startup'], output / 'startup-results.json')
    if startup.get('ticks') != 4096 or startup.get('left_right_steering_checked') is not True:
        raise RuntimeError('Fresh race physics or steering regression failed')
    if (any(startup.get(name, 0) <= 0 for name in ('drop_start_frames', 'press_fire_frames', 'dust_frames', 'spark_frames'))
            or startup.get('effects_view_isolation') is not True):
        raise RuntimeError('Native start/recovery messages or crash particle rendering regression failed')
    if (startup.get('boost_flame_phase_mask') != 7 or startup.get('multiplayer_damage_entries') != 9
            or any(startup.get(name, 0) <= 0 for name in ('boost_flame_frames', 'boost_release_frames', 'chain_frames', 'chain_lifting_frames'))):
        raise RuntimeError('Native boost flames, lifting chains or carried damage regression failed')
    bridge = run_json([bridge_test, pack, output / 'bridge-driving'], output / 'bridge-driving-results.json')
    if bridge.get('ticks') != 512 or bridge.get('bridge_occupied_ticks', 0) <= 0 or bridge.get('released_ticks', 0) <= 0:
        raise RuntimeError('Draw Bridge driving did not cover occupancy and release')
    season = run_json([season_test, pack, output / 'season'], output / 'season-results.json')
    if (season.get('human_races') != 4 or season.get('opponent_completed_races') != 1
            or season.get('original_physics_rate') is not True or season.get('return_to_main_menu') is not True):
        raise RuntimeError('The native local season did not complete its original route')
    saves = run_json([save_test, pack, output / 'saves'], output / 'save-results.json')
    if (saves.get('native_saves') != 4 or saves.get('native_loads') != 5 or saves.get('loaded_races') != 3
            or saves.get('replay_routes') != 3 or saves.get('fresh_launch_load') is not True
            or saves.get('corrupt_load_preserves_live_state') is not True or saves.get('write_retry_and_abort') is not True):
        raise RuntimeError('Native save, load or Replay route coverage differs')
    multiplayer = run_json([multiplayer_test, pack, output / 'multiplayer'], output / 'multiplayer-results.json')
    if (multiplayer.get('human_races') != 88 or multiplayer.get('seasons') != 11
            or multiplayer.get('human_ids') != 8 or multiplayer.get('forfeited_tracks') != 14
            or multiplayer.get('pace_car_finishes') != 1 or multiplayer.get('per_driver_damage') is not True
            or multiplayer.get('career_track_access') is not True or multiplayer.get('four_season_reset') is not True):
        raise RuntimeError('Native multiplayer championship coverage differs')
    career = run_json([career_test, pack, output / 'career'], output / 'career-results.json')
    if (career.get('career_contexts') != 8 or career.get('real_races') != 8
            or career.get('forfeited_seasons') != 16 or career.get('relegations') != 6
            or career.get('native_saves') != 8 or career.get('native_loads') != 16 or career.get('replay_routes') != 8
            or career.get('fresh_launch_continuation') is not True or career.get('original_physics_rate') is not True):
        raise RuntimeError('Single-player career continuation coverage differs')
    storage = run_json([store_test, output / 'save-store'], output / 'save-store-results.json')
    if storage.get('slots') != 30 or storage.get('atomic_failure_checked') is not True:
        raise RuntimeError('Native save-file storage regression failed')
    display = run_json([display_test, pack, output / 'display'], output / 'display-results.json')
    if (display.get('preference_roundtrips') != 48 or display.get('scaling_cases') != 12
            or display.get('legacy_preferences_migrated') is not True or display.get('control_preset_navigation') is not True
            or display.get('fullscreen_roundtrips') != 4 or display.get('paused_presentations') != 120
            or display.get('original_tick_phase_preserved') is not True
            or display.get('final_simulation_memory_identical') is not True):
        raise RuntimeError('Native display preferences or pause isolation regression failed')
    controls = run_json([controls_test], output / 'controls-results.json')
    if controls.get('control_presets') is not True or controls.get('mismatches') != 0:
        raise RuntimeError('Native digital control presets regression failed')
    original = {}
    if args.verify_original:
        oracle_build = ORACLE_BUILD
        build([configure_command(oracle=True),
               ['cmake', '--build', oracle_build, '--config', 'Release', '--target',
                'native_menu_render_tests', 'native_menu_flow_tests', 'native_track_preview_tests', 'native_palette_fade_tests',
                'native_hall_of_fame_tests', 'native_hall_records_tests',
                'native_save_state_tests', 'native_save_menu_tests',
                'native_multiplayer_state_tests', 'native_multiplayer_render_tests',
                'native_opponent_race_tests', 'native_competition_render_tests',
                'native_career_state_tests',
                'native_render_audit', 'replay_scene_commands_tests',
                'driving_replay_oracle', 'draw_bridge_oracle', '--parallel', '4']], output / 'oracle-build.log')
        payload = ROOT / 'analysis/reference/vesuri/offline-extracted-payload.bin'
        for name, argument in (('native_menu_render', payload), ('native_menu_flow', payload), ('native_track_preview', pack),
                               ('native_palette_fade', payload), ('native_hall_of_fame', payload), ('native_hall_records', payload),
                               ('native_save_state', payload), ('native_save_menu', payload),
                               ('native_multiplayer_state', payload), ('native_multiplayer_render', payload),
                               ('native_opponent_race', payload), ('native_competition_render', payload),
                               ('native_career_state', payload)):
            target = oracle_build / 'Release' / (name + '_tests.exe')
            original[name] = run_json([target, argument], output / (name + '-original-results.json'))
            original[name]['oracle'] = identity(target)
        scene_commands = oracle_build / 'Release/replay_scene_commands_tests.exe'
        original['scene_commands'] = run_json([scene_commands, pack], output / 'scene-commands-original-results.json')
        original['scene_commands']['oracle'] = identity(scene_commands)
        if (original['scene_commands'].get('cases') != 1920
                or original['scene_commands'].get('sequential_practice_frames') != 384
                or original['scene_commands'].get('mismatches') != 0):
            raise RuntimeError('Shared scene command original-code coverage differs')
        audit_target = oracle_build / 'Release/native_render_audit.exe'
        original['render_audit'] = run_json([audit_target, pack, output / 'render-audit'], output / 'render-audit-results.json')
        original['render_audit']['oracle'] = identity(audit_target)
        if (original['render_audit'].get('scenes') != 32
                or original['render_audit'].get('simulation_memory_checks') != 32
                or original['render_audit'].get('horizon_cases') != 100
                or original['render_audit'].get('generic_faces') != 0):
            raise RuntimeError('Rendering isolation audit coverage differs')
        if original['native_hall_of_fame'].get('cases') != 640 or original['native_hall_records'].get('cases') != 2944:
            raise RuntimeError('Hall of Fame original-code coverage differs')
        if (original['native_save_state'].get('cases') != 800 or original['native_save_state'].get('rejection_cases') != 39
                or original['native_save_menu'].get('pixel_cases') != 1364):
            raise RuntimeError('Load/Save/Replay original-code coverage differs')
        if (original['native_competition_render'].get('cases') != 476
                or original['native_competition_render'].get('career_skipped_awards') != 68):
            raise RuntimeError('Career result-screen coverage differs')
        if (original['native_career_state'].get('cases') != 2392
                or original['native_career_state'].get('season_sequences') != 96
                or original['native_career_state'].get('flow_seasons') != 16
                or original['native_career_state'].get('save_roundtrips') != 8):
            raise RuntimeError('Original career progression coverage differs')
        original['payload'] = identity(payload)
        original['draw_bridge'] = run_json([oracle_build / 'Release/draw_bridge_oracle.exe', payload], output / 'draw-bridge-original-results.json')
        original['menu_entered_races'] = []
        oracle = oracle_build / 'Release/driving_replay_oracle.exe'
        original['control_latch_race'] = compare_race(oracle, output / 'integration/control-original', output)
        for ordinal in range(8):
            folder = output / 'integration' / ('route-' + str(ordinal))
            original['menu_entered_races'].append({'ordinal': ordinal, **compare_race(oracle, folder, output)})
        original['menu_repeated_races'] = [compare_race(oracle, output / 'integration' / ('route-' + str(ordinal)) / 'repeat', output)
                                            for ordinal in range(8)]
        original['fresh_races'] = [compare_race(oracle, output / 'startup' / ('track-' + str(track)), output)
                                   for track in range(8)]
        original['bridge_approach'] = compare_race(oracle, output / 'bridge-driving/synthetic-approach', output, 'seed.bin')
        original['season_races'] = [compare_race(oracle, output / 'season' / ('race-' + str(race)), output)
                                    for race in range(4)]
        original['opening_races'] = [compare_race(oracle, output / 'opening' / name, output)
                                     for name in ('single-practice', 'single-season', 'local-practice')]
        original['loaded_races'] = [compare_race(oracle, output / 'saves' / name, output)
                                    for name in ('loaded-career-race', 'loaded-rich-race', 'loaded-super-race')]
        multiplayer_seeds = sorted((output / 'multiplayer').rglob('native-initial.bin'))
        if len(multiplayer_seeds) != 88:
            raise RuntimeError('Multiplayer race trace count differs')
        original['multiplayer_races'] = [compare_race(oracle, seed.parent, output) for seed in multiplayer_seeds]
        original['career_races'] = [compare_race(oracle, output / 'career' / ('career-' + str(context) + '-' + str(division)), output)
                                    for context in (0, 11) for division in range(4)]
    if sources() != records:
        raise RuntimeError('Sources changed during native game checks')
    if (identity(ROOT / 'logo.png') if (ROOT / 'logo.png').exists() else None) != artwork:
        raise RuntimeError('Launcher artwork changed during native game checks')
    build([['cmake', '--install', binary_dir, '--config', 'Release', '--prefix', ROOT / 'dist']], output / 'install.log')
    staged = ROOT / 'dist/Stunt Car Racer.exe'
    if identity(staged)['sha256'] != identity(executable)['sha256']:
        raise RuntimeError('Staged player executable differs from the checked build')
    report = {'status': 'passed', 'capture_input': False, 'rom_input': False, 'cpu_emulation': False,
              'pack': identity(pack), 'game': identity(staged), 'build_game': identity(executable), 'sources': records, 'artwork': artwork, 'launcher_disk': disk_fixture, 'launcher': launcher,
              'integration': integration, 'navigation': navigation, 'opening': opening, 'startup': startup, 'bridge_driving': bridge,
              'season': season, 'multiplayer': multiplayer, 'career': career, 'saves': saves, 'save_store': storage, 'display': display, 'controls': controls, 'original_comparisons': original}
    (output / 'check-results.json').write_text(json.dumps(report, indent=2) + '\n')
    if original:
        (output / 'original-check-results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'integration': integration, 'opening': opening, 'startup': startup, 'bridge_driving': bridge, 'season': season, 'multiplayer': multiplayer, 'career': career, 'saves': saves, 'save_store': storage,
                      'display': display, 'controls': controls, 'original_checks': bool(original), 'report': str(output / 'check-results.json')}))


if __name__ == '__main__':
    main()
