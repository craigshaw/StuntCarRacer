"""Verify the complete original physics driver and all newly recovered dependencies."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from native_build import ORACLE_BUILD, build_sources, configure_command
from prepare_wheel_oracle import prepare, ROOT, LOCK, DEST
from check_wheel_coordinates import identity, PAYLOAD_SHA

STAGES = [
    ('steering-response', 'verify_steering_response.py', 'steering_response_oracle'),
    ('car-dynamics', 'verify_car_dynamics.py', 'car_dynamics_oracle'),
    ('suspension-effects', 'verify_suspension_effects.py', 'suspension_effects_oracle'),
    ('audio-playback', 'verify_audio_playback.py', 'audio_playback_oracle'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, action='append',
                        help='Extracted capture directory; repeatable')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/physics-update/check-results.json')
    args = parser.parse_args()
    lock = prepare()
    out = ROOT / 'analysis/physics-update'
    out.mkdir(parents=True, exist_ok=True)
    payload_path = ROOT / 'analysis/reference/vesuri/offline-extracted-payload.bin'
    payload_id = identity(payload_path, PAYLOAD_SHA)
    payload = payload_path.read_bytes()
    dirs = args.capture or [ROOT / 'analysis/captures/menu', ROOT / 'analysis/captures/menu-user']
    if not args.capture and (ROOT / 'analysis/captures/race/manifest.json').exists():
        dirs.append(ROOT / 'analysis/captures/race')
    captures, capture_args, capture_ram = [], [], []
    for directory in dirs:
        directory = directory.resolve()
        manifest_path = directory / 'manifest.json'
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
        bank = next(b for b in manifest['banks'] if b['tag'] == 'CRAM')
        if int(bank['base'], 0) != 0 or bank['size'] != 0x80000:
            raise RuntimeError('Expected 512K chip RAM at zero')
        chip_path = directory / bank['file']
        captures.append({'label': directory.name, 'manifest': identity(manifest_path),
                         'source': identity(Path(manifest['source_path']), manifest['source_sha256']),
                         'chip_ram': identity(chip_path, bank['sha256'])})
        capture_ram.append(chip_path.read_bytes())
        capture_args += [directory.name, str(chip_path)]

    comparisons = {}
    for directory, verifier, _ in STAGES:
        # Each verifier pins the original ADF/payload and upstream source, then
        # resolves actual operands without executing the downloaded disassembly.
        subprocess.run([sys.executable, str(ROOT / 'tools/research/reference' / verifier),
                        '--capture', str(dirs[-1].resolve())], cwd=ROOT, check=True)
        report_path = ROOT / 'analysis' / directory / 'reference-comparison.json'
        report = json.loads(report_path.read_text(encoding='utf-8'))
        spans = report['routines'] + report.get('tables', [])
        for span in spans:
            start, end = int(span['runtime_address'], 0), int(span['end_exclusive'], 0)
            if not 0xe700 <= start < end <= 0xe700 + len(payload):
                raise RuntimeError('Invalid original span bounds')
            original = payload[start - 0xe700:end - 0xe700]
            if hashlib.sha256(original).hexdigest() != span['sha256']:
                raise RuntimeError('Original span hash mismatch')
            for ram in capture_ram:
                if ram[start:end] != original:
                    raise RuntimeError(f'Capture differs from original at {start:#x}')
        comparisons[directory] = {'identity': identity(report_path), 'spans': [
            {k: span[k] for k in ('name', 'runtime_address', 'end_exclusive', 'sha256')}
            for span in spans]}

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

    stage_results = {}
    run_targets = [(directory, target) for directory, _, target in STAGES]
    run_targets.append(('complete-driver', 'physics_update_oracle'))
    for directory, target in run_targets:
        exe = build / 'Release' / (target + '.exe')
        run = subprocess.run([str(exe), str(payload_path), *capture_args], cwd=ROOT,
                             capture_output=True, text=True)
        if run.returncode:
            raise RuntimeError(f'{target} failed ({run.returncode}):\n{run.stderr}\n{run.stdout}')
        checks = json.loads(run.stdout)
        if checks['mismatches'] != 0 or checks['total_cases'] <= 0:
            raise RuntimeError(f'{target} did not report passing comparisons')
        stage_results[directory] = {'executable': identity(exe), 'results': checks}
        print(f"{directory}: {checks['total_cases']} cases; zero mismatches.")

    source_paths = [ROOT / 'CMakeLists.txt', Path(__file__), ROOT / 'scripts/prepare_wheel_oracle.py',
                    ROOT / 'scripts/check_wheel_coordinates.py',
                    ROOT / 'tools/research/reference/verify_transform_reference.py']
    source_paths += sorted((ROOT / 'src').rglob('*.cpp')) + sorted((ROOT / 'src').rglob('*.h'))
    source_paths += [ROOT / 'tools/research/reference' / verifier for _, verifier, _ in STAGES]
    source_paths += [ROOT / 'tests/oracle' / (target + '.cpp') for _, _, target in STAGES]
    source_paths.append(ROOT / 'tests/oracle/physics_update_oracle.cpp')
    source_paths += sorted((ROOT / 'tests/support').glob('*fixture.h'))
    source_paths = sorted(set(source_paths) | build_sources())
    report = {
        'verified_at_utc': datetime.now(timezone.utc).isoformat(), 'payload': payload_id,
        'musashi_commit': lock['commit'], 'musashi_lock': identity(LOCK),
        'captures': captures, 'reference_comparisons': comparisons,
        'build_commands': commands, 'sources': [identity(path.resolve()) for path in source_paths],
        'stages': stage_results,
        'driver': {'entry': '0x6185c', 'end_exclusive': '0x618ce',
                   'sha256': hashlib.sha256(payload[0x6185c-0xe700:0x618ce-0xe700]).hexdigest()},
        'limitations': [
            'Complete synchronous original driver 0x6185c..0x618ce, not the outer game loop or an interrupt-interleaved Amiga execution.',
            'The native library has no CPU/RAM/emulator dependency; RAM adapters and Musashi are test-only.',
            'Audio is checked as ordered Paula register writes and final bookkeeping, not waveforms or DMA/interrupt timing.',
            'Segment arrays and sample parameters are runtime data decoded from verified captures; immutable tables remain local assets.',
            'The full driver needs a loaded track. The two menu captures have none and are explicitly skipped for that oracle.',
            'The original sampler/integration D1/D2 high-byte-zero preconditions are set once at driver entry.',
            'Persistent native sequences use scripted external inputs; these are not recorded human-driven laps.',
            'Finite adversarial/sweep/random comparisons are not exhaustive over all combined inputs and do not establish whole-game fidelity.',
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(args.output)


if __name__ == '__main__':
    main()
