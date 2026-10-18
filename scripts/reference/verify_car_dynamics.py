"""Verify every operand in the original orientation, roll helper and drag routines."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

ROOT = Path(__file__).resolve().parents[2]
REF = ROOT / 'analysis/reference/vesuri'
BASE = 0xe700
ADF_SHA = '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA = '508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
SOURCE_SHA = '321034a19432bc23f2373acb68a121ee46dc4c5ed382e6df30ee05aff3c7924a'
COMMIT = '3cce78d193c3d0208be34d0bbfc3155e787cd406'
SPANS = [
    ('updateCarOrientation', 'updateVelocityDamping', 0x620b8, 0x62138),
    ('calculateRollAngleTransition', 'getSteeringMultiplier', 0x6217a, 0x621da),
    ('getSteeringMultiplier', 'updateCollisionState', 0x621da, 0x621f4),
    ('updateCollisionState', 'calculateSuspensionSpringForces', 0x621f4, 0x622dc),
]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, default=ROOT / 'analysis/captures/race')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/car-dynamics')
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
    symbols = {name.lower(): start for name, _, start, _ in SPANS}
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
    routines, listing = [], []
    for name, next_name, start, end in SPANS:
        data = payload[start - BASE:end - BASE]
        require(chip[start:end] == data, name + ' capture differs from original')
        decoded = list(decoder.disasm(data, start))
        require(sum(i.size for i in decoded) == len(data), name + ' incomplete MC68000 decode')
        selected, labels = [], {}
        for number in range(lines.index(name + ':') + 1, lines.index(next_name + ':')):
            text = lines[number].split(';')[0].strip()
            if not text:
                continue
            if text.endswith(':'):
                index = len(selected)
                labels[text[:-1].lower()] = decoded[index].address if index < len(decoded) else end
            else:
                selected.append((number + 1, text))
        require(len(selected) == len(decoded), name + ' instruction count differs')
        instructions = []
        for ins, (number, text) in zip(decoded, selected):
            resolved = text.lower()
            for label, address in sorted(labels.items(), key=lambda pair: -len(pair[0])):
                resolved = resolved.replace(label, str(address))
            expected = normalizer.canonical(resolved, symbols)
            actual = re.sub(r'^(b\w+)\.[ws] ', r'\1 ', normalizer.canonical(ins.mnemonic + ' ' + ins.op_str))
            require(actual == expected, f'{name} {ins.address:#x}: {actual} != {expected}')
            instructions.append({'address': hex(ins.address), 'bytes': ins.bytes.hex(),
                                 'original': ins.mnemonic + ' ' + ins.op_str, 'source_line': number, 'source': text})
        routines.append({'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                         'payload_offset': hex(start - BASE), 'disk_offset': hex(start - BASE + 0xdc00),
                         'sha256': sha(data), 'size': len(data), 'instruction_count': len(decoded),
                         'source_instruction_and_operand_matches': len(decoded), 'source_differences': [],
                         'operand_wildcards': 0, 'capture_equals_original': True, 'instructions': instructions})
        listing.append('; ' + name + ' SHA256 ' + sha(data))
        listing.extend(f'{ins.address:#x} {ins.bytes.hex():24} {ins.mnemonic} {ins.op_str}' for ins in decoded)
        listing.append('')
    record = {
        'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA, 'reference_commit': COMMIT,
        'reference_source_sha256': SOURCE_SHA, 'reference_url': item['url'],
        'capture': {'directory': str(args.capture.resolve()), 'ram_sha256': sha(chip), 'source_sha256': capture['source_sha256']},
        'routines': routines, 'tables': [],
        'entry_condition': 'No general-register value preconditions. getSteeringMultiplier returns its word in D0; upper D0 remains unchanged.',
        'mutable_words': {'gasOutputAccumulatorValue': '0x1bd2a', 'adjustedRollAngle': '0x1bd32',
                          'adjustedPitchAngle': '0x1bd34', 'adjustedYawAngle': '0x1bd36',
                          'worldAccelerationX': '0x1bcf6', 'worldAccelerationY': '0x1bcf8', 'worldAccelerationZ': '0x1bcfa'},
        'mutable_bytes': {'rollTransitionFlag': '0x1bbc1'},
        'memory_scratch_writes': [],
        'aliases': [
            {'address': '0x1bd2b', 'meaning': 'gasOutputIntensityValue is the low byte of the mutable word at0x1bd2a.'},
            {'address': '0x1bd46', 'meaning': 'Drag reads only the high byte of lbW00D646; low byte remains untouched.'},
            {'address': '0x1bca2', 'meaning': 'postWreckStateFlag aliases byte2 (high byte of low word) of trackOffsetAdjustment at0x1bca0.'},
        ],
        'quirks_preserved': [
            'Accumulator correction ORs the high bytes of accumulator and local Z, but tests the accumulator low byte separately.',
            'Unsigned BCS controls roll threshold and accumulator clamp; NEG.W 0x8000 remains0x8000.',
            'Roll fallback drops baseTargetRoll and uses rollSpringAngle minus local X.',
            'Negative collision response byte is complemented, not negated: FF->0, FE->1, FD->2, FC->3.',
            'Normal drag selects a signed maximum of wrapped absolute local-velocity words;0x8000 remains negative.',
            'Drafting subtracts0x0a00 and clamps only on unsigned borrow.',
            'Signed product high word is rounded down before arithmetic word shift1,3or5 and wrapped acceleration subtraction.',
        ],
    }
    args.output.mkdir(parents=True, exist_ok=True)
    report = args.output / 'reference-comparison.json'
    report.write_text(json.dumps(record, indent=2) + '\n')
    (args.output / 'original-disassembly.asm').write_text('\n'.join(listing))
    print(json.dumps({'report': str(report), 'instructions': sum(r['instruction_count'] for r in routines),
                      'source_differences': 0, 'operand_wildcards': 0}, indent=2))


if __name__ == '__main__':
    main()
