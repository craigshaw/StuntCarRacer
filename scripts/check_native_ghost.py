"""Build the static-track ghost proof and retain identified checks and visuals."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

from native_build import NATIVE_BUILD, ROOT, build_sources
from check_native_rendering import image_python


def identity(path):
    path = path.resolve()
    return {'path': path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def sources():
    paths = {Path(__file__).resolve(), ROOT / 'scripts/check_native_rendering.py', *build_sources()}
    for directory in ('src', 'app', 'tests', 'tools/replay'):
        for extension in ('*.h', '*.cpp'):
            paths.update((ROOT / directory).rglob(extension))
    return [identity(path) for path in sorted(paths)]


def visuals(output):
    # Use the same optional bundled Pillow runtime as the replay rendering QA.
    code = r'''
from pathlib import Path
import csv
import sys
from PIL import Image, ImageDraw
root = Path(sys.argv[1])
for source in root.glob('hero-*.bmp'):
    image = Image.open(source).convert('RGB')
    image.resize((image.width * 2, 480), Image.Resampling.NEAREST).save(source.with_suffix('.png'))
rows = []
for tick in [48, 120, 192, 360, 384, 480]:
    row = Image.new('RGB', (1296, 265), '#151923')
    draw = ImageDraw.Draw(row)
    for column, (kind, label) in enumerate([('baseline', 'Original world'), ('unmasked', 'Ghost without occlusion'), ('ghost', 'Ghost with track mask')]):
        source = root / f'wide-{tick}-{kind}.bmp'
        if source.exists():
            frame = Image.open(source).convert('RGB').resize((432, 240), Image.Resampling.NEAREST)
            row.paste(frame, (column * 432, 25))
        draw.text((column * 432 + 8, 6), f'Tick {tick}.5 | {label}', fill='white')
    rows.append(row)
sheet = Image.new('RGB', (1296, len(rows) * 265))
for index, row in enumerate(rows):
    sheet.paste(row, (0, index * 265))
sheet.save(root / 'comparison.png')
proportions = Image.new('RGB', (1152, 3 * 372), '#151923')
labels = [(0, 'Rear'), (5461, '30 degree'), (8192, '45 degree')]
for row, (angle, label) in enumerate(labels):
    for column, kind in enumerate(['reference', 'ghost']):
        source = Image.open(root / 'proportions' / f'{angle}-48-{kind}.ppm').convert('RGB')
        frame = source.crop((64, 40, 256, 136)).resize((576, 346), Image.Resampling.NEAREST)
        proportions.paste(frame, (column * 576, row * 372 + 26))
        draw = ImageDraw.Draw(proportions)
        title = 'Recovered opponent' if kind == 'reference' else 'Ghost exterior'
        draw.text((column * 576 + 8, row * 372 + 7), f'{label} | {title} | matched footprint, distance 48', fill='white')
proportions.save(root / 'proportions.png')
for folder, title in [('preview', 'Recorded trajectory: grounded driving'), ('preview-jump', 'Recorded trajectory: ramp / airborne motion')]:
    frames = []
    timeline = list(csv.DictReader((root / folder / 'timeline.csv').open()))
    for source, entry in zip(sorted((root / folder).glob('*.bmp')), timeline):
        frame = Image.open(source).convert('RGB').resize((864, 480), Image.Resampling.NEAREST)
        draw = ImageDraw.Draw(frame)
        draw.rectangle((0, 0, 863, 21), fill='#151923')
        draw.text((8, 5), f'{title} | player {entry["player_tick"]}, ghost {entry["ghost_tick"]} | prototype', fill='white')
        frames.append(frame)
    frames[0].save(root / f'{folder}.gif', save_all=True, append_images=frames[1:], duration=60, loop=0, disposal=2)
'''
    interpreter = image_python()
    subprocess.run([str(interpreter), '-c', code, str(output)], check=True, cwd=ROOT)
    return {'interpreter': identity(interpreter), 'files': [identity(path) for path in
        (output / 'hero-wide.png', output / 'comparison.png', output / 'proportions.png', output / 'preview.gif', output / 'preview-jump.gif')]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--chip', type=Path, default=ROOT / 'analysis/captures/race/chip-000000.bin')
    parser.add_argument('--payload', type=Path, default=ROOT / 'analysis/reference/vesuri/offline-extracted-payload.bin')
    parser.add_argument('--inputs', type=Path, default=ROOT / 'tests/replays/laps/authored-lap-506.csv')
    parser.add_argument('--reference', type=Path, default=ROOT / 'analysis/native-cockpit/captured-three-lap-race/original.csv')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/ghost-proof')
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixture_paths = (args.chip, args.payload, args.inputs, args.reference)
    fixtures = [identity(path) for path in fixture_paths]
    source_records = sources()
    target_names = ('native_ghost_proof', 'ghost_car_tests', 'presentation_interpolation_tests',
                    'native_extended_view_tests', 'native_track_visibility_tests', 'native_lap_timer_tests')
    executables = {name: NATIVE_BUILD / 'Release' / (name + '.exe') for name in target_names}
    receipt = output / 'build-receipt.json'
    if args.skip_build:
        previous = json.loads(receipt.read_text())
        if previous['sources'] != source_records or previous['executables'] != {
                name: identity(path) for name, path in executables.items()}:
            raise RuntimeError('Ghost proof build is stale; rerun without --skip-build')
    else:
        with (output / 'build.log').open('w') as log:
            for command in (["cmake", "--preset", "windows-release"],
                            ["cmake", "--build", "--preset", "windows-release", "--target", *target_names,
                             "--parallel", "1", "--", "/nr:false"]):
                subprocess.run(command, cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT)
        receipt.write_text(json.dumps({'sources': source_records, 'executables': {
            name: identity(path) for name, path in executables.items()}}, indent=2) + '\n')
    test_pattern = '^(' + '|'.join(target_names) + ')$'
    with (output / 'ctest.log').open('w') as log:
        subprocess.run(['ctest', '--preset', 'windows-release', '-R', test_pattern], cwd=ROOT,
                       check=True, stdout=log, stderr=subprocess.STDOUT)
    started = time.perf_counter()
    run = subprocess.run([str(executables['native_ghost_proof']), *map(str, fixture_paths), str(output), '--preview'],
                         cwd=ROOT, capture_output=True, text=True)
    (output / 'proof.stdout.txt').write_text(run.stdout)
    (output / 'proof.stderr.txt').write_text(run.stderr)
    run.check_returncode()
    report = json.loads((output / 'results.json').read_text())
    if (report.get('status') != 'passed' or not report.get('simulation_and_rng_unchanged')
            or not report.get('static_depth_boundary_gate_passed')
            or report.get('complete_depth_coverage_missing_pixels') != 0):
        raise RuntimeError('Ghost proof did not establish isolation and complete painted-depth coverage')
    qa = visuals(output)
    if sources() != source_records or [identity(path) for path in fixture_paths] != fixtures:
        raise RuntimeError('Sources or fixtures changed during the ghost proof')
    result = {'status': 'passed', 'scope': 'static-track development rendering proof, not player ghost feature',
              'production_occlusion_accepted': False,
              'retained_original_trace_compared': True, 'original_cpu_executed': False,
              'seconds': time.perf_counter() - started, 'sources': source_records, 'fixtures': fixtures,
              'executables': {name: identity(path) for name, path in executables.items()},
              'checks': report, 'visuals': qa}
    (output / 'check-results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'report': str(output / 'check-results.json'),
                      'preview': str(output / 'preview.gif'), 'jump_preview': str(output / 'preview-jump.gif')}, indent=2))


if __name__ == '__main__':
    main()
