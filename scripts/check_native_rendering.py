"""Build the native viewer, replay verified controls, and export checked frames."""
import argparse
import csv
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

from check_driving_replay import (ADF_SHA, PAYLOAD_SHA, DISK_OFFSET, PAYLOAD_SIZE, TRACE_FIELDS,
                                  ROOT, identity, read_inputs, verify_native_trace,
                                  verify_replay_result, verify_trace)
from replay_lap import initial_lap_state, validate_laps


RENDER_STEMS = {'track_mesh', 'render_projection', 'planar_image', 'software_renderer',
                'track_sides', 'mountain_horizon'}
SNAPSHOT_TICKS = (0, 24, 128, 272, 505, 961, 1415, 1462)


def stored_path(record):
    path = Path(record['path'])
    return path if path.is_absolute() else ROOT / path


def presentation_source(path):
    relative = path.resolve().relative_to(ROOT).as_posix()
    return relative == 'CMakeLists.txt' or relative.startswith('app/') or (
        relative.startswith('src/') and path.stem in RENDER_STEMS) or (
        relative.startswith('tests/') and any(path.stem.startswith(stem) for stem in RENDER_STEMS)) or (
        relative.startswith('scripts/reference/') and path.stem in {'verify_track_mesh', 'verify_render_projection'})


def verify_reference(path, inputs, capture_record):
    """Validate retained original trace evidence against current gameplay code."""
    report = json.loads(path.read_text(encoding='utf-8'))
    if report.get('status') != 'passed' or report.get('full_loop_requested') is not True:
        raise ValueError('Reference report is not a passing full-loop execution')
    identity(stored_path(report['adf']), ADF_SHA)
    identity(stored_path(report['payload']), PAYLOAD_SHA)
    identity(stored_path(report['musashi_lock']), report['musashi_lock']['sha256'])
    for name in ('manifest', 'source', 'chip_ram'):
        expected = capture_record[name]
        if report['capture'][name]['sha256'] != expected['sha256']:
            raise ValueError(f'Reference report uses a different captured {name}')
        identity(stored_path(report['capture'][name]), expected['sha256'])
    core_records = {}
    for record in report['sources']:
        source = stored_path(record).resolve()
        if presentation_source(source):
            continue
        identity(source, record['sha256'])
        core_records[source] = record
    required = set((ROOT / 'src').glob('*.cpp')) | set((ROOT / 'src').glob('*.h'))
    required |= set((ROOT / 'tests').glob('*fixture.h'))
    required |= set((ROOT / 'tests').glob('driving_replay*.h'))
    required |= {ROOT / 'tests/driving_replay_oracle.cpp', ROOT / 'tests/driving_replay_native.cpp',
                 ROOT / 'scripts/check_driving_replay.py', ROOT / 'scripts/replay_lap.py'}
    missing = [str(source.relative_to(ROOT)) for source in required
               if not presentation_source(source) and source.resolve() not in core_records]
    if missing:
        raise ValueError('Reference report omits current gameplay sources: ' + ', '.join(sorted(missing)))
    matches = [item for item in report['replays'] if item['inputs']['identity']['sha256'] == inputs['identity']['sha256']]
    if len(matches) != 1:
        raise ValueError('Reference report must contain exactly one replay of the selected controls')
    replay = matches[0]
    identity(stored_path(replay['inputs']['identity']), inputs['identity']['sha256'])
    verify_replay_result(replay['results'], inputs['ticks'], full_loop=True)
    trace = stored_path(replay['trace']).resolve()
    identity(trace, replay['trace']['sha256'])
    trace_checks = verify_trace(trace, stored_path(replay['inputs']['identity']), inputs['ticks'])
    if trace_checks['original_instructions'] != replay['results']['original_instructions']:
        raise ValueError('Reference trace and original-execution instruction totals differ')
    native = replay.get('native_only_verification')
    if not native:
        raise ValueError('Reference report lacks its independent native-only replay comparison')
    native_trace = stored_path(native['trace'])
    identity(native_trace, native['trace']['sha256'])
    verify_native_trace(native_trace, trace, stored_path(replay['inputs']['identity']), inputs['ticks'], native['results'])
    for name in ('stdout', 'stderr'):
        identity(stored_path(replay[name]), replay[name]['sha256'])
        identity(stored_path(native[name]), native[name]['sha256'])
    return {'manifest': identity(path), 'trace': identity(trace), 'ticks': inputs['ticks'],
            'gameplay_sources': [core_records[source] for source in sorted(core_records)],
            'original_executable_at_reference_execution': replay['executable'],
            'trace_checks': trace_checks}


def viewer_sources():
    paths = {ROOT / 'CMakeLists.txt'}
    for directory in ('src', 'app'):
        for pattern in ('*.cpp', '*.h'):
            paths.update((ROOT / directory).glob(pattern))
    paths.update((ROOT / 'tests').glob('*fixture.h'))
    paths.update((ROOT / 'tests').glob('driving_replay*.h'))
    return sorted(paths)


def image_python():
    if importlib.util.find_spec('PIL'):
        return Path(sys.executable)
    bundled = Path(os.environ['USERPROFILE']) / '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
    if bundled.exists():
        return bundled
    raise RuntimeError('Pillow is required only for QA image conversion; use the bundled workspace Python runtime')


def verify_viewer_report(report, ticks, snapshots):
    if report.get('status') != 'passed' or report.get('traceMatched') is not True:
        raise ValueError('Native viewer did not establish matching replay traces')
    if report.get('cpu_emulation') is not False or type(report.get('ticks')) is not int or report['ticks'] != ticks:
        raise ValueError('Native viewer must report all input ticks and no CPU emulation')
    if type(report.get('rendered_frames')) is not int or report['rendered_frames'] != ticks:
        raise ValueError('Native viewer must render every replay tick before passing')
    for field in ('mesh_vertices', 'mesh_faces', 'side_mesh_faces', 'mountain_shapes',
                  'mountain_placements', 'background_compressed_bytes'):
        if type(report.get(field)) is not int or report[field] <= 0:
            raise ValueError(f'Native viewer must report positive {field}')
    if report['side_mesh_faces'] != 2 * report['mesh_faces']:
        raise ValueError('Native viewer must build both sidewalls for every road strip')
    exported = report.get('snapshots')
    if not isinstance(exported, list) or [item.get('tick') for item in exported] != snapshots:
        raise ValueError('Native viewer snapshot ticks differ from the requested QA frames')
    for frame in exported:
        if not isinstance(frame.get('path'), str):
            raise ValueError('Native viewer snapshot path missing')
        for field in ('road_faces', 'side_faces', 'mountain_instances', 'mountain_polygons',
                      'triangles', 'road_pixels', 'side_pixels', 'mountain_pixels'):
            if type(frame.get(field)) is not int or frame[field] < 0:
                raise ValueError(f'Native viewer snapshot must report nonnegative {field}')
    initial = next((frame for frame in exported if frame['tick'] == 0), None)
    if initial is None or any(initial[field] <= 0 for field in ('road_pixels', 'side_pixels', 'mountain_pixels')):
        raise ValueError('Initial QA frame must show road, sidewalls and mountains')


def verify_viewer_trace(trace, reference, inputs, ticks, viewer_report):
    if viewer_report.get('traceMatched') is not True or viewer_report.get('cpu_emulation') is not False or viewer_report.get('ticks') != ticks:
        raise ValueError('Viewer trace comparison requires matching native viewer evidence')
    checks = verify_trace(trace, inputs, ticks, original_execution=False)
    with trace.open(encoding='ascii', newline='') as stream:
        native = list(csv.DictReader(stream))
    with reference.open(encoding='ascii', newline='') as stream:
        original = list(csv.DictReader(stream))
    if len(original) != ticks:
        raise ValueError('Original trace row count changed before viewer verification')
    fields = [field for field in TRACE_FIELDS if field != 'original_instructions']
    for tick, (observed, expected) in enumerate(zip(native, original)):
        for field in fields:
            if int(observed[field]) != int(expected[field]):
                raise ValueError(f'Native viewer differs from original at tick {tick}, field {field}')
    checks.update(viewer_internal_comparison=True, gameplay_columns_match_oracle_trace=True, compared_fields=fields)
    return checks


def convert_qa_frames(paths, output):
    interpreter = image_python()
    code = '''from PIL import Image, ImageDraw
from pathlib import Path
import sys
images=[]
for name in sys.argv[2:]:
    path=Path(name)
    with Image.open(path) as source:
        if source.size!=(320,200): raise ValueError('Viewer frame must be320x200')
        image=source.convert('RGB')
        image.save(path.with_suffix('.png'))
        images.append((path,image))
sheet=Image.new('RGB',(640,220*((len(images)+1)//2)),(30,30,30))
draw=ImageDraw.Draw(sheet)
for i,(path,image) in enumerate(images):
    x=(i%2)*320;y=(i//2)*220
    draw.text((x+4,y+3),path.stem,fill='white')
    sheet.paste(image,(x,y+20))
sheet.save(sys.argv[1])
'''
    command = [str(interpreter), '-c', code, str(output), *map(str, paths)]
    subprocess.run(command, check=True, cwd=ROOT)
    return {'interpreter': identity(interpreter), 'contact_sheet': identity(output),
            'pngs': [identity(path.with_suffix('.png')) for path in paths]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, default=ROOT / 'analysis/captures/race')
    parser.add_argument('--inputs', type=Path, default=ROOT / 'tests/replays/laps/authored-race-1463.csv')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/native-render/check-results.json')
    parser.add_argument('--reference-report', type=Path, default=ROOT / 'analysis/driving-replay/lap-regressions/check-results.json')
    parser.add_argument('--refresh-reference', action='store_true', help='Regenerate the original/native reference even when retained evidence is current')
    parser.add_argument('--skip-build', action='store_true', help='Reuse a native viewer newer than every source/fixture')
    args = parser.parse_args()
    output = args.output.resolve();out = output.parent;out.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({'status': 'verification_in_progress', 'started_at_utc': datetime.now(timezone.utc).isoformat()}) + '\n')
    capture = args.capture.resolve();inputs_path = args.inputs.resolve();inputs = read_inputs(inputs_path)
    manifest = json.loads((capture / 'manifest.json').read_text())
    bank = next(item for item in manifest['banks'] if item['tag'] == 'CRAM')
    chip = (capture / bank['file']).resolve()
    if int(bank['base'], 0) != 0 or bank['size'] != 0x80000:
        raise ValueError('Native viewer requires512KiB captured Chip RAM mapped at zero')
    capture_record = {'manifest': identity(capture / 'manifest.json'),
                      'source': identity(Path(manifest['source_path']), manifest['source_sha256']),
                      'chip_ram': identity(chip, bank['sha256'])}
    adf = ROOT / 'Stunt Car Racer (MicroStyle).adf'
    payload = ROOT / 'analysis/reference/vesuri/offline-extracted-payload.bin'
    adf_identity, payload_identity = identity(adf, ADF_SHA), identity(payload, PAYLOAD_SHA)
    if adf.read_bytes()[DISK_OFFSET:DISK_OFFSET + PAYLOAD_SIZE] != payload.read_bytes():
        raise ValueError('Native viewer payload differs from its documented ADF range')
    reference_errors = [];reference = None
    if not args.refresh_reference:
        candidates = dict.fromkeys([args.reference_report.resolve(), out / 'reference/check-results.json'])
        for candidate in candidates:
            try:
                reference = verify_reference(candidate, inputs, capture_record)
                break
            except (OSError, ValueError, KeyError, RuntimeError) as error:
                reference_errors.append({'path': str(candidate), 'reason': str(error)})
    if reference is None:
        reference_path = out / 'reference/check-results.json'
        command = [sys.executable, str(ROOT / 'scripts/check_driving_replay.py'), '--capture', str(capture),
                   '--inputs', str(inputs_path), '--full-loop', '--require-complete-lap', '--native-only-verify',
                   '--output', str(reference_path)]
        subprocess.run(command, check=True, cwd=ROOT)
        reference = verify_reference(reference_path, inputs, capture_record)
        reference['regeneration'] = {'command': command, 'reason': 'explicit_refresh' if args.refresh_reference else reference_errors}
    else:
        reference['regeneration'] = None
    reference['rejected_retained_references'] = reference_errors
    reference_trace = stored_path(reference['trace']).resolve()
    print(f'Original reference verified: {inputs["ticks"]} ticks; replaying in the native viewer.', flush=True)
    asset_output = out / 'assets'
    subprocess.run([sys.executable, str(ROOT / 'scripts/prepare_replay_render_assets.py'), '--capture', str(capture), '--output', str(asset_output)], check=True, cwd=ROOT)
    build = ROOT / 'analysis/native-render/viewer-build'
    executable = build / 'Release/native_replay_viewer.exe'
    sources = viewer_sources();source_records = [identity(path) for path in sources]
    build_receipt = build / 'viewer-build-receipt.json'
    build_commands = []
    if not args.skip_build:
        build_commands = [['cmake', '-S', str(ROOT), '-B', str(build), '-G', 'Visual Studio 18 2026', '-A', 'x64', '-DSCR_BUILD_ORACLE=OFF'],
                          ['cmake', '--build', str(build), '--config', 'Release', '--target', 'native_replay_viewer', '--parallel', '4']]
        with (out / 'build.log').open('w', encoding='utf-8') as log:
            for command in build_commands:
                log.write(json.dumps(command) + '\n');log.flush()
                subprocess.run(command, check=True, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    if not executable.exists():
        raise RuntimeError('Native viewer executable is missing; rebuild without --skip-build')
    if args.skip_build:
        try:
            receipt = json.loads(build_receipt.read_text(encoding='utf-8'))
            if receipt['sources'] != source_records:
                raise ValueError('Native viewer source identities differ from the last build')
            identity(executable, receipt['executable']['sha256'])
        except (OSError, ValueError, KeyError, RuntimeError) as error:
            raise RuntimeError('No matching native viewer build receipt; rebuild without --skip-build') from error
    else:
        # CMake may correctly leave an unchanged executable untouched after a
        # build-file edit. Record content identities rather than compare mtimes.
        for path, record in zip(sources, source_records):
            identity(path, record['sha256'])
        build_receipt.write_text(json.dumps({'executable': identity(executable), 'sources': source_records}, indent=2) + '\n', encoding='utf-8')
    snapshots = [tick for tick in SNAPSHOT_TICKS if tick < inputs['ticks']]
    frame_directory = out / 'frames';frame_directory.mkdir(exist_ok=True)
    trace = out / 'viewer-native.csv';viewer_report_path = out / 'viewer.json'
    trace.unlink(missing_ok=True);viewer_report_path.unlink(missing_ok=True)
    command = [str(executable), '--chip', str(chip), '--payload', str(payload), '--inputs', str(inputs_path),
               '--reference', str(reference_trace), '--trace', str(trace), '--report', str(viewer_report_path),
               '--headless', '--frames-dir', str(frame_directory)]
    for tick in snapshots:
        command.extend(['--snapshot', str(tick)])
    executable_before = identity(executable)
    run = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
    (out / 'viewer-stdout.txt').write_text(run.stdout, encoding='utf-8');(out / 'viewer-stderr.txt').write_text(run.stderr, encoding='utf-8')
    if run.returncode:
        raise RuntimeError(f'Native viewer failed({run.returncode}); see {out / "viewer-stderr.txt"}')
    viewer_report = json.loads(viewer_report_path.read_text(encoding='utf-8'))
    verify_viewer_report(viewer_report, inputs['ticks'], snapshots)
    asset_manifest = json.loads((asset_output / 'assets-manifest.json').read_text())
    if viewer_report['background_compressed_bytes'] != asset_manifest['background']['compressed_bytes']:
        raise ValueError('Native viewer background extent differs from verified original asset decoding')
    trace_checks = verify_viewer_trace(trace, reference_trace, inputs_path, inputs['ticks'], viewer_report)
    lap_checks = validate_laps(trace, initial_lap_state(chip.read_bytes()), require_complete_lap=True)
    frames = []
    for item in viewer_report['snapshots']:
        path = Path(item['path']).resolve()
        if path.parent != frame_directory.resolve() or path.name != f'frame-{item["tick"]:04d}.bmp':
            raise ValueError('Native viewer snapshot path differs from requested frame output')
        frames.append(path)
    qa = convert_qa_frames(frames, out / 'contact-sheet.png')
    identity(executable, executable_before['sha256']);identity(inputs_path, inputs['identity']['sha256'])
    identity(chip, capture_record['chip_ram']['sha256']);identity(payload, payload_identity['sha256'])
    for path, record in zip(sources, source_records):
        identity(path, record['sha256'])
    identity(reference_trace, reference['trace']['sha256'])
    provenance = inputs_path.with_suffix('.provenance.json')
    record = {'status': 'passed', 'verified_at_utc': datetime.now(timezone.utc).isoformat(),
        'adf': adf_identity, 'payload': payload_identity, 'capture': capture_record, 'inputs': inputs,
        'input_provenance': identity(provenance) if provenance.exists() else None,
        'original_reference': reference, 'executable': executable_before, 'command': command,
        'build_commands': build_commands, 'build_skipped': args.skip_build, 'build_receipt': identity(build_receipt), 'sources': source_records,
        'asset_manifest': identity(asset_output / 'assets-manifest.json'),
        'viewer_report': identity(viewer_report_path), 'viewer_results': viewer_report,
        'native_trace': identity(trace), 'trace_checks': trace_checks, 'lap_validation': lap_checks,
        'snapshots': [identity(path) for path in frames], 'qa_images': qa,
        'wrapper_sources': [identity(Path(__file__)), identity(ROOT / 'scripts/prepare_replay_render_assets.py')],
        'limitations': ['The viewer runs native gameplay and software rendering; it does not emulate the68000.',
            'Every declared gameplay trace column is compared with the retained original execution; renderer pixels are not claimed equal to original DMA/blitter output.',
            'Controls are authored regression inputs; human play and real interrupt timing are not reproduced.',
            'Original cockpit imagery and generated masked assets are loaded from verified local inputs; no copyrighted asset bytes are embedded in tracked source.']}
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(f'Native viewer passed: {inputs["ticks"]} matching ticks, {lap_checks["complete_laps"]} complete laps, {len(frames)} snapshots.\n{output}', flush=True)


if __name__ == '__main__':
    main()
