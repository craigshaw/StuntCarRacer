"""Extract RAM/register evidence from a WinUAE 6.0.3 USS; never execute it.

Format reference: tonioni/WinUAE commit
857f48fcb09399d4d95527e29a2fdeaedb41f827, savestate.cpp save_chunk and
newcpu.cpp save_cpu. Only the project's A500 512K chip/512K slow profile
is mapped here. All other chunks are inventoried without interpretation.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
WINUAE_REV = '857f48fcb09399d4d95527e29a2fdeaedb41f827'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def u32(data, offset):
    return struct.unpack_from('>I', data, offset)[0]


def chunks(raw):
    """Yield (metadata, decoded bytes), including both END markers.

    WinUAE pads by 4-(length&3), including FOUR bytes when already aligned.
    END is an exceptional eight-byte record with no flags or padding.
    """
    pos = 0
    while pos < len(raw):
        if pos + 8 > len(raw):
            raise ValueError(f'Truncated header at {pos:#x}')
        tag = raw[pos:pos + 4].decode('ascii')
        size = u32(raw, pos + 4)
        if tag == 'END ' and size == 8:
            yield {'tag': tag, 'file_offset': pos, 'stored_size': 8}, b''
            pos += 8
            continue
        if size <= 12 or pos + size > len(raw):
            raise ValueError(f'Invalid chunk {tag!r} at {pos:#x}: {size}')
        flags = u32(raw, pos + 8)
        if flags & ~1:
            raise ValueError(f'Unknown flags {flags:#x} for {tag!r}')
        stored = raw[pos + 12:pos + size]
        if flags & 1:
            expected_size = u32(stored, 0)
            if expected_size > 32 * 1024 * 1024:
                raise ValueError('Chunk exceeds this capture parser limit')
            decoder = zlib.decompressobj()
            data = decoder.decompress(stored[4:], expected_size + 1)
            if (len(data) != expected_size or not decoder.eof
                    or decoder.unused_data or decoder.unconsumed_tail):
                raise ValueError(f'Bad compressed data/length in {tag!r}')
        else:
            data = stored
        padding = 4 - (size & 3)
        if raw[pos + size:pos + size + padding] != bytes(padding):
            raise ValueError(f'Invalid padding after {tag!r}')
        yield {'tag': tag, 'file_offset': pos, 'stored_size': size,
               'flags': flags, 'decoded_size': len(data), 'sha256': sha(data)}, data
        pos += size + padding
    if pos != len(raw):
        raise ValueError('Did not consume the exact file')


def decode_cpu(data):
    if len(data) < 90 or u32(data, 0) != 68000:
        raise ValueError('Only the observed 68000 CPU layout is supported')
    regs = {f'D{i}': f'0x{u32(data, 8 + i * 4):08x}' for i in range(8)}
    regs.update({f'A{i}': f'0x{u32(data, 40 + i * 4):08x}' for i in range(7)})
    sr = struct.unpack_from('>H', data, 84)[0]
    regs.update(PC=f'0x{u32(data, 68):08x}',
                USP=f'0x{u32(data, 76):08x}', ISP=f'0x{u32(data, 80):08x}',
                SR=f'0x{sr:04x}')
    regs['A7'] = regs['ISP'] if sr & 0x2000 else regs['USP']
    return {'model': 68000, 'flags': f'0x{u32(data, 4):08x}',
            'registers': regs, 'stopped_flags': u32(data, 86),
            'note': 'One saved CPU state; PC is not an execution trace.'}


def extract(source, out, context='unspecified'):
    raw = source.read_bytes()
    parsed = list(chunks(raw))
    if not parsed or parsed[0][0]['tag'] != 'ASF ':
        raise ValueError('Not a UAE state file')
    by_tag = {}
    for meta, data in parsed:
        if meta['tag'] != 'END ' and meta['tag'] in by_tag:
            raise ValueError(f'Duplicate chunk {meta["tag"]!r}')
        by_tag[meta['tag']] = data
    header = by_tag['ASF '][4:].split(b'\0')
    if header[:2] != [b'UAE', b'6.0.3']:
        raise ValueError(f'Expected WinUAE 6.0.3, got {header[:2]}')
    config = by_tag.get('CONF', b'').decode('utf-8-sig', errors='replace').strip('\0')
    settings = dict(line.split('=', 1) for line in config.splitlines()
                    if '=' in line and not line.startswith(';'))
    required = {'cpu_model': '68000', 'chipmem_size': '1', 'bogomem_size': '2'}
    for name, value in required.items():
        if settings.get(name) != value:
            raise ValueError(f'Unsupported mapping: {name}={settings.get(name)!r}')
    if len(by_tag.get('CRAM', b'')) != 0x80000 or len(by_tag.get('BRAM', b'')) != 0x80000:
        raise ValueError('Expected exactly 512K chip RAM and 512K slow RAM')
    # A500 OCS/ECS-Agnus with 512K chip does not use 1MB-chip alias mapping.
    cpu = decode_cpu(by_tag['CPU '])
    out.mkdir(parents=True, exist_ok=False)
    (out / 'configuration.uae').write_text(config, encoding='utf-8')
    banks = []
    for tag, name, base in [('CRAM', 'chip-000000.bin', 0), ('BRAM', 'slow-c00000.bin', 0xc00000)]:
        data = by_tag[tag]
        (out / name).write_bytes(data)
        banks.append({'tag': tag, 'file': name, 'base': hex(base),
                      'end_exclusive': hex(base + len(data)),
                      'size': len(data), 'sha256': sha(data)})
    manifest = {'source_path': str(source.resolve()), 'source_sha256': sha(raw),
                'source_bytes': len(raw), 'producer': 'WinUAE 6.0.3',
                'format_reference_commit': WINUAE_REV,
                'capture_description': header[2].decode('utf-8', errors='replace'),
                'capture_context': context,
                'configuration_summary': {k: settings.get(k) for k in
                                          ('cpu_model', 'chipset', 'chipset_compatible',
                                           'chipmem_size', 'bogomem_size', 'sound_output', 'ntsc')},
                'chunks': [m for m, _ in parsed], 'cpu': cpu, 'banks': banks,
                'mapping_basis': 'A500 profile, CONF sizes, memory.cpp:3186 (BRAM at 0xc00000)',
                'limitations': ['A single saved state does not establish execution coverage.',
                                'Capture context is caller-provided; RAM/register evidence is separate.',
                                'Non-RAM chunks inventoried, not fully decoded.']}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('state', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/captures/menu')
    parser.add_argument('--context', choices=['menu', 'race', 'unspecified'], default='unspecified')
    args = parser.parse_args()
    result = extract(args.state, args.output, args.context)
    print(json.dumps({'sha256': result['source_sha256'], 'cpu': result['cpu'],
                      'banks': result['banks']}, indent=2))
