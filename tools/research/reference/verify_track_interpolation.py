"""Verify the complete original track-interpolation routine against pinned source.

Decode original supplied ADF bytes as MC68000, resolve every source operand,
and compare capture code identity. Outputs are local analysis artifacts only.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re

from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

ROOT = Path(__file__).resolve().parents[3]
REFERENCE = ROOT / 'analysis/reference/vesuri'
OUTPUT = ROOT / 'analysis/track-interpolation'
BASE, DISK_OFFSET, PAYLOAD_SIZE = 0xe700, 0xdc00, 408540
START, END = 0x5c554, 0x5c5f2
COMMIT = '3cce78d193c3d0208be34d0bbfc3155e787cd406'
ADF_SHA = '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA = '508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
SOURCE_SHA = '321034a19432bc23f2373acb68a121ee46dc4c5ed382e6df30ee05aff3c7924a'
INPUTS = [('control1', 'splineControlPoint1', 0x1bc02, 2),
          ('control2', 'splineControlPoint2', 0x1bc04, 2),
          ('control3', 'splineControlPoint3', 0x1bc06, 2),
          ('control4', 'splineControlPoint4', 0x1bc08, 2),
          ('lateral', 'lateralTrackPosition', 0x1bc4d, 1),
          ('blend', 'segmentBlendParam', 0x1bc41, 1)]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def canonical(instruction, symbols=None):
    parts = instruction.lower().split(None, 1)
    mnemonic, operand = parts[0], parts[1] if len(parts) > 1 else ''
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
    operand = re.sub(r'(?<![\w])(\d+)\.[wl]\b', r'\1', operand)
    mnemonic = re.sub(r'^(move|add|sub|cmp)a\.', r'\1.', mnemonic)
    mnemonic = re.sub(r'^(add|sub|and|or|eor|cmp)i\.', r'\1.', mnemonic)
    mnemonic = re.sub(r'^(muls|mulu|divs|divu)\.w$', r'\1', mnemonic)
    mnemonic = re.sub(r'^(bra|bne|bpl|blt|ble)\.[ws]$', r'\1', mnemonic)
    return mnemonic + ' ' + re.sub(r'\s+', '', operand)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, action='append',
                        help='Parsed capture directory; repeat for more captures (default: race).')
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf) == ADF_SHA, 'Original ADF hash mismatch')
    payload = adf[DISK_OFFSET:DISK_OFFSET + PAYLOAD_SIZE]
    require(sha(payload) == PAYLOAD_SHA, 'Original payload hash mismatch')
    provenance = json.loads((REFERENCE / 'provenance.json').read_text(encoding='utf-8-sig'))
    require(provenance['commit'] == COMMIT, 'Pinned reference commit mismatch')
    info = next(f for f in provenance['files'] if f['path'] == 'StuntCarRacer.s')
    source_bytes = (REFERENCE / info['path']).read_bytes()
    require(sha(source_bytes) == SOURCE_SHA == info['sha256'], 'Reference source hash mismatch')
    require(len(source_bytes) == info['size'], 'Reference source length mismatch')
    lines = source_bytes.decode('utf-8').splitlines()
    symbols = {}
    for line in lines:
        match = re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)', line, re.I)
        if match:
            symbols[match[1].lower()] = BASE + int(match[2], 16)
        match = re.match(r'^(lbC[0-9a-f]+):', line, re.I)
        if match:
            symbols[match[1].lower()] = BASE + int(match[1][3:], 16)
    for name, reference_name, address, width in INPUTS:
        require(symbols[reference_name.lower()] == address, 'Input address mismatch: ' + name)
    require(symbols['tempbyte1'] == 0x1bb18, 'Output address mismatch')
    first = lines.index('calculateBilinearTrackInterpolation:')
    last = lines.index('updateCarWorldPositionCoordinate:')
    source = [(i + 1, lines[i].split(';')[0].strip()) for i in range(first + 1, last)]
    source = [(i, text) for i, text in source if text and not text.endswith(':')]
    code = payload[START - BASE:END - BASE]
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    decoded = list(decoder.disasm(code, START))
    require(sum(i.size for i in decoded) == len(code) == 158, 'Instruction extent mismatch')
    require(decoded[-1].mnemonic == 'rts', 'Expected final RTS')
    require(len(source) == len(decoded), 'Source instruction count mismatch')
    instructions = []
    for (line, source_instruction), instruction in zip(source, decoded):
        actual = instruction.mnemonic + ' ' + instruction.op_str
        require(canonical(source_instruction, symbols) == canonical(actual),
                f'{instruction.address:#x}: {source_instruction!r} != {actual!r}')
        instructions.append({'address': hex(instruction.address), 'bytes': instruction.bytes.hex(),
                             'original': actual, 'source_line': line, 'source': source_instruction})
    captures = []
    for directory in args.capture or [ROOT / 'analysis/captures/race']:
        manifest = json.loads((directory / 'manifest.json').read_text())
        bank = next(b for b in manifest['banks'] if b['tag'] == 'CRAM')
        chip = (directory / bank['file']).read_bytes()
        require(bank['base'] == '0x0' and len(chip) == bank['size'] == 0x80000,
                'Capture must map 512KiB chip RAM at zero')
        require(sha(chip) == bank['sha256'], 'Capture RAM hash mismatch')
        require(sha(Path(manifest['source_path']).read_bytes()) == manifest['source_sha256'],
                'Capture savestate hash mismatch')
        require(chip[START:END] == code, 'Capture differs from original interpolation code')
        captures.append({'directory': str(directory.resolve()), 'ram_sha256': sha(chip),
                         'savestate_sha256': manifest['source_sha256'],
                         'pc': manifest['cpu']['registers']['PC'], 'code_bytes_equal_disk': True,
                         'inputs': {name: int.from_bytes(chip[address:address + width], 'big')
                                    for name, _, address, width in INPUTS},
                         'stored_output_word32': int.from_bytes(chip[0x1bb18:0x1bb1c], 'big')})
    routine = {'name': 'calculateBilinearTrackInterpolation', 'runtime_address': hex(START),
               'end_exclusive': hex(END), 'payload_offset': hex(START - BASE),
               'disk_offset': hex(DISK_OFFSET + START - BASE), 'size': len(code),
               'sha256': sha(code), 'instruction_count': len(decoded), 'source_line': first + 1,
               'source_matches_all_instructions_and_operands': True, 'operand_wildcards': 0,
               'instructions': instructions}
    record = {'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA,
              'payload_base': hex(BASE), 'payload_disk_offset': hex(DISK_OFFSET),
              'reference': {'repository': provenance['repository'], 'commit': COMMIT,
                            'source_url': info['url'], 'source_sha256': SOURCE_SHA},
              'routines': [routine], 'captures': captures,
              'inputs': [{'name': name, 'reference_name': reference_name,
                          'address': hex(address), 'size': width}
                         for name, reference_name, address, width in INPUTS],
              'output': {'register': 'D0', 'address': '0x1bb18', 'size': 4,
                         'reference_name': 'tempByte1', 'meaning': 'signed result bits; eight fractional bits relative to control points'},
              'calls': [], 'tables': [], 'hardware_accesses': [],
              'behavior': ['Endpoint differences wrap at 16 bits before signed multiplication.',
                           'Each lateral edge retains eight fractional bits.',
                           'Absolute inter-edge delta >=32768 takes an arithmetic shift by three before low-word truncation.',
                           'The low word controls multiplication/sign, including when a coarse delta exceeds signed16 range.',
                           'Negative-product path clears its low byte before negation; positive path does not.',
                           'Coarse path restores three bits before final arithmetic right shift by eight.'],
              'limitations': ['Static comparison and code identity do not establish runtime path coverage.',
                              'tempByte1 is shared scratch memory; captured value need not belong to captured inputs.',
                              'No routine/table changes found in this span; surrounding road-selection and bounce behavior remain separate.']}
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / 'reference-comparison.json').write_text(json.dumps(record, indent=2) + '\n')
    (OUTPUT / 'original-code.bin').write_bytes(code)
    (OUTPUT / 'original-disassembly.asm').write_text('\n'.join(
        f"{int(i['address'], 16):08x} {i['bytes']:24s} {i['original']}" for i in instructions) + '\n')
    print(json.dumps({'comparison': str(OUTPUT / 'reference-comparison.json'),
                      'instruction_count': len(decoded), 'size': len(code), 'sha256': sha(code),
                      'operand_wildcards': 0, 'verified_captures': len(captures)}, indent=2))


if __name__ == '__main__':
    main()
