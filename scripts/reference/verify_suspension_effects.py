"""Verify complete countdown/collision/impact tails against original bytes and pinned source."""
import argparse
import json
import re
from pathlib import Path
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
from verify_transform_reference import (ROOT, REFERENCE, BASE, DISK_OFFSET, PAYLOAD_SIZE,
                                        COMMIT, ADF_SHA, PAYLOAD_SHA, SOURCE_SHA, sha, require, canonical)
OUT = ROOT / 'analysis/suspension-effects'
SPECS = [
    ('suspension effects tail', 0x62042, 0x620b6, '.targetPitchRateDone:', 'updateCarOrientation:'),
    ('handleRaceStartCountdown', 0x5b32e, 0x5b472, 'handleRaceStartCountdown:', 'adjustCarHeightToTrack:'),
    ('adjustCarHeightToTrack', 0x5b472, 0x5b4a8, 'adjustCarHeightToTrack:', 'updateRaceStartRoll:'),
    ('updateRaceStartRoll', 0x5b4a8, 0x5b506, 'updateRaceStartRoll:', 'applyCameraLateralOffset:'),
    ('checkMultiplayerTrackSwap', 0x58758, 0x5879e, 'checkMultiplayerTrackSwap:', 'displayRacePositions:'),
    ('generateRandomNumber', 0x62574, 0x625a2, 'generateRandomNumber:', 'displayMessage:'),
    ('setMessageParameters', 0x5e4ec, 0x5e502, 'setMessageParameters:', 'displayGameMessage:'),
    ('applyOpponentCollisionForces', 0x63e2e, 0x63ede, 'applyOpponentCollisionForces:', 'calculateOpponentWheelForces:'),
]
LOCALS = {
    '.cooldownTimerOk': 0x62068, '.impactSoundDone': 0x620b4, '.minimumVolumeOk': 0x62084, '.maximumVolumeOk': 0x62092,
    '.done': 0x5b38e, '.checkInitialRaise': 0x5b36e, '.setStartRotation': 0x5b358, '.decrementTimer': 0x5b366,
    '.timerOk': 0x5b36c, '.checkFullRaise': 0x5b390, '.holdAndCountdown': 0x5b402, '.messageIndexOk': 0x5b3d0,
    '.raceStartTimerOk': 0x5b3de, '.crashRecoveryTimerOk': 0x5b3f6, '.checkTransition': 0x5b430,
    '.checkRaceActive': 0x5b446, '.startRace': 0x5b450, '.raceIsActive': 0x5b470,
}


def normal(text, symbols=None):
    for label, address in sorted(LOCALS.items(), key=lambda item: -len(item[0])):
        text = re.sub(re.escape(label) + r'\b', str(address), text, flags=re.I)
    value = canonical(text, symbols)
    return re.sub(r'^(b\w+)\.[ws] ', r'\1 ', value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, action='append')
    args = parser.parse_args()
    adf = (ROOT / 'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf) == ADF_SHA, 'ADF changed')
    payload = adf[DISK_OFFSET:DISK_OFFSET + PAYLOAD_SIZE]
    require(sha(payload) == PAYLOAD_SHA, 'Payload changed')
    require((REFERENCE / 'offline-extracted-payload.bin').read_bytes() == payload, 'Retained payload differs')
    source = (REFERENCE / 'StuntCarRacer.s').read_bytes()
    provenance = json.loads((REFERENCE / 'provenance.json').read_text())
    info = next(f for f in provenance['files'] if f['path'] == 'StuntCarRacer.s')
    require(provenance['commit'] == COMMIT and sha(source) == SOURCE_SHA == info['sha256'], 'Reference changed')
    lines = source.decode('utf-8').splitlines()
    symbols = {name.lower(): start for name, start, _, _, _ in SPECS}
    symbols['playsample'] = 0xf362
    for line in lines:
        match = re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)', line, re.I)
        if match: symbols[match[1].lower()] = BASE + int(match[2], 16)
        match = re.match(r'^(lbC[0-9a-f]+):', line, re.I)
        if match: symbols[match[1].lower()] = BASE + int(match[1][3:], 16)
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    routines, differences = [], []
    for name, start, end, label, next_label in SPECS:
        code = payload[start - BASE:end - BASE]
        decoded = list(decoder.disasm(code, start))
        require(sum(i.size for i in decoded) == len(code), 'Incomplete decode: ' + name)
        first = lines.index(label); last = lines.index(next_label, first + 1)
        selected = []
        for n in range(first + 1, last):
            raw = lines[n]; text = raw.split(';')[0].strip(); note = None
            if name == 'handleRaceStartCountdown' and n < lines.index('.checkInitialRaise:', first):
                if text.lower() in ('tst.b\tframethrottleflag', 'bmi.s\t.timerok'):
                    differences.append({'routine': name, 'source_line': n + 1, 'source': text,
                                        'kind': 'added enhanced throttle instruction', 'original': None})
                    continue
            if name == 'handleRaceStartCountdown' and raw.startswith(';.done:'):
                text = 'RTS'; note = 'RTS removed from enhanced source'
            if name == 'updateRaceStartRoll' and re.match(r'^;\s*BEQ\s+lbC04CDCA', raw):
                text = raw[1:].split(';')[0].strip(); note = 'BEQ removed from enhanced source'
            text = re.sub(r'^[.\w]+:\s*', '', text)
            if text: selected.append((n + 1, text, note))
        require(len(selected) == len(decoded), f'{name}: source count {len(selected)} != original {len(decoded)}')
        instructions = []
        for (line, source_text, note), instruction in zip(selected, decoded):
            text = source_text
            # Narrow amendments enumerate known source changes. Every other
            # mnemonic and resolved operand must match the original exactly.
            changes = {
                0x5b346: ('#$28', '#$2C'), 0x5b354: ('#$D8', '#$D4'),
                0x5b3ac: ('.done', str(0x5b400)),
                0x5b47a: ('trackSurfaceHeight', '$1BC60'),
                0x5b4a8: ('#$0C', '#$10'), 0x5b4b8: ('#$F4', '#$F0'),
                0x5b4be: ('#TIMESTEP_FACTOR', '#$EE'),
                0x5b4ca: ('offsetFromRoadCenter', '$1BC5C'),
                0x620ac: ('#$05*FRAMERATE_MULTIPLIER', '#$05'),
                0x63ed4: ('#$05*FRAMERATE_MULTIPLIER', '#$05'),
            }
            if instruction.address in changes:
                old, new = changes[instruction.address]
                require(old in text, f'Expected known change at{instruction.address:#x}')
                text = text.replace(old, new); note = 'explicit enhanced-source operand difference'
            actual = instruction.mnemonic + ' ' + instruction.op_str
            require(normal(text, symbols) == normal(actual), f'{name} {instruction.address:#x}: {text!r} != {actual!r}')
            record = {'address': hex(instruction.address), 'bytes': instruction.bytes.hex(), 'original': actual,
                      'source_line': line, 'source': source_text}
            if note:
                record['difference'] = note
                differences.append({'routine': name, 'address': hex(instruction.address), 'source_line': line,
                                    'source': source_text, 'original': actual, 'kind': note})
            instructions.append(record)
        routines.append({'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                         'payload_offset': hex(start - BASE), 'disk_offset': hex(DISK_OFFSET + start - BASE),
                         'size': len(code), 'sha256': sha(code), 'instruction_count': len(decoded),
                         'remaining_source_instructions_and_operands_match': True, 'operand_wildcards': 0,
                         'instructions': instructions})
    captures = []
    for directory in args.capture or [ROOT / 'analysis/captures/race']:
        manifest = json.loads((directory / 'manifest.json').read_text())
        bank = next(b for b in manifest['banks'] if b['tag'] == 'CRAM'); chip = (directory / bank['file']).read_bytes()
        require(bank['base'] == '0x0' and len(chip) == bank['size'] == 0x80000, 'Invalid capture mapping')
        require(sha(chip) == bank['sha256'], 'Capture RAM changed')
        require(sha(Path(manifest['source_path']).read_bytes()) == manifest['source_sha256'], 'Capture USS changed')
        for routine in routines:
            start, end = int(routine['runtime_address'], 16), int(routine['end_exclusive'], 16)
            require(chip[start:end] == payload[start - BASE:end - BASE], 'Capture code changed: ' + routine['name'])
        captures.append({'directory': str(directory.resolve()), 'ram_sha256': sha(chip),
                         'savestate_sha256': manifest['source_sha256'], 'all_code_equal_disk': True})
    report = {'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA, 'reference_commit': COMMIT,
              'reference_source_sha256': SOURCE_SHA, 'reference_url': info['url'], 'routines': routines,
              'tables': [], 'source_differences': differences, 'captures': captures,
              'external_recovered_callee': {'name': 'playSample', 'address': '0xf362',
                  'requests': [2, 3], 'contract': 'Actual original code executes in oracle; native requestSample reproduces RAM and ordered Paula writes.'},
              'entry_precondition': 'D2 bits8..15 zero for updateRaceStartRoll byte timestep write followed by MULS.W.',
              'behavior': [
                  'All countdown branches and multiplayer track-side toggling are implemented, including message/RNG state.',
                  'Original height lookup reads0x1bc60 and lateral offset reads0x1bc5c, each two bytes after enhanced-source fields.',
                  'Initial roll±44, hold target±16 and timestep238 are original values; high timer branch ignores frame throttle.',
                  'Original collision/impact cooldown resets are5.',
                  'Opponent speed subtraction wraps as a word before its sign clamp.',
                  'Impact volume uses unsigned byte minimum7, byte shift-left2 with wrap, then maximum64.',
                  'Saved yaw word is written before collision changes yaw spring.',
                  'Cooldown bytes and random seed storage adjoin code but remain mutable data, not executed instructions.'],
              'limitations': ['Audio playback command/state parity does not itself establish PCM mixing or real-time output fidelity.',
                              'No interrupt interleaving, cycle timing or arbitrary-register equivalence is claimed.']}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'reference-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    (OUT / 'original-disassembly.asm').write_text('\n'.join(
        f"{i['address']} {i['bytes']:24s} {i['original']}" for r in routines for i in r['instructions']) + '\n')
    print(json.dumps({'report': str(OUT / 'reference-comparison.json'),
                      'instruction_count': sum(r['instruction_count'] for r in routines),
                      'source_differences': len(differences)}, indent=2))


if __name__ == '__main__': main()
