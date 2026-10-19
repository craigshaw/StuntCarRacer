"""Verify and replay a headless original/native local-practice driving interval."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from prepare_wheel_oracle import prepare, ROOT, LOCK, DEST
from replay_lap import initial_lap_state, validate_laps

ADF_SHA = '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA = '508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
BASE, DISK_OFFSET, PAYLOAD_SIZE = 0xe700, 0xdc00, 408540
VERIFIERS = [
    ('player-controls', 'verify_player_controls.py'),
    ('track-progress', 'verify_track_progress.py'),
    ('opponent-collision', 'verify_opponent_collision.py'),
    ('driving-effects', 'verify_driving_effects.py'),
    ('race-timing', 'verify_race_timing.py'),
    ('race-transitions', 'verify_race_transitions.py'),
    ('race-restart', 'verify_race_restart.py'),
]
CORE_SPANS = [('main_loop_gameplay_interval', 0x5d47e, 0x5d4ba),
              ('restart_warmup_schedule', 0x5d402, 0x5d47e),
              ('complete_physics_driver', 0x6185c, 0x618ce)]
REPLAY_STAGES = ('controls', 'physics', 'engine', 'track', 'particles',
                 'particle-rng', 'wheel-bounce', 'damage', 'timing')
TRACE_FIELDS = ('tick', 'controls', 'world_x', 'world_y', 'world_z', 'pitch', 'yaw',
                'roll', 'speed', 'segment', 'progress', 'lateral', 'off_track',
                'boost_fuel', 'damage', 'original_instructions', 'lap_counter',
                'crossing_armed', 'race_complete', 'outer_action', 'restarts', 'quit')
# The original setPixelColor changes BSET/BCLR in plotPixel. No other captured
# opcode differences are admitted, and only bit6 is mutable at these addresses.
PIXEL_OPCODE_BITS = {address: 0x40 for address in
    (0x66405, 0x66409, 0x6640d, 0x66411, 0x6641b, 0x6641d, 0x66421, 0x66425)}


def identity(path, expected=None):
    path = path.resolve()
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if expected is not None and digest != expected:
        raise RuntimeError(f'SHA-256 mismatch: {path}')
    try:
        display = str(path.relative_to(ROOT))
    except ValueError:
        display = str(path)
    return {'path': display, 'bytes': len(data), 'sha256': digest}


def read_inputs(path):
    # The C++ reader expects an unquoted ASCII header and decimal rows. Reject
    # BOMs, Unicode digits and CSV quoting instead of accepting a wider dialect.
    with path.open(encoding='ascii', newline='') as stream:
        contents = stream.read()
    if '\r' in contents.replace('\r\n', ''):
        raise ValueError(f'{path}: only LF or CRLF line endings are supported')
    lines = contents.splitlines()
    if not lines or lines[0] not in ('tick,controls', 'tick,controls,quit'):
        raise ValueError(f'{path}: expected tick,controls or tick,controls,quit ASCII header (no BOM)')
    extended = lines[0] == 'tick,controls,quit'
    row_pattern = r'[0-9]+,[0-9]+,[0-9]+' if extended else r'[0-9]+,[0-9]+'
    if any(not re.fullmatch(row_pattern, line) for line in lines[1:]):
        raise ValueError(f'{path}: expected one unquoted decimal input row per line')
    with path.open(encoding='ascii', newline='') as stream:
        rows = csv.DictReader(stream)
        if rows.fieldnames != (['tick', 'controls', 'quit'] if extended else ['tick', 'controls']):
            raise ValueError(f'{path}: invalid input header')
        count, masks, quit_ticks = 0, set(), []
        for row in rows:
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f'{path}: malformed row at tick {count}')
            if not row['tick'].isdecimal() or not row['controls'].isdecimal():
                raise ValueError(f'{path}: tick/controls must be decimal integers')
            tick, controls = int(row['tick']), int(row['controls'])
            quit_pressed = int(row.get('quit', 0))
            if tick != count or not 0 <= controls <= 31:
                raise ValueError(f'{path}: ticks must start at zero and be contiguous; controls must be 0..31')
            if quit_pressed not in (0, 1):
                raise ValueError(f'{path}: quit must be 0 or 1')
            if quit_pressed:
                quit_ticks.append(tick)
            masks.add(controls)
            count += 1
        if not count:
            raise ValueError(f'{path}: replay must contain at least one tick')
    return {'identity': identity(path), 'ticks': count, 'control_masks': sorted(masks),
            'columns': ['tick', 'controls', 'quit'] if extended else ['tick', 'controls'],
            'quit_ticks': quit_ticks}


def verify_trace(trace, inputs, expected_ticks, original_execution=True):
    with inputs.open(encoding='ascii', newline='') as stream:
        expected = list(csv.DictReader(stream))
    with trace.open(encoding='ascii', newline='') as stream:
        rows = csv.DictReader(stream)
        if rows.fieldnames is None or len(rows.fieldnames) != len(set(rows.fieldnames)):
            raise RuntimeError(f'{trace}: missing/duplicate trace column names')
        if not set(TRACE_FIELDS).issubset(rows.fieldnames):
            raise RuntimeError(f'{trace}: missing expected gameplay trace columns')
        count, instructions = 0, 0
        for row in rows:
            if count >= expected_ticks or None in row or any(value is None for value in row.values()):
                raise RuntimeError(f'{trace}: extra or malformed trace row {count}')
            if any(not re.fullmatch(r'[0-9]+', row[field]) for field in TRACE_FIELDS):
                raise RuntimeError(f'{trace}: trace row {count} has nondecimal/missing gameplay fields')
            if int(row['tick']) != count or int(row['controls']) != int(expected[count]['controls']):
                raise RuntimeError(f'{trace}: tick/control mismatch at row {count}')
            if int(row['quit']) != int(expected[count].get('quit', 0)):
                raise RuntimeError(f'{trace}: quit input mismatch at row {count}')
            instructions += int(row['original_instructions'])
            if original_execution and not int(row['original_instructions']):
                raise RuntimeError(f'{trace}: no original instructions recorded at tick {count}')
            if not original_execution and int(row['original_instructions']):
                raise RuntimeError(f'{trace}: native-only trace claims original execution at tick {count}')
            count += 1
        if count != expected_ticks:
            raise RuntimeError(f'{trace}: expected {expected_ticks} trace rows, received {count}')
    return {'rows': count, 'controls_match_inputs': True, 'original_instructions': instructions}


def verify_replay_result(checks, ticks, full_loop=False):
    if type(checks.get('mismatches')) is not int or checks['mismatches'] != 0 or type(checks.get('ticks')) is not int or checks['ticks'] != ticks:
        raise RuntimeError('Replay did not report matching comparisons for every requested tick')
    required_flags = {
        'native_state_reseeded': False,
        'original_code_patched_by_harness': False,
        'registers_reset_between_ticks': not full_loop,
        'ordered_sample_audio_writes_compared': True,
        'whole_ram_compared': False,
        'interrupts_emulated': False,
        'damage_occupancy_compared': True,
        'per_tick_stages_complete': True,
        'full_loop': full_loop,
    }
    for name, expected in required_flags.items():
        if checks.get(name) is not expected:
            raise RuntimeError(f'Replay must explicitly report {name}={expected}')
    interval = {'entry': '0x5d47e', 'boundary_before': 'next-0x5d47e-or-exit' if full_loop else '0x5d4ba'}
    if checks.get('interval') != interval:
        raise RuntimeError('Replay did not execute the declared original gameplay interval')
    for key in ('restarts', 'frame_wait_vblanks'):
        if type(checks.get(key)) is not int or checks[key] < 0:
            raise RuntimeError(f'Replay must report a nonnegative {key}')
    if not full_loop and (checks['restarts'] or checks['frame_wait_vblanks']):
        raise RuntimeError('Legacy gameplay interval cannot report outer-loop restarts or frame waits')
    for key in ('compared_bytes', 'original_instructions'):
        if type(checks.get(key)) is not int or checks[key] <= 0:
            raise RuntimeError(f'Replay must report positive {key}')
    if type(checks.get('audio_register_writes')) is not int or checks['audio_register_writes'] < 0:
        raise RuntimeError('Replay must report a nonnegative ordered sample-audio write count')
    stages, projections = checks.get('stages'), checks.get('projection_byte_addresses')
    expected_stages = {stage: ticks for stage in REPLAY_STAGES}
    if full_loop:
        expected_stages['outer'] = ticks
        if checks['restarts']:
            expected_stages['restart'] = checks['restarts']
    if not isinstance(stages, dict) or set(stages) != set(expected_stages):
        raise RuntimeError('Replay stage map does not match the required comparison stages')
    if not isinstance(projections, dict) or set(projections) != set(expected_stages):
        raise RuntimeError('Replay must declare every stage byte-address projection')
    for stage, expected_count in expected_stages.items():
        if type(stages[stage]) is not int or stages[stage] != expected_count:
            raise RuntimeError(f'Replay did not execute/compare {stage} exactly {expected_count} times')
        addresses = projections[stage]
        if not isinstance(addresses, list) or not addresses or any(type(address) is not int or not 0 <= address < 0x80000 for address in addresses):
            raise RuntimeError(f'Replay has an invalid/empty byte-address projection for {stage}')
        if len(addresses) != len(set(addresses)):
            raise RuntimeError(f'Replay repeats a byte address in the {stage} projection')
    # The executable's legacy compared_bytes counter also includes one comparison
    # for each occupied/unoccupied pixel in the 256x8 damage strip per tick.
    expected_comparisons = sum(expected_stages[stage] * len(addresses) for stage, addresses in projections.items()) + ticks * 256 * 8
    if checks['compared_bytes'] != expected_comparisons:
        raise RuntimeError('Replay comparison count does not match its declared projections and damage occupancy checks')


def verify_native_trace(native_trace, original_trace, inputs, ticks, checks):
    if type(checks.get('ticks')) is not int or checks['ticks'] != ticks:
        raise RuntimeError('Native-only runner did not report every requested tick')
    if checks.get('cpu_emulation') is not False or checks.get('comparison_performed') is not False:
        raise RuntimeError('Native-only runner must explicitly declare no CPU emulation or internal comparison')
    trace_checks = verify_trace(native_trace, inputs, ticks, original_execution=False)
    with native_trace.open(encoding='ascii', newline='') as stream:
        native_rows = list(csv.DictReader(stream))
    with original_trace.open(encoding='ascii', newline='') as stream:
        original_rows = list(csv.DictReader(stream))
    if len(original_rows) != ticks:
        raise RuntimeError('Oracle trace row count changed before native-only comparison')
    gameplay_fields = [field for field in TRACE_FIELDS if field != 'original_instructions']
    for tick, (native_row, original_row) in enumerate(zip(native_rows, original_rows)):
        for field in gameplay_fields:
            if int(native_row[field]) != int(original_row[field]):
                raise RuntimeError(f'Native-only trace differs at tick {tick}, field {field}: native={native_row[field]}, original={original_row[field]}')
    trace_checks.update({'gameplay_columns_match_oracle_trace': True, 'compared_fields': gameplay_fields})
    return trace_checks


def verify_leaf_result(checks, target):
    if type(checks.get('mismatches')) is not int or checks['mismatches'] != 0:
        raise RuntimeError(f'{target} did not report zero mismatches')
    if type(checks.get('total_cases')) is not int or checks['total_cases'] <= 0:
        raise RuntimeError(f'{target} did not report a positive number of comparisons')


def verify_spans(report, payload, ram):
    records = []
    for span in report['routines'] + report.get('tables', []):
        start, end = int(span['runtime_address'], 0), int(span['end_exclusive'], 0)
        if not BASE <= start < end <= BASE + len(payload):
            raise RuntimeError(f'Invalid original span bounds: {span["name"]}')
        original = payload[start - BASE:end - BASE]
        if hashlib.sha256(original).hexdigest() != span['sha256']:
            raise RuntimeError(f'Original span hash mismatch: {span["name"]}')
        mutations = []
        for offset, (loaded_byte, disk_byte) in enumerate(zip(ram[start:end], original)):
            if loaded_byte == disk_byte:
                continue
            address = start + offset
            mask = PIXEL_OPCODE_BITS.get(address, 0) if start == 0x663f2 and end == 0x6642a else 0
            if (loaded_byte ^ disk_byte) & (0xff ^ mask):
                raise RuntimeError(f'Unexpected captured code/table bit change: {span["name"]} at {address:#x}')
            mutations.append({'address': hex(address), 'disk_byte': disk_byte, 'captured_byte': loaded_byte,
                              'allowed_mutable_bits': hex(mask), 'reason': 'Original setPixelColor BSET/BCLR selection'})
        record = {key: span[key] for key in ('name', 'runtime_address', 'end_exclusive', 'sha256')}
        record['captured_sha256'] = hashlib.sha256(ram[start:end]).hexdigest()
        record['original_runtime_opcode_mutations'] = mutations
        records.append(record)
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, default=ROOT / 'analysis/captures/race',
                        help='Extracted loaded local-practice capture directory')
    parser.add_argument('--inputs', type=Path, action='append',
                        help='CSV with tick,controls[,quit]; repeat for independent scenarios')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/driving-replay/check-results.json')
    parser.add_argument('--skip-build', action='store_true', help='Use an existing executable newer than its sources')
    parser.add_argument('--verify-only', action='store_true', help='Check inputs and identities without building or executing a replay')
    parser.add_argument('--run-leaf-tests', action='store_true',
                        help='Build/run supporting differential oracles using this capture and record fresh provenance')
    parser.add_argument('--native-only-verify', action='store_true',
                        help='Also run the executable without Musashi and compare its gameplay trace with the oracle trace')
    parser.add_argument('--additional-reference', type=Path, action='append', default=[],
                        help='Additional original code/table manifest to validate against disk and capture')
    parser.add_argument('--full-loop', action='store_true',
                        help='Include outer restart/race-end/buffer gates with persistent original CPU context')
    parser.add_argument('--require-complete-lap', action='store_true',
                        help='Require a full observed track cycle between two lap crossings (requires --full-loop)')
    args = parser.parse_args()
    if args.verify_only and (args.run_leaf_tests or args.native_only_verify):
        parser.error('--verify-only cannot be combined with --run-leaf-tests or --native-only-verify')
    if args.require_complete_lap and (not args.full_loop or args.verify_only):
        parser.error('--require-complete-lap requires --full-loop and an executed replay')
    output = args.output.resolve()
    out = output.parent
    out.mkdir(parents=True, exist_ok=True)
    # A failed rerun must not leave a previous run's passing report at the
    # requested output path. Detailed logs/identities are written below.
    output.write_text(json.dumps({'status': 'verification_in_progress',
        'started_at_utc': datetime.now(timezone.utc).isoformat(), 'replay_executed': False}, indent=2) + '\n', encoding='utf-8')
    capture = args.capture.resolve()
    manifest_path = capture / 'manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    bank = next(item for item in manifest['banks'] if item['tag'] == 'CRAM')
    if int(bank['base'], 0) != 0 or bank['size'] != 0x80000:
        raise RuntimeError('Replay requires 512K chip RAM mapped at zero')
    chip = (capture / bank['file']).resolve()
    capture_record = {'manifest': identity(manifest_path),
                      'source': identity(Path(manifest['source_path']), manifest['source_sha256']),
                      'chip_ram': identity(chip, bank['sha256'])}
    ram = chip.read_bytes()
    if len(ram) != 0x80000:
        raise RuntimeError('Capture chip-RAM file is not exactly 512K')
    if not 0 < ram[0x1ca1a] <= 100 or ram[0x1bb1c] >= ram[0x1ca1a]:
        raise RuntimeError('Replay requires a valid loaded track and player segment')
    if ram[0x57c3c] != 0 or ram[0x1ca22] & 0x80:
        raise RuntimeError('This autonomous replay supports local practice only; network/opponent AI is not substituted')

    adf_path = ROOT / 'Stunt Car Racer (MicroStyle).adf'
    adf_id = identity(adf_path, ADF_SHA)
    payload_path = ROOT / 'analysis/reference/vesuri/offline-extracted-payload.bin'
    payload_id = identity(payload_path, PAYLOAD_SHA)
    payload = payload_path.read_bytes()
    if len(payload) != PAYLOAD_SIZE or adf_path.read_bytes()[DISK_OFFSET:DISK_OFFSET + PAYLOAD_SIZE] != payload:
        raise RuntimeError('Payload does not match its documented original disk range')
    lock = prepare()
    scenarios = [path.resolve() for path in (args.inputs or sorted((ROOT / 'tests/replays').glob('*.csv')))]
    if not scenarios:
        raise RuntimeError('No input scenarios; supply --inputs or add tests/replays/*.csv')
    inputs = [read_inputs(path) for path in scenarios]
    if not args.full_loop and any(item['quit_ticks'] for item in inputs):
        raise ValueError('Quit controls require --full-loop; the legacy interval stops before that query')
    lap_initial = initial_lap_state(ram) if args.full_loop else None

    comparisons = {}
    for directory, script in VERIFIERS:
        subprocess.run([sys.executable, str(ROOT / 'scripts/reference' / script), '--capture', str(capture)],
                       cwd=ROOT, check=True)
        path = ROOT / 'analysis' / directory / 'reference-comparison.json'
        report = json.loads(path.read_text(encoding='utf-8'))
        comparisons[directory] = {'manifest': identity(path), 'spans': verify_spans(report, payload, ram)}
    for index, path in enumerate(args.additional_reference):
        path = path.resolve()
        comparisons[f'additional-{index}'] = {'manifest': identity(path),
            'spans': verify_spans(json.loads(path.read_text(encoding='utf-8')), payload, ram)}
    core = {'routines': [{'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                          'sha256': hashlib.sha256(payload[start - BASE:end - BASE]).hexdigest()}
                         for name, start, end in CORE_SPANS]}
    core_spans = verify_spans(core, payload, ram)

    build = ROOT / 'analysis/wheel/build'
    exe = build / 'Release/driving_replay_oracle.exe'
    native_exe = build / 'Release/driving_replay_native.exe'
    native_sources = [ROOT / 'CMakeLists.txt', ROOT / 'tests/driving_replay_oracle.cpp',
                      ROOT / 'tests/driving_replay_native.cpp']
    native_sources += sorted((ROOT / 'src').glob('*.cpp')) + sorted((ROOT / 'src').glob('*.h'))
    native_sources += sorted((ROOT / 'tests').glob('*fixture.h'))
    native_sources += sorted((ROOT / 'tests').glob('driving_replay*.h'))
    native_sources = sorted(set(native_sources))
    native_source_identities = [identity(path) for path in native_sources]
    commands = []
    results = []
    fresh_leaf_results = {}
    if not args.verify_only:
        leaf_targets = [name.replace('-', '_') + '_oracle' for name, _ in VERIFIERS] if args.run_leaf_tests else []
        targets = ['driving_replay_oracle', *leaf_targets]
        if args.native_only_verify:
            targets.append('driving_replay_native')
        if not args.skip_build:
            commands = [
                ['cmake', '-S', str(ROOT), '-B', str(build), '-G', 'Visual Studio 18 2026', '-A', 'x64',
                 '-DSCR_BUILD_ORACLE=ON', f'-DMUSASHI_DIR={DEST.as_posix()}'],
                ['cmake', '--build', str(build), '--config', 'Release', '--target', *targets, '--parallel', '4'],
            ]
            with (out / 'build.log').open('w', encoding='utf-8') as log:
                for command in commands:
                    log.write(json.dumps(command) + '\n')
                    log.flush()
                    subprocess.run(command, cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT)
        for target in targets:
            executable = build / 'Release' / (target + '.exe')
            if not executable.exists():
                raise RuntimeError(f'{target} does not exist; run without --skip-build')
            sources = native_sources + [ROOT / 'tests' / (target + '.cpp')]
            if args.skip_build and any(path.stat().st_mtime_ns > executable.stat().st_mtime_ns for path in sources):
                raise RuntimeError(f'{target} predates a native source/fixture; rebuild without --skip-build')
        if args.run_leaf_tests:
            leaf_out = ROOT / 'analysis/outer-loop'
            leaf_out.mkdir(parents=True, exist_ok=True)
            for name, _ in VERIFIERS:
                target = name.replace('-', '_') + '_oracle'
                executable = build / 'Release' / (target + '.exe')
                result_path = leaf_out / (name.replace('-', '_') + '-oracle.json')
                stderr_path = result_path.with_name(result_path.stem + '-stderr.txt')
                command = [str(executable), str(payload_path), 'replay-capture', str(chip)]
                executable_before = identity(executable)
                leaf_source = ROOT / 'tests' / (target + '.cpp')
                leaf_source_before = identity(leaf_source)
                run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
                result_path.write_text(run.stdout, encoding='utf-8')
                stderr_path.write_text(run.stderr, encoding='utf-8')
                if run.returncode:
                    raise RuntimeError(f'{target} failed ({run.returncode}); see {stderr_path}')
                checks = json.loads(run.stdout)
                verify_leaf_result(checks, target)
                identity(executable, executable_before['sha256'])
                identity(leaf_source, leaf_source_before['sha256'])
                identity(payload_path, payload_id['sha256'])
                identity(chip, capture_record['chip_ram']['sha256'])
                record = {'status': 'executed_this_run', 'verified_at_utc': datetime.now(timezone.utc).isoformat(),
                          'identity': identity(result_path), 'stderr': identity(stderr_path), 'results': checks,
                          'executable': executable_before, 'command': command, 'payload': payload_id,
                          'capture': capture_record, 'musashi_commit': lock['commit'],
                          'sources': native_source_identities + [leaf_source_before]}
                provenance_path = result_path.with_name(result_path.stem + '-provenance.json')
                provenance_path.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
                record['provenance_manifest'] = identity(provenance_path)
                fresh_leaf_results[name] = record
                print(f'{target}: {checks["total_cases"]} cases; zero mismatches.')
        for index, (scenario, input_record) in enumerate(zip(scenarios, inputs)):
            stem = f'{index:02d}-{scenario.stem}'
            trace = out / (stem + '-trace.csv')
            trace.unlink(missing_ok=True)
            command = [str(exe), str(chip), str(scenario), str(trace)]
            if args.full_loop:
                command.append('--full-loop')
            executable_before = identity(exe)
            run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            stdout = out / (stem + '-stdout.json')
            stderr = out / (stem + '-stderr.txt')
            stdout.write_text(run.stdout, encoding='utf-8')
            stderr.write_text(run.stderr, encoding='utf-8')
            if run.returncode:
                raise RuntimeError(f'Replay {scenario.name} failed ({run.returncode}); see {stderr} and {stdout}')
            checks = json.loads(run.stdout)
            verify_replay_result(checks, input_record['ticks'], args.full_loop)
            if not trace.exists() or not trace.stat().st_size:
                raise RuntimeError(f'Replay {scenario.name} did not produce its trace')
            trace_checks = verify_trace(trace, scenario, input_record['ticks'])
            if trace_checks['original_instructions'] != checks['original_instructions']:
                raise RuntimeError(f'Replay {scenario.name}: trace/result instruction totals differ')
            identity(scenario, input_record['identity']['sha256'])
            identity(chip, capture_record['chip_ram']['sha256'])
            identity(exe, executable_before['sha256'])
            result = {'inputs': input_record, 'command': command, 'results': checks,
                            'executable': executable_before,
                            'trace': identity(trace), 'trace_checks': trace_checks,
                            'stdout': identity(stdout), 'stderr': identity(stderr)}
            if args.full_loop:
                result['lap_validation'] = validate_laps(trace, lap_initial, args.require_complete_lap)
                if result['lap_validation']['restart_events']:
                    trace_restarts = result['lap_validation']['restart_events'][-1]['count']
                else:
                    trace_restarts = 0
                if trace_restarts != checks['restarts']:
                    raise RuntimeError(f'Replay {scenario.name}: trace/result restart totals differ')
            if args.native_only_verify:
                native_trace = out / (stem + '-native-trace.csv')
                native_trace.unlink(missing_ok=True)
                native_command = [str(native_exe), str(chip), str(scenario), str(native_trace)]
                if args.full_loop:
                    native_command.append('--full-loop')
                native_before = identity(native_exe)
                native_run = subprocess.run(native_command, cwd=ROOT, capture_output=True, text=True)
                native_stdout = out / (stem + '-native-stdout.json')
                native_stderr = out / (stem + '-native-stderr.txt')
                native_stdout.write_text(native_run.stdout, encoding='utf-8')
                native_stderr.write_text(native_run.stderr, encoding='utf-8')
                if native_run.returncode:
                    raise RuntimeError(f'Native-only replay {scenario.name} failed ({native_run.returncode}); see {native_stderr}')
                native_checks = json.loads(native_run.stdout)
                native_trace_checks = verify_native_trace(native_trace, trace, scenario, input_record['ticks'], native_checks)
                identity(trace, result['trace']['sha256'])
                identity(native_exe, native_before['sha256'])
                identity(scenario, input_record['identity']['sha256'])
                identity(chip, capture_record['chip_ram']['sha256'])
                result['native_only_verification'] = {'command': native_command, 'results': native_checks,
                    'executable': native_before, 'trace': identity(native_trace), 'trace_checks': native_trace_checks,
                    'stdout': identity(native_stdout), 'stderr': identity(native_stderr)}
            results.append(result)
            print(f'{scenario.name}: {checks["ticks"]} ticks; zero mismatches.')

    for path, before in zip(native_sources, native_source_identities):
        identity(path, before['sha256'])

    source_paths = native_sources + [Path(__file__), ROOT / 'scripts/prepare_wheel_oracle.py', ROOT / 'scripts/replay_lap.py']
    source_paths += sorted((ROOT / 'scripts/reference').glob('verify_*.py'))
    leaf_results = {}
    for name, _ in VERIFIERS:
        if name in fresh_leaf_results:
            leaf_results[name] = fresh_leaf_results[name]
            continue
        path = ROOT / 'analysis/outer-loop' / (name.replace('-', '_') + '-oracle.json')
        if not path.exists():
            leaf_results[name] = {'status': 'not_available', 'expected_path': str(path.relative_to(ROOT))}
            continue
        try:
            leaf = json.loads(path.read_text(encoding='utf-8-sig'))
        except (ValueError, UnicodeError):
            if not args.verify_only:
                raise RuntimeError(f'Retained leaf result is incomplete/invalid: {path}')
            leaf_results[name] = {'status': 'incomplete_or_invalid', 'identity': identity(path)}
            continue
        verify_leaf_result(leaf, str(path))
        leaf_results[name] = {'status': 'retained_not_rerun', 'identity': identity(path), 'results': leaf,
                              'provenance': 'Existing result artifact; this replay wrapper did not execute that leaf test.'}
    report = {
        'verified_at_utc': datetime.now(timezone.utc).isoformat(),
        'status': 'identities_only_no_replay' if args.verify_only else 'passed',
        'adf': adf_id, 'payload': payload_id, 'payload_mapping': {'base': hex(BASE), 'disk_offset': hex(DISK_OFFSET), 'size': PAYLOAD_SIZE},
        'capture': capture_record, 'musashi_commit': lock['commit'], 'musashi_lock': identity(LOCK),
        'reference_comparisons': comparisons, 'core_spans': core_spans, 'input_scenarios': inputs,
        'build_commands': commands, 'build_skipped': args.skip_build, 'leaf_tests_requested': args.run_leaf_tests,
        'native_only_verification_requested': args.native_only_verify,
        'full_loop_requested': args.full_loop, 'complete_lap_required': args.require_complete_lap,
        'executable': None if args.verify_only else identity(exe),
        'sources': [identity(path) for path in source_paths], 'replays': results, 'leaf_verification_results': leaf_results,
        'ticks_compared': sum(item['results']['ticks'] for item in results),
        'complete_laps_observed': sum(item.get('lap_validation', {}).get('complete_laps', 0) for item in results),
        'interval': {'entry': '0x5d47e', 'boundary_before': 'next-0x5d47e-or-exit' if args.full_loop else '0x5d4ba'},
        'limitations': [
            'Local practice only. Opponent collision stages have separate tests; autonomous opponent AI and network play are outside this replay.',
            'Input origin is not established by CSV controls alone; checked-in scenarios are authored experiments.',
            'The native side independently evolves gameplay state; original outputs must never supply its per-tick track position or physics inputs.',
            'The original CPU renderer executes with a blitter-done boundary. Pixel output, blitter operation, DMA and interrupt timing are not reproduced.',
            'The compared gameplay projection and audio effects are declared by the executable result; this wrapper does not imply whole-RAM/frame equality.',
            ('Full-loop mode includes outer restart/race-end and buffer gates. Terminal traces stop before waitForFire or quit fade; menu return/presentation are outside this replay.'
             if args.full_loop else 'The legacy interval stops before outer pause/restart/end gates and display-buffer synchronization.'),
            'Headless frame waits explicitly supply vblank progress; real interrupt scheduling and display timing are not reproduced.',
            'No interrupts are interleaved. Asynchronous device state is supplied at the explicit headless hardware boundary.',
            'Finite replay agreement does not establish exhaustive input coverage or whole-game fidelity.',
        ],
    }
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
