"""Validate the stage-1 crossing method; retain native-only, identified evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from native_build import ROOT, NATIVE_BUILD
from check_native_ghost import identity, sources


def source_records():
    return sorted([*sources(), identity(Path(__file__))], key=lambda item: item['path'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--configuration', choices=('Release', 'Debug'), default='Release')
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    preset = 'windows-debug' if args.configuration == 'Debug' else 'windows-release'
    default_output = ROOT / 'analysis/timing-estimation'
    if args.configuration == 'Debug':
        default_output /= 'debug'
    output = (args.output or default_output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    pack = ROOT / 'analysis/assets/stunt-car-racer.scr-assets'
    alternate = ROOT / 'analysis/assets/stunt-car-racer-woodpecker.scr-assets'
    capture = ROOT / 'analysis/captures/race/chip-000000.bin'
    controls = ROOT / 'tests/replays/laps/authored-race-1463.csv'
    provenance = controls.with_suffix('.provenance.json')
    payload = ROOT / 'analysis/reference/vesuri/offline-extracted-payload.bin'
    paths = [pack, capture, controls, provenance, payload]
    if alternate.is_file():
        paths.append(alternate)
    fixtures = [identity(path) for path in paths]
    authored = json.loads(provenance.read_text())
    # Git may check out CSV with CRLF; provenance hashes its canonical LF bytes.
    canonical = controls.read_bytes().replace(b'\r\n', b'\n')
    if hashlib.sha256(canonical).hexdigest() != authored['input_sha256']:
        raise RuntimeError('Controls differ from authored replay provenance')
    if identity(capture)['sha256'] != authored['starting_capture']['chip_ram_sha256']:
        raise RuntimeError('Capture differs from authored replay provenance')
    before = source_records()
    targets = ('native_timing_proof', 'crossing_estimation_tests')
    executables = {target: NATIVE_BUILD / args.configuration / (target + '.exe') for target in targets}
    receipt = output / 'build-receipt.json'
    if args.skip_build:
        previous = json.loads(receipt.read_text())
        if previous['sources'] != before or previous['executables'] != {name: identity(path) for name, path in executables.items()}:
            raise RuntimeError('Timing proof is stale; rerun without --skip-build')
    else:
        commands = [
            ['cmake', '--preset', preset],
            ['cmake', '--build', '--preset', preset, '--target', *targets, '--parallel', '1', '--', '/nr:false'],
        ]
        with (output / 'build.log').open('w') as log:
            for command in commands:
                subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        receipt.write_text(json.dumps({'sources': before, 'executables': {name: identity(path) for name, path in executables.items()}}, indent=2) + '\n')
    with (output / 'ctest.log').open('w') as log:
        subprocess.run(['ctest', '--preset', preset, '-V', '-R', '^(crossing_estimation_tests|native_timing_proof)$'],
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    runs = {}
    packs = [('primary', pack, True)]
    if alternate.is_file():
        packs.append(('alternate', alternate, False))
    for label, asset_pack, replay in packs:
        directory = output / label
        command = [executables['native_timing_proof'], asset_pack, directory]
        if replay:
            command += [capture, controls]
        run = subprocess.run(list(map(str, command)), cwd=ROOT, capture_output=True, text=True)
        (output / (label + '.stdout.txt')).write_text(run.stdout)
        (output / (label + '.stderr.txt')).write_text(run.stderr)
        run.check_returncode()
        result = json.loads((directory / 'results.json').read_text())
        if (result['status'] != 'passed' or result['geometry_contexts'] != 16
                or result['synthetic_native_brackets'] != 24192
                or not result['simulation_memory_identical']
                or result['recovery_observations'] != 2 * result['recoveries']
                or (replay and result['captured_crossings'] != 4)):
            raise RuntimeError('Crossing proof omitted required geometry, recovery or replay coverage')
        runs[label] = result
    if source_records() != before or [identity(path) for path in paths] != fixtures:
        raise RuntimeError('Timing proof sources or fixtures changed during verification')
    binaries = {name: identity(path) for name, path in executables.items()}
    if binaries != json.loads(receipt.read_text())['executables']:
        raise RuntimeError('Timing proof executable changed during verification')
    contexts = sum(run['geometry_contexts'] for run in runs.values())
    brackets = sum(run['synthetic_native_brackets'] for run in runs.values())
    ticks = sum(run['native_ticks'] for run in runs.values())
    measured = max(run['observed_max_progress_difference_us'] for run in runs.values())
    stress = max(run['synthetic_max_progress_difference_us'] for run in runs.values())
    decision = f'''# Stage 1 crossing method decision

Selected method: fixed-point-world-plane-v1. Configuration: {args.configuration}.

Use the native car reference position to intersect the straight line joining
consecutive ordinary observations with the shared edge of the two timing cells.
Every inspected timing pair has straight type-0 geometry, no reversal, equal
2048-unit progress lengths and the same cardinal direction. Gate validation is
mandatory; other geometry is unsupported by this method.

The recovered progress calculation discards the low 12 world-position bits.
The world-plane method avoids that quantisation and agrees with exact rational
intersections to nearest-microsecond rounding in the mathematical cases.
It preserves native event eligibility and uses checked, bounded integer inputs.

Validated {contexts} track/league/asset contexts, {brackets} constructed native
brackets and {ticks} ordinary native updates. The observed maximum disagreement
with progress interpolation was {measured} us. Deliberately slow constructed
crossings reached {stress} us; these are synthetic placements, not driven laps.
The retained authored replay supplies four boundaries and three complete laps
on one circuit. Fresh drives cover initial crossings and recovery in all tracks
and both leagues. Full simulation memory matched the observer-free native run
after every driven update.

Require an accepted native crossing, a previous observation exactly 120000 us
earlier, adjacent forward timing segments, successful lookups, a monotone signed
distance bracket and an intersection within the finite cell edge. Reject recovery
observations, changed restart identity and duplicate/missing timestamps. Unsupported
or invalid observations remain approximate and ineligible for Enhanced ranking.

Ordinary wheel observations use outer index n times 120000 us. Both internal
recovery observations use (n + 1) times 120000 us, also the timestamp of the next
ordinary observation. Wheels and timing callbacks retain the same car position
and progress. No extra movement duration may be inferred from recovery callbacks.
The 260000 us recovery penalty belongs to the separate stage-2 capture suite.

This is a deterministic linear measurement convention, not a claim of physical
accuracy within 1 ms. This proof executes no original CPU and writes no
simulation state. Production timing capture is verified separately. The capture is a development
fixture only; native play still uses the asset pack without a capture.

Source, fixture and executable SHA-256 identities, per-run summaries and artifact
identities are retained in check-results.json. Raw evidence is in primary/ and,
when available, alternate/; CTest mathematical checks are in ctest.log.
'''
    (output / 'decision.md').write_text(decision)
    artifacts = [identity(path) for label in runs for path in sorted((output / label).glob('*.csv'))]
    artifacts += [identity(output / 'ctest.log'), identity(output / 'decision.md')]
    report = {
        'status': 'passed', 'stage': 1, 'method': 'fixed-point-world-plane-v1',
        'scope': 'native geometry, mathematical brackets, timestamp audit and observer isolation; no production timing integration',
        'configuration': args.configuration, 'original_cpu_executed': False, 'sources': before, 'fixtures': fixtures,
        'executables': binaries, 'runs': runs, 'artifacts': artifacts,
    }
    (output / 'check-results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'method': report['method'], 'report': str(output / 'check-results.json'), 'runs': runs}, indent=2))


if __name__ == '__main__':
    main()
