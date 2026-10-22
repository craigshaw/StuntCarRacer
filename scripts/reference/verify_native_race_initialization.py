"""Verify fresh native game/player resets against original MC68000 instructions.

The only runtime input is the plaintext payload from the pinned local ADF.
Captures and ROMs are neither read nor used to generate expected state.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

ROOT = Path(__file__).resolve().parents[2]
BASE, DISK_OFFSET, PAYLOAD_SIZE = 0xe700, 0xdc00, 408540
ADF_SHA = '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA = '508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adf', type=Path, default=ROOT/'Stunt Car Racer (MicroStyle).adf')
    parser.add_argument('--payload', type=Path, default=ROOT/'analysis/reference/vesuri/offline-extracted-payload.bin')
    parser.add_argument('--test-executable', type=Path, default=ROOT/'build/windows-oracle/Release/native_race_initialization_tests.exe')
    parser.add_argument('--output', type=Path, default=ROOT/'analysis/native-race-init')
    args = parser.parse_args()
    adf, payload = args.adf.read_bytes(), args.payload.read_bytes()
    if sha(adf) != ADF_SHA or sha(payload) != PAYLOAD_SHA or payload != adf[DISK_OFFSET:DISK_OFFSET+PAYLOAD_SIZE]:
        raise RuntimeError('Original ADF/payload identity mismatch')
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    routines = []
    listing = []
    for name, start, end in [
        ('successfulLoadImmediateAndVector', 0x5c960, 0x5c96e),
        ('successfulLoadCleanupCall', 0x5ceaa, 0x5ceb4),
        ('floppyCleanup', 0x5ceb4, 0x5cee2),
        ('initializeCIA', 0xee8a, 0xeec8),
        ('validateSaveDataChecksum', 0x5cefa, 0x5cf2c),
        ('initializeGameData', 0x5a9b6, 0x5aa38),
        ('initializeGameTables', 0x5aa38, 0x5aa84),
        ('loadPlayerConfiguration', 0x5d726, 0x5d87e),
        ('clearGameDataSlot', 0x5df0a, 0x5df2e),
        ('initializeRandomSeeds', 0x636a2, 0x636b8),
        ('loadPaletteColors', 0x69e9a, 0x69eb4),
    ]:
        code = payload[start-BASE:end-BASE]
        decoded = list(decoder.disasm(code, start))
        if sum(instruction.size for instruction in decoded) != len(code):
            raise RuntimeError(f'{name}: incomplete MC68000 decoding')
        listing.extend(f'{instruction.address:06x}: {instruction.mnemonic} {instruction.op_str}' for instruction in decoded)
        routines.append({'name': name, 'runtime_address': hex(start), 'end_exclusive': hex(end),
                         'payload_offset': hex(start-BASE), 'disk_offset': hex(DISK_OFFSET+start-BASE),
                         'size': len(code), 'sha256': sha(code), 'instruction_count': len(decoded)})
    result = subprocess.run([str(args.test_executable.resolve()), str(args.payload.resolve())],
                            check=True, capture_output=True, text=True)
    tests = json.loads(result.stdout)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/'original-instructions.txt').write_text('\n'.join(listing)+'\n', encoding='utf-8')
    report = {'adf_sha256': sha(adf), 'payload_sha256': sha(payload),
              'payload_disk_offset': hex(DISK_OFFSET), 'payload_load_address': hex(BASE),
              'captured_state_used': False, 'routines': routines, 'differential_tests': tests,
              'test_executable_sha256': sha(args.test_executable.read_bytes()),
              'sources': {str(path.relative_to(ROOT)): sha(path.read_bytes()) for path in [
                  ROOT/'app/native_race_initialization.cpp', ROOT/'app/native_race_initialization.h',
                  ROOT/'tests/native_race_initialization_tests.cpp', Path(__file__).resolve()]},
              'limits': ['Native full-race closure reuses separately verified restart and gameplay stages.',
                         'Unused Practice opponent look-ahead geometry is omitted; its random advance is retained.',
                         'CIA and custom-chip writes are supplied by native backends and not compared here.',
                         'Tests compare complete game RAM for the three reset routines, not full GUI startup.']}
    (args.output/'check-results.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'passed': True, **tests, 'report': str(args.output/'check-results.json')}))


if __name__ == '__main__':
    main()
