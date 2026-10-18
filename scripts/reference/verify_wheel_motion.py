"""Pin the original wheel-speed/damping bytes and enumerate enhanced-source changes."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re

from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

ROOT = Path(__file__).resolve().parents[2]
REF = ROOT / 'analysis/reference/vesuri'
BASE, DISK_OFFSET, SIZE = 0xe700, 0xdc00, 408540
ADF_SHA = '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA = '508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
SOURCE_SHA = '321034a19432bc23f2373acb68a121ee46dc4c5ed382e6df30ee05aff3c7924a'
COMMIT = '3cce78d193c3d0208be34d0bbfc3155e787cd406'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, default=ROOT / 'analysis/captures/race')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/wheel-motion')
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf) == ADF_SHA, 'Original ADF changed')
    payload = adf[DISK_OFFSET:DISK_OFFSET + SIZE]
    require(sha(payload) == PAYLOAD_SHA, 'Original payload changed')
    source = (REF / 'StuntCarRacer.s').read_bytes()
    provenance = json.loads((REF / 'provenance.json').read_text())
    item = next(f for f in provenance['files'] if f['path'] == 'StuntCarRacer.s')
    require(provenance['commit'] == COMMIT and sha(source) == SOURCE_SHA == item['sha256'], 'Reference changed')
    lines = source.decode().splitlines()
    symbols = {}
    for line in lines:
        match = re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)', line, re.I)
        if match:
            symbols[match[1].lower()] = BASE + int(match[2], 16)
    spec = importlib.util.spec_from_file_location('transform_reference', Path(__file__).with_name('verify_transform_reference.py'))
    normalizer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(normalizer)
    capture = json.loads((args.capture / 'manifest.json').read_text())
    bank = next(b for b in capture['banks'] if b['tag'] == 'CRAM')
    chip = (args.capture / bank['file']).read_bytes()
    require(int(bank['base'], 0) == 0 and len(chip) == bank['size'] == 0x80000, 'Unexpected chip mapping')
    require(sha(chip) == bank['sha256'], 'Capture RAM changed')
    require(sha(Path(capture['source_path']).read_bytes()) == capture['source_sha256'], 'Capture state changed')
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)

    def canonical(text, table=None):
        return re.sub(r'^(b\w+)\.[ws] ', r'\1 ', normalizer.canonical(text, table))

    def compare(name, start, end, source_lines, labels, changes):
        data = payload[start - BASE:end - BASE]
        require(chip[start:end] == data, name + ' capture differs from original')
        decoded = list(decoder.disasm(data, start))
        require(sum(i.size for i in decoded) == len(data), name + ' incomplete MC68000 decode')
        require(len(decoded) == len(source_lines), name + ' instruction count mismatch')
        instructions, differences = [], []
        for ins, number in zip(decoded, source_lines):
            text = lines[number - 1].lstrip(';').split(';')[0].strip()
            resolved = text.lower()
            for label, address in labels.items():
                resolved = resolved.replace(label, str(address))
            expected, actual = canonical(resolved, symbols), canonical(ins.mnemonic + ' ' + ins.op_str)
            change = changes.get(ins.address)
            if change:
                require(actual == change['original'] and expected == change['enhanced'],
                        f'{name} documented difference changed at {ins.address:#x}')
                differences.append({'address': hex(ins.address), 'source_line': number,
                                    'original': actual, 'enhanced': expected, 'reason': change['reason']})
            else:
                require(actual == expected, f'{name} {ins.address:#x}: {actual} != {expected}')
            instructions.append({'address': hex(ins.address), 'bytes': ins.bytes.hex(),
                                 'original': ins.mnemonic + ' ' + ins.op_str, 'source_line': number,
                                 'source': lines[number - 1].strip(), 'operands_equal': actual == expected})
        return {'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                'payload_offset': hex(start - BASE), 'disk_offset': hex(start - BASE + DISK_OFFSET),
                'size': len(data), 'sha256': sha(data), 'instruction_count': len(decoded),
                'capture_equals_original': True, 'operand_wildcards': 0,
                'source_differences': differences, 'instructions': instructions}

    speed_lines = [10238,10239,10240,10242,10243,10244,10245,10246,10247,10248,
                   10251,10252,10253,10254,10255,10258,10259,10260,10261,10263,10264]
    speed_changes = {
        0x60fe0: {'original': 'lsr.w #2,d0', 'enhanced': 'lsr.w #5,d0',
                  'reason': 'Original decays by one quarter per update; reference uses one thirty-second.'},
    }
    for address, original, enhanced in [
        (0x60fda, 'move.w 113762,d0', 'move.w 113760,d0'),
        (0x60fe2, 'sub.w d0,113762', 'sub.w d0,113760'),
        (0x60ff4, 'move.w d0,113762', 'move.w d0,113760'),
        (0x6100a, 'move.w d0,113762', 'move.w d0,113760'),
    ]:
        speed_changes[address] = {'original': original, 'enhanced': enhanced,
                                  'reason': 'Original wheel-speed word is 0x1bc62; enhanced wheelSpeed equ is 0x1bc60.'}
    speed = compare('updateWheelSpeed', 0x60fbe, 0x61012, speed_lines,
                    {'.velocitypositive': 0x60fca, '.wheelsmoving': 0x60fea,
                     '.highspeed': 0x60ffc, '.wheelspeedok': 0x6100a}, speed_changes)
    damping_lines = [11464,11467,11468,11469,11470,11471,11476,11477,
                     11478,11480,11481,11484,11489,11490,11491,11492]
    damping = compare('updateVelocityDamping', 0x62138, 0x6217a, damping_lines,
                      {'lbc053a5c': 0x6215c}, {
        0x6214e: {'original': 'beq 401756', 'enhanced': 'bra 401756',
                  'reason': 'Original conditionally skips yaw contribution after TST.B; enhanced branch is unconditional.'},
    })
    added = [11465,11466,11473,11474,11475,11482,11483,11485,11487]
    active = [n for n in range(11464,11493) if lines[n-1].split(';')[0].strip()
              and not lines[n-1].split(';')[0].strip().endswith(':')]
    require(set(active) == (set(damping_lines) - {11470}) | set(added), 'Unaccounted enhanced damping instructions')
    damping['enhanced_only_instructions'] = [{'source_line': n, 'source': lines[n-1].strip()} for n in added]
    damping['original_commented_out_in_reference'] = [{'address': '0x62148', 'source_line': 11470,
                                                       'instruction': 'TST.B wheelMovementActive'}]
    record = {
        'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA, 'reference_commit': COMMIT,
        'reference_source_sha256': SOURCE_SHA, 'reference_url': item['url'],
        'capture': {'directory': str(args.capture.resolve()), 'ram_sha256': sha(chip), 'source_sha256': capture['source_sha256']},
        'routines': [speed, damping], 'tables': [],
        'entry_condition': 'No register-value preconditions; both routines return by RTS without calling helpers.',
        'mutable_words': {'carSpeedMagnitude': '0x1bd5c', 'wheelSpeed_original': '0x1bc62',
                          'angularAccelerationX': '0x1bcfc', 'angularAccelerationZ': '0x1bd00'},
        'preserved_memory': 'Every other byte, including original 0x1bc60 and angularAccelerationY at 0x1bcfe.',
        'quirks_preserved': [
            'NEG.W of 0x8000 stays negative; signed BGE selects the low-speed branch, whose ASL.W #3 yields zero.',
            'Inactive wheel decay uses unsigned logical shift and can leave a residual speed of 1, 2 or 3.',
            'High-speed saturation tests carry from ADD.W #0x3000 after the shift, not carry from ASL.W.',
            'Both damping shifts are arithmetic right by4; movement byte gates only yaw contribution arithmetic right by2.',
            'Every nonzero movement byte enables the moving path; no sign-bit-only test.',
            'No scratch bytes or memory byte aliases are written by these functions.',
        ],
    }
    args.output.mkdir(parents=True, exist_ok=True)
    report = args.output / 'reference-comparison.json'
    report.write_text(json.dumps(record, indent=2) + '\n')
    listing = []
    for routine in record['routines']:
        listing.append('; ' + routine['name'] + ' SHA256 ' + routine['sha256'])
        listing.extend(f"{i['address']} {i['bytes']:24} {i['original']}" for i in routine['instructions'])
        listing.append('')
    (args.output / 'original-disassembly.asm').write_text('\n'.join(listing))
    print(json.dumps({'report': str(report), 'original_instructions': 37, 'operand_wildcards': 0,
                      'wheel_instruction_or_operand_differences': len(speed['source_differences']),
                      'damping_added_instructions': len(added), 'damping_removed_instructions': 1,
                      'damping_changed_branches': 1}, indent=2))


if __name__ == '__main__':
    main()
