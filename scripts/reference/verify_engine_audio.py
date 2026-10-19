"""Pin engine sample generation, the original VBlank audio tail and audio IRQs.

This checks complete instructions and operands against the supplied disk,
independently decoded in MC68000 mode. It does not execute the enhanced source.
"""
import argparse
import json
import re
from pathlib import Path
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
from verify_transform_reference import (ROOT, REFERENCE, BASE, DISK_OFFSET,
    PAYLOAD_SIZE, ADF_SHA, PAYLOAD_SHA, SOURCE_SHA, COMMIT, sha, require, canonical)


def generate(sample):
    require(len(sample) == 3172, 'Engine sample extent changed')
    output = bytearray(3200)
    levels = [(0x19de6, 1586)]
    count, source, destination, extent = 3172, 0, 0, 0
    for level in range(1, 8):
        start = destination
        count >>= 1
        for index in range(count):
            output[destination] = sample[source + 2*index] if level == 1 else output[source + 2*index]
            destination += 1
        extent = max(extent, destination)
        destination &= ~1
        levels.append((0x1aa4a + start, (destination-start)//2))
        source = start
    return levels, bytes(output[:extent])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, default=ROOT/'analysis/captures/race')
    parser.add_argument('--output', type=Path, default=ROOT/'analysis/audio-engine')
    args = parser.parse_args()
    adf = (ROOT/'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf) == ADF_SHA, 'ADF identity changed')
    payload = adf[DISK_OFFSET:DISK_OFFSET+PAYLOAD_SIZE]
    require(sha(payload) == PAYLOAD_SHA, 'Payload identity changed')
    require((REFERENCE/'offline-extracted-payload.bin').read_bytes() == payload, 'Retained payload changed')
    source = (REFERENCE/'StuntCarRacer.s').read_bytes()
    require(sha(source) == SOURCE_SHA, 'Pinned source identity changed')
    provenance = json.loads((REFERENCE/'provenance.json').read_text())
    require(provenance['commit'] == COMMIT, 'Pinned revision changed')
    manifest = json.loads((args.capture/'manifest.json').read_text())
    bank = next(item for item in manifest['banks'] if item['tag'] == 'CRAM')
    chip = (args.capture/bank['file']).read_bytes()
    require(int(bank['base'], 0) == 0 and len(chip) == bank['size'] == 0x80000, 'Capture mapping changed')
    require(sha(chip) == bank['sha256'], 'Capture RAM changed')
    require(sha(Path(manifest['source_path']).read_bytes()) == manifest['source_sha256'], 'Capture source changed')
    lines = source.decode().splitlines()
    symbols = {}
    for line in lines:
        match = re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)', line, re.I)
        if match:
            symbols[match[1].lower()] = BASE + int(match[2], 16)
        match = re.match(r'^(lbC[0-9a-f]+):', line, re.I)
        if match:
            symbols[match[1].lower()] = BASE + int(match[1][3:], 16)
    symbols.update({'_custom': 0xdff000, 'aud0': 0xa0, 'ac_per': 6, 'ac_len': 4,
        'dmaconr': 2, 'dmacon': 0x96, 'intena': 0x9a, 'dmaf_aud0': 1,
        'intf_aud0': 0x80, 'audiochannel0period': 0xf472,
        'audiochannelmasks': 0xf350, 'audiodmaenableguard': 0xf3ee,
        'sampleengineparameters': 0xf474, 'sampleenginedata': 0x19de6,
        'downsampledenginedata': 0x1aa4a, 'sampleenginepointer': 0x1b73a,
        'sampleenginesize': 0x1b73e, 'downsampledsampleenginetable': 0xf478,
        'dmaconvaluetoset': 0x69ede})

    def normalize(text, table=None):
        result = canonical(text, table)
        result = re.sub(r'^(b\w+)\.[bws] ', r'\1 ', result)
        result = re.sub(r'^(lea|bclr)\.[bl] ', r'\1 ', result)
        while re.search(r'(?<!\w)(\d+)\+(\d+)(?!\w)', result):
            result = re.sub(r'(?<!\w)(\d+)\+(\d+)(?!\w)', lambda m: str(int(m[1])+int(m[2])), result)
        return result

    vblank_first = next(index for index, text in enumerate(lines)
        if text.strip() == 'MOVE.W\tdmaconValueToSet,_custom+dmacon')
    spans = [('audio interrupts', lines.index('audioChannel0Interrupt:')+1,
              lines.index('playSample:'), 0xf2cc, 0xf350),
             ('generateEngineSamples', lines.index('generateEngineSamples:')+1,
              lines.index('readJoystickState:'), 0xf402, 0xf472),
             ('VBlank audio tail', vblank_first,
              lines.index('adjustCoordinateBoundsFromCenter:'), 0x6171a, 0x617c4)]
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    routines, listing = [], []
    for name, first, last, start, end in spans:
        code = payload[start-BASE:end-BASE]
        decoded = list(decoder.disasm(code, start))
        require(sum(ins.size for ins in decoded) == len(code), name+' undecoded bytes')
        selected, local = [], {}
        for index in range(first, last):
            text = lines[index].split(';')[0].strip()
            if not text:
                continue
            if ':' in text:
                label, text = text.split(':', 1)
                local[label.lower()] = decoded[len(selected)].address if len(selected) < len(decoded) else end
                text = text.strip()
                if not text:
                    continue
            selected.append((index+1, text))
        require(len(selected) == len(decoded), f'{name}: source instruction count differs')
        table = {**symbols, **local}
        instructions = []
        for ins, (number, text) in zip(decoded, selected):
            resolved = text.lower()
            for label, address in sorted(local.items(), key=lambda item: -len(item[0])):
                resolved = resolved.replace(label, str(address))
            actual = ins.mnemonic+' '+ins.op_str
            require(normalize(actual) == normalize(resolved, table), f'{ins.address:#x}: {text} != {actual}')
            instructions.append({'address': hex(ins.address), 'bytes': ins.bytes.hex(),
                'original': actual, 'source_line': number, 'source': text})
            listing.append(f'{ins.address:06x}: {actual}')
        require(chip[start:end] == code, name+' capture differs from payload')
        routines.append({'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
            'payload_offset': hex(start-BASE), 'disk_offset': hex(DISK_OFFSET+start-BASE),
            'size': len(code), 'sha256': sha(code), 'instruction_count': len(decoded),
            'operand_wildcards': 0, 'capture_bytes_equal_disk': True, 'instructions': instructions})
    stored_sample = payload[0x19de6-BASE:0x1aa4a-BASE]
    sample = bytes(value ^ 0x80 for value in stored_sample)
    require(chip[0x19de6:0x1aa4a] == sample, 'Captured engine sample differs from disk XOR0x80 initialization')
    levels, generated = generate(sample)
    require(chip[0x1aa4a:0x1aa4a+len(generated)] == generated, 'Captured generated engine bank differs')
    for index, (address, length) in enumerate(levels):
        stored = chip[0xf474+8*index:0xf47c+8*index]
        require(int.from_bytes(stored[:4], 'big') == address and int.from_bytes(stored[4:], 'big') == length,
            f'Captured level{index} metadata differs')
    report = {'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA,
        'reference_source_sha256': SOURCE_SHA, 'reference_commit': COMMIT,
        'capture_ram_sha256': sha(chip), 'routines': routines,
        'engine_sample': {'runtime_address': '0x19de6', 'payload_offset': hex(0x19de6-BASE),
            'disk_offset': hex(DISK_OFFSET+0x19de6-BASE), 'size': len(sample),
            'disk_sha256': sha(stored_sample), 'initialized_sha256': sha(sample),
            'encoding': 'Disk unsigned8bit PCM; original initialization XOR0x80 converts to signed8bit mono PCM.'},
        'generated_bank': {'runtime_address': '0x1aa4a', 'size': len(generated), 'sha256': sha(generated),
            'levels': [{'index': index, 'address': hex(address), 'word_length': length,
                'byte_length': length*2, 'selected_by_vblank': index <= 6}
                for index, (address, length) in enumerate(levels)]},
        'original_symbol_bindings': [{'symbol': name, 'original_address': hex(symbols[name])}
            for name in ['audiochannel0period', 'sampleengineparameters', 'dmaconvaluetoset']],
        'limitations': ['The VBlank contract begins after countdown and wheel-graphics work at0x6171a.',
            'No original PCM output, analog-filter response or cycle-exact interrupt schedule is claimed.',
            'Generation creates8records; VBlank pitch selection uses only0..6.']}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/'reference-comparison.json').write_text(json.dumps(report, indent=2)+'\n')
    (args.output/'original-disassembly.asm').write_text('\n'.join(listing)+'\n')
    print(f'Engine audio: {sum(item["instruction_count"] for item in routines)} exact source instructions; '
          f'{len(sample)} source bytes, {len(generated)} generated bytes and8level records verified.')


if __name__ == '__main__':
    main()
