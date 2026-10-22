"""Verify original wheel height smoothing and track-edge contact against pinned source."""
import argparse
import json
import re
from pathlib import Path

from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
from verify_track_interpolation import (ROOT, REFERENCE, BASE, DISK_OFFSET, PAYLOAD_SIZE,
                                        COMMIT, ADF_SHA, PAYLOAD_SHA, SOURCE_SHA,
                                        sha, require, canonical)

OUTPUT = ROOT / 'analysis/wheel-contact'
ROUTINES = [
    ('calculateBilinearTrackInterpolation', 0x5c554, 0x5c5f2, 'updateCarWorldPositionCoordinate'),
    ('updateCarWorldPositionCoordinate', 0x5c5f2, 0x5c65a, 'calculatePerspectiveAdjustment'),
    ('checkLateralBoundsAndBounce', 0x5c808, 0x5c890, 'startupInitAndEnterMenu'),
]
FIELDS = [
    ('wheelHeightFrontLeft', 0x1bca4, 4), ('wheelHeightFrontRight', 0x1bca8, 4),
    ('wheelHeightRear', 0x1bcac, 4), ('tempByte1', 0x1bb18, 4),
    ('wheelOutOfLateralBoundsFlag', 0x1bb65, 1), ('renderingOrderMode', 0x1bb9a, 1),
    ('wheelBouncePhaseAccumulator', 0x1bbda, 1), ('lateralPositionOutOfBounds', 0x1bc22, 2),
    ('reverseDirectionFlag', 0x1bc32, 1), ('carSpeedMagnitude', 0x1bd5c, 1),
    ('cameraAngleX', 0x1bce4, 1),
]


def normalized(text, symbols=None):
    value = canonical(text, symbols)
    value = re.sub(r'(?<![\w])0\(', '(', value)
    value = re.sub(r'^(beq|bgt|bmi)\.[ws] ', r'\1 ', value)
    return re.sub(r'^(bclr|bset)\.[bl] ', r'\1 ', value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, action='append')
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf) == ADF_SHA, 'ADF identity mismatch')
    payload = adf[DISK_OFFSET:DISK_OFFSET + PAYLOAD_SIZE]
    require(sha(payload) == PAYLOAD_SHA, 'Payload identity mismatch')
    require((REFERENCE / 'offline-extracted-payload.bin').read_bytes() == payload,
            'Retained plaintext payload differs from original disk range')
    provenance = json.loads((REFERENCE / 'provenance.json').read_text(encoding='utf-8-sig'))
    require(provenance['commit'] == COMMIT, 'Reference commit mismatch')
    info = next(f for f in provenance['files'] if f['path'] == 'StuntCarRacer.s')
    source_bytes = (REFERENCE / info['path']).read_bytes()
    require(sha(source_bytes) == SOURCE_SHA == info['sha256'], 'Reference source hash mismatch')
    lines = source_bytes.decode('utf-8').splitlines()
    symbols = {name.lower(): address for name, address, _, _ in ROUTINES}
    for line in lines:
        match = re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)', line, re.I)
        if match:
            symbols[match[1].lower()] = BASE + int(match[2], 16)
        match = re.match(r'^(lbC[0-9a-f]+):', line, re.I)
        if match:
            symbols[match[1].lower()] = BASE + int(match[1][3:], 16)
    for name, address, _ in FIELDS:
        require(symbols[name.lower()] == address, 'Field address mismatch: ' + name)
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    routines = []
    for name, start, end, next_name in ROUTINES:
        first, last = lines.index(name + ':'), lines.index(next_name + ':')
        source = [(i + 1, lines[i].split(';')[0].strip()) for i in range(first + 1, last)]
        source = [(line, text) for line, text in source if text and not text.endswith(':')]
        code = payload[start - BASE:end - BASE]
        decoded = list(decoder.disasm(code, start))
        require(sum(i.size for i in decoded) == len(code), 'Incomplete decode: ' + name)
        require(len(decoded) == len(source), 'Instruction count mismatch: ' + name)
        instructions = []
        for (line, source_instruction), instruction in zip(source, decoded):
            original = instruction.mnemonic + ' ' + instruction.op_str
            require(normalized(source_instruction, symbols) == normalized(original),
                    f'{instruction.address:#x}: {source_instruction!r} != {original!r}')
            instructions.append({'address': hex(instruction.address), 'bytes': instruction.bytes.hex(),
                                 'original': original, 'source_line': line, 'source': source_instruction})
        routines.append({'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                         'payload_offset': hex(start - BASE), 'disk_offset': hex(DISK_OFFSET + start - BASE),
                         'size': len(code), 'sha256': sha(code), 'instruction_count': len(decoded),
                         'source_matches_all_instructions_and_operands': True, 'operand_wildcards': 0,
                         'instructions': instructions})
    captures = []
    for directory in args.capture or [ROOT / 'analysis/captures/race']:
        manifest = json.loads((directory / 'manifest.json').read_text())
        bank = next(b for b in manifest['banks'] if b['tag'] == 'CRAM')
        chip = (directory / bank['file']).read_bytes()
        require(bank['base'] == '0x0' and len(chip) == bank['size'] == 0x80000, 'Invalid capture mapping')
        require(sha(chip) == bank['sha256'], 'RAM hash mismatch')
        require(sha(Path(manifest['source_path']).read_bytes()) == manifest['source_sha256'], 'USS hash mismatch')
        for _, start, end, _ in ROUTINES:
            require(chip[start:end] == payload[start - BASE:end - BASE], 'Capture routine differs from original')
        captures.append({'directory': str(directory.resolve()), 'ram_sha256': sha(chip),
                         'savestate_sha256': manifest['source_sha256'],
                         'pc': manifest['cpu']['registers']['PC'], 'code_bytes_equal_disk': True,
                         'fields': {name: int.from_bytes(chip[address:address + size], 'big')
                                    for name, address, size in FIELDS}})
    report = {'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA,
              'payload_base': hex(BASE), 'payload_disk_offset': hex(DISK_OFFSET),
              'reference': {'repository': provenance['repository'], 'commit': COMMIT,
                            'source_url': info['url'], 'source_sha256': SOURCE_SHA},
              'routines': routines, 'tables': [], 'captures': captures,
              'fields': [{'reference_name': name, 'address': hex(address), 'size': size}
                         for name, address, size in FIELDS],
              'behavior': [
                  'The out-of-bounds flag bit7 is consumed; other bits survive.',
                  'Edge distance uses wrapped signed-word arithmetic, including NEG.W of 0x8000.',
                  'Near-edge heights fall by distance*16+256 if signed result stays at least4096; bounce byte becomes64 or128.',
                  'Far/low edge heights clamp to4096 and renderingOrderMode becomes(old>>1)|128.',
                  'The speed and pitch inputs are their big-endian high bytes tested as signed bytes.',
                  'Low speed and small absolute pitch average old and sampled heights with an unsigned33-bit sum and carry rotation.',
                  'Pitch byte0x80 remains negative after byte negation and therefore takes the averaging branch.'],
              'limitations': ['A saved state is not a trace; stored wheel heights can belong to an earlier update.',
                              'Physical units and bounce/rendering field names remain provisional.',
                              'Native contract omits original registers, condition flags, timing and wheel-array indexing.']}
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / 'reference-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    (OUTPUT / 'original-disassembly.asm').write_text('\n'.join(
        f"{int(i['address'], 16):08x} {i['bytes']:24s} {i['original']}"
        for routine in routines for i in routine['instructions']) + '\n')
    for routine in routines:
        start, end = int(routine['runtime_address'], 16), int(routine['end_exclusive'], 16)
        (OUTPUT / (routine['name'] + '.bin')).write_bytes(payload[start - BASE:end - BASE])
    print(json.dumps({'comparison': str(OUTPUT / 'reference-comparison.json'),
                      'routines': [{key: r[key] for key in ('name', 'runtime_address', 'end_exclusive', 'sha256', 'instruction_count')}
                                   for r in routines], 'verified_captures': len(captures)}, indent=2))


if __name__ == '__main__':
    main()
