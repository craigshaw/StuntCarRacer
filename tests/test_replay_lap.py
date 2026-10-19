"""Adversarial evidence checks, using synthetic traces rather than game laps."""
import csv
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from replay_lap import LAP_TRACE_FIELDS, expand_controls, initial_lap_state, validate_laps


def test_directory():
    parent = Path(__file__).resolve().parents[1] / 'analysis/lap-validation-tests'
    parent.mkdir(parents=True, exist_ok=True)
    return tempfile.TemporaryDirectory(dir=parent)


class LapEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.directory = test_directory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.initial = {'segment': 0, 'lap_counter': 0, 'crossing_armed': 0,
                        'race_complete': 0, 'lap_line_segment': 1,
                        'finish_line_segment': 3, 'track_segment_count': 4,
                        'maximum_laps': 4}

    def trace(self, steps):
        path = self.root / 'trace.csv'
        with path.open('w', encoding='ascii', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=LAP_TRACE_FIELDS)
            writer.writeheader()
            for tick, update in enumerate(steps):
                row = dict.fromkeys(LAP_TRACE_FIELDS, 0)
                row.update(tick=tick, **update)
                writer.writerow(row)
        return path

    def full_cycle(self, counter=1):
        return [{'segment': 1, 'lap_counter': counter, 'crossing_armed': 128},
                {'segment': 2, 'lap_counter': counter, 'crossing_armed': 128},
                {'segment': 3, 'lap_counter': counter},
                {'segment': 0, 'lap_counter': counter},
                {'segment': 1, 'lap_counter': counter + 1, 'crossing_armed': 128}]

    def test_two_crossings_and_full_cycle_are_certified(self):
        result = validate_laps(self.trace(self.full_cycle()), self.initial, True)
        self.assertEqual(result['complete_laps'], 1)
        self.assertEqual(result['laps'][0]['start_tick'], 0)
        self.assertEqual(result['laps'][0]['end_tick'], 4)
        self.assertEqual(result['laps'][0]['crossing_disarmed_tick'], 2)
        self.assertEqual(result['laps'][0]['ordered_segment_boundaries'], 4)

    def test_first_counter_increment_is_only_start_crossing(self):
        trace = self.trace(self.full_cycle()[:1])
        self.assertEqual(validate_laps(trace, self.initial)['complete_laps'], 0)
        with self.assertRaisesRegex(ValueError, 'no complete lap'):
            validate_laps(trace, self.initial, True)

    def test_partial_captured_lap_does_not_count(self):
        self.initial.update(lap_counter=1, crossing_armed=128, segment=1)
        steps = self.full_cycle()[1:]
        result = validate_laps(self.trace(steps), self.initial)
        self.assertEqual(result['complete_laps'], 0)
        self.assertEqual(result['crossings'], [{'tick': 3, 'lap_counter': 2}])

    def test_partial_captured_lap_followed_by_full_cycle_counts_once(self):
        self.initial.update(lap_counter=1)
        result = validate_laps(self.trace(self.full_cycle(2)), self.initial, True)
        self.assertEqual(result['complete_laps'], 1)
        self.assertEqual(result['laps'][0]['lap_counter_before'], 2)

    def test_segment_shortcut_is_not_certified(self):
        steps = self.full_cycle()
        del steps[1]
        result = validate_laps(self.trace(steps), self.initial)
        self.assertEqual(result['complete_laps'], 0)
        self.assertEqual(result['interrupted_candidates'][0]['reason'], 'segment order is incomplete or reversed')

    def test_reverse_cycle_is_not_certified(self):
        steps = self.full_cycle()
        steps[1]['segment'], steps[3]['segment'] = 0, 2
        self.assertEqual(validate_laps(self.trace(steps), self.initial)['complete_laps'], 0)

    def test_remaining_on_one_segment_cannot_make_a_lap(self):
        steps = [self.full_cycle()[0]] * 100
        self.assertEqual(validate_laps(self.trace(steps), self.initial)['complete_laps'], 0)

    def test_missing_disarm_rejects_counter_crossing(self):
        steps = self.full_cycle()
        for row in steps:
            row['crossing_armed'] = 128
        with self.assertRaisesRegex(ValueError, 'crossing conditions'):
            validate_laps(self.trace(steps), self.initial)

    def test_off_track_counter_increment_is_rejected(self):
        steps = self.full_cycle()
        steps[-1]['off_track'] = 64
        with self.assertRaisesRegex(ValueError, 'crossing conditions'):
            validate_laps(self.trace(steps), self.initial)

    def test_restart_cannot_join_two_partial_laps(self):
        steps = self.full_cycle()
        steps[2].update(lap_counter=0, restarts=1, outer_action=1)
        for row in steps[3:]:
            row.update(lap_counter=0, restarts=1)
        result = validate_laps(self.trace(steps), self.initial)
        self.assertEqual(result['complete_laps'], 0)
        self.assertEqual(result['restart_events'][0]['tick'], 2)
        self.assertEqual(result['interrupted_candidates'][0]['reason'], 'restart')

    def test_counter_jump_without_restart_is_rejected(self):
        steps = self.full_cycle()
        steps[2]['lap_counter'] = 0
        with self.assertRaisesRegex(ValueError, 'counter changed without a restart'):
            validate_laps(self.trace(steps), self.initial)

    def test_counter_increment_away_from_line_is_rejected(self):
        steps = self.full_cycle()
        steps[-1]['segment'] = 2
        with self.assertRaisesRegex(ValueError, 'crossing conditions'):
            validate_laps(self.trace(steps), self.initial)

    def test_race_completion_marker_is_retained(self):
        steps = self.full_cycle()
        steps[-1].update(race_complete=4, outer_action=2)
        result = validate_laps(self.trace(steps), self.initial, True)
        self.assertEqual(result['race_complete_changes'], [{'tick': 4, 'before': 0, 'after': 4, 'outer_action': 2}])

    def test_capture_markers_are_read_from_documented_addresses(self):
        ram = bytearray(0x80000)
        for address, value in {0x1bb1c: 15, 0x1bb20: 0, 0x1bb1e: 128,
                               0x1bbaf: 0, 0x1ca32: 16, 0x1ca1d: 37,
                               0x1ca1a: 44, 0x1bb99: 4}.items():
            ram[address] = value
        result = initial_lap_state(ram)
        self.assertEqual(result['track_segment_count'], 44)
        self.assertEqual(result['lap_line_segment'], 16)
        self.assertEqual(result['finish_line_segment'], 37)
        self.assertEqual(result['crossing_armed'], 128)


class TimelineExpansionTests(unittest.TestCase):
    def setUp(self):
        self.directory = test_directory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.source = self.root / 'timeline.json'
        self.output = self.root / 'inputs.csv'

    def write(self, steps):
        self.source.write_text(json.dumps({'provenance': 'Synthetic authored test controls.', 'steps': steps}), encoding='utf-8')

    def test_expansion_preserves_controls_and_origin(self):
        self.write([{'ticks': 2, 'controls': 1}, {'ticks': 1, 'controls': 0}])
        result = expand_controls(self.source, self.output)
        self.assertEqual(self.output.read_text(encoding='ascii'), 'tick,controls\n0,1\n1,1\n2,0\n')
        self.assertEqual(result['ticks'], 3)
        self.assertEqual(result['provenance'], 'Synthetic authored test controls.')

    def test_state_injection_keys_are_rejected_before_writing(self):
        self.write([{'ticks': 1, 'controls': 1}, {'ticks': 5, 'controls': 0, 'segment': 3}])
        with self.assertRaisesRegex(ValueError, 'exactly ticks and controls'):
            expand_controls(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_existing_controls_are_preserved(self):
        self.write([{'ticks': 1, 'controls': 1}])
        self.output.write_text('existing input', encoding='ascii')
        with self.assertRaises(FileExistsError):
            expand_controls(self.source, self.output)
        self.assertEqual(self.output.read_text(encoding='ascii'), 'existing input')

    def test_quit_schema_is_explicit_and_defaults_to_released(self):
        self.write([{'ticks': 2, 'controls': 1}, {'ticks': 1, 'controls': 0, 'quit': 1}])
        expand_controls(self.source, self.output)
        self.assertEqual(self.output.read_text(encoding='ascii'), 'tick,controls,quit\n0,1,0\n1,1,0\n2,0,1\n')

    def test_boolean_and_out_of_range_masks_are_rejected(self):
        for value in (True, -1, 32):
            self.write([{'ticks': 1, 'controls': value}])
            with self.assertRaisesRegex(ValueError, 'mask must be 0..31'):
                expand_controls(self.source, self.output)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
