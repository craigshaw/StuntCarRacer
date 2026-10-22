"""Compare fresh native asset startup with unmodified original 68000 routines.

All payload, captured reference and generated output bytes remain under ignored
local directories. The native routine itself only receives zero RAM plus bytes
extracted directly from the supplied ADF.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
ADF_SHA = '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA = '508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
BASE, DISK_OFFSET, PAYLOAD_BYTES = 0xe700, 0xdc00, 408540


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--capture', type=Path, help='Optional independently captured immutable asset comparison')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/native-assets-init')
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    if sha(adf) != ADF_SHA:
        raise ValueError('Original ADF identity changed')
    payload = adf[DISK_OFFSET:DISK_OFFSET + PAYLOAD_BYTES]
    if sha(payload) != PAYLOAD_SHA:
        raise ValueError('Original payload identity changed')
    args.output.mkdir(parents=True, exist_ok=True)
    original = args.output / 'original-payload.bin'
    original.write_bytes(payload)
    executable = args.oracle.resolve()
    executable_sha = sha(executable.read_bytes())
    command = [str(executable), str(original.resolve())]
    capture = None
    if args.capture:
        manifest = json.loads((args.capture / 'manifest.json').read_text())
        bank = next(item for item in manifest['banks'] if item['tag'] == 'CRAM')
        chip = args.capture / bank['file']
        chip_bytes = chip.read_bytes()
        if len(chip_bytes) != 0x80000 or sha(chip_bytes) != bank['sha256']:
            raise ValueError('Captured Chip RAM reference identity changed')
        capture = {'chip_ram_sha256': bank['sha256'], 'bytes': len(chip_bytes),
                   'use': 'test comparison only; not a native startup input'}
        command.append(str(chip.resolve()))
    run = subprocess.run(command, check=True, capture_output=True, text=True)
    results = json.loads(run.stdout)
    if results.get('mismatches') != 0 or results.get('total_cases') != 4 or results.get('native_capture_input') is not False:
        raise ValueError('Original/native startup comparison did not pass')
    if sha(executable.read_bytes()) != executable_sha:
        raise ValueError('Oracle executable changed during verification')
    if sha(original.read_bytes()) != PAYLOAD_SHA:
        raise ValueError('Oracle original payload changed during verification')
    if capture and sha(chip.read_bytes()) != capture['chip_ram_sha256']:
        raise ValueError('Captured reference changed during verification')
    routines = []
    for name, first, end in [
        ('initialize', 0xed56, 0xee8a),
        ('generateEngineSamples', 0xf402, 0xf472),
        ('setupFrameBufferAddresses', 0x1b7b6, 0x1b7dc),
        ('initializeGraphicsData', 0x69cfc, 0x69d4e),
        ('loadGraphicsElement with bitmap and sprite helpers', 0x69d4e, 0x69e9a),
        ('decompressRLEObjectToMask', 0x59450, 0x594c6),
        ('font patch', 0x5aa02, 0x5aa38),
        ('copyMainGameBackground', 0x1bace, 0x1baf8),
        ('decompressRLEImage', 0x593f8, 0x59450),
    ]:
        data = payload[first - BASE:end - BASE]
        routines.append({'name': name, 'runtime_address': hex(first), 'end_exclusive': hex(end),
                         'adf_offset': hex(first - BASE + DISK_OFFSET), 'bytes': len(data), 'sha256': sha(data)})
    record = {'status': 'passed', 'adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA,
              'command': command, 'executable_sha256': executable_sha, 'results': results,
              'capture_comparison': capture, 'routines': routines,
              'source_pcm': {'runtime_address': '0xf4b8', 'adf_offset': hex(0xf4b8 - BASE + DISK_OFFSET),
                             'bytes': 46482, 'sha256': sha(payload[0xf4b8 - BASE:0x1aa4a - BASE]),
                             'operation': 'XOR 0x80 followed by original engine bank generation'},
              'rea_evidence': {'bundle': 'analysis/rea/native-startup-evidence.json',
                               'initialize': 'ev_ee09524338ca4cb4b1cfcf73a4f3b153bd547a532a56e69de64a237e6a2cb9e0',
                               'graphics': 'ev_5c768550df78b85825678438cf671a008f1e7764e5dbaa97eddec8733fc36f6a',
                               'graphic_element': 'ev_3831d11eac7433c2ec3d62e02122ef3c0ff2b5b343f3b0b40385c1101b227664'},
              'comparison_domain': 'All 524288 RAM bytes except 68000 hardware interrupt vectors 0x64..0x80; hardware I/O writes accepted in test only.',
              'limitations': ['Native startup deliberately omits Amiga OS interrupt vectors and hardware I/O.',
                              'Graphics and audio memory results are verified, not Amiga DMA or video beam timing.']}
    files = ['app/native_asset_initialization.h', 'app/native_asset_initialization.cpp',
             'tests/oracle/native_asset_initialization_tests.cpp', 'scripts/reference/verify_native_asset_initialization.py',
             'src/engine_audio.cpp', 'src/planar_image.cpp', 'app/replay_render_assets.cpp']
    files += [path.relative_to(ROOT).as_posix() for folder in ('src/state', 'tests/support')
              for path in sorted((ROOT / folder).glob('*.h'))]
    record['sources'] = [{'path': name, 'sha256': sha((ROOT / name).read_bytes())} for name in files]
    destination = args.output / 'check-results.json'
    destination.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'report': str(destination), **results}, indent=2))


if __name__ == '__main__':
    main()
