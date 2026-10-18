"""Verify the original body-height calculation, trig helpers and capture identity."""
import argparse
import json
from pathlib import Path
import re

from verify_transform_reference import (
    ROOT, REFERENCE, BASE, DISK_OFFSET, PAYLOAD_SIZE, ADF_SHA, PAYLOAD_SHA,
    COMMIT, SOURCE_SHA, sha, require, compare_routine,
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, default=ROOT / 'analysis/captures/race')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/expected-heights')
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf) == ADF_SHA, 'Original ADF hash mismatch')
    payload = adf[DISK_OFFSET:DISK_OFFSET + PAYLOAD_SIZE]
    require(sha(payload) == PAYLOAD_SHA, 'Payload hash mismatch')
    source = (REFERENCE / 'StuntCarRacer.s').read_bytes()
    require(sha(source) == SOURCE_SHA, 'Pinned reference hash mismatch')
    provenance = json.loads((REFERENCE / 'provenance.json').read_text(encoding='utf-8'))
    require(provenance['commit'] == COMMIT, 'Reference commit mismatch')
    lines = source.decode('utf-8').splitlines()
    symbols = {'calculatesine': 0x64d08, 'calculatecosine': 0x64d10}
    for line in lines:
        match = re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)', line, re.I)
        if match:
            symbols[match[1].lower()] = BASE + int(match[2], 16)
        match = re.match(r'^(lbC[0-9a-f]+):', line, re.I)
        if match:
            symbols[match[1].lower()] = BASE + int(match[1][3:], 16)
    manifest = json.loads((args.capture / 'manifest.json').read_text(encoding='utf-8'))
    bank = next(b for b in manifest['banks'] if b['tag'] == 'CRAM')
    ram = (args.capture / bank['file']).read_bytes()
    require(int(bank['base'], 0) == 0 and bank['size'] == len(ram) == 0x80000,
            'Expected 512K chip RAM at zero')
    require(sha(ram) == bank['sha256'], 'Capture RAM hash mismatch')
    require(sha(Path(manifest['source_path']).read_bytes()) == manifest['source_sha256'],
            'Capture savestate hash mismatch')
    routines = [
        compare_routine(lines, symbols, payload, ram, 'calculateExpectedTrackSurfaceHeights',
                        'updateWheelSuspensionPhysics', 0x61b70, 0x61bcc),
        compare_routine(lines, symbols, payload, ram, 'calculateSine',
                        'calculateDivision', 0x64d08, 0x64d66),
    ]
    table = payload[0x1ca42 - BASE:0x1cc46 - BASE]
    require(sha(table) == '8bb8fb619a21dfb7851e2587cda428d4313a0497c8a2be87155e75e72764cb7e',
            'Original trig table hash mismatch')
    require(table == ram[0x1ca42:0x1cc46], 'Capture trig table differs')
    record = {
        'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA,
        'reference_commit': COMMIT, 'source_sha256': SOURCE_SHA,
        'routines': routines,
        'tables': [{'name': 'trig_table', 'runtime_address': '0x1ca42',
                    'end_exclusive': '0x1cc46', 'payload_offset': hex(0x1ca42 - BASE),
                    'disk_offset': hex(DISK_OFFSET + 0x1ca42 - BASE),
                    'size': len(table), 'sha256': sha(table), 'capture_bytes_equal_disk': True}],
        'inputs': {'pitch': '0x1bce4', 'roll': '0x1bce8', 'world_height': '0x1bcdc'},
        'outputs': {'front_left': '0x1bc94', 'front_right': '0x1bc98',
                    'rear_center': '0x1bc9c', 'pitch_sine_scratch_word': '0x1bbf6'},
        'capture': {'directory': str(args.capture.resolve()), 'ram_sha256': sha(ram),
                    'savestate_sha256': manifest['source_sha256']},
        'limitations': ['Static comparison does not establish runtime execution.',
                        'Expected heights and sampled road heights are separate inputs to suspension.',
                        'Physical coordinate conventions remain provisional.'],
    }
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'reference-comparison.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    listing = '\n'.join(f"{i['address']} {i['bytes']:24s} {i['original']}"
                        for r in routines for i in r['instructions'])
    (args.output / 'original-disassembly.asm').write_text(listing + '\n', encoding='utf-8')
    print(f"Expected heights: {routines[0]['instruction_count']} instructions; "
          'all operands and capture bytes match.')


if __name__ == '__main__':
    main()
