"""Reproduce pinned Vesuri slave's extraction algorithm against our supplied ADF.

Does not execute the upstream source. Output remains an offline decrypted image,
not a claim about observed runtime mapping. Capture comparison is separate.
"""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'analysis/reference/vesuri'
ADF = ROOT / 'Stunt Car Racer (MicroStyle).adf'
EXPECTED = '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
source = ADF.read_bytes()
if hashlib.sha256(source).hexdigest() != EXPECTED:
    raise ValueError('Supplied ADF differs from the recorded original')
provenance = json.loads((OUT/'provenance.json').read_text(encoding='utf-8-sig'))
if provenance['commit'] != '3cce78d193c3d0208be34d0bbfc3155e787cd406':
    raise ValueError('Unexpected reference revision')
for item in provenance['files']:
    if hashlib.sha256((OUT/item['path']).read_bytes()).hexdigest() != item['sha256']:
        raise ValueError('Reference file changed: ' + item['path'])
encrypted = source[0x2c00:0x2c04].hex() == '6000007a'
offset, size = (0xe898, 408544) if encrypted else (0xdc00,408540)
data = bytearray(source[offset:offset+size])
if encrypted:
    d5, d6, key = 0xc905b365, 0xa0cff27b, 0x59f3a592
    for i in range(0, len(data), 4):
        key = (key << 1) & 0xffffffff
        # 68000 register BTST uses bit index modulo 32. Add 1 when the bits differ.
        if ((key >> (d5 & 31)) ^ (key >> (d6 & 31))) & 1:
            key = (key + 1) & 0xffffffff
        plain = (int.from_bytes(data[i:i+4], 'big') + key) & 0xffffffff
        data[i:i+4] = plain.to_bytes(4, 'big')
        key = (key + plain) & 0xffffffff
payload = bytes(data[4:] if encrypted else data)
assert payload[:4].hex() == '207c0000'
(OUT / 'offline-extracted-payload.bin').write_bytes(payload)
record = {
    'source_path': ADF.name, 'source_sha256': EXPECTED,
    'method': 'Independent Python transcription of pinned slave lines 204-267, 400-415',
    'encrypted': encrypted,
    'disk_marker_offset': hex(0x2c00), 'disk_marker': source[0x2c00:0x2c04].hex(),
    'disk_offset': hex(offset), 'disk_size': size,
    'disk_range_sha256': hashlib.sha256(source[offset:offset+size]).hexdigest(),
    'decrypted_prefix_discarded_hex': data[:4].hex() if encrypted else None,
    'payload_size': len(payload), 'payload_sha256': hashlib.sha256(payload).hexdigest(),
    'payload_prefix_hex': payload[:32].hex(),
    'reference_load_address': hex(0xe700),
    'mapping_status': 'Reference source ORIGINAL_LOAD_ADDRESS; runtime corroboration pending',
    'reference_commit': json.loads((OUT/'provenance.json').read_text(encoding='utf-8-sig'))['commit']
}
(OUT/'offline-extraction.json').write_text(json.dumps(record, indent=2)+'\n')
text = (OUT/'StuntCarRacer.s').read_text()
symbols=[]
for n,line in enumerate(text.splitlines(),1):
    match=re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-fA-F]+)',line)
    if match:
        value=int(match[2],16)
        symbols.append({'name':match[1], 'payload_offset':hex(value),
                        'reference_runtime_address':hex(0xe700+value),'source_line':n,
                        'status':'upstream label; semantic meaning not independently verified'})
(OUT/'data-symbols.json').write_text(json.dumps(symbols, indent=2)+'\n')
print(json.dumps(record, indent=2))
print('Data equates:',len(symbols))
