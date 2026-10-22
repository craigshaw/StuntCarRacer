"""Guard the replay wrapper's claims against malformed or incomplete evidence."""
import csv
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'tests/support'))
from test_paths import temporary_directory
from check_driving_replay import (REPLAY_STAGES, TRACE_FIELDS, read_inputs,
                                  verify_native_trace, verify_replay_result,
                                  verify_trace)


def result(full=False, restarts=0):
    ticks = 3
    stages = {name: ticks for name in REPLAY_STAGES}
    if full:
        stages['outer'] = ticks
        if restarts:
            stages['restart'] = restarts
    projections = {name: [100, 101] for name in stages}
    return {'ticks': ticks, 'mismatches': 0,
            'compared_bytes': sum(stages[name] * len(value) for name, value in projections.items()) + 2048 * ticks,
            'original_instructions': 300, 'native_state_reseeded': False,
            'original_code_patched_by_harness': False, 'registers_reset_between_ticks': not full,
            'ordered_sample_audio_writes_compared': True, 'audio_register_writes': 0,
            'whole_ram_compared': False, 'interrupts_emulated': False,
            'damage_occupancy_compared': True, 'per_tick_stages_complete': True,
            'full_loop': full, 'restarts': restarts, 'frame_wait_vblanks': 12 if full else 0,
            'interval': {'entry': '0x5d47e', 'boundary_before': 'next-0x5d47e-or-exit' if full else '0x5d4ba'},
            'stages': stages, 'projection_byte_addresses': projections}


class ReplayResultTests(unittest.TestCase):
    def test_legacy_contract(self):
        verify_replay_result(result(), 3)

    def test_full_loop_requires_persistent_cpu_context(self):
        checks = result(True)
        verify_replay_result(checks, 3, True)
        checks['registers_reset_between_ticks'] = True
        with self.assertRaisesRegex(RuntimeError, 'registers_reset_between_ticks'):
            verify_replay_result(checks, 3, True)

    def test_restart_projection_is_weighted_by_actual_restart_count(self):
        checks = result(True, 1)
        verify_replay_result(checks, 3, True)
        checks['compared_bytes'] += 4  # Incorrectly counting restart once per tick.
        with self.assertRaisesRegex(RuntimeError, 'comparison count'):
            verify_replay_result(checks, 3, True)

    def test_missing_outer_comparison_is_rejected(self):
        checks = result(True)
        del checks['stages']['outer']
        with self.assertRaisesRegex(RuntimeError, 'stage map'):
            verify_replay_result(checks, 3, True)

    def test_restart_count_requires_its_projection(self):
        checks = result(True, 1)
        del checks['projection_byte_addresses']['restart']
        with self.assertRaisesRegex(RuntimeError, 'every stage'):
            verify_replay_result(checks, 3, True)

    def test_full_and_legacy_intervals_cannot_be_mixed(self):
        with self.assertRaises(RuntimeError):
            verify_replay_result(result(True), 3)


class ReplayInputAndTraceTests(unittest.TestCase):
    def setUp(self):
        self.directory = temporary_directory('replay-wrapper-tests')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def write(self, name, contents):
        path = self.root / name
        path.write_text(contents, encoding='ascii', newline='')
        return path

    def trace(self, name, instructions=100):
        path = self.root / name
        rows = [dict.fromkeys(TRACE_FIELDS, 0) for _ in range(3)]
        for tick, row in enumerate(rows):
            row.update(tick=tick, original_instructions=instructions)
        rows[-1].update(quit=1, outer_action=3)
        with path.open('w', encoding='ascii', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=TRACE_FIELDS)
            writer.writeheader()
            writer.writerows(rows)
        return path

    def test_legacy_and_quit_input_schemas(self):
        old = self.write('old.csv', 'tick,controls\n0,1\n1,0\n')
        self.assertEqual(read_inputs(old)['quit_ticks'], [])
        extended = self.write('extended.csv', 'tick,controls,quit\r\n0,1,0\r\n1,0,1\r\n')
        self.assertEqual(read_inputs(extended)['quit_ticks'], [1])

    def test_invalid_quit_masks_and_state_fields_are_rejected(self):
        for contents in ('tick,controls,quit\n0,1,2\n',
                         'tick,controls,quit\n0,1,-1\n',
                         'tick,controls,segment\n0,1,5\n'):
            with self.assertRaises(ValueError):
                read_inputs(self.write('invalid.csv', contents))

    def test_recorded_original_throttle_is_accepted(self):
        path = self.write('original.csv', 'tick,controls,quit,hold_acceleration\r\n0,1,0,0\r\n1,0,1,0\r\n')
        checked = read_inputs(path)
        self.assertEqual(checked['ticks'], 2)
        self.assertEqual(checked['quit_ticks'], [1])
        self.assertEqual(checked['columns'], ['tick', 'controls', 'quit', 'hold_acceleration'])

    def test_modern_or_mixed_recordings_cannot_claim_original_equivalence(self):
        for timeline in ('0,1,0,1\n1,0,0,1\n', '0,1,0,0\n1,0,0,1\n'):
            with self.assertRaisesRegex(ValueError, 'Modern held-acceleration'):
                read_inputs(self.write('modern.csv', 'tick,controls,quit,hold_acceleration\n' + timeline))

    def test_invalid_throttle_modes_are_rejected(self):
        for row in ('0,1,0,2', '0,1,0,-1', '0,1,0,', '0,1,0,0,0'):
            with self.assertRaises(ValueError):
                read_inputs(self.write('invalid.csv', 'tick,controls,quit,hold_acceleration\n' + row + '\n'))

    def test_trace_quit_must_match_timeline(self):
        inputs = self.write('inputs.csv', 'tick,controls,quit\n0,0,0\n1,0,0\n2,0,1\n')
        trace = self.trace('trace.csv')
        verify_trace(trace, inputs, 3)
        wrong = self.write('wrong.csv', 'tick,controls\n0,0\n1,0\n2,0\n')
        with self.assertRaisesRegex(RuntimeError, 'quit input mismatch'):
            verify_trace(trace, wrong, 3)

    def test_native_comparison_covers_lap_and_outer_fields(self):
        inputs = self.write('inputs.csv', 'tick,controls,quit\n0,0,0\n1,0,0\n2,0,1\n')
        original = self.trace('original.csv')
        native = self.trace('native.csv', 0)
        checks = {'ticks': 3, 'cpu_emulation': False, 'comparison_performed': False}
        verify_native_trace(native, original, inputs, 3, checks)
        with native.open(encoding='ascii', newline='') as stream:
            rows = list(csv.DictReader(stream))
        rows[1]['lap_counter'] = '1'
        with native.open('w', encoding='ascii', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=TRACE_FIELDS)
            writer.writeheader()
            writer.writerows(rows)
        with self.assertRaisesRegex(RuntimeError, 'lap_counter'):
            verify_native_trace(native, original, inputs, 3, checks)


if __name__ == '__main__':
    unittest.main()
