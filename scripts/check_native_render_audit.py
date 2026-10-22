"""Run a bounded world-rendering, view-isolation and original-geometry audit."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from native_build import ORACLE_BUILD, build_sources, configure_command
from import_native_assets import DEFAULT_PACK, ROOT, import_assets


def identity(path):
    return {'path': str(path.resolve().relative_to(ROOT)),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def sources():
    paths = {ROOT / 'CMakeLists.txt', Path(__file__).resolve()}
    for folder in ('app', 'src', 'tests'):
        paths.update((ROOT / folder).rglob('*.h'))
        paths.update((ROOT / folder).rglob('*.cpp'))
    paths.update(build_sources())
    return [identity(path) for path in sorted(paths)]


def run_json(command, path):
    result = subprocess.run([str(item) for item in command], cwd=ROOT,
                            capture_output=True, text=True)
    path.with_suffix('.stdout.txt').write_text(result.stdout)
    path.with_suffix('.stderr.txt').write_text(result.stderr)
    if result.returncode:
        raise RuntimeError('Rendering audit failed: ' + result.stderr)
    report = json.loads(result.stdout)
    if report.get('mismatches') != 0:
        raise RuntimeError('Rendering audit did not report zero mismatches')
    path.write_text(json.dumps(report, indent=2) + '\n')
    return report


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
    output = ROOT / 'analysis/render-audit'
    output.mkdir(parents=True, exist_ok=True)
    build_dir = ORACLE_BUILD
    targets = ('native_render_audit', 'opponent_far_track_oracle',
               'mountain_horizon_oracle', 'near_track_visibility_oracle',
               'replay_scene_commands_tests')
    executables = {name: build_dir / 'Release' / (name + '.exe') for name in targets}
    records = sources()
    receipt = output / 'build-receipt.json'
    if args.skip_build:
        old = json.loads(receipt.read_text())
        if old['sources'] != records or any(old['executables'][name] != identity(path)
                                           for name, path in executables.items()):
            raise RuntimeError('Rendering audit build is stale; rerun without --skip-build')
    else:
        commands = [
            configure_command(oracle=True),
            ['cmake', '--build', build_dir, '--config', 'Release', '--target',
             *targets, '--parallel', '4']]
        with (output / 'build.log').open('w') as log:
            for command in commands:
                subprocess.run([str(item) for item in command], cwd=ROOT,
                               check=True, stdout=log, stderr=subprocess.STDOUT)
        receipt.write_text(json.dumps({'sources': records, 'executables': {
            name: identity(path) for name, path in executables.items()}}, indent=2) + '\n')
    pack_record = identity(pack)
    audit = run_json([executables['native_render_audit'], pack, output / 'scenes'],
                     output / 'native-results.json')
    if (audit.get('scenes') != 32 or audit.get('simulation_memory_checks') != 32
            or audit.get('horizon_cases') != 100 or audit.get('generic_faces') != 0
            or audit.get('offtrack_ticks', 0) <= 0 or audit.get('restarts', 0) <= 0
            or audit.get('bridge_changes', 0) <= 0 or audit.get('layout_cases') != 24
            or audit.get('alternate_angular_view_changes', 0) <= 0
            or audit.get('cockpit_visibility_changes') != audit.get('snapshot_checks')
            or audit.get('original_world_pixel_identity_claimed') is not False):
        raise RuntimeError('Rendering audit coverage differs from its bounded contract')
    payload = output / 'scenes/source-payload.bin'
    seed = output / 'scenes/track-0-context-0-mode-0/seed.bin'
    controls = seed.parent / 'controls.csv'
    originals = {
        'far_track': run_json([executables['opponent_far_track_oracle'], pack],
                              output / 'far-track-results.json'),
        'mountains': run_json([executables['mountain_horizon_oracle'], payload, seed],
                              output / 'mountain-results.json'),
        'near_track': run_json([executables['near_track_visibility_oracle'], payload,
                               seed, controls], output / 'near-track-results.json'),
        'scene_commands': run_json([executables['replay_scene_commands_tests'], pack],
                                   output / 'scene-command-results.json')}
    if originals['near_track'].get('full_near_frames', 0) <= 0:
        raise RuntimeError('The original near-track check omitted evolving complete frames')
    if originals['scene_commands'].get('sequential_practice_frames', 0) <= 0:
        raise RuntimeError('The original caller check omitted sequential Practice frames')
    if sources() != records or identity(pack) != pack_record:
        raise RuntimeError('Rendering audit inputs changed during verification')
    report = {'status': 'passed', 'pack': pack_record, 'sources': records,
              'executables': {name: identity(path) for name, path in executables.items()},
              'native': audit, 'original_geometry': originals,
              'world_pixel_identity_claimed': False,
              'evidence_boundary': 'Original geometry and command comparisons; native coverage and view isolation. No full original blitter pixel reference.'}
    (output / 'check-results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'scenes': audit['scenes'], 'ticks': audit['ticks'],
                      'horizon_cases': audit['horizon_cases'],
                      'original_world_pixel_identity_claimed': False,
                      'output': str(output.relative_to(ROOT))}))


if __name__ == '__main__':
    main()
