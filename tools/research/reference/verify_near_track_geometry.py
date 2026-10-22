"""Pin dense near-track height generation, projection and viewport cut geometry."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
from verify_track_progress import canonical

ROOT = Path(__file__).resolve().parents[3]
REF = ROOT / 'analysis/reference/vesuri'
BASE = 0xe700
ADF_SHA = '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA = '508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
SOURCE_SHA = '321034a19432bc23f2373acb68a121ee46dc4c5ed382e6df30ee05aff3c7924a'
COMMIT = '3cce78d193c3d0208be34d0bbfc3155e787cd406'
SPANS = [
    ('transformTrackSegmentCoordinates', 0x6521c, 0x654c0, 'checkTrackEdgeVisibility'),
    ('checkTrackEdgeVisibility', 0x654c2, 0x65564, 'shiftCoordinateArrays'),
    ('shiftCoordinateArrays', 0x65564, 0x655ea, 'processTrackVisibility'),
    ('processTrackVertexWithCharacteristics', 0x65720, 0x65756, 'setupTrackGeometryForFrame2'),
    ('setupTrackGeometryForFrame2', 0x65756, 0x65a18, 'processRenderData'),
    ('processRenderData', 0x65a18, 0x65a72, 'renderBarrierPost'),
    ('renderBarrierPost', 0x65a72, 0x65ae2, 'processTrackEdgeVertex'),
    ('processTrackEdgeVertex', 0x65ae2, 0x65bec, 'generateTrackSegmentData'),
    ('linearInterpolateCoordinates', 0x5a520, 0x5a54e, 'addBaseCoordinateAndStore'),
    ('addBaseCoordinateAndStore', 0x5a550, 0x5a55a, 'interpolateOpponentTrackPosition'),
    ('interpolateCoordinatePair', 0x5a576, 0x5a58e, 'loadAndTransformTrackVertex'),
    ('loadAndTransformTrackVertex', 0x5a58e, 0x5a5b6, 'processTrackCharacteristics'),
    ('processTrackCharacteristics', 0x5a5b6, 0x5a62a, 'storeTrackCoordinates'),
    ('storeTrackCoordinates', 0x5a62a, 0x5a64e, 'lbC04BF4E'),
    ('projectTrackXToScreen', 0x62454, 0x6249c, 'transformXCoordinateToPerspective'),
    ('transformCoordinate', 0x624c2, 0x624f0, 'transformYCoordinateToPerspective')]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(value, message):
    if not value:
        raise ValueError(message)


def normal(text, symbols=None):
    text = canonical(text, symbols)
    if text.startswith('movem.'):
        text = re.sub(r'([da])([0-7])-\1([0-7])', lambda match: '/'.join(
            match[1] + str(n) for n in range(int(match[2]), int(match[3]) + 1)), text)
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, default=ROOT / 'analysis/captures/race')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/near-track-geometry')
    parser.add_argument('--inputs', type=Path, default=ROOT / 'tests/replays/laps/authored-race-1463.csv')
    parser.add_argument('--oracle', type=Path)
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf) == ADF_SHA, 'ADF identity changed')
    payload = adf[0xdc00:0xdc00 + 408540]
    require(sha(payload) == PAYLOAD_SHA, 'Payload identity changed')
    source = (REF / 'StuntCarRacer.s').read_bytes()
    require(sha(source) == SOURCE_SHA, 'Pinned source identity changed')
    provenance = json.loads((REF / 'provenance.json').read_text())
    require(provenance['commit'] == COMMIT, 'Pinned source commit changed')
    manifest = json.loads((args.capture / 'manifest.json').read_text())
    bank = next(item for item in manifest['banks'] if item['tag'] == 'CRAM')
    chip_path = args.capture / bank['file']
    chip = chip_path.read_bytes()
    require(len(chip) == bank['size'] == 0x80000 and int(bank['base'], 0) == 0, 'Capture mapping changed')
    require(sha(chip) == bank['sha256'], 'Capture bank changed')
    require(sha(Path(manifest['source_path']).read_bytes()) == manifest['source_sha256'], 'Capture source changed')
    lines = source.decode().splitlines()
    symbols = {}
    for filename in ('code-symbols.csv', 'data-symbols.csv'):
        for row in csv.DictReader((ROOT / 'analysis/map' / filename).open()):
            symbols[row['name'].lower()] = int(row['ram_address'], 0)
    for line in lines:
        match = re.match(r'^(lbC[0-9a-f]+):', line, re.I)
        if match:
            symbols[match[1].lower()] = BASE + int(match[1][3:], 16)
    symbols.update({name.lower(): start for name, start, _, _ in SPANS})
    symbols.update({'loadtracksegmentconfiguration': 0x5fe56, 'transformsegmenttoviewspace': 0x5ff94,
        'interpolateopponenttrackpositionfromdata': 0x5c6c4, 'reversetracksegmentdata': 0x6055e,
        'applyscreenspacerotation': 0x62518, 'apply3dtransformation': 0x6250a,
        'transformxcoordinatetoperspective': 0x6249c, 'transformycoordinatetoperspective': 0x624f0,
        'calculatedivision': 0x64d66, 'calculatevectormagnitude': 0x64de8,
        'checkfarclippingplane': 0x6847c, 'drawclippedline': 0x668a8,
        'segmentdatapointers': 0x7aa1a, 'rendercommandqueue': 0x7b08a,
        'interpolationblendfactor': 0x5a54e})
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    routines, listing = [], []
    for name, start, end, next_name in SPANS:
        code = payload[start - BASE:end - BASE]
        require(chip[start:end] == code, name + ': captured original bytes differ')
        decoded = list(decoder.disasm(code, start))
        require(sum(item.size for item in decoded) == len(code), name + ': incomplete MC68000 decode')
        first, last = lines.index(name + ':') + 1, lines.index(next_name + ':')
        selected, local = [], dict(symbols)
        for index in range(first, last):
            text = lines[index].split(';')[0].strip()
            if not text:
                continue
            if ':' in text:
                label, text = text.split(':', 1)
                local[label.lower()] = decoded[len(selected)].address if len(selected) < len(decoded) else end
                text = text.strip()
            if text:
                selected.append((index + 1, text))
        require(len(selected) == len(decoded), f'{name}: source/byte instruction count differs {len(selected)}/{len(decoded)}')
        records = []
        for ins, (number, text) in zip(decoded, selected):
            actual, expected = normal(ins.mnemonic + ' ' + ins.op_str), normal(text, local)
            require(actual == expected, f'{name} {ins.address:#x}: {actual} !={expected} ({text})')
            records.append({'address': hex(ins.address), 'bytes': ins.bytes.hex(),
                'original': ins.mnemonic + ' ' + ins.op_str, 'source_line': number, 'source': text})
            listing.append(f'{ins.address:#x} {ins.bytes.hex():24} {ins.mnemonic} {ins.op_str}')
        routines.append({'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
            'payload_offset': hex(start - BASE), 'disk_offset': hex(start - BASE + 0xdc00), 'sha256': sha(code),
            'instruction_count': len(records), 'capture_equals_original': True, 'operand_wildcards': 0,
            'instructions': records})
    report = {'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA,
        'reference_commit': COMMIT, 'reference_source_sha256': SOURCE_SHA,
        'routines': routines, 'operand_wildcards': 0, 'source_operand_differences': [],
        'capture': {'directory': str(args.capture.resolve()), 'ram_sha256': sha(chip), 'source_sha256': manifest['source_sha256']},
        'unreachable_original_branch': {'address': '0x652b8', 'bytes': payload[0x652b8-BASE:0x652c4-BASE].hex(),
            'finding': 'MOVE.B #0,lbB00D4AD clears N; its following BPL always skips reverseTrackSegmentData. The native port preserves this original behavior.'},
        'coordinate_units': 'Dense upper raw heights use ASR2(cameraHeightBaseline-height), then polar vertical angle; the far cached strip uses ASR3. Both use original screen roll and integer centering.',
        'history_alias': 'transformedCoordinates1/2 at0x1c0a0/0x1c0a2 alias coordinateLookupTable+0xf0/+0xf2. They retain the previous segment unrolled upper endpoint X for edge admission; no separate far/capture history is invented.',
        'data_words_inside_code': [{'address': '0x654c0', 'bytes': 2, 'meaning': 'lastProcessed byte plus padding'},
            {'address': '0x5a54e', 'bytes': 2, 'meaning': 'interpolation blend factor written by original helper; not executable instructions'}],
        'boundaries': ['Original line pixel backend is intercepted only while testing initial cut edges; geometry, boundary ray extension and admission execute unchanged bytes.',
            'Oracle exercises the complete1463-tick independently evolved gameplay timeline with persistent native renderer buffers, three near segments per frame, then synthetic preview/budget/carry and word-height contexts.',
            'Queue visibility/admission, painter command generation, original clipping and scanline pixel equality belong to separate modules/tests.'],
        'rea_evidence': ['analysis/near-track/rea-geometry.json', 'analysis/near-track/rea-setup.json']}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'reference-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    (args.output / 'original-disassembly.asm').write_text('\n'.join(listing) + '\n')
    if args.oracle:
        payload_path = REF / 'offline-extracted-payload.bin'
        require(payload_path.read_bytes() == payload, 'Extracted payload differs')
        result = subprocess.run([str(args.oracle.resolve()), str(payload_path), str(chip_path), str(args.inputs.resolve())],
            cwd=ROOT, text=True, capture_output=True, check=True)
        oracle = json.loads(result.stdout)
        require(oracle['mismatches'] == 0 and oracle['total_cases'] >= 20000, 'Insufficient differential coverage')
        oracle.update({'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA, 'ram_sha256': sha(chip),
            'inputs_sha256': sha(args.inputs.read_bytes()), 'executable': str(args.oracle.resolve()),
            'executable_sha256': sha(args.oracle.read_bytes()), 'source_sha256': {
                str(path.relative_to(ROOT)): sha(path.read_bytes()) for path in [ROOT / 'src/near_track_geometry.h',
                ROOT / 'src/near_track_geometry.cpp', ROOT / 'tests/support/near_track_geometry_fixture.h',
                ROOT / 'tests/oracle/near_track_geometry_oracle.cpp', Path(__file__).resolve(),
                *sorted((ROOT / 'src/state').glob('*.h'))]}})
        (args.output / 'oracle.json').write_text(json.dumps(oracle, indent=2) + '\n')
    print(json.dumps({'report': str(args.output / 'reference-comparison.json'),
        'instructions': sum(item['instruction_count'] for item in routines), 'operand_wildcards': 0,
        'oracle': str(args.output / 'oracle.json') if args.oracle else None}, indent=2))


if __name__ == '__main__':
    main()
