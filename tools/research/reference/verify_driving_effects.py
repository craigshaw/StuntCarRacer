"""Pin original driving-effects code, helper closure, draw assets and captures.

Selected unchanged source functions are compared instruction-for-instruction.
Enhanced particle timing, damage scheduling and pixel rendering differ; their
original implementations are decoded directly and exercised by the oracle.
"""
import argparse
import json
import re
from pathlib import Path
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
from verify_transform_reference import (ROOT, REFERENCE, BASE, DISK_OFFSET, PAYLOAD_SIZE,
                                        COMMIT, ADF_SHA, PAYLOAD_SHA, SOURCE_SHA, sha, require, canonical)
OUT = ROOT / 'analysis/driving-effects'
SPECS = [
    ('updateEngineAudioPitch', 0x5b56c, 0x5b646, 'runPlayerNameEntry'),
    ('handleGroundCollision', 0x60c92, 0x60cdc, 'noEdgeCollision'),
    ('noEdgeCollision', 0x60cdc, 0x60cde, None),
    ('initializeDebrisParticlePositions', 0x60cde, 0x60cf8, None),
    ('handleEdgeCollisionAndParticles', 0x60cf8, 0x60e88, None),
    ('validateAndRenderParticle', 0x60e88, 0x60f36, None),
    ('initializeDustCloudParticle', 0x60f36, 0x60f5a, 'renderDustCloud'),
    ('renderDustCloud', 0x60f5a, 0x60f9c, None),
    ('updateWheelSuspensionPosition', 0x5e778, 0x5e848, 'initiateCarWreck'),
    ('updateDamageAndTimers', 0x5dfb4, 0x5e098, None),
    ('updateDamageBarHUD', 0x66106, 0x6626a, 'resetDamageAccumulators'),
    ('resetDamageAccumulators', 0x6626a, 0x66286, 'testPixelAtLocation'),
    ('testPixelAtLocation', 0x66286, 0x662aa, 'updateChainAnimation'),
    ('initiateCarWreck', 0x5e848, 0x5e8ac, None),
    ('setPixelColor', 0x66348, 0x663f2, None),
    ('plotPixel', 0x663f2, 0x6642a, None),
    ('renderHoleHelpers', 0x6091a, 0x60988, None),
    ('renderMaskedGraphicsObject', 0x69f42, 0x69fe8, None),
    ('renderObjectColumn', 0x69fe8, 0x6a0be, None),
    ('renderGraphicsObjectAtPosition', 0x6a0be, 0x6a166, None),
    ('generateRandomNumber', 0x62574, 0x625a2, 'displayMessage'),
    ('setMessageParameters', 0x5e4ec, 0x5e502, 'displayGameMessage'),
    ('playSample', 0xf362, 0xf3ee, None),
]
PATCHES = [0x66405, 0x66409, 0x6640d, 0x66411, 0x6641b, 0x6641d, 0x66421, 0x66425]
DIFFERENCES = [
    'Original ground/edge sample playback has no enhanced cooldown throttle.',
    'Original particle vertical acceleration is2, initial impact shift1, horizontal velocity ASR3; enhanced values1,2,4.',
    'Original noEdgeCollision is a bare RTS; enhanced clears grind cooldown when on-road.',
    'Original initialization reloads A1 inside the particle loop; enhanced branches directly to the store.',
    'Original edge routine contains unreachable random pitch alternatives after unconditional branch60d56.',
    'Original dust renderer has no enhanced x-coordinate early-return guard.',
    'Original damage cooldown decrements1, resets69 and always plays its selected sample; enhanced has frame scaling and creak throttling.',
    'Original wreck writes visual-effect high byte1bc56; enhanced comments out that store.',
    'Original pixel colors mutate bit6 in eight plotPixel opcode bytes; enhanced replaces this mechanism with an offset/jump implementation.',
]


def normal(text, symbols=None):
    value = canonical(text, symbols)
    value = re.sub(r'^(b\w+)\.[ws] ', r'\1 ', value)
    value = re.sub(r'^(lea|btst|bset|bclr)\.[bl] ', r'\1 ', value)
    value = re.sub(r'(?<!\w)(\d+)\+(\d+)(?!\w)', lambda m: str(int(m[1]) + int(m[2])), value)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', action='append', type=Path)
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf) == ADF_SHA, 'ADF changed')
    payload = adf[DISK_OFFSET:DISK_OFFSET + PAYLOAD_SIZE]
    require(sha(payload) == PAYLOAD_SHA, 'Payload changed')
    require((REFERENCE / 'offline-extracted-payload.bin').read_bytes() == payload, 'Retained payload differs')
    source = (REFERENCE / 'StuntCarRacer.s').read_bytes()
    provenance = json.loads((REFERENCE / 'provenance.json').read_text())
    require(provenance['commit'] == COMMIT and sha(source) == SOURCE_SHA, 'Reference changed')
    lines = source.decode().splitlines()
    symbols = {name.lower(): start for name, start, _, _ in SPECS}
    symbols.update({'setparticlecountandplaygrindsound': 0x60da2, 'rendermaskedgraphicsobject': 0x69f42})
    for line in lines:
        match = re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)', line, re.I)
        if match: symbols[match[1].lower()] = BASE + int(match[2], 16)
        match = re.match(r'^(lb[cbw][0-9a-f]+):', line, re.I)
        if match: symbols[match[1].lower()] = BASE + int(match[1][3:], 16)
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    routines, compared = [], 0
    for name, start, end, next_name in SPECS:
        code = payload[start - BASE:end - BASE]
        decoded = list(decoder.disasm(code, start))
        require(sum(i.size for i in decoded) == len(code), 'Incomplete decode: ' + name)
        instructions = [{'address': hex(i.address), 'bytes': i.bytes.hex(), 'original': i.mnemonic + ' ' + i.op_str} for i in decoded]
        if next_name:
            first = lines.index(name + ':'); last = lines.index(next_name + ':', first + 1)
            selected = []
            for n in range(first + 1, last):
                text = re.sub(r'^[.\w]+:\s*', '', lines[n].split(';')[0].strip())
                if text: selected.append((n + 1, text))
            require(len(selected) == len(decoded), f'{name}: source/original instruction counts differ')
            for record, (line, text) in zip(instructions, selected):
                require(normal(text, symbols) == normal(record['original']), f'{name} {record["address"]}: {text} != {record["original"]}')
                record.update({'source_line': line, 'source': text}); compared += 1
        routines.append({'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                         'payload_offset': hex(start - BASE), 'disk_offset': hex(DISK_OFFSET + start - BASE),
                         'size': len(code), 'sha256': sha(code), 'instruction_count': len(decoded),
                         'source_comparison': 'all instructions and operands equal' if next_name else 'original-byte recovery; enhanced body not asserted equivalent',
                         'instructions': instructions})
    tables = []
    for name, start, end in [('sineTable', 0x1ca42, 0x1cc46), ('dustCloudOffsetTable', 0x60f9c, 0x60fac),
                            ('dustCloudAnimSequence', 0x60fac, 0x60fbc), ('bitMaskTable', 0x66860, 0x668a0)]:
        tables.append({'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                       'sha256': sha(payload[start - BASE:end - BASE])})
    captures = []
    for directory in args.capture or [ROOT / 'analysis/captures/race']:
        manifest = json.loads((directory / 'manifest.json').read_text())
        bank = next(b for b in manifest['banks'] if b['tag'] == 'CRAM'); chip = (directory / bank['file']).read_bytes()
        require(bank['base'] == '0x0' and len(chip) == bank['size'] == 0x80000 and sha(chip) == bank['sha256'], 'Capture mapping/hash changed')
        require(sha(Path(manifest['source_path']).read_bytes()) == manifest['source_sha256'], 'Savestate changed')
        for span in routines + tables:
            start, end = int(span['runtime_address'], 16), int(span['end_exclusive'], 16)
            for address in range(start, end):
                mask = 0xbf if address in PATCHES else 0xff
                require(not ((chip[address] ^ payload[address - BASE]) & mask), f'Unexpected captured code/table difference {address:#x}')
        assets = []
        for index in range(0x17, 0x25):
            parameter = 0x6a16c + 16 * index + 4
            pointer = int.from_bytes(chip[0x6a4ac + 4 * index:0x6a4b0 + 4 * index], 'big')
            width = int.from_bytes(chip[parameter:parameter + 2], 'big') + 1
            height = int.from_bytes(chip[parameter + 2:parameter + 4], 'big') + 1
            size = width * height * 10
            require(0x1000 <= pointer and pointer + size < 0x6a594, 'Captured sprite source outside admitted region')
            assets.append({'sprite_id': index, 'runtime_address': hex(pointer), 'size': size, 'sha256': sha(chip[pointer:pointer + size])})
        captures.append({'directory': str(directory.resolve()), 'ram_sha256': sha(chip), 'savestate_sha256': manifest['source_sha256'],
                         'immutable_code_and_tables_equal_disk': True, 'mutable_plot_opcodes': {hex(a): hex(chip[a]) for a in PATCHES}, 'relocated_sprite_assets': assets})
    report = {'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA, 'reference_commit': COMMIT,
              'reference_source_sha256': SOURCE_SHA, 'routines': routines, 'tables': tables, 'captures': captures,
              'exact_source_instruction_comparisons': compared, 'documented_enhanced_differences': DIFFERENCES,
              'contracts': ['Original D1/D2 bits8..15 are zero at stage entry; remaining CPU registers are not native outputs.',
                            'Ground runs only with raceStartTimer0 and negative offTrackFlags, then edge runs unconditionally.',
                            'Captured graphics pointers and sprites are preserved. Renderer helper instructions execute without stubs.',
                            'Only original playSample may issue Paula MMIO, compared as ordered16-bit writes.',
                            'Damage feedback preserves current-strip occupancy and exact full colors in both framebuffers, including the masked existing/new hole sprites that overlap the strip.',
                            'Feedback strip is physical framebuffer x32..287/y0..7. Hole position10 lies at x288..319, outside all possible damage-feedback reads, and remains a visual event.',
                            'All arithmetic/particle/UI RAM outputs are compared; particle raster pixels and general renderer scratch are outside the native rendering contract.'],
              'limitations': ['This is gameplay-state and audio-command parity, not complete raster output or PCM playback parity.',
                              'Draw-state admission is the captured framebuffer layout and safe UI/sprite domains, not arbitrary RAM/pointer equivalence.',
                              'Enhanced renderer bodies are not automatically interchangeable with original self-modifying pixel code.']}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'reference-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    (OUT / 'original-disassembly.asm').write_text('\n'.join(f"{i['address']} {i['bytes']:24s} {i['original']}" for r in routines for i in r['instructions']) + '\n')
    print(json.dumps({'report': str(OUT / 'reference-comparison.json'), 'instruction_count': sum(r['instruction_count'] for r in routines),
                      'exact_source_instruction_comparisons': compared}, indent=2))


if __name__ == '__main__': main()
