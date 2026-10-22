"""Verify original compression/damping through the pre-countdown boundary."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re

from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

ROOT = Path(__file__).resolve().parents[3]
REF = ROOT / 'analysis/reference/vesuri'
OUT = ROOT / 'analysis/suspension-response'
BASE = 0xe700
ADF_SHA = '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA = '508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
SOURCE_SHA = '321034a19432bc23f2373acb68a121ee46dc4c5ed382e6df30ee05aff3c7924a'
COMMIT = '3cce78d193c3d0208be34d0bbfc3155e787cd406'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, default=ROOT / 'analysis/captures/race')
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf) == ADF_SHA, 'Original ADF changed')
    payload = adf[0xdc00:0xdc00 + 408540]
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
    symbols.update({'applymomentumamplification': 0x6180e, 'calculatesuspensionspringforces': 0x622dc,
                    'attenuatedisplacementvalue': 0x62424, 'multiplyandrandomize': 0x612a2})
    spec = importlib.util.spec_from_file_location('transform_reference', Path(__file__).with_name('verify_transform_reference.py'))
    normalizer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(normalizer)
    capture = json.loads((args.capture / 'manifest.json').read_text())
    bank = next(b for b in capture['banks'] if b['tag'] == 'CRAM')
    chip = (args.capture / bank['file']).read_bytes()
    require(int(bank['base'], 0) == 0 and len(chip) == bank['size'] == 0x80000, 'Unexpected chip RAM mapping')
    require(sha(chip) == bank['sha256'], 'Capture RAM changed')
    require(sha(Path(capture['source_path']).read_bytes()) == capture['source_sha256'], 'Capture state changed')
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)

    def span(name, start, end, decode=True):
        data = payload[start - BASE:end - BASE]
        require(chip[start:end] == data, name + ' capture/code differs')
        result = {'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                  'payload_offset': hex(start - BASE), 'disk_offset': hex(start - BASE + 0xdc00),
                  'sha256': sha(data), 'size': len(data), 'capture_equals_original': True}
        if decode:
            ins = list(decoder.disasm(data, start))
            require(sum(i.size for i in ins) == len(data), 'Incomplete decoding: ' + name)
            result['instructions'] = [{'address': hex(i.address), 'bytes': i.bytes.hex(),
                                       'original': i.mnemonic + ' ' + i.op_str} for i in ins]
        return result

    def compare(name, first, last, start, end):
        result = span(name, start, end)
        source_ins, local = [], {}
        for n in range(first, last):
            text = lines[n].split(';')[0].strip()
            if not text:
                continue
            if text.endswith(':'):
                pos = len(source_ins)
                local[text[:-1].lower()] = int(result['instructions'][pos]['address'], 16) if pos < len(result['instructions']) else end
            else:
                source_ins.append((n + 1, text))
        require(len(source_ins) == len(result['instructions']), name + ' instruction count mismatch')
        for (n, text), ins in zip(source_ins, result['instructions']):
            resolved = text.lower()
            for label, address in sorted(local.items(), key=lambda item: -len(item[0])):
                resolved = resolved.replace(label, str(address))
            expected = normalizer.canonical(resolved, symbols)
            actual = normalizer.canonical(ins['original'])
            actual = re.sub(r'^(b\w+)\.[ws] ', r'\1 ', actual)
            # The only immediate arithmetic expression in this selected body.
            expected = expected.replace('#4608-1', '#4607')
            require(actual == expected, f'{name} {ins["address"]}: {actual} != {expected}')
            ins.update({'source_line': n, 'source': text})
        result.update({'source_instruction_and_operand_matches': len(source_ins), 'operand_wildcards': 0})
        return result

    first = lines.index('updateWheelSuspensionPhysics:') + 1
    last = next(n for n in range(first, len(lines)) if lines[n].strip() == 'JSR\thandleRaceStartCountdown')
    routines = [compare('updateWheelSuspensionPhysics pre-countdown prefix', first, last, 0x61bcc, 0x62042)]
    momentum = span('applyMomentumAmplification original', 0x6180e, 0x6181e)
    require(payload[0x6180e - BASE:0x6181e - BASE].hex() == '363c011467000006c1c3e080d0464e75', 'Original momentum instructions changed')
    momentum['source_differences'] = [
        {'original_address': '0x6180e', 'original': 'MOVE.W #$0114,D3',
         'enhanced_source': 'MOVE.W #$0114*FRAMERATE_MULTIPLIER,D3', 'effect': 'Use original multiplier 276, not the enhanced multiplier.'},
        {'original_address': '0x61812', 'original': 'BEQ $6181a',
         'enhanced_source': 'Branch commented out as removed.', 'effect': 'Original branch is retained in oracle bytes; preceding nonzero MOVE makes it untaken.'},
    ]
    routines.append(momentum)
    for name, next_name, start, end in [
        ('calculateSuspensionSpringForces', 'attenuateDisplacementValue', 0x622dc, 0x62424),
        ('attenuateDisplacementValue', 'projectTrackXToScreen', 0x62424, 0x62454),
        ('multiplyAndRandomize', 'checkKeyPressed', 0x612a2, 0x612be),
    ]:
        routines.append(compare(name, lines.index(name + ':') + 1, lines.index(next_name + ':'), start, end))
    table = span('attenuationTable', 0x1eeca, 0x1ef4a, False)
    boundary = span('unexecuted countdown call boundary', 0x62042, 0x62048)
    require(boundary['instructions'][0]['original'] == 'jsr $5b32e.l', 'Boundary changed')
    precondition_path = ROOT / 'analysis/suspension-springs/reference-comparison.json'
    precondition = json.loads(precondition_path.read_text())
    require(precondition['source_adf_sha256'] == ADF_SHA and precondition['reference_source_sha256'] == SOURCE_SHA,
            'Spring precondition evidence mismatch; run verify_suspension_springs.py first')
    record = {
        'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA, 'reference_commit': COMMIT,
        'reference_source_sha256': SOURCE_SHA, 'reference_url': item['url'],
        'capture': {'directory': str(args.capture.resolve()), 'ram_sha256': sha(chip), 'source_sha256': capture['source_sha256']},
        'routines': routines, 'tables': [table], 'boundary': boundary,
        'execution_contract': 'Begin0x61bcc, stop before executing0x62042. Original instruction bytes remain unmodified; oracle redirects PC to a STOP sentinel at boundary.',
        'entry_condition': precondition['entry_condition'],
        'precondition_evidence_sha256': sha(precondition_path.read_bytes()),
        'quirks_preserved': [
            'MOVE.B zero at maxCompressionVelocity clears only the high byte of its big-endian word.',
            'One shared highCompressionFrameCount is incremented or reset sequentially across all three wheels.',
            'BMI after momentum JSR tests final ADD.W result sign, not the pre-call displacement delta.',
            'Velocity cap0x11ff runs only on the high-compression branch; other nonnegative paths may retain higher values.',
            'Damage counter increment wraps a byte and compares signed bytes against damageFrameLimit.',
            'Original word sums, differences and 3x roll-rate multiplication wrap before subsequent signed operations.',
            'Settling track7 instruction at0x61ffc writes F8 toD1, leaving the D3 reference at-128.',
            'Spring component low-byte holes, untouched word and all scratch outputs remain preserved as previously verified.',
        ],
        'excluded_tail': ['handleRaceStartCountdown', 'applyOpponentCollisionForces', 'impact sound cooldown/playback'],
    }
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'reference-comparison.json').write_text(json.dumps(record, indent=2) + '\n')
    listing = []
    for routine in routines:
        listing.append('; ' + routine['name'] + ' SHA256 ' + routine['sha256'])
        listing.extend(f"{i['address']} {i['bytes']:24} {i['original']}" for i in routine['instructions'])
        listing.append('')
    (OUT / 'original-disassembly.asm').write_text('\n'.join(listing))
    print(json.dumps({'report': str(OUT / 'reference-comparison.json'),
                      'prefix_instructions': len(routines[0]['instructions']),
                      'momentum_source_differences': 2, 'boundary': '0x62042'}, indent=2))


if __name__ == '__main__':
    main()
