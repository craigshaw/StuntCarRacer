"""Compare recovered A500 RAM with the pinned disk extraction and build a map.

All intervals use exclusive ends. Labels from the enhanced reference are
navigation aids unless an explicit stronger check is attached. This script
does not classify every byte, or infer that unchanged bytes were executed.
"""
from pathlib import Path
import argparse
import csv
import hashlib
import json

ROOT = Path(__file__).resolve().parents[2]
REF = ROOT / 'analysis/reference/vesuri'
BASE = 0xe700
DISK_OFFSET = 0xdc00
SIZE = 408540
EXPECTED_DISK = '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
EXPECTED_PAYLOAD = '508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'


def sha(b):
    return hashlib.sha256(b).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write_json(path, obj):
    path.write_text(json.dumps(obj, indent=2) + '\n', encoding='utf-8')


def runs(values):
    """Half-open intervals of consecutive true values."""
    start = None
    for i, value in enumerate(values):
        if value and start is None:
            start = i
        elif not value and start is not None:
            yield start, i
            start = None
    if start is not None:
        yield start, i + 1


def csv_write(path, rows):
    if not rows:
        return
    with path.open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def build(capture, out):
    disk = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    if sha(disk) != EXPECTED_DISK:
        raise ValueError('Supplied ADF differs from recorded original')
    payload = (REF / 'offline-extracted-payload.bin').read_bytes()
    if sha(payload) != EXPECTED_PAYLOAD or payload != disk[DISK_OFFSET:DISK_OFFSET + SIZE]:
        raise ValueError('Extraction does not match the recorded disk range')
    cap = read_json(capture / 'manifest.json')
    chip = (capture / 'chip-000000.bin').read_bytes()
    if len(chip) != 0x80000 or sha(chip) != cap['banks'][0]['sha256']:
        raise ValueError('Chip RAM does not match capture manifest')
    loaded = chip[BASE:BASE + SIZE]
    if len(loaded) != SIZE:
        raise ValueError('Incomplete loaded image')
    # Multiple separated unique unchanged anchors corroborate the entire mapping.
    anchors = []
    for offset, length, name in [(0, 48, 'payload startup'),
                                 (0xe342, 128, 'reference sine table'),
                                 (0x531ce, 130, 'wheel-corner routine'),
                                 (0x5315c, 114, 'physics driver')]:
        block = payload[offset:offset + length]
        hits = []
        at = chip.find(block)
        while at >= 0:
            hits.append(at)
            at = chip.find(block, at + 1)
        anchors.append({'name': name, 'payload_offset': hex(offset), 'size': length,
                        'ram_matches': [hex(x) for x in hits],
                        'expected_address': hex(BASE + offset),
                        'unique_expected_match': hits == [BASE + offset]})
    if not all(a['unique_expected_match'] for a in anchors):
        raise ValueError('Load-address anchors did not all match uniquely')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'loaded-game-e700.bin').write_bytes(loaded)
    diff = [a != b for a, b in zip(payload, loaded)]
    changed = sum(diff)
    diff_rows = []
    for start, end in runs(diff):
        diff_rows.append({'payload_start': hex(start), 'payload_end_exclusive': hex(end),
                          'ram_start': hex(BASE + start), 'ram_end_exclusive': hex(BASE + end),
                          'adf_start': hex(DISK_OFFSET + start), 'bytes': end - start})
    csv_write(out / 'changed-ranges.csv', diff_rows)
    stable = sorted(runs([not x for x in diff]), key=lambda r: r[1] - r[0], reverse=True)
    selected = read_json(REF / 'selected-comparison.json')
    # Distinguish whole routine verification from mere prefix matches.
    code = []
    for label in read_json(REF / 'matched-code-labels.json'):
        match = label['match']
        offset = int(match['payload_offset'], 16)
        prefix = b''.join(bytes.fromhex(i['bytes']) for i in match['instructions'])
        if payload[offset:offset + len(prefix)] != prefix:
            raise ValueError(f'Prefix no longer matches {label["name"]}')
        code.append({'name': label['name'], 'kind': 'code_candidate',
                     'payload_offset': hex(offset), 'ram_address': hex(BASE + offset),
                     'adf_offset': hex(DISK_OFFSET + offset), 'checked_bytes': len(prefix),
                     'ram_equals_disk_for_checked_bytes': loaded[offset:offset + len(prefix)] == prefix,
                     'source_line': label['source_line'],
                     'evidence': 'unique constrained instruction-prefix match; not whole function',
                     'semantic_status': 'upstream name; provisional'})
    selected_checks = []
    for key in ('wheel_corner', 'physics_driver', 'keyboard_query'):
        entry = selected[key]
        start = int(entry['runtime_address'], 16)
        end = int(entry['end_exclusive'], 16)
        a, b = start - BASE, end - BASE
        same = payload[a:b] == loaded[a:b]
        selected_checks.append({'name': entry['name'], 'start': hex(start),
                                'end_exclusive': hex(end), 'bytes': end - start,
                                'ram_equals_disk': same, 'ram_sha256': sha(loaded[a:b]),
                                'reference_comparison': entry['status']})
        for row in code:
            if row['name'] == entry['name']:
                row.update(checked_bytes=end - start,
                           ram_equals_disk_for_checked_bytes=same,
                           evidence=entry['status'], semantic_status='corroborated by original instructions')
    # The main loop is a label inside a larger function, not necessarily a
    # standalone subroutine. Its checked extent is only the call-sequence prefix.
    loop = selected['main_loop']
    lo, hi = 0x5d47e - BASE, 0x5d4ba - BASE
    code.append({'name': loop['name'], 'kind': 'code_loop_label',
                 'payload_offset': hex(lo), 'ram_address': hex(BASE + lo),
                 'adf_offset': hex(DISK_OFFSET + lo), 'checked_bytes': hi - lo,
                 'ram_equals_disk_for_checked_bytes': loaded[lo:hi] == payload[lo:hi],
                 'source_line': loop['source_line'], 'evidence': loop['status'],
                 'semantic_status': 'call-sequence correspondence; modified upstream scheduling'})
    data = []
    for symbol in read_json(REF / 'data-symbols.json'):
        offset = int(symbol['payload_offset'], 16)
        data.append({'name': symbol['name'], 'kind': 'data_label_candidate',
                     'payload_offset': hex(offset), 'ram_address': hex(BASE + offset),
                     'adf_offset': hex(DISK_OFFSET + offset) if offset < SIZE else '',
                     'byte_in_payload': 0 <= offset < SIZE,
                     'source_line': symbol['source_line'],
                     'evidence': 'pinned source equate plus independently corroborated load base',
                     'semantic_status': 'unverified; enhanced source may reuse original fields'})
    csv_write(out / 'code-symbols.csv', sorted(code, key=lambda x: int(x['ram_address'], 16)))
    csv_write(out / 'data-symbols.csv', sorted(data, key=lambda x: int(x['ram_address'], 16)))
    write_json(out / 'symbols.json', {'code': code, 'data': data})
    # Data conversion predicted from original startup code, not an assumption
    # that RAM must be an exact file copy.
    sample_a, sample_b = 0xf4b8 - BASE, 0x1aa4a - BASE
    expected_samples = bytes(v ^ 0x80 for v in payload[sample_a:sample_b])
    sample_observed = loaded[sample_a:sample_b]
    sample_conversion = {'start': '0xf4b8', 'end_exclusive': '0x1aa4a',
                         'bytes': len(expected_samples),
                         'matches_xor_80_conversion': sample_observed == expected_samples,
                         'mismatching_bytes': sum(a != b for a, b in zip(sample_observed, expected_samples)),
                         'basis': 'original startup at 0xee64..0xee76; pinned reference comparison'}
    region_specs = [
        (0xe700, 0xe74e, 'code', 'Startup/checksum and supervisor entry', 'original instructions; unchanged startup anchor'),
        (0xe74e, 0xe83a, 'data', 'Copper display list', 'startup sets pointer; fffffffe terminator at e836'),
        (0xe83a, 0xed56, 'mixed', 'Palette, stack, keyboard and serial state', 'source labels plus startup references; boundaries provisional'),
        (0xed56, 0xf4b8, 'mixed/code', 'Hardware/interrupt support and sample setup', 'original vector installation and instruction decoding'),
        (0xf4b8, 0x1aa4a, 'data', 'Sound sample source bytes', 'all 46482 RAM bytes equal disk XOR 80'),
        (0x1aa4a, 0x1b6c8, 'data/work', 'Generated engine sample area', 'original generator writes here; exact contents not yet reconstructed'),
        (0x1b6c8, 0x1ca42, 'mixed', 'Low code, sound parameters and game state', 'source labels and direct code references; not a pure data section'),
        (0x1ca42, 0x1ec46, 'data/candidate', 'Sine, reciprocal and arctangent tables', 'source labels; unchanged bytes; mathematical meanings not yet checked'),
        (0x1ec46, 0x20184, 'data/candidate', 'Matrices, names, track data, font and state', 'source equates; exact object boundaries provisional'),
        (0x20184, 0x5700c, 'data/candidate', 'Graphics assets', 'source image labels; upper bound is next known code, not verified asset end'),
        (0x5700c, 0x6a584, 'mixed/code', 'Main game, physics, input, menu and renderer', 'many matched routines; interleaved mutable data remains'),
        (0x6a584, 0x6a594, 'data/work', 'Framebuffer pointers and nearby state', 'startup writes pointer cells at 6a584/6a588/6a58c'),
        (0x6a594, 0x72294, 'data/work', 'Framebuffer 1', 'startup base and 32000-byte span'),
        (0x72294, 0x79f94, 'data/work', 'Framebuffer 2', 'startup base and 32000-byte span; mostly beyond payload'),
    ]
    regions = []
    for start, end, kind, name, basis in region_specs:
        a, b = max(start - BASE, 0), min(end - BASE, SIZE)
        regions.append({'start': hex(start), 'end_exclusive': hex(end), 'kind': kind,
                        'name': name, 'evidence': basis, 'bytes_in_disk_payload': max(0, b-a),
                        'changed_bytes_in_payload': sum(diff[a:b]) if a < b else 0})
    csv_write(out / 'regions.csv', regions)
    hardware = {'chip_ram': ['0x000000', '0x080000'], 'slow_ram': ['0xc00000', '0xc80000'],
                'pc_at_capture': cap['cpu']['registers']['PC'],
                'framebuffer_pointer_cells': {hex(a): hex(int.from_bytes(chip[a:a+4], 'big'))
                                              for a in (0x6a584, 0x6a588, 0x6a58c)},
                'vectors': [{'vector_address': hex(a), 'handler_address': hex(int.from_bytes(chip[a:a+4], 'big'))}
                            for a in (0x64, 0x68, 0x6c, 0x70, 0x74, 0x78, 0x7c, 0x80)]}
    manifest = {'source_adf_sha256': sha(disk), 'disk_offset': hex(DISK_OFFSET),
                'payload_bytes': SIZE, 'payload_sha256': sha(payload), 'ram_base': hex(BASE),
                'ram_end_exclusive': hex(BASE + SIZE), 'loaded_sha256': sha(loaded),
                'capture_manifest': str((capture / 'manifest.json').resolve()),
                'capture_sha256': cap['source_sha256'],
                'reference_commit': read_json(REF / 'provenance.json')['commit'],
                'mapping_anchors': anchors, 'equal_bytes': SIZE - changed,
                'changed_bytes': changed, 'equal_percent': round(100 * (SIZE - changed) / SIZE, 4),
                'changed_runs': len(diff_rows),
                'largest_unchanged_ranges': [{'start': hex(BASE + a), 'end_exclusive': hex(BASE + b), 'bytes': b-a}
                                            for a, b in stable[:12]],
                'sample_conversion': sample_conversion, 'selected_routines': selected_checks,
                'prefix_label_candidates': len(code) - 1,
                'additional_loop_labels': 1, 'data_label_candidates': len(data),
                'hardware': hardware,
                'limitations': ['One menu snapshot; no gameplay execution trace.',
                                'Reference source is enhanced; names and boundaries remain provisional except specified checks.',
                                'This is an initial sparse map, not a complete partition of code and data.',
                                'RAM differences outside explicitly checked regions remain unexplained.']}
    write_json(out / 'manifest.json', manifest)
    lines = [
        'STUNT CAR RACER - INITIAL LOADED CODE/DATA MAP',
        'All end addresses are exclusive. This is a sparse, evidence-graded map.', '',
        f'ADF SHA-256: {sha(disk)}',
        f'State SHA-256: {cap["source_sha256"]}',
        f'Payload: disk 0xdc00 + {SIZE} bytes -> chip RAM [0xe700,0x722dc).',
        'Four separated byte sequences each occur once in RAM at the predicted address.',
        f'{SIZE-changed:,} bytes unchanged ({manifest["equal_percent"]}%); {changed:,} changed.',
        'All 46,482 source sample bytes match the startup XOR-0x80 conversion.',
        'Other differences include mutable state, generated samples and framebuffer overlap;',
        'individual differences outside the sample conversion have not all been explained.', '',
        'CONFIRMED STATIC CORRESPONDENCE (not proof of gameplay execution)',
        '0x6185c: physics driver; 114 unchanged bytes, 17 reference-named calls.',
        '0x618ce: wheel-coordinate routine; 130 unchanged bytes; all 29 instructions',
        '          and operands match the pinned source; Ghidra and MC68000 agree.',
        '0x5d47e: original main-loop prefix; nine calls correspond to reference names.',
        '0x61956: timestep immediate is 0xee (238); enhanced reference uses 39 for PAL.',
        f'Captured PC: {hardware["pc_at_capture"]}, inside checkKeyPressed at 0x612be.',
        'This is a menu snapshot, not an observation of the racing physics executing.', '',
        'COARSE REGIONS (candidate ranges may include mixed code/data)',
    ]
    lines += [f'{r["start"]:>9} .. {r["end_exclusive"]:<9} {r["kind"]:<16} {r["name"]}' for r in regions]
    lines += ['', 'FILES',
              'regions.csv: bounds, evidence and changed-byte counts.',
              'code-symbols.csv: 160 prefix candidates plus main loop; checked-byte status.',
              'data-symbols.csv: 897 upstream equates; meanings remain provisional.',
              'symbols.json: both inventories for later tooling.',
              'changed-ranges.csv: every RAM-versus-file difference interval.',
              'manifest.json: hashes, anchors, vectors and verification results.',
              'loaded-game-e700.bin: exact slice of captured chip RAM, not the untouched file.',
              '../reference/vesuri/selected-comparison.json: source/instruction comparisons.',
              '../rea/: disk and loaded-image evidence bundles and reusable snapshots.', '',
              'NEXT: capture a race state and original inputs, then translate and compare',
              'the small wheel-coordinate routine with explicit 16-bit arithmetic rules.',
              'Asset formats, full function boundaries, timing and hardware dependencies',
              'still need analysis before a faithful native runtime can be designed.']
    (out / 'report.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return manifest


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture', type=Path, default=ROOT / 'analysis/captures/menu')
    p.add_argument('--output', type=Path, default=ROOT / 'analysis/map')
    args = p.parse_args()
    result = build(args.capture, args.output)
    print(json.dumps(result, indent=2))
