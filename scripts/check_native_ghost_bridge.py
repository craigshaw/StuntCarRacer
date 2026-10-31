"""Build the Draw Bridge ghost proof and retain identified checks and visuals."""
import argparse
import json
from pathlib import Path
import subprocess
import time

from native_build import NATIVE_BUILD, ROOT
from check_native_ghost import identity, sources
from check_native_rendering import image_python


def source_records():
    return sorted([*sources(), identity(Path(__file__))], key=lambda item: item['path'])


def visuals(output):
    code = r'''
from pathlib import Path
import csv
import json
import sys
from PIL import Image, ImageDraw
root = Path(sys.argv[1])
report = json.loads((root / 'results.json').read_text())
scenarios = [('matching', 'Matching geometry'), ('mismatch', 'Different live geometry'),
             ('same-shape', 'Different phases, same crossing shape')]
timelines = {}
for folder, title in scenarios:
    timeline = list(csv.DictReader((root / folder / 'timeline.csv').open()))
    timelines[folder] = timeline
    frames = []
    for entry in timeline:
        source = root / folder / 'frames' / f'{int(entry["frame"]):04}.bmp'
        frame = Image.open(source).convert('RGB').resize((864, 480), Image.Resampling.NEAREST)
        draw = ImageDraw.Draw(frame)
        differs = entry['conditions_differ'] == '1'
        label = 'BRIDGE CONDITIONS DIFFER' if differs else 'Compatible crossing / outside bridge guard'
        draw.rectangle((0, 0, 863, 35), fill='#151923')
        draw.text((8, 4), f'{title} | observer view | native synthetic approach', fill='white')
        draw.text((8, 20), f'{float(entry["seconds"]):.2f}s | {entry["region"]} | ghost {float(entry["alpha"]):.0%} | {label}',
                  fill='#ffcf83' if differs else '#a4e5dc')
        frames.append(frame)
    if not frames:
        raise RuntimeError('No bridge preview frames')
    frames[0].save(root / f'{folder}.gif', save_all=True, append_images=frames[1:],
                   duration=120, loop=0, disposal=2)
ticks = [report['first_approach_tick'] - 5, report['first_approach_tick'] + 3,
         report['first_crossing_tick'] + 4, report['first_crossing_tick'] + 40,
         report['safe_landing_exit_tick'] - 2, report['safe_landing_exit_tick'] + 4]
sheet = Image.new('RGB', (1296, len(ticks) * 266), '#151923')
draw = ImageDraw.Draw(sheet)
for row, tick in enumerate(ticks):
    for column, (folder, title) in enumerate(scenarios):
        entry = min(timelines[folder], key=lambda item: abs(float(item['source_tick']) - tick))
        source = root / folder / 'frames' / f'{int(entry["frame"]):04}.bmp'
        frame = Image.open(source).convert('RGB').resize((432, 240), Image.Resampling.NEAREST)
        sheet.paste(frame, (column * 432, row * 266 + 26))
        draw.text((column * 432 + 6, row * 266 + 3), f'{title} | {entry["region"]}', fill='white')
        draw.text((column * 432 + 6, row * 266 + 14),
                  f'{float(entry["seconds"]):.2f}s | ghost {float(entry["alpha"]):.0%}', fill='#a4e5dc')
sheet.save(root / 'comparison.png')
'''
    interpreter = image_python()
    subprocess.run([str(interpreter), '-c', code, str(output)], check=True, cwd=ROOT)
    return {'interpreter': identity(interpreter), 'files': [identity(output / name) for name in
            ('matching.gif', 'mismatch.gif', 'same-shape.gif', 'comparison.png')]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, default=ROOT / 'analysis/assets/stunt-car-racer.scr-assets')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/ghost-bridge-proof')
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    assets = identity(args.assets)
    sources_before = source_records()
    targets = ('native_ghost_bridge_proof', 'ghost_bridge_tests', 'draw_bridge_driving_tests',
               'ghost_car_tests', 'native_extended_view_tests', 'presentation_interpolation_tests')
    executables = {name: NATIVE_BUILD / 'Release' / (name + '.exe') for name in targets}
    receipt = output / 'build-receipt.json'
    if args.skip_build:
        previous = json.loads(receipt.read_text())
        if previous['sources'] != sources_before or previous['executables'] != {
                name: identity(path) for name, path in executables.items()}:
            raise RuntimeError('Draw Bridge ghost proof build is stale; rerun without --skip-build')
    else:
        with (output / 'build.log').open('w') as log:
            for command in (['cmake', '--preset', 'windows-release'],
                            ['cmake', '--build', '--preset', 'windows-release', '--target', *targets,
                             '--parallel', '1', '--', '/nr:false']):
                subprocess.run(command, cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT)
        receipt.write_text(json.dumps({'sources': sources_before, 'executables': {
            name: identity(path) for name, path in executables.items()}}, indent=2) + '\n')
    started = time.perf_counter()
    with (output / 'ctest.log').open('w') as log:
        subprocess.run(['ctest', '--preset', 'windows-release', '-R', '^(' + '|'.join(targets) + ')$'],
                       cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT)
    run = subprocess.run([str(executables['native_ghost_bridge_proof']), str(args.assets.resolve()),
                          str(output), '--preview'], cwd=ROOT, capture_output=True, text=True)
    (output / 'proof.stdout.txt').write_text(run.stdout)
    (output / 'proof.stderr.txt').write_text(run.stderr)
    run.check_returncode()
    report = json.loads((output / 'results.json').read_text())
    if report.get('status') != 'passed':
        raise RuntimeError('Draw Bridge ghost proof did not pass')
    cases = ('matching', 'mismatch_practice_vs_league', 'same_shape_different_phase')
    if any(report[name]['missing_depth_pixels'] or not report[name]['simulation_and_rng_identical']
           or not report[name]['reverse_pause_checks'] for name in cases):
        raise RuntimeError('Draw Bridge proof did not establish coverage, isolation and retained queries')
    qa = visuals(output)
    if source_records() != sources_before or identity(args.assets) != assets:
        raise RuntimeError('Sources or assets changed during the Draw Bridge ghost proof')
    derived = [identity(output / 'source-payload.bin')]
    for folder in ('recording', 'matching', 'mismatch', 'same-shape'):
        derived.extend(identity(output / folder / name) for name in ('seed.bin', 'controls.csv', 'native.csv'))
    result = {'status': 'passed', 'scope': 'Draw Bridge development proof, not player ghost feature',
              'production_acceptance': False, 'original_cpu_executed': False,
              'seconds': time.perf_counter() - started, 'sources': sources_before,
              'fixtures': [assets], 'derived_fixtures': derived,
              'executables': {name: identity(path) for name, path in executables.items()},
              'checks': report, 'visuals': qa}
    (output / 'check-results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'report': str(output / 'check-results.json'),
                      'preview': str(output / 'mismatch.gif'), 'comparison': str(output / 'comparison.png')}, indent=2))


if __name__ == '__main__':
    main()
