"""Verify Enhanced timing settings, frame-aligned clocks, Hall filters and saved finishes (stage 5)."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from native_build import ROOT, NATIVE_BUILD
from check_native_ghost import identity, sources, image_python


def source_records():
    return sorted([*sources(), identity(Path(__file__))], key=lambda item: item['path'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', choices=('Release', 'Debug'), default='Release')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    preset = 'windows-debug' if args.configuration == 'Debug' else 'windows-release'
    default = ROOT / 'analysis/enhanced-ui'
    if args.configuration == 'Debug':
        default /= 'debug'
    output = (args.output or default).resolve()
    output.mkdir(parents=True, exist_ok=True)
    paths = [ROOT / path for path in ('analysis/assets/stunt-car-racer.scr-assets',
             'analysis/captures/race/chip-000000.bin', 'analysis/reference/vesuri/offline-extracted-payload.bin',
             'tests/replays/laps/authored-race-1463.csv', 'tests/replays/laps/authored-race-1463.provenance.json')]
    alternate = ROOT / 'analysis/assets/stunt-car-racer-woodpecker.scr-assets'
    if alternate.is_file():
        paths.append(alternate)
    fixtures = [identity(path) for path in paths]
    provenance = json.loads(paths[4].read_text())
    if hashlib.sha256(paths[3].read_bytes().replace(b'\r\n', b'\n')).hexdigest() != provenance['input_sha256']:
        raise RuntimeError('Authored controls differ from retained provenance')
    before = source_records()
    tests = ('enhanced_clock_tests', 'native_enhanced_ui_tests', 'native_display_tests', 'native_launcher_tests',
             'hall_browser_tests', 'native_lap_timer_tests', 'native_enhanced_session_tests', 'native_autosave_tests',
             'native_career_session_tests', 'native_multiplayer_session_tests', 'enhanced_capture_tests', 'native_lap_timing_tests')
    targets = (*tests, 'native_game')
    binaries = {name: NATIVE_BUILD / args.configuration / ('Stunt Car Racer.exe' if name == 'native_game' else name + '.exe') for name in targets}
    receipt = output / 'build-receipt.json'
    if args.skip_build:
        previous = json.loads(receipt.read_text())
        if previous['sources'] != before or previous['executables'] != {name: identity(path) for name, path in binaries.items()}:
            raise RuntimeError('Enhanced UI build is stale; rerun without --skip-build')
    else:
        with (output / 'build.log').open('w') as log:
            for command in (['cmake', '--preset', preset],
                            ['cmake', '--build', '--preset', preset, '--target', *targets, '--parallel', '1', '--', '/nr:false']):
                subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        receipt.write_text(json.dumps({'sources': before, 'executables': {name: identity(path) for name, path in binaries.items()}}, indent=2) + '\n')
    with (output / 'ctest.log').open('w') as log:
        subprocess.run(['ctest', '--preset', preset, '-V', '-R', '^(' + '|'.join(tests) + ')$'], cwd=ROOT,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    source = NATIVE_BUILD / 'test-output' / args.configuration / 'native_enhanced_ui_tests'
    results = source / 'results.json'
    checks = json.loads(results.read_text())
    if (checks['status'] != 'passed' or checks['migration_versions'] != 9 or checks['view_mask'] != 15
            or checks['native_recovery_checks'] < 1 or checks['interpolated_crossings'] != 3 or checks['hall_category_switches'] != 2
            or not all(checks[key] for key in ('apply_cancel', 'pinned_category', 'finish_checkpoint', 'full_memory_and_rng_isolation'))):
        raise RuntimeError('Enhanced UI acceptance omitted required cases')
    session_results = NATIVE_BUILD / 'test-output' / args.configuration / 'native_enhanced_session_tests/results.json'
    session = json.loads(session_results.read_text())
    if session['captured_laps'] != 9 or not session['full_memory_and_rng_isolation']:
        raise RuntimeError('Dual-category session regression is incomplete')
    visuals = output / 'visuals'
    code = '''from pathlib import Path
from PIL import Image
import sys
source, output = map(Path, sys.argv[1:])
for path in source.rglob('*.ppm'):
    target = (output / path.relative_to(source)).with_suffix('.png')
    target.parent.mkdir(parents=True, exist_ok=True)
    Image.open(path).save(target)
'''
    subprocess.run([str(image_python()), '-c', code, str(source), str(visuals)], cwd=ROOT, check=True)
    executables = {name: identity(path) for name, path in binaries.items()}
    if before != source_records() or fixtures != [identity(path) for path in paths] or executables != json.loads(receipt.read_text())['executables']:
        raise RuntimeError('Enhanced UI sources, fixtures or executables changed during verification')
    report = {'status': 'passed', 'stage': 5, 'configuration': args.configuration,
              'scope': 'Enhanced timing preference/UI, immutable presented clocks, Hall categories and compatible saved finishes; final staging pending',
              'sources': before, 'fixtures': fixtures, 'executables': executables, 'ctest_targets': tests,
              'checks': checks, 'session_regression': session, 'real_player_data_used': False, 'original_cpu_executed': False,
              'synthetic_visuals': ['approximate.png'],
              'visuals': [identity(path) for path in sorted(visuals.rglob('*.png'))],
              'artifacts': [identity(results), identity(session_results), identity(output / 'ctest.log')]}
    (output / 'check-results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'report': str(output / 'check-results.json'), 'checks': checks}, indent=2))


if __name__ == '__main__':
    main()
