"""Verify the original angle-to-matrix-to-wheel chain against the native port."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

from native_build import ORACLE_BUILD, build_sources, configure_command
from prepare_wheel_oracle import prepare, ROOT, LOCK, DEST
from check_wheel_coordinates import identity, PAYLOAD_SHA, ROUTINE_SHA

# All addresses map to payload file offsets by subtracting the verified 0xe700.
SPANS = {
    'matrix_producer': (0x61368, 0x6158c, 'fe479d970f48c56a6dd32c57c4c167c70292bc15f78ab82de0587abb2be70242'),
    'trig_functions': (0x64d08, 0x64d66, '133f20902b3e7eebd47b30012e203b855266978bb715ced76d40ae501e3aad52'),
    'trig_table': (0x1ca42, 0x1cc46, '8bb8fb619a21dfb7851e2587cda428d4313a0497c8a2be87155e75e72764cb7e'),
    'wheel_coordinates': (0x618ce, 0x61950, ROUTINE_SHA),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, action='append',
                        help='Extracted capture directory with manifest.json; repeatable')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/transform/check-results.json')
    args = parser.parse_args()
    lock = prepare()
    out = ROOT / 'analysis/transform'
    out.mkdir(parents=True, exist_ok=True)
    payload_path = ROOT / 'analysis/reference/vesuri/offline-extracted-payload.bin'
    payload_id = identity(payload_path, PAYLOAD_SHA)
    payload = payload_path.read_bytes()
    span_records = {}
    for name, (start, end, digest) in SPANS.items():
        data = payload[start - 0xe700:end - 0xe700]
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError(f'Original {name} hash mismatch')
        fixture = out / f'{name}.bin'
        fixture.write_bytes(data)
        span_records[name] = {**identity(fixture, digest), 'start': hex(start),
                              'end_exclusive': hex(end), 'payload_offset': hex(start - 0xe700)}

    capture_ids, capture_args = [], []
    dirs = args.capture or [ROOT / 'analysis/captures/menu', ROOT / 'analysis/captures/menu-user']
    if not args.capture and (ROOT / 'analysis/captures/race/manifest.json').exists():
        dirs.append(ROOT / 'analysis/captures/race')
    for directory in dirs:
        directory = directory.resolve()
        manifest_path = directory / 'manifest.json'
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
        chip = next(b for b in manifest['banks'] if b['tag'] == 'CRAM')
        if int(chip['base'], 0) != 0 or chip['size'] != 0x80000:
            raise RuntimeError('Expected 512K chip RAM at zero')
        chip_path = directory / chip['file']
        ram_id = identity(chip_path, chip['sha256'])
        source_id = identity(Path(manifest['source_path']), manifest['source_sha256'])
        ram = chip_path.read_bytes()
        for name, (start, end, digest) in SPANS.items():
            if hashlib.sha256(ram[start:end]).hexdigest() != digest:
                raise RuntimeError(f'Capture {name} differs from original: {directory}')
        capture_ids.append({'label': directory.name, 'manifest': identity(manifest_path),
                            'source': source_id, 'chip_ram': ram_id,
                            'all_code_and_table_spans_match_original': True})
        capture_args += [directory.name, str(chip_path)]

    build = ORACLE_BUILD
    commands = [
        configure_command(oracle=True),
        ['cmake', '--build', str(build), '--config', 'Release', '--parallel', '4'],
    ]
    with (out / 'build.log').open('w', encoding='utf-8') as log:
        for command in commands:
            log.write(json.dumps(command) + '\n')
            log.flush()
            subprocess.run(command, cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT)
    exe = build / 'Release/transform_oracle.exe'
    result = subprocess.run([str(exe), str(payload_path), *capture_args], cwd=ROOT,
                            check=True, capture_output=True, text=True)
    checks = json.loads(result.stdout)
    report = {
        'verified_at_utc': datetime.now(timezone.utc).isoformat(), 'payload': payload_id,
        'original_spans': span_records, 'musashi_commit': lock['commit'],
        'musashi_lock': identity(LOCK), 'build_commands': commands,
        'sources': [identity(ROOT / p) for p in [
            'CMakeLists.txt', 'src/transform_matrices.cpp', 'src/transform_matrices.h',
            'src/wheel_coordinates.cpp', 'src/wheel_coordinates.h', 'tests/transform_oracle.cpp',
            'scripts/check_transform_matrices.py', 'scripts/check_wheel_coordinates.py',
            'scripts/prepare_wheel_oracle.py']] + [identity(path) for path in sorted(build_sources() - {ROOT / 'CMakeLists.txt'})],
        'executable': identity(exe), 'captures': capture_ids, 'results': checks,
        'limitations': [
            'All 65536 angle words are tested for each trig entry, with the verified original table.',
            'Matrix vector coverage is not all 2^64 combinations of four angle words.',
            'Checks coefficient memory, retained holes and downstream wheel outputs; not timing or CPU flags.',
            'Snapshot angles can be newer than stored coefficients: physics updates angles after matrix generation.',
            'Captured-input replay is not a WinUAE instruction trace or a proof of full-game fidelity.',
            'Pitch/yaw/roll names follow reference X/Y/Z angle fields; physical coordinate conventions remain provisional.',
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"Trig: {checks['trig_cases']}; matrices: {checks['matrix_cases']}; "
          f"chained wheels: {checks['chained_wheel_cases']}; mismatches: {checks['mismatches']}.")
    print(args.output)


if __name__ == '__main__':
    main()
