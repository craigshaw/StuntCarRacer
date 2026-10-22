"""Verify original velocity/world integration and enumerate enhanced-source changes."""
import argparse
import json
import re
from pathlib import Path

from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
from verify_transform_reference import (ROOT, REFERENCE, BASE, DISK_OFFSET, PAYLOAD_SIZE,
                                        COMMIT, ADF_SHA, PAYLOAD_SHA, SOURCE_SHA,
                                        sha, require, canonical)

OUT = ROOT / 'analysis/motion-integration'
ROUTINES = [
    ('updateWorldPosition', 0x61950, 0x61ad4, 'applyVelocityIntegration'),
    ('applyVelocityIntegration', 0x61adc, 0x61b26, 'integrateVelocityComponents'),
    ('integrateVelocityComponents', 0x61b26, 0x61b70, 'calculateExpectedTrackSurfaceHeights'),
]
FIELDS = [
    ('cameraWorldX', 0x1bcd8, 4), ('cameraWorldY', 0x1bcdc, 4), ('cameraWorldZ', 0x1bce0, 4),
    ('cameraAngleX', 0x1bce4, 2), ('cameraAngleY', 0x1bce6, 2), ('cameraAngleZ', 0x1bce8, 2),
    ('worldXSpeed', 0x1bcea, 2), ('worldYSpeed', 0x1bcec, 2), ('worldZSpeed', 0x1bcee, 2),
    ('rotationSpeedX', 0x1bcf0, 2), ('rotationSpeedY', 0x1bcf2, 2), ('rotationSpeedZ', 0x1bcf4, 2),
    ('worldAccelerationX', 0x1bcf6, 2), ('worldAccelerationY', 0x1bcf8, 2), ('worldAccelerationZ', 0x1bcfa, 2),
    ('angularAccelerationX', 0x1bcfc, 2), ('angularAccelerationY', 0x1bcfe, 2), ('angularAccelerationZ', 0x1bd00, 2),
    ('transformedAngularVelocityX', 0x1bd3a, 2), ('transformedAngularVelocityY', 0x1bd3c, 2), ('transformedAngularVelocityZ', 0x1bd3e, 2),
    ('raceCompletionCheckFlag', 0x1bb75, 1), ('renderingOrderMode', 0x1bb9a, 1),
    ('lbB00D4AB', 0x1bbab, 1), ('viewportCenterY', 0x1bc42, 2),
]


def normalize(text, symbols=None):
    text = canonical(text, symbols)
    text = re.sub(r'^(b\w+)\.[ws] ', r'\1 ', text)
    return re.sub(r'^(bclr|bset)\.[bl] ', r'\1 ', text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, action='append')
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf) == ADF_SHA, 'ADF changed')
    payload = adf[DISK_OFFSET:DISK_OFFSET + PAYLOAD_SIZE]
    require(sha(payload) == PAYLOAD_SHA, 'Payload changed')
    require((REFERENCE / 'offline-extracted-payload.bin').read_bytes() == payload, 'Retained payload differs')
    provenance = json.loads((REFERENCE / 'provenance.json').read_text())
    info = next(f for f in provenance['files'] if f['path'] == 'StuntCarRacer.s')
    source = (REFERENCE / info['path']).read_bytes()
    require(provenance['commit'] == COMMIT and sha(source) == SOURCE_SHA == info['sha256'], 'Reference changed')
    lines = source.decode('utf-8').splitlines()
    symbols = {name.lower(): start for name, start, _, _ in ROUTINES}
    for line in lines:
        match = re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)', line, re.I)
        if match:
            symbols[match[1].lower()] = BASE + int(match[2], 16)
        match = re.match(r'^;?(lbC[0-9a-f]+):', line, re.I)
        if match:
            symbols[match[1].lower()] = BASE + int(match[1][3:], 16)
    for name, address, _ in FIELDS:
        require(symbols[name.lower()] == address, 'Field mapping mismatch: ' + name)
    require(symbols['cameraanglethresholds'] == 0x61ad4, 'Threshold table mapping mismatch')
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)

    def span(name, start, end, is_code=True):
        data = payload[start - BASE:end - BASE]
        result = {'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                  'payload_offset': hex(start - BASE), 'disk_offset': hex(DISK_OFFSET + start - BASE),
                  'size': len(data), 'sha256': sha(data)}
        if is_code:
            decoded = list(decoder.disasm(data, start))
            require(sum(i.size for i in decoded) == len(data), 'Incomplete MC68000 decode: ' + name)
            result['instruction_count'] = len(decoded)
            result['instructions'] = [{'address': hex(i.address), 'bytes': i.bytes.hex(),
                                       'original': i.mnemonic + ' ' + i.op_str} for i in decoded]
        return result

    routines, differences = [], []
    for name, start, end, next_name in ROUTINES:
        routine = span(name, start, end)
        first, last = lines.index(name + ':'), lines.index(next_name + ':')
        source_instructions = []
        for n in range(first + 1, last):
            raw = lines[n]
            removed_branch = bool(re.match(r'^;\s*BEQ\s+lbC0534(?:38|50|68)\b', raw))
            text = raw[1:].split(';')[0].strip() if removed_branch else raw.split(';')[0].strip()
            if text and not text.endswith(':'):
                source_instructions.append((n + 1, text, removed_branch))
        require(len(source_instructions) == routine['instruction_count'], 'Count mismatch: ' + name)
        routine_differences = []
        for (line, text, removed), instruction in zip(source_instructions, routine['instructions']):
            amended = text
            difference = None
            if 'TIMESTEP_FACTOR' in text:
                require(text == 'MOVE.B\t#TIMESTEP_FACTOR,D2', 'Unexpected timestep expression')
                amended = text.replace('TIMESTEP_FACTOR', '$EE')
                difference = {'kind': 'changed_timestep', 'original_value': 238,
                              'enhanced_source_expression': '$EE/FRAMERATE_MULTIPLIER',
                              'enhanced_pal_value': 39, 'enhanced_ntsc_value': 34}
            elif removed:
                difference = {'kind': 'instruction_removed_from_enhanced_source',
                              'effect': 'Original BEQ remains; preceding nonzero timestep byte makes it untaken.'}
            require(normalize(amended, symbols) == normalize(instruction['original']),
                    f'{name} {instruction["address"]}: {instruction["original"]!r} != {amended!r}')
            instruction.update({'source_line': line, 'source': text,
                                'source_instruction_commented_out': removed})
            if difference:
                difference.update({'address': instruction['address'], 'source_line': line,
                                   'original': instruction['original'], 'source': text})
                differences.append(difference); routine_differences.append(difference)
        routine.update({'source_differences': routine_differences,
                        'remaining_instructions_and_operands_match': True, 'operand_wildcards': 0})
        routines.append(routine)
    require(sum(d['kind'] == 'changed_timestep' for d in differences) == 12, 'Unexpected timestep count')
    require(sum(d['kind'] == 'instruction_removed_from_enhanced_source' for d in differences) == 3,
            'Unexpected removed branch count')
    continuous = span('continuous original driver tail', 0x618c0, 0x618ce)
    expected_tail = ['JSR applyVelocityIntegration', 'JSR updateWorldPosition', 'RTS']
    require(len(continuous['instructions']) == len(expected_tail), 'Driver tail changed')
    for ins, text in zip(continuous['instructions'], expected_tail):
        require(normalize(ins['original']) == normalize(text, symbols), 'Driver call order changed')
    continuous['purpose'] = 'Unmodified consecutive original calls test world acceleration -> velocity -> position with continuous register flow.'
    routines.append(continuous)
    table = span('cameraAngleThresholds', 0x61ad4, 0x61adc, False)
    table['word_order'] = ['normalPositive', 'specialPositive', 'normalNegative', 'specialNegative']
    table['words'] = [int.from_bytes(payload[a - BASE:a - BASE + 2], 'big') for a in range(0x61ad4, 0x61adc, 2)]
    captures = []
    for directory in args.capture or [ROOT / 'analysis/captures/race']:
        manifest = json.loads((directory / 'manifest.json').read_text())
        bank = next(b for b in manifest['banks'] if b['tag'] == 'CRAM')
        chip = (directory / bank['file']).read_bytes()
        require(bank['base'] == '0x0' and len(chip) == bank['size'] == 0x80000, 'Invalid capture mapping')
        require(sha(chip) == bank['sha256'], 'Capture RAM changed')
        require(sha(Path(manifest['source_path']).read_bytes()) == manifest['source_sha256'], 'Capture USS changed')
        for record in routines + [table]:
            start, end = int(record['runtime_address'], 16), int(record['end_exclusive'], 16)
            require(chip[start:end] == payload[start - BASE:end - BASE], 'Captured code/table differs: ' + record['name'])
        captures.append({'directory': str(directory.resolve()), 'ram_sha256': sha(chip),
                         'savestate_sha256': manifest['source_sha256'], 'code_and_table_equal_disk': True,
                         'fields': {name: int.from_bytes(chip[a:a + n], 'big') for name, a, n in FIELDS}})
    report = {'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA,
              'reference_commit': COMMIT, 'reference_source_sha256': SOURCE_SHA, 'reference_url': info['url'],
              'routines': routines, 'tables': [table], 'source_differences': differences, 'captures': captures,
              'fields': [{'reference_name': name, 'address': hex(a), 'size': n} for name, a, n in FIELDS],
              'entry_condition': {
                  'register': 'D2', 'bits_8_to_15': 0,
                  'reason': 'MOVE.B #238 changes only the low byte; MULS.W reads the complete low word.',
                  'scope': 'Explicit native calling convention, not proof of arbitrary entry-register equivalence.',
                  'continuous_tail': 'The driver tail preserves D2 high byte across world acceleration and position integration; position later clears D2.W.'},
              'quirks_preserved': [
                  'Original timestep is238/256, not enhanced PAL39/256 or NTSC34/256.',
                  'Each signed product rounds downward before wrapped addition.',
                  'Position uses X/Z scale64 and Y scale128, wrapping32 bits.',
                  'Y clamp compares signed high word>=1000 and replaces only that word, preserving low fraction.',
                  'Pitch/roll clamps use unsigned word comparison; negative equality enters clamp, positive equality does not.',
                  'Clamped pitch/roll cancel corresponding rotation only if its sign matches the selected limit.',
                  'Completion bit7 and rendering mode exactly0xe0 select the alternate limit pair.',
                  'Steep-pitch bit7 preserves the lower7 flag bits; byte NEG of0x80 remains negative.',
                  'ViewportCenterY receives negated pitch modulo65536.'],
              'limitations': ['No full physics tick, interrupt timing or CPU flag/register equivalence is claimed.',
                              'Transforms and acceleration generation remain separate inputs.',
                              'Snapshot fields can come from different updates; captured-input replay is not a trace.']}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'reference-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    (OUT / 'original-disassembly.asm').write_text('\n'.join(
        f"{i['address']} {i['bytes']:24s} {i['original']}" for r in routines for i in r['instructions']) + '\n')
    for r in routines + [table]:
        start, end = int(r['runtime_address'], 16), int(r['end_exclusive'], 16)
        (OUT / (re.sub(r'[^A-Za-z0-9]+', '-', r['name']) + '.bin')).write_bytes(payload[start - BASE:end - BASE])
    print(json.dumps({'report': str(OUT / 'reference-comparison.json'),
                      'instruction_count': sum(r['instruction_count'] for r in routines),
                      'source_timestep_differences': 12, 'restored_removed_branches': 3,
                      'verified_captures': len(captures)}, indent=2))


if __name__ == '__main__':
    main()
