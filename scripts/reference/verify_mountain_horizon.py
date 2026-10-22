"""Pin original mountain selection, decoded assets, projection and commands."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess

from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
from verify_track_progress import canonical

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = ROOT / 'analysis/reference/vesuri'
BASE, DISK_OFFSET, SIZE = 0xe700, 0xdc00, 408540
ADF_SHA = '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA = '508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
SOURCE_SHA = '321034a19432bc23f2373acb68a121ee46dc4c5ed382e6df30ee05aff3c7924a'
COMMIT = '3cce78d193c3d0208be34d0bbfc3155e787cd406'
SPANS = [('renderMountainHorizon', 0x6953e, 0x696fc, 'loadMountainData'),
         ('loadMountainData', 0x696fc, 0x69736, 'initializeGraphicsData')]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def normalized(text, symbols=None):
    text = canonical(text, symbols)
    if text.startswith('movem.'):
        text = re.sub(r'([da])([0-7])-\1([0-7])',
                      lambda match: '/'.join(match[1] + str(n) for n in range(int(match[2]), int(match[3]) + 1)), text)
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, default=ROOT / 'analysis/captures/race')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/mountain-horizon')
    parser.add_argument('--oracle', type=Path)
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf) == ADF_SHA, 'ADF identity changed')
    payload = adf[DISK_OFFSET:DISK_OFFSET + SIZE]
    require(sha(payload) == PAYLOAD_SHA, 'Payload identity changed')
    source = (REFERENCE / 'StuntCarRacer.s').read_bytes()
    require(sha(source) == SOURCE_SHA, 'Pinned source identity changed')
    provenance = json.loads((REFERENCE / 'provenance.json').read_text())
    require(provenance['commit'] == COMMIT, 'Pinned source commit changed')
    source_info = next(record for record in provenance['files'] if record['path'] == 'StuntCarRacer.s')
    require(source_info['sha256'] == SOURCE_SHA, 'Reference provenance changed')
    capture_manifest = json.loads((args.capture / 'manifest.json').read_text())
    chip_bank = next(record for record in capture_manifest['banks'] if record['tag'] == 'CRAM')
    chip_path = args.capture / chip_bank['file']
    chip = chip_path.read_bytes()
    require(len(chip) == chip_bank['size'] == 0x80000 and int(chip_bank['base'], 0) == 0, 'Unexpected capture mapping')
    require(sha(chip) == chip_bank['sha256'], 'Capture bank changed')
    require(sha(Path(capture_manifest['source_path']).read_bytes()) == capture_manifest['source_sha256'], 'Capture source changed')
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
    symbols.update({'mountainsilhouettetable': 0x699b8, 'trackmountaindatatable': 0x69a80,
                    'projectvertextoscreen': 0x6252a, 'drawclippedline': 0x668a8,
                    'setupbitplanemasks': 0x6770a, 'sorttrianglevertices': 0x6950c,
                    'renderquadrilateral': 0x66618, 'rendercommandqueue': 0x7b08a})
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    routines, listing = [], []
    differences = []
    for name, start, end, next_name in SPANS:
        code = payload[start - BASE:end - BASE]
        require(chip[start:end] == code, name + ': captured original code differs')
        decoded = list(decoder.disasm(code, start))
        require(sum(instruction.size for instruction in decoded) == len(code), name + ': incomplete MC68000 decode')
        first, last = lines.index(name + ':') + 1, lines.index(next_name + ':')
        selected, local = [], dict(symbols)
        for line_index in range(first, last):
            raw = lines[line_index]
            text = raw.split(';')[0].strip()
            # This disk also selects list0 for every track. The enhanced
            # source only removes the dead read preceding the immediate0.
            if name == 'loadMountainData' and raw.strip() == ';MOVE.B\tcurrentTrackID,D1':
                text = raw.strip()[1:]
                differences.append({'kind': 'restore_commented_dead_track_id_read',
                                    'source_line': line_index + 1, 'source': text,
                                    'effect': 'None: the next original instruction overwrites D1 with immediate0.'})
            if not text:
                continue
            if ':' in text:
                label, text = text.split(':', 1)
                local[label.lower()] = decoded[len(selected)].address if len(selected) < len(decoded) else end
                text = text.strip()
            if text:
                selected.append((line_index + 1, text))
        require(len(selected) == len(decoded), name + ': instruction count differs')
        records = []
        for instruction, (line_index, text) in zip(decoded, selected):
            actual = normalized(instruction.mnemonic + ' ' + instruction.op_str)
            expected = normalized(text, local)
            require(actual == expected, f'{name} {instruction.address:#x}: {actual} !={expected} ({text})')
            records.append({'address': hex(instruction.address), 'bytes': instruction.bytes.hex(),
                            'original': instruction.mnemonic + ' ' + instruction.op_str,
                            'source_line': line_index, 'source': text})
            listing.append(f'{instruction.address:#x} {instruction.bytes.hex():24} {instruction.mnemonic} {instruction.op_str}')
        routines.append({'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                         'payload_offset': hex(start - BASE), 'disk_offset': hex(start - BASE + DISK_OFFSET),
                         'sha256': sha(code), 'capture_equals_original': True,
                         'instruction_count': len(records), 'operand_wildcards': 0, 'instructions': records})
    def byte(address):
        return payload[address - BASE]
    def word(address):
        return int.from_bytes(payload[address - BASE:address - BASE + 2], 'big')
    def longword(address):
        return int.from_bytes(payload[address - BASE:address - BASE + 4], 'big')
    shapes = []
    for index in range(25):
        template, variation = longword(0x699b8 + 8 * index), longword(0x699bc + 8 * index)
        cursor, substitute = template + 2, variation
        vertices = []
        for _ in range(word(template)):
            point = []
            for _ in range(2):
                value = word(cursor)
                cursor += 2
                if value & 0x8000:
                    value = word(substitute)
                    substitute += 2
                point.append(value if value < 0x8000 else value - 0x10000)
            vertices.append(point)
        edge_count = byte(cursor)
        cursor += 1
        edges = [[byte(cursor + 2 * n) // 2, byte(cursor + 2 * n + 1) // 2] for n in range(edge_count)]
        cursor += 2 * edge_count
        face_count = byte(cursor)
        cursor += 1
        faces = []
        for _ in range(face_count):
            color, count = byte(cursor), byte(cursor + 1)
            cursor += 2
            faces.append({'color': color, 'edges': [byte(cursor + n) // 4 for n in range(count)]})
            cursor += count
        shapes.append({'index': index, 'template_address': hex(template), 'template_end_exclusive': hex(cursor),
                       'template_sha256': sha(payload[template - BASE:cursor - BASE]),
                       'variation_address': hex(variation), 'variation_bytes_consumed': substitute - variation,
                       'variation_sha256': sha(payload[variation - BASE:substitute - BASE]),
                       'vertices': vertices, 'edges': edges, 'faces': faces})
    placements = longword(0x69a80)
    require(placements == longword(0x69a84), 'Two original mountain list pointers differ')
    placement_count = byte(placements)
    tables = []
    for name, start, end in [('templates and substitutions', 0x6979e, 0x699b8),
                              ('shape and placement pointer tables', 0x699b8, 0x69a88),
                              ('placement list', placements, placements + 1 + placement_count * 2)]:
        data = payload[start - BASE:end - BASE]
        require(chip[start:end] == data, name + ': captured original data differs')
        tables.append({'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                       'payload_offset': hex(start - BASE), 'disk_offset': hex(start - BASE + DISK_OFFSET),
                       'bytes': len(data), 'sha256': sha(data), 'capture_equals_original': True})
    report = {'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA,
              'reference_commit': COMMIT, 'reference_source_sha256': SOURCE_SHA,
              'reference_url': source_info['url'], 'routines': routines,
              'source_differences': differences, 'operand_wildcards': 0, 'tables': tables,
              'capture': {'directory': str(args.capture.resolve()), 'ram_sha256': sha(chip),
                          'source_sha256': capture_manifest['source_sha256']},
              'shape_variants': len(shapes), 'templates': len({shape['template_address'] for shape in shapes}),
              'placement_count': placement_count,
              'selected_shape_indices': sorted({byte(placements + 2 + 2 * n) for n in range(placement_count)}),
              'track_selection': 'Original reads currentTrackID then immediately replaces it with0; both table pointers name the same32-placement list. The pinned source comments out only the dead read.',
              'selection': 'Visit placements in descending index order. Keep uint8(angle-(yawHigh-28))<44.',
              'coordinate_pipeline': 'Signed-word ((selectedDelta-28)<<8)-(yawLow&0xfe), ASR3. Add local shapeX; Y=-(ASR(viewportCenterY,3))-localShapeY. Execute original Q15 roll, ASR2 and center(128,64).',
              'renderer_boundary': 'The oracle replaces original drawClippedLine, setupBitplaneMasks and polygon pixel backends with command recorders and symbolic line handles. It executes the unchanged selector, asset substitution, every vertex roll and polygon command consumer. Original clipping and pixel fills are outside the equality claim.',
              'rea_evidence': 'analysis/mountain-horizon/rea-render.json',
              'assets_embedded_in_source': False}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'reference-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    (args.output / 'original-disassembly.asm').write_text('\n'.join(listing) + '\n')
    (args.output / 'decoded-assets.json').write_text(json.dumps({'shapes': shapes,
        'placement_address': hex(placements), 'placements': [
            {'horizontal_angle': byte(placements + 1 + 2 * n), 'shape_index': byte(placements + 2 + 2 * n)}
            for n in range(placement_count)]}, indent=2) + '\n')
    if args.oracle:
        payload_path = REFERENCE / 'offline-extracted-payload.bin'
        require(payload_path.read_bytes() == payload, 'Local extracted payload differs')
        result = subprocess.run([str(args.oracle.resolve()), str(payload_path), str(chip_path)],
                                cwd=ROOT, text=True, capture_output=True, check=True)
        oracle = json.loads(result.stdout)
        require(oracle['mismatches'] == 0 and oracle['total_cases'] >= 6817, 'Insufficient oracle coverage')
        oracle.update({'executable': str(args.oracle.resolve()), 'executable_sha256': sha(args.oracle.read_bytes()),
                       'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA, 'ram_sha256': sha(chip),
                       'source_sha256': {str(path.relative_to(ROOT)): sha(path.read_bytes()) for path in [
                           ROOT / 'src/mountain_horizon.h', ROOT / 'src/mountain_horizon.cpp',
                           ROOT / 'tests/oracle/mountain_horizon_oracle.cpp', Path(__file__).resolve()]}})
        (args.output / 'oracle.json').write_text(json.dumps(oracle, indent=2) + '\n')
    print(json.dumps({'report': str(args.output / 'reference-comparison.json'),
                      'original_instructions': sum(item['instruction_count'] for item in routines),
                      'operand_wildcards': 0, 'templates': report['templates'],
                      'shape_variants': len(shapes), 'placements': placement_count,
                      'oracle': str(args.output / 'oracle.json') if args.oracle else None}, indent=2))


if __name__ == '__main__':
    main()
