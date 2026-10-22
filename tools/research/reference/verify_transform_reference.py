"""Compare the original transform producer/trig routines with pinned source.

No upstream code is executed. Decode the verified disk payload as MC68000,
resolve every source operand, and compare the complete instruction sequences.
Capture equality establishes code/table identity, not that mutable inputs and
outputs in an arbitrary snapshot belong to the same physics update.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct

from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

ROOT = Path(__file__).resolve().parents[3]
REFERENCE = ROOT / 'analysis/reference/vesuri'
BASE, DISK_OFFSET, PAYLOAD_SIZE = 0xe700, 0xdc00, 408540
COMMIT = '3cce78d193c3d0208be34d0bbfc3155e787cd406'
ADF_SHA = '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA = '508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
SOURCE_SHA = '321034a19432bc23f2373acb68a121ee46dc4c5ed382e6df30ee05aff3c7924a'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def canonical(instruction, symbols=None):
    """Normalize spelling aliases only; unknown symbols are an error."""
    parts = instruction.lower().split(None, 1)
    mnemonic = parts[0]
    operand = parts[1] if len(parts) > 1 else ''
    if symbols is not None:
        def resolve(match):
            token = match[0]
            if token in symbols:
                return str(symbols[token])
            if re.fullmatch(r'[ad][0-7]|sp|w|l', token):
                return token
            raise ValueError('Unresolved operand symbol: ' + token)
        operand = re.sub(r'(?<![\w$])[a-z_][\w]*', resolve, operand)
    operand = re.sub(r'\$([0-9a-f]+)', lambda m: str(int(m[1], 16)), operand)
    operand = re.sub(r'\ba7\b', 'sp', operand)
    # Source labels do not print Capstone's absolute-address .l suffix.
    # Keep register-index .w/.l suffixes: their distinction is significant.
    operand = re.sub(r'(?<![\w])(\d+)\.[wl]\b', r'\1', operand)
    operand = re.sub(r'\b0\((a[0-7]|sp),', r'(\1,', operand)
    mnemonic = re.sub(r'^(move|add|sub|cmp)a\.', r'\1.', mnemonic)
    mnemonic = re.sub(r'^(add|sub|and|or|eor|cmp)i\.', r'\1.', mnemonic)
    mnemonic = re.sub(r'^(muls|mulu|divs|divu)\.w$', r'\1', mnemonic)
    mnemonic = re.sub(r'^(bra|bne|bpl|ble)\.[ws]$', r'\1', mnemonic)
    return mnemonic + ' ' + re.sub(r'\s+', '', operand)


def compare_routine(lines, symbols, payload, chip, name, next_name, start, end):
    first = lines.index(name + ':')
    last = lines.index(next_name + ':')
    source = []
    for index in range(first + 1, last):
        text = lines[index].split(';')[0].strip()
        if text and not text.endswith(':'):
            source.append((index + 1, text))
    data = payload[start - BASE:end - BASE]
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    decoded = list(decoder.disasm(data, start))
    require(sum(i.size for i in decoded) == len(data), name + ': undecoded bytes')
    require(len(source) == len(decoded), name + ': instruction count differs')
    instructions = []
    for (line, text), ins in zip(source, decoded):
        actual = ins.mnemonic + ' ' + ins.op_str
        require(canonical(text, symbols) == canonical(actual),
                f'{name} at {ins.address:#x}: {text!r} != {actual!r}')
        instructions.append({'address': hex(ins.address), 'bytes': ins.bytes.hex(),
                             'original': actual, 'source_line': line, 'source': text})
    same = chip[start:end] == data
    require(same, name + ': race capture differs from original code')
    return {'name': name, 'source_line': first + 1, 'runtime_address': hex(start),
            'end_exclusive': hex(end), 'payload_offset': hex(start - BASE),
            'disk_offset': hex(DISK_OFFSET + start - BASE), 'size': len(data),
            'sha256': sha(data), 'instruction_count': len(decoded),
            'source_matches_all_instructions_and_operands': True,
            'operand_wildcards': 0, 'capture_bytes_equal_disk': same,
            'instructions': instructions}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, default=ROOT / 'analysis/captures/race')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/transform')
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf) == ADF_SHA, 'Original ADF hash mismatch')
    payload = adf[DISK_OFFSET:DISK_OFFSET + PAYLOAD_SIZE]
    require(sha(payload) == PAYLOAD_SHA, 'Original payload hash mismatch')
    provenance = json.loads((REFERENCE / 'provenance.json').read_text())
    require(provenance['commit'] == COMMIT, 'Reference commit mismatch')
    source_info = next(f for f in provenance['files'] if f['path'] == 'StuntCarRacer.s')
    source_bytes = (REFERENCE / source_info['path']).read_bytes()
    require(sha(source_bytes) == SOURCE_SHA == source_info['sha256'], 'Reference source hash mismatch')
    require(len(source_bytes) == source_info['size'], 'Reference source length mismatch')
    lines = source_bytes.decode('utf-8').splitlines()
    symbols = {}
    for line in lines:
        match = re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)', line, re.I)
        if match:
            symbols[match[1].lower()] = BASE + int(match[2], 16)
        match = re.match(r'^(lbC[0-9a-f]+):', line, re.I)
        if match:
            symbols[match[1].lower()] = BASE + int(match[1][3:], 16)
    # Both entry addresses are independently decoded below, not wildcards.
    symbols.update({'calculatesine': 0x64d08, 'calculatecosine': 0x64d10})
    capture = json.loads((args.capture / 'manifest.json').read_text())
    bank = next(b for b in capture['banks'] if b['tag'] == 'CRAM')
    chip_path = args.capture / bank['file']
    chip = chip_path.read_bytes()
    require(bank['base'] == '0x0' and len(chip) == bank['size'] == 0x80000,
            'Capture must contain 512 KiB chip RAM mapped at zero')
    require(sha(chip) == bank['sha256'], 'Capture RAM hash mismatch')
    save_path = Path(capture['source_path'])
    require(sha(save_path.read_bytes()) == capture['source_sha256'], 'Capture savestate hash mismatch')
    routines = [
        compare_routine(lines, symbols, payload, chip, 'calculateTransformMatrices',
                        'transformWorldCoordinates', 0x61368, 0x6158c),
        compare_routine(lines, symbols, payload, chip, 'calculateSine',
                        'calculateDivision', 0x64d08, 0x64d66),
    ]
    table_start, table_end = symbols['sinetable'], 0x1cc46
    table_data = payload[table_start - BASE:table_end - BASE]
    require(table_start == 0x1ca42 and len(table_data) == 516, 'Unexpected trig table extent')
    require(chip[table_start:table_end] == table_data, 'Capture trig table differs from disk')
    table = struct.unpack('>258H', table_data)
    matrix_base = symbols['lbl00db30']
    naming = []
    offsets = {'transformMatrix34': 0x34, 'transformMatrix38': 0x38,
               'transformMatrix3E': 0x3e, 'transformMatrix44': 0x42,
               'transformMatrix46': 0x44, 'transformMatrix48': 0x46}
    for name, expected_offset in offsets.items():
        address = symbols[name.lower()]
        require(address - matrix_base == expected_offset, 'Unexpected matrix field address')
        naming.append({'reference_name': name, 'runtime_address': hex(address),
                       'actual_offset_from_matrix_base': hex(address - matrix_base),
                       'captured_word': hex(int.from_bytes(chip[address:address + 2], 'big'))})
    record = {
        'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA,
        'payload_base': hex(BASE), 'payload_disk_offset': hex(DISK_OFFSET),
        'reference': {'repository': provenance['repository'], 'commit': COMMIT,
                      'source_url': source_info['url'], 'source_sha256': SOURCE_SHA},
        'capture': {'directory': str(args.capture.resolve()), 'ram_sha256': sha(chip),
                    'savestate_path': str(save_path), 'savestate_sha256': capture['source_sha256'],
                    'pc': capture['cpu']['registers']['PC']},
        'routines': routines,
        'trig_table': {'runtime_address': hex(table_start), 'end_exclusive': hex(table_end),
                       'payload_offset': hex(table_start - BASE),
                       'disk_offset': hex(DISK_OFFSET + table_start - BASE),
                       'size': len(table_data), 'word_count': len(table), 'sha256': sha(table_data),
                       'capture_bytes_equal_disk': True,
                       'unsigned_range': [min(table), max(table)],
                       'descending': all(a >= b for a, b in zip(table, table[1:])),
                       'first_word': table[0], 'final_two_words': list(table[-2:]),
                       'note': 'The pinned source references this table via incbin; contents are verified disk/capture bytes.'},
        'matrix_base': hex(matrix_base), 'matrix_bytes': 0x48,
        'wheel_input_names': naming,
        'naming_warning': 'transformMatrix44/46/48 are source names, not byte offsets: actual offsets are 0x42/0x44/0x46.',
        'producer_inputs': [{'name': name, 'address': hex(symbols[name.lower()]),
                             'captured_word': hex(int.from_bytes(chip[symbols[name.lower()]:symbols[name.lower()] + 2], 'big'))}
                            for name in ['cameraAngleX', 'cameraAngleY', 'cameraAngleZ', 'segmentTargetAngle']],
        'retained_matrix_word_offsets': ['0x26', '0x32', '0x36', '0x3a', '0x3c', '0x40'],
        'normalizations': ['MOVE/MOVEA destination spelling', 'immediate arithmetic mnemonic aliases',
                           'MULS/MULU .w spelling', 'word branch suffixes', 'numeric absolute-address suffixes',
                           'A7/SP aliases', 'explicit zero indexed displacement', 'hexadecimal/decimal spelling'],
        'limitations': ['Static full-function comparison and code/table identity, not runtime execution coverage.',
                       'One captured frame does not guarantee angle inputs and stored matrix outputs belong to one update.',
                       'No enhanced-source differences found in these two spans; this does not validate other source routines.'],
    }
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'reference-comparison.json').write_text(json.dumps(record, indent=2) + '\n')
    listing = []
    for routine in routines:
        listing.append('; ' + routine['name'] + ' original disk code; SHA-256 ' + routine['sha256'])
        for ins in routine['instructions']:
            listing.append(f"{int(ins['address'], 16):08x} {ins['bytes']:24s} {ins['original']}")
        listing.append('')
    (args.output / 'original-disassembly.asm').write_text('\n'.join(listing))
    print(json.dumps({'comparison': str(args.output / 'reference-comparison.json'),
                      'instruction_counts': [r['instruction_count'] for r in routines],
                      'operand_wildcards': 0, 'code_and_table_equal_capture': True}, indent=2))


if __name__ == '__main__':
    main()
