"""Compare complete opponent positioning/contact stages with original MC68000 bytes."""
import argparse
import json
import re
from pathlib import Path
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
from verify_transform_reference import (ROOT, REFERENCE, BASE, DISK_OFFSET, PAYLOAD_SIZE,
                                        COMMIT, ADF_SHA, PAYLOAD_SHA, SOURCE_SHA, sha, require, canonical)
OUT = ROOT / 'analysis/opponent-collision'
SPECS = [
    ('handleOpponentPositioning', 0x636c0, 0x63926, 'adjustOpponentPositionAggressive'),
    ('adjustOpponentPositionAggressive', 0x63926, 0x63996, 'processOpponentLogic'),
    ('calculateOpponentCollisionForces', 0x63ce4, 0x63e2e, 'applyOpponentCollisionForces'),
    ('advanceToNextSegment', 0x5c51a, 0x5c538, 'retreatToPreviousSegment'),
    ('calculatePlayerDistance', 0x6076c, 0x607d0, 'checkForTieCondition'),
]


def normal(text, symbols=None):
    if symbols:
        for label, address in sorted(symbols.items(), key=lambda pair: -len(pair[0])):
            if label.startswith('.'):
                text = re.sub(re.escape(label) + r'\b', str(address), text, flags=re.I)
    return re.sub(r'^(b\w+)\.[ws] ', r'\1 ', canonical(text, symbols))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, action='append')
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf) == ADF_SHA, 'ADF changed')
    payload = adf[DISK_OFFSET:DISK_OFFSET + PAYLOAD_SIZE]
    require(sha(payload) == PAYLOAD_SHA, 'Payload changed')
    require((REFERENCE / 'offline-extracted-payload.bin').read_bytes() == payload, 'Retained payload differs')
    source = (REFERENCE / 'StuntCarRacer.s').read_bytes()
    provenance = json.loads((REFERENCE / 'provenance.json').read_text())
    info = next(f for f in provenance['files'] if f['path'] == 'StuntCarRacer.s')
    require(provenance['commit'] == COMMIT and sha(source) == SOURCE_SHA == info['sha256'], 'Reference changed')
    lines = source.decode('utf-8').splitlines()
    symbols = {name.lower(): start for name, start, _, _ in SPECS}
    for line in lines:
        match = re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)', line, re.I)
        if match: symbols[match[1].lower()] = BASE + int(match[2], 16)
        match = re.match(r'^(lbC[0-9a-f]+):', line, re.I)
        if match: symbols[match[1].lower()] = BASE + int(match[1][3:], 16)
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    prepared, differences = [], []
    for name, start, end, next_name in SPECS:
        code = payload[start - BASE:end - BASE]
        decoded = list(decoder.disasm(code, start))
        require(sum(i.size for i in decoded) == len(code), 'Incomplete decode: ' + name)
        first = lines.index(name + ':'); last = lines.index(next_name + ':', first + 1)
        selected, labels = [], {}
        for n in range(first + 1, last):
            text = lines[n].split(';')[0].strip()
            if name == 'calculateOpponentCollisionForces' and text.lower() in ('tst.b\tframethrottleflag', 'bmi\t.skipcollisiondamage'):
                differences.append({'routine': name, 'source_line': n + 1, 'source': text,
                                    'kind': 'added enhanced damage throttle instruction', 'original': None})
                continue
            match = re.match(r'^([.\w]+):\s*(.*)', text)
            if match:
                require(len(selected) <= len(decoded), 'Label after function end')
                labels[match[1].lower()] = decoded[len(selected)].address if len(selected) < len(decoded) else end
                text = match[2]
            if text: selected.append((n + 1, text))
        require(len(selected) == len(decoded), f'{name}: source count {len(selected)} != original {len(decoded)}')
        symbols.update({label: address for label, address in labels.items() if not label.startswith('.')})
        prepared.append((name, start, end, code, decoded, selected, labels))
    routines = []
    changes = {
        0x6375a: ('lateralRoadPosition', '$1BC5E'), 0x6376c: ('speedMinor', '$1BC5F'),
        0x638e0: ('#$FF', '#$F7'), 0x638e4: ('BRA', 'BNE'), 0x638f0: ('#$01', '#$09'),
        0x63cf0: ('#$03*FRAMERATE_MULTIPLIER', '#$03'),
    }
    for name, start, end, code, decoded, selected, labels in prepared:
        resolved = dict(symbols); resolved.update(labels)
        instructions = []
        for (line, source_text), instruction in zip(selected, decoded):
            text = source_text
            if instruction.address in changes:
                old, new = changes[instruction.address]
                require(old in text, f'Expected known change at {instruction.address:#x}')
                text = text.replace(old, new)
                differences.append({'routine': name, 'address': hex(instruction.address), 'source_line': line,
                                    'source': source_text, 'kind': 'explicit enhanced-source operand/mnemonic difference'})
            actual = instruction.mnemonic + ' ' + instruction.op_str
            require(normal(text, resolved) == normal(actual), f'{name} {instruction.address:#x}: {text!r} != {actual!r}')
            instructions.append({'address': hex(instruction.address), 'bytes': instruction.bytes.hex(),
                                 'original': actual, 'source_line': line, 'source': source_text})
        routines.append({'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                         'payload_offset': hex(start - BASE), 'disk_offset': hex(DISK_OFFSET + start - BASE),
                         'size': len(code), 'sha256': sha(code), 'instruction_count': len(decoded),
                         'remaining_source_instructions_and_operands_match': True, 'operand_wildcards': 0,
                         'instructions': instructions})
    tables = []
    for name, start, end in [('opponentBehaviorTraits', 0x1ed8a, 0x1edaa), ('geometryParameterFlags', 0x1f0c2, 0x1f0d2)]:
        tables.append({'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                       'payload_offset': hex(start - BASE), 'disk_offset': hex(DISK_OFFSET + start - BASE),
                       'sha256': sha(payload[start - BASE:end - BASE])})
    captures = []
    for directory in args.capture or [ROOT / 'analysis/captures/race']:
        manifest = json.loads((directory / 'manifest.json').read_text())
        bank = next(b for b in manifest['banks'] if b['tag'] == 'CRAM'); chip = (directory / bank['file']).read_bytes()
        require(bank['base'] == '0x0' and len(chip) == bank['size'] == 0x80000, 'Invalid capture mapping')
        require(sha(chip) == bank['sha256'], 'Capture RAM changed')
        require(sha(Path(manifest['source_path']).read_bytes()) == manifest['source_sha256'], 'Capture USS changed')
        for span in routines + tables:
            start, end = int(span['runtime_address'], 16), int(span['end_exclusive'], 16)
            require(chip[start:end] == payload[start - BASE:end - BASE], 'Capture code/table changed: ' + span['name'])
        captures.append({'directory': str(directory.resolve()), 'ram_sha256': sha(chip),
                         'savestate_sha256': manifest['source_sha256'], 'all_code_and_tables_equal_disk': True})
    report = {'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA, 'reference_commit': COMMIT,
              'reference_source_sha256': SOURCE_SHA, 'reference_url': info['url'], 'routines': routines,
              'tables': tables, 'source_differences': differences, 'captures': captures,
              'entry_precondition': 'Distance and full positioning: D1 and D2 bits8..15 zero. Distance indices<100; positioning opponentId<32, segmentCount1..100, opponentSegment<segmentCount. Force-only entry has no register input.',
              'call_order': ['processOpponentLogic (upstream)', 'calculatePlayerDistance', 'handleOpponentPositioning', 'setupTrackGeometryForFrame (downstream)'],
              'behavior': ['Complete collision gates, drafting flag, behavior traits, two-segment lookahead, and lateral movement.',
                           'All arithmetic retains original byte/word wrap. NEG.W 0x8000 and signed separation comparison are preserved.',
                           'Lateral impulse overwrites the HIGH byte only. Other retained forces and collision latch are significant.',
                           'Damage sums wrap as a word before extracting the high byte; per-wheel accumulation saturates as unsigned bytes.',
                           'Distance preserves word wrap before arithmetic shift and compares direct/wrapped distances unsigned; the complete direction byte is retained.',
                           'No hardware access or audio requests occur in this stage.'],
              'remaining_input_producers': ['processOpponentLogic including opponent acceleration/progress and wheel geometry',
                                           'network-only convertOpponentWheelsToCarFootprint'],
              'limitations': ['Controlled opponent trajectories are explicit stage inputs, not a recovered opponent AI simulation.',
                              'No interrupt interleaving, cycle timing or arbitrary-register equivalence is claimed.']}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'reference-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    (OUT / 'original-disassembly.asm').write_text('\n'.join(
        f"{i['address']} {i['bytes']:24s} {i['original']}" for r in routines for i in r['instructions']) + '\n')
    print(json.dumps({'report': str(OUT / 'reference-comparison.json'), 'instruction_count': sum(r['instruction_count'] for r in routines),
                      'source_differences': len(differences)}, indent=2))


if __name__ == '__main__': main()
