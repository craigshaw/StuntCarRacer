"""Build a local, reproducible data pack from the verified original ADF.

Only the extraction recipe is distributed. The pack and manifest remain under
ignored analysis/. No ROM, savestate, emulator or downloaded reference is used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
SOURCE_REGISTRY = ROOT / 'setup/adf-sources.json'
SOURCE_DEFINITIONS = json.loads(SOURCE_REGISTRY.read_text(encoding='utf-8'))
ADF_SOURCES = {source['sha256']: source for source in SOURCE_DEFINITIONS['sources']}
# Retained reference/oracle fixtures use the first, canonical disk revision.
ADF_SHA = SOURCE_DEFINITIONS['sources'][0]['sha256']
PAYLOAD_SHA = SOURCE_DEFINITIONS['sources'][0]['payload_sha256']
ADF_BYTES = SOURCE_DEFINITIONS['adf_bytes']
DISK_OFFSET = SOURCE_DEFINITIONS['disk_offset']
PAYLOAD_BYTES = SOURCE_DEFINITIONS['payload_bytes']
LOAD_ADDRESS = SOURCE_DEFINITIONS['load_address']
MAGIC, VERSION, HEADER_BYTES = b'SCRDATA1', 1, 88
DEFAULT_PACK = ROOT / 'analysis/assets/stunt-car-racer.scr-assets'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build_pack(adf):
    source = ADF_SOURCES.get(sha(adf))
    if len(adf) != ADF_BYTES or source is None:
        raise ValueError('ADF does not match a supported original disk SHA-256')
    payload = adf[DISK_OFFSET:DISK_OFFSET + PAYLOAD_BYTES]
    if sha(payload) != source['payload_sha256']:
        raise ValueError('Original payload identity changed')
    header = struct.pack('<8sII32s32sII', MAGIC, VERSION, HEADER_BYTES,
                         bytes.fromhex(sha(adf)), bytes.fromhex(source['payload_sha256']), LOAD_ADDRESS, len(payload))
    assert len(header) == HEADER_BYTES
    return header + payload


def import_assets(adf_path, output):
    adf_path, output = Path(adf_path).resolve(), Path(output).resolve()
    if not output.is_relative_to((ROOT / 'analysis').resolve()):
        raise ValueError('Derived asset packs must stay under this project\'s ignored analysis/ directory')
    if adf_path in {output, output.with_suffix(output.suffix + '.json'), output.with_suffix(output.suffix + '.tmp')}:
        raise ValueError('Asset import must preserve the original ADF path')
    original = adf_path.read_bytes()
    pack = build_pack(original)
    source = ADF_SOURCES[sha(original)]
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + '.tmp')
    temporary.write_bytes(pack)
    temporary.replace(output)
    record = {'format': 'SCRDATA1', 'version': VERSION,
              'source': {'filename': adf_path.name, 'bytes': len(original), 'sha256': sha(original), 'variant': source['name']},
              'source_image': {'disk_offset': DISK_OFFSET, 'bytes': PAYLOAD_BYTES,
                               'load_address': LOAD_ADDRESS, 'sha256': source['payload_sha256']},
              'pack': {'filename': output.name, 'bytes': len(pack), 'sha256': sha(pack)},
              'recipe': {'path': 'scripts/import_native_assets.py',
                         'sha256': sha(Path(__file__).read_bytes()), 'sources_sha256': sha(SOURCE_REGISTRY.read_bytes())},
              'capture_input': False, 'rom_input': False, 'cpu_execution': False,
              'description': 'Original source image retained as read-only data. Native startup decodes assets and builds race state; original machine code is never executed.'}
    output.with_suffix(output.suffix + '.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adf', type=Path, default=ROOT / 'Stunt Car Racer (MicroStyle).adf')
    parser.add_argument('--output', type=Path, default=DEFAULT_PACK)
    args = parser.parse_args()
    record = import_assets(args.adf, args.output)
    print(json.dumps({'status': 'passed', 'path': str(args.output), **record}, indent=2))


if __name__ == '__main__':
    main()
