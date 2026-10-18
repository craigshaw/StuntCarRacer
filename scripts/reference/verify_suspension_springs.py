"""Verify the original spring stage, helpers, table and normal-call D1 contract."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re

from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

ROOT = Path(__file__).resolve().parents[2]
REF = ROOT / 'analysis/reference/vesuri'
OUT = ROOT / 'analysis/suspension-springs'
BASE = 0xe700
ADF_SHA = '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA = '508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
SOURCE_SHA = '321034a19432bc23f2373acb68a121ee46dc4c5ed382e6df30ee05aff3c7924a'
COMMIT = '3cce78d193c3d0208be34d0bbfc3155e787cd406'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, default=ROOT / 'analysis/captures/race')
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf) == ADF_SHA, 'Original ADF changed')
    payload = adf[0xdc00:0xdc00 + 408540]
    require(sha(payload) == PAYLOAD_SHA, 'Payload changed')
    source = (REF / 'StuntCarRacer.s').read_bytes()
    provenance = json.loads((REF / 'provenance.json').read_text())
    require(provenance['commit'] == COMMIT, 'Reference revision changed')
    item = next(f for f in provenance['files'] if f['path'] == 'StuntCarRacer.s')
    require(sha(source) == SOURCE_SHA == item['sha256'], 'Reference source changed')
    lines = source.decode().splitlines()
    # Reuse only our spelling-normalization function; importing this script has
    # no analysis/write side effects and does not execute upstream source.
    spec = importlib.util.spec_from_file_location('transform_reference', Path(__file__).with_name('verify_transform_reference.py'))
    normalizer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(normalizer)
    symbols = {}
    for line in lines:
        match = re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)', line, re.I)
        if match:
            symbols[match[1].lower()] = BASE + int(match[2], 16)
    symbols.update({'attenuatedisplacementvalue': 0x62424, 'multiplyandrandomize': 0x612a2})
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    capture = json.loads((args.capture / 'manifest.json').read_text())
    bank = next(b for b in capture['banks'] if b['tag'] == 'CRAM')
    chip_path = args.capture / bank['file']
    chip = chip_path.read_bytes()
    require(int(bank['base'], 0) == 0 and bank['size'] == len(chip) == 0x80000,
            'Capture must contain 512KiB chip RAM mapped at zero')
    require(sha(chip) == bank['sha256'], 'Capture RAM changed')
    require(sha(Path(capture['source_path']).read_bytes()) == capture['source_sha256'],
            'Capture savestate changed')

    def span(name, start, end, decode=True):
        data = payload[start - BASE:end - BASE]
        require(chip[start:end] == data, name + ' differs in captured RAM')
        record = {'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                  'payload_offset': hex(start - BASE), 'disk_offset': hex(0xdc00 + start - BASE),
                  'size': len(data), 'sha256': sha(data), 'capture_equals_original': True}
        if decode:
            ins = list(decoder.disasm(data, start))
            require(sum(i.size for i in ins) == len(data), name + ' decode incomplete')
            record['instructions'] = [{'address': hex(i.address), 'bytes': i.bytes.hex(),
                                       'original': i.mnemonic + ' ' + i.op_str} for i in ins]
        return record

    routines = []
    for name, next_name, start, end in [
        ('calculateSuspensionSpringForces', 'attenuateDisplacementValue', 0x622dc, 0x62424),
        ('attenuateDisplacementValue', 'projectTrackXToScreen', 0x62424, 0x62454),
        ('multiplyAndRandomize', 'checkKeyPressed', 0x612a2, 0x612be),
    ]:
        record = span(name, start, end)
        first, last = lines.index(name + ':'), lines.index(next_name + ':')
        source_instructions = []
        local_labels = {}
        for number in range(first + 1, last):
            text = lines[number].split(';')[0].strip()
            if not text:
                continue
            if text.endswith(':'):
                local_labels[text[:-1].lower()] = int(record['instructions'][len(source_instructions)]['address'], 16)
            else:
                source_instructions.append((number + 1, text))
        require(len(source_instructions) == len(record['instructions']), name + ' count mismatch')
        for (number, source_ins), original in zip(source_instructions, record['instructions']):
            resolved = source_ins.lower()
            for label, address in local_labels.items():
                resolved = resolved.replace(label, str(address))
            expected = normalizer.canonical(resolved, symbols)
            actual = normalizer.canonical(original['original'])
            actual = re.sub(r'^bge\.[ws] ', 'bge ', actual)
            require(expected == actual, f'{name} {original["address"]}: {expected} != {actual}')
            original.update({'source_line': number, 'source': source_ins})
        record.update({'source_all_instructions_and_operands_match': True,
                       'operand_wildcards': 0, 'instruction_count': len(source_instructions)})
        routines.append(record)
    table = span('attenuationTable', 0x1eeca, 0x1ef4a, False)
    table['note'] = '128-byte original table; indexed by raw unsigned magnitude / 2 when D1 bits8..15 are zero.'
    caller = [span('preceding driver calls', 0x61880, 0x6188c),
              span('calculateViewAngles', 0x615e6, 0x61618),
              span('getPositiveTrigValue and matrixMultiply', 0x61340, 0x6135a),
              span('suspension caller through spring call', 0x61bcc, 0x61f7c),
              span('applyMomentumAmplification', 0x6180e, 0x6181e)]
    prior = caller[3]['instructions'][:-1]
    require(not any(re.search(r'\bd1\b', i['original']) for i in prior), 'Caller unexpectedly accesses D1')
    calls = [i['original'] for i in prior if i['original'].startswith('jsr ')]
    require(calls == ['jsr $6180e.l'] * 3, 'Caller dependencies changed')
    require(not any(re.search(r'\bd1\b', i['original']) for i in caller[4]['instructions']), 'Momentum helper changes D1')
    record = {
        'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA,
        'reference_commit': COMMIT, 'reference_source_sha256': SOURCE_SHA,
        'reference_source_url': item['url'], 'routines': routines, 'tables': [table],
        'capture': {'directory': str(args.capture.resolve()), 'ram_sha256': sha(chip),
                    'source_path': capture['source_path'], 'source_sha256': capture['source_sha256']},
        'entry_condition': {
            'required': 'D1 bits8..15 == zero; other entry register values do not affect memory outputs.',
            'reason': 'MOVE.B #255,D1 and LSR.B #1,D1 retain the high byte of D1.W; indexed addressing sign-extends that word.',
            'normal_game_chain': 'View-angle code at0x61606 sets D1.W=0x000e. The multiply helper doubles then halves it. Three momentum calls and caller instructions leave D1 unchanged before0x61f76 calls springs.',
            'standalone_helper_limit': 'Nonzero D1 high byte addresses outside the 128-byte table. Native scoped API deliberately covers the normal-game call contract; oracle separately checks that hidden index behavior.',
            'evidence': caller,
        },
        'inputs': {'front_left_travel': '0x1bcb0:u32', 'front_right_travel': '0x1bcb4:u32',
                   'rear_travel': '0x1bcb8:u32', 'average_velocity': '0x1bd38:u16'},
        'word_outputs': ['0x1bd40', '0x1bd42', '0x1bd44', '0x1bd48', '0x1bd4a', '0x1bd4c'],
        'byte_outputs': ['0x1bd4e', '0x1bd50', '0x1bd52', '0x1bb1a', '0x1bb1b',
                         '0x1bb2b', '0x1bb2c', '0x1bb2d', '0x1bbbb'],
        'preserved_output_region_bytes': ['0x1bd46', '0x1bd47', '0x1bd4f', '0x1bd51', '0x1bd53'],
        'arithmetic_notes': [
            'Front travel addition and differences wrap at32 bits before arithmetic shifts; wider averages differ.',
            'Attenuation uses signed CMP.W after NEG.W. Input0x8000 stays negative and produces raw byte0.',
            'multiplyAndRandomize is unsigned byte multiplication without randomness; high byte is result and low byte is scratch.',
            'Sign bytes are high bytes of big-endian displacement words. Pitch displacement is XOR0x8000 before sign extraction.',
            'Component bytes occupy high bytes of words; adjacent low bytes are preserved.',
        ],
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
                      'instruction_counts': [r['instruction_count'] for r in routines],
                      'operand_wildcards': 0}, indent=2))


if __name__ == '__main__':
    main()
