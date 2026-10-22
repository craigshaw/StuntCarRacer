"""Verify capture-free track loading against the original disk instructions.

All fixture bytes come from the locally supplied ADF. Output disassembly and
provenance stay under analysis/native-track-init; compiler output uses build/.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from native_build import ORACLE_BUILD, build_sources, configure_command

ROOT = Path(__file__).resolve().parents[1]
ADF_SHA = '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA = '508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
RANGES = [
    ('track byte reader/directional repeat', 0x5ae00, 0x5ae46),
    ('processTrackDataBuffer', 0x5ae46, 0x5b2c4),
    ('track height interpolation reader', 0x5b2c8, 0x5b32e),
    ('initializeSegmentFlags', 0x5d85a, 0x5d87e),
    ('loadTrackSegmentConfiguration', 0x5fe56, 0x5ff94),
    ('initializeTrackDifficultyParameters', 0x63a8e, 0x63b08),
    ('generateRandomNumber', 0x62574, 0x625a2),
    ('initializeLookupTables', 0x64304, 0x64338),
    ('setupTrackGeometryForFrame2', 0x65756, 0x65a18),
    ('generateTrackSegmentData', 0x65bec, 0x65dea),
    ('initializeTrackSegmentBuffers', 0x65dea, 0x65e30),
    ('next/previous segment', 0x5c51a, 0x5c554),
    ('interpolateOpponentTrackPositionFromData', 0x5c6c4, 0x5c808),
]


def sha(blob):
    return hashlib.sha256(blob).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'analysis/native-track-init')
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    adf_path = ROOT/'Stunt Car Racer (MicroStyle).adf'
    adf = adf_path.read_bytes()
    if sha(adf) != ADF_SHA:
        raise RuntimeError('ADF source identity changed')
    payload = adf[0xdc00:0xdc00+408540]
    if sha(payload) != PAYLOAD_SHA:
        raise RuntimeError('Extracted payload identity changed')
    # This retained binary is derived only from the verified ADF, never RAM.
    payload_path = output/'source-payload.bin'
    payload_path.write_bytes(payload)
    build = ORACLE_BUILD
    executable = build/'Release/track_initialization_oracle.exe'
    compiled = build_sources() | set((ROOT/'src').glob('*.h')) | set((ROOT/'src').glob('*.cpp'))
    compiled.update((ROOT/'tests').glob('*fixture.h'))
    compiled.update({Path(__file__).resolve(), ROOT/'tests/track_initialization_oracle.cpp'})
    source_hashes = {str(path.relative_to(ROOT)): sha(path.read_bytes()) for path in sorted(compiled)}
    receipt_path = output/'build-receipt.json'
    if args.skip_build:
        receipt = json.loads(receipt_path.read_text())
        if (receipt['sources'] != source_hashes or not executable.is_file()
                or receipt['executable_sha256'] != sha(executable.read_bytes())):
            raise RuntimeError('Track oracle build is stale; rerun without --skip-build')
    if not args.skip_build:
        subprocess.run(configure_command(oracle=True), cwd=ROOT, check=True)
        subprocess.run(['cmake', '--build', str(build), '--config', 'Release',
            '--target', 'track_initialization_oracle'], cwd=ROOT, check=True)
        if any(sha(path.read_bytes()) != source_hashes[str(path.relative_to(ROOT))] for path in compiled):
            raise RuntimeError('Track oracle sources changed during compilation')
        receipt_path.write_text(json.dumps({'sources': source_hashes,
            'executable_sha256': sha(executable.read_bytes())}, indent=2)+'\n')
    result = subprocess.run([str(executable), str(payload_path)], check=True,
        text=True, capture_output=True)
    (output/'oracle-output.txt').write_text(result.stdout, encoding='utf-8')
    metrics = json.loads(result.stdout.splitlines()[-1])
    if metrics['cases'] != 24 or metrics['mismatches'] != 0:
        raise RuntimeError('Unexpected track oracle coverage')
    from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    spans, listing = [], []
    for name, first, end in RANGES:
        code = payload[first-0xe700:end-0xe700]
        decoded = list(decoder.disasm(code, first))
        if sum(ins.size for ins in decoded) != len(code):
            raise RuntimeError(name+' has undecoded bytes')
        spans.append({'name': name, 'ram_first': hex(first), 'ram_end_exclusive': hex(end),
            'payload_offset': hex(first-0xe700), 'adf_offset': hex(0xdc00+first-0xe700),
            'sha256': sha(code), 'bytes': len(code), 'instructions': len(decoded)})
        listing.extend(f'{ins.address:06x}: {ins.mnemonic} {ins.op_str}' for ins in decoded)
    (output/'original-disassembly.asm').write_text('\n'.join(listing)+'\n', encoding='utf-8')
    files = ['src/track_initialization.h', 'src/track_initialization.cpp',
        'src/track_sampling.h', 'src/track_sampling.cpp',
        'tests/track_initialization_oracle.cpp', 'scripts/check_native_track_initialization.py']
    report = {'passed': True, 'source_adf_sha256': ADF_SHA, 'payload_sha256': PAYLOAD_SHA,
        'payload_adf_offset': '0xdc00', 'payload_length': len(payload), 'payload_ram_base': '0xe700',
        'oracle': metrics, 'original_code': spans,
        'source_sha256': {path: sha((ROOT/path).read_bytes()) for path in files},
        'build_sources': {str(path.relative_to(ROOT)): sha(path.read_bytes()) for path in sorted(build_sources())},
        'oracle_executable_sha256': sha(executable.read_bytes()),
        'inputs': 'Fresh zeroed512KiB memory plus original disk payload; no RAM captures.',
        'comparison_ranges': ['0x1bdd0..0x1bf6c dense heights/processed flags/lower vertex initialisation',
            '0x1c280..0x1c380 cell grid',
            '0x1c4c0..0x1ca36 topology/interpolation/metadata/AI visibility',
            '0x625a2..0x625a7 RNG', '0x63ce0..0x63ce3 difficulty',
            '0x65e70..0x65ed4 render depths', '0x7aa1a..0x80000 complete poisoned render cache',
            '0x66102..0x66106 cache end pointer'],
        'coverage': 'All8original tracks, fresh primary/alternate player difficulty and retained subsequent-heat difficulty.',
        'limitations': ['This stage generates track data; it does not initialise player/opponent race state.',
            'The Draw Bridge track is generated at its unanimated payload configuration. Its moving bridge simulation is separate.',
            'Scratch registers and every intermediate scratch RAM byte are outside the persistent output comparison.']}
    (output/'check-results.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(metrics))


if __name__ == '__main__':
    main()
