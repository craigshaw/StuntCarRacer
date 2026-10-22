"""Verify original sample playback and its ordered Paula-register writes."""
import argparse
import json
import re
from pathlib import Path
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
from verify_transform_reference import (ROOT, REFERENCE, BASE, DISK_OFFSET, PAYLOAD_SIZE,
    ADF_SHA, PAYLOAD_SHA, SOURCE_SHA, COMMIT, sha, require, canonical)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, default=ROOT / 'analysis/captures/race')
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf) == ADF_SHA, 'ADF identity changed')
    payload = adf[DISK_OFFSET:DISK_OFFSET + PAYLOAD_SIZE]
    require(sha(payload) == PAYLOAD_SHA, 'Payload identity changed')
    require((REFERENCE / 'offline-extracted-payload.bin').read_bytes() == payload, 'Retained payload changed')
    source = (REFERENCE / 'StuntCarRacer.s').read_bytes()
    require(sha(source) == SOURCE_SHA, 'Pinned source identity changed')
    provenance = json.loads((REFERENCE / 'provenance.json').read_text())
    require(provenance['commit'] == COMMIT, 'Pinned revision changed')
    lines = source.decode().splitlines()
    selected = [(i+1, lines[i].split(';')[0].strip()) for i in range(lines.index('playSample:')+1, lines.index('disableAudio:'))]
    selected = [(i, t) for i, t in selected if t]
    symbols = {'sampleparametertable': 0x1b6ca, 'audiochannelmasks': 0xf350,
               'audiodmaenableguard': 0xf3ee, '_custom': 0xdff000,
               'intena': 0x9a, 'dmacon': 0x96, 'intreq': 0x9c, 'intb_setclr': 15}
    def normalize(t, table=None):
        t = canonical(t, table).replace('d3-d4','d3/d4').replace('a0-a1','a0/a1')
        t = re.sub(r'^(lea|bset)\.[bl] ', r'\1 ', t)
        t = re.sub(r'(?<!\w)(\d+)\+(\d+)(?!\w)', lambda m: str(int(m[1])+int(m[2])), t)
        return t
    start, end = 0xf362, 0xf3ee
    code = payload[start-BASE:end-BASE]
    decoded = list(Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000).disasm(code, start))
    require(sum(i.size for i in decoded) == len(code) and len(decoded) == len(selected), 'Playback instruction boundary mismatch')
    instructions = []
    for ins, (line, original_source) in zip(decoded, selected):
        actual = ins.mnemonic + ' ' + ins.op_str
        require(normalize(actual) == normalize(original_source, symbols), f'{ins.address:#x}: {original_source} != {actual}')
        instructions.append({'address': hex(ins.address), 'bytes': ins.bytes.hex(), 'original': actual,
                             'source_line': line, 'source': original_source})
    manifest = json.loads((args.capture / 'manifest.json').read_text())
    bank = next(b for b in manifest['banks'] if b['tag'] == 'CRAM')
    ram = (args.capture / bank['file']).read_bytes()
    require(int(bank['base'],0) == 0 and len(ram) == 0x80000 and sha(ram) == bank['sha256'], 'Capture mapping/identity changed')
    require(sha(Path(manifest['source_path']).read_bytes()) == manifest['source_sha256'], 'Savestate changed')
    require(ram[start:end] == code, 'Capture playback code differs')
    routine = {'name': 'playSample', 'runtime_address': hex(start), 'end_exclusive': hex(end),
               'payload_offset': hex(start-BASE), 'disk_offset': hex(DISK_OFFSET+start-BASE),
               'size': len(code), 'sha256': sha(code), 'instruction_count': len(decoded),
               'operand_wildcards': 0, 'source_differences': [], 'instructions': instructions}
    report = {'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA,
              'reference_source_sha256': SOURCE_SHA, 'routines': [routine], 'tables': [],
              'mutable_inputs': ['Eight sample parameter records at0x1b6ca; addresses/lengths are initialized at runtime.',
                                 'Channel masks at0xf352+4i are explicit inputs; channels are restricted to0..3.'],
              'hardware_effects': 'Ten ordered16-bit Paula writes, splitting the32-bit sample pointer high word first.',
              'limitations': ['Synchronous RAM/register-write contract; no audio waveform or interrupt-interleaving equivalence.']}
    out = ROOT / 'analysis/audio-playback'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'reference-comparison.json').write_text(json.dumps(report, indent=2)+'\n')
    (out / 'original-disassembly.asm').write_text('\n'.join(f'{i.address:06x}: {i.mnemonic} {i.op_str}' for i in decoded)+'\n')
    print(f'Audio playback: {len(decoded)} original instructions; all source operands match.')


if __name__ == '__main__':
    main()
