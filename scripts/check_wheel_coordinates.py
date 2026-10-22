"""Build and compare the native function with hash-verified original 68000 bytes."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

from native_build import ORACLE_BUILD, build_sources, configure_command
from prepare_wheel_oracle import prepare, ROOT, LOCK, DEST

PAYLOAD_SHA = '508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
ROUTINE_SHA = '1a9ca0b143b8dcfe574f2cffc5242309c0f40acaf2b03f9cef12dff394ee33aa'


def identity(path, expected=None):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if expected is not None and digest != expected:
        raise RuntimeError(f'SHA-256 mismatch: {path}')
    return {'path': str(path.relative_to(ROOT)), 'bytes': len(data), 'sha256': digest}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', action='append', type=Path,
                        help='Extracted capture directory containing manifest.json; repeatable')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/wheel/check-results.json')
    args = parser.parse_args()
    lock = prepare()
    out = ROOT / 'analysis/wheel'
    out.mkdir(parents=True, exist_ok=True)
    payload = ROOT / 'analysis/reference/vesuri/offline-extracted-payload.bin'
    payload_id = identity(payload, PAYLOAD_SHA)
    code = payload.read_bytes()[0x531ce:0x53250]
    if hashlib.sha256(code).hexdigest() != ROUTINE_SHA:
        raise RuntimeError('Original routine identity mismatch')
    fixture = out / 'wheel-original.bin'
    fixture.write_bytes(code)

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
            raise RuntimeError('Expected chip RAM mapped at zero')
        chip_path = directory / chip['file']
        ram_id = identity(chip_path, chip['sha256'])
        source_id = identity(Path(manifest['source_path']), manifest['source_sha256'])
        if chip_path.read_bytes()[0x618ce:0x61950] != code:
            raise RuntimeError(f'Capture routine differs from original: {directory}')
        capture_ids.append({'label': directory.name, 'manifest': identity(manifest_path),
                            'source': source_id, 'chip_ram': ram_id})
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
    exe = build / 'Release/wheel_oracle.exe'
    result = subprocess.run([str(exe), str(fixture), *capture_args], cwd=ROOT,
                            check=True, capture_output=True, text=True)
    checks = json.loads(result.stdout)
    report = {
        'verified_at_utc': datetime.now(timezone.utc).isoformat(),
        'payload': payload_id,
        'routine': {**identity(fixture, ROUTINE_SHA), 'payload_offset': '0x531ce',
                    'loaded_start': '0x618ce', 'loaded_end_exclusive': '0x61950'},
        'musashi_commit': lock['commit'], 'musashi_lock': identity(LOCK),
        'build_commands': commands,
        'sources': [identity(ROOT / p) for p in ['CMakeLists.txt', 'src/wheel_coordinates.cpp',
                    'src/wheel_coordinates.h', 'tests/wheel_oracle.cpp',
                    'scripts/check_wheel_coordinates.py', 'scripts/prepare_wheel_oracle.py']] + [identity(path) for path in sorted(build_sources() - {ROOT / 'CMakeLists.txt'})],
        'executable': identity(exe), 'captures': capture_ids, 'results': checks,
        'limitations': [
            'Checks six output words for isolated invocation, not flags/registers or timing.',
            'Test vectors cover boundaries, independent sweeps and random combinations; not all 2^96 inputs.',
            'Snapshot input replay is not an execution trace of the routine in WinUAE.',
            'Stored snapshot outputs may be stale or belong to a different invocation.',
            'Matrix meanings and units remain provisional; point names come from the matched reference.',
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"{checks['total_cases']} cases; {checks['mismatches']} mismatches. {args.output}")


if __name__ == '__main__':
    main()
