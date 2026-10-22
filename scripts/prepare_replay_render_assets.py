"""Verify original sources and decode local-only cockpit/reference images."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]
ADF_SHA = '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA = '508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
BASE, DISK_OFFSET = 0xe700, 0xdc00
PALETTE, BACKGROUND = 0x20184, 0x201a4


def sha(data):
    return hashlib.sha256(data).hexdigest()


def decode_rle(data):
    output = bytearray(32000)
    offset = 0
    for y in range(200):
        for plane in range(4):
            row = bytearray()
            while len(row) < 40:
                if offset >= len(data):
                    raise ValueError('Truncated original image stream')
                control = data[offset]
                offset += 1
                if control == 128:
                    continue
                count = control + 1 if control < 128 else 257 - control
                if len(row) + count > 40:
                    raise ValueError('Original image run crosses row boundary')
                if control < 128:
                    if offset + count > len(data):
                        raise ValueError('Truncated literal run')
                    row.extend(data[offset:offset + count])
                    offset += count
                else:
                    if offset >= len(data):
                        raise ValueError('Truncated repeated run')
                    row.extend([data[offset]] * count)
                    offset += 1
            begin = plane * 8000 + y * 40
            output[begin:begin + 40] = row
    return bytes(output), offset


def indexed(planar):
    if len(planar) != 32000:
        raise ValueError('Expected one original320x200x4 framebuffer')
    return bytes(sum(((planar[p * 8000 + y * 40 + x // 8] >> (7 - x % 8)) & 1) << p for p in range(4))
                 for y in range(200) for x in range(320))


def palette(words):
    # Original copyPaletteToCopperlist at0x1ba82 doubles each channel and
    # sets bit0 if nonzero. Its source colours have three significant bits.
    channel = lambda value: ((value << 1) | bool(value)) * 17
    return [(channel((word >> 8) & 7), channel((word >> 4) & 7), channel(word & 7)) for word in words]


def write_png(path, pixels, colours):
    def chunk(kind, contents):
        return struct.pack('>I', len(contents)) + kind + contents + struct.pack('>I', zlib.crc32(kind + contents))
    rows = b''.join(b'\0' + b''.join(bytes(colours[index]) for index in pixels[y * 320:(y + 1) * 320]) for y in range(200))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 320, 200, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def composite(pixels, ram, object_id):
    word = lambda a: int.from_bytes(ram[a:a + 2], 'big')
    record = 0x6a16c + object_id * 16
    source = int.from_bytes(ram[0x6a4ac + object_id * 4:0x6a4b0 + object_id * 4], 'big')
    width, height = word(record + 4) + 1, word(record + 6) + 1
    x, y = (word(record + 8) & 255) * 16, word(record + 10) & 255
    if width > 20 or height > 200 or source + width * height * 10 > len(ram):
        raise ValueError('Captured software object lies outside supported image domain')
    for row in range(height):
        for group in range(width):
            at = source + (row * width + group) * 10
            mask = word(at)
            planes = [word(at + 2 + p * 2) for p in range(4)]
            for bit in range(16):
                xx, yy = x + group * 16 + bit, y + row
                if xx >= 320 or yy >= 200:
                    continue
                colour = sum(((v >> (15 - bit)) & 1) << p for p, v in enumerate(planes))
                previous = pixels[yy * 320 + xx] if mask & (1 << (15 - bit)) else 0
                pixels[yy * 320 + xx] = previous | colour
    return {'id': object_id, 'source': hex(source), 'bytes': width * height * 10,
            'sha256': sha(ram[source:source + width * height * 10]), 'x': x, 'y': y,
            'width': width * 16, 'height': height}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, default=ROOT / 'analysis/captures/race')
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/native-render')
    parser.add_argument('--oracle', type=Path, help='Also run an already-built original/native image oracle')
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    if sha(adf) != ADF_SHA:
        raise ValueError('Supplied ADF identity changed')
    payload = adf[DISK_OFFSET:DISK_OFFSET + 408540]
    if sha(payload) != PAYLOAD_SHA:
        raise ValueError('Original payload identity changed')
    manifest = json.loads((args.capture / 'manifest.json').read_text())
    if sha(Path(manifest['source_path']).read_bytes()) != manifest['source_sha256']:
        raise ValueError('Source savestate identity changed')
    bank = next(item for item in manifest['banks'] if item['tag'] == 'CRAM')
    ram = (args.capture / bank['file']).read_bytes()
    if len(ram) != 0x80000 or sha(ram) != bank['sha256']:
        raise ValueError('Captured Chip RAM identity changed')
    original, consumed = decode_rle(payload[BACKGROUND - BASE:])
    if ram[BACKGROUND:BACKGROUND + consumed] != payload[BACKGROUND - BASE:BACKGROUND - BASE + consumed]:
        raise ValueError('Captured compressed background differs from original disk bytes')
    colours = palette(struct.unpack('>16H', payload[PALETTE - BASE:PALETTE - BASE + 32]))
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'cockpit-planar.bin').write_bytes(original)
    write_png(args.output / 'cockpit-background.png', indexed(original), colours)
    frame = int.from_bytes(ram[0x6a588:0x6a58c], 'big')
    if frame + 32000 > len(ram):
        raise ValueError('Captured display framebuffer outside Chip RAM')
    write_png(args.output / 'captured-display-frame.png', indexed(ram[frame:frame + 32000]), colours)
    composed = bytearray(indexed(original))
    objects = [composite(composed, ram, obj) for obj in (13, 14, ram[0x1bbdb], 5 - ram[0x1bbdb], 10, 11, 12)]
    write_png(args.output / 'cockpit-foreground.png', composed, colours)
    routines = []
    for name, start, end in [('RLE image decode', 0x593f8, 0x59450),
                             ('masked software object', 0x69f42, 0x69fe8),
                             ('palette conversion', 0x1ba82, 0x1bace),
                             ('background decode and copy', 0x1bace, 0x1baf8)]:
        original_code = payload[start - BASE:end - BASE]
        if ram[start:end] != original_code:
            raise ValueError(f'Original image routine differs in capture: {name}')
        routines.append({'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                         'disk_offset': hex(start - BASE + DISK_OFFSET), 'bytes': end - start,
                         'sha256': sha(original_code), 'capture_equals_original': True})
    record = {'status': 'assets_prepared', 'adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA,
        'capture': {'source_sha256': manifest['source_sha256'], 'chip_ram_sha256': bank['sha256']},
        'palette': {'runtime_address': hex(PALETTE), 'payload_offset': hex(PALETTE - BASE), 'disk_offset': hex(PALETTE - BASE + DISK_OFFSET), 'bytes': 32, 'sha256': sha(payload[PALETTE - BASE:PALETTE - BASE + 32]), 'rgb': colours},
        'background': {'runtime_address': hex(BACKGROUND), 'payload_offset': hex(BACKGROUND - BASE), 'disk_offset': hex(BACKGROUND - BASE + DISK_OFFSET), 'compressed_bytes': consumed, 'compressed_sha256': sha(payload[BACKGROUND - BASE:BACKGROUND - BASE + consumed]), 'planar_bytes': len(original), 'planar_sha256': sha(original)},
        'viewport': {'x': 32, 'y': 16, 'width': 256, 'height': 128, 'evidence': 'Original viewportTopAddress=renderFrameBuffer+0x284; original draw clipping bounds x<0x100,y<0x80.'},
        'display_frame_runtime_address': hex(frame), 'objects': objects, 'routines': routines,
        'font': {'runtime_address': '0x1fe82', 'bytes': 768, 'captured_sha256': sha(ram[0x1fe82:0x20182]), 'disk_sha256': sha(payload[0x11782:0x11a82]), 'initialized_byte_changes': [{'runtime_address': hex(0x1fe82 + i), 'disk': a, 'capture': b} for i, (a, b) in enumerate(zip(payload[0x11782:0x11a82], ram[0x1fe82:0x20182])) if a != b], 'reason': 'Original startup explicitly patches font bytes; native font uses captured initialized data.'},
        'evidence': {'rle_routine': '0x593f8..0x59450', 'masked_object_routine': '0x69f42..0x69fe8', 'background_copy': '0x1bace..0x1baf8', 'palette_to_copper': '0x1ba82..0x1bace'},
        'limitations': ['Captured display frame is a saved buffer, not a DMA/display timing observation.', 'Decoded static background has no moving road; foreground image is the decoded original background plus selected original masked objects.']}
    source_files = ['src/planar_image.h', 'src/planar_image.cpp',
                    'app/replay_render_assets.h', 'app/replay_render_assets.cpp',
                    'scripts/prepare_replay_render_assets.py', 'tests/oracle/planar_image_oracle.cpp']
    record['sources'] = [{'path': path, 'sha256': sha((ROOT / path).read_bytes())} for path in source_files]
    if args.oracle:
        executable_hash = sha(args.oracle.read_bytes())
        command = [str(args.oracle.resolve()), str(ROOT / 'analysis/reference/vesuri/offline-extracted-payload.bin'), str((args.capture / bank['file']).resolve())]
        oracle_payload = Path(command[1]).read_bytes()
        if sha(oracle_payload) != PAYLOAD_SHA:
            raise ValueError('Oracle payload does not match verified original')
        run = subprocess.run(command, check=True, capture_output=True, text=True)
        checks = json.loads(run.stdout)
        if checks.get('mismatches') != 0 or type(checks.get('total_cases')) is not int or checks['total_cases'] <= 0:
            raise ValueError('Original image oracle did not report passing comparisons')
        if sha(args.oracle.read_bytes()) != executable_hash or sha((args.capture / bank['file']).read_bytes()) != bank['sha256']:
            raise ValueError('Image oracle executable or capture changed during execution')
        record['oracle'] = {'command': command, 'executable_sha256': executable_hash, 'results': checks}
        record['status'] = 'assets_and_original_comparison_passed'
    (args.output / 'assets-manifest.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'manifest': str(args.output / 'assets-manifest.json'), 'compressed_bytes': consumed, 'images': ['cockpit-background.png', 'captured-display-frame.png', 'cockpit-foreground.png']}, indent=2))


if __name__ == '__main__':
    main()
