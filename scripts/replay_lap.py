"""Summarize observed full laps, or expand authored controls into replay CSV.

This module never changes game state. A lap certificate requires two observed
lap-counter crossings, a complete ordered track cycle, and the original
crossing-latch disarm between them. It is intentionally stricter than merely
accepting a changed lap counter from a partially completed captured lap.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re


LAP_TRACE_FIELDS = ('tick', 'segment', 'off_track', 'lap_counter',
                    'crossing_armed', 'race_complete', 'outer_action', 'restarts')


def initial_lap_state(ram):
    """Read immutable track markers and initial counters from captured Chip RAM."""
    if len(ram) != 0x80000:
        raise ValueError('Lap validation requires exactly 512 KiB of captured Chip RAM')
    state = {name: ram[address] for name, address in {
        'segment': 0x1bb1c, 'lap_counter': 0x1bb20,
        'crossing_armed': 0x1bb1e, 'race_complete': 0x1bbaf,
        'lap_line_segment': 0x1ca32, 'finish_line_segment': 0x1ca1d,
        'track_segment_count': 0x1ca1a, 'maximum_laps': 0x1bb99,
    }.items()}
    count = state['track_segment_count']
    if not 1 < count <= 100 or any(state[name] >= count for name in
            ('segment', 'lap_line_segment', 'finish_line_segment')):
        raise ValueError('Lap validation requires valid loaded track markers')
    if state['lap_line_segment'] == state['finish_line_segment']:
        raise ValueError('Lap and crossing-disarm markers must be distinct')
    return state


def validate_laps(trace, initial_state, require_complete_lap=False):
    """Return trace evidence; optionally reject a trace with no certified lap.

    The caller must first establish the trace's original/native comparisons and
    input/capture identities. This function checks progress evidence only. A
    trace is not itself proof of original execution or input provenance.
    """
    count = initial_state['track_segment_count']
    line = initial_state['lap_line_segment']
    disarm = initial_state['finish_line_segment']
    previous_lap = initial_state['lap_counter']
    previous_armed = bool(initial_state['crossing_armed'] & 0x80)
    previous_complete = initial_state['race_complete']
    previous_restarts = 0
    crossings, laps, interrupted, race_changes, restart_events = [], [], [], [], []
    candidate = None
    rows_count = 0

    def interrupt(reason, tick):
        nonlocal candidate
        if candidate is not None:
            interrupted.append({'start_tick': candidate['start_tick'],
                                'end_tick': tick, 'reason': reason})
            candidate = None

    with Path(trace).open(encoding='ascii', newline='') as stream:
        rows = csv.DictReader(stream)
        if rows.fieldnames is None or len(rows.fieldnames) != len(set(rows.fieldnames)):
            raise ValueError(f'{trace}: missing/duplicate trace column names')
        if not set(LAP_TRACE_FIELDS).issubset(rows.fieldnames):
            raise ValueError(f'{trace}: missing lap/transition trace fields')
        for source in rows:
            if None in source or any(value is None for value in source.values()):
                raise ValueError(f'{trace}: malformed row {rows_count}')
            if any(not re.fullmatch(r'[0-9]+', source[name]) for name in LAP_TRACE_FIELDS):
                raise ValueError(f'{trace}: invalid lap field at row {rows_count}')
            row = {name: int(source[name]) for name in LAP_TRACE_FIELDS}
            tick = row['tick']
            if tick != rows_count:
                raise ValueError(f'{trace}: lap trace ticks must be contiguous from zero')
            rows_count += 1
            if any(row[name] > 255 for name in
                   ('segment', 'off_track', 'lap_counter', 'crossing_armed', 'race_complete')):
                raise ValueError(f'{trace}: lap byte field out of range at tick {tick}')
            if row['restarts'] < previous_restarts:
                raise ValueError(f'{trace}: cumulative restart count decreased at tick {tick}')
            restarted = row['restarts'] != previous_restarts
            if restarted:
                restart_events.append({'tick': tick, 'count': row['restarts'],
                                       'outer_action': row['outer_action']})
                interrupt('restart', tick)
            if row['race_complete'] != previous_complete:
                race_changes.append({'tick': tick, 'before': previous_complete,
                                     'after': row['race_complete'], 'outer_action': row['outer_action']})
            armed = bool(row['crossing_armed'] & 0x80)
            on_track = not (row['off_track'] & 0x40)
            delta = row['lap_counter'] - previous_lap
            if not restarted and delta not in (0, 1):
                raise ValueError(f'{trace}: lap counter changed without a restart at tick {tick}')
            if not restarted and delta == 1:
                if row['segment'] != line or not armed or previous_armed or not on_track:
                    raise ValueError(f'{trace}: lap increment lacks its original crossing conditions at tick {tick}')
            if candidate is not None and on_track:
                segment = row['segment']
                if segment != candidate['last_segment']:
                    expected = (candidate['last_segment'] + 1) % count
                    if segment != expected or candidate['boundaries'] >= count:
                        interrupt('segment order is incomplete or reversed', tick)
                    else:
                        candidate['boundaries'] += 1
                        candidate['last_segment'] = segment
                if candidate is not None and previous_armed and not armed and segment == disarm:
                    candidate['disarm_tick'] = tick
            if not restarted and delta == 1:
                crossings.append({'tick': tick, 'lap_counter': row['lap_counter']})
                if candidate is not None:
                    if candidate['boundaries'] == count and candidate['disarm_tick'] is not None:
                        laps.append({'start_tick': candidate['start_tick'], 'end_tick': tick,
                                     'ticks_between_crossings': tick - candidate['start_tick'],
                                     'lap_counter_before': candidate['lap_counter'],
                                     'lap_counter_after': row['lap_counter'],
                                     'ordered_segment_boundaries': count,
                                     'crossing_disarmed_tick': candidate['disarm_tick']})
                    else:
                        interrupt('counter crossed without a complete observed track cycle', tick)
                candidate = {'start_tick': tick, 'lap_counter': row['lap_counter'],
                             'boundaries': 0, 'last_segment': line, 'disarm_tick': None}
            previous_lap, previous_armed = row['lap_counter'], armed
            previous_complete, previous_restarts = row['race_complete'], row['restarts']
    if not rows_count:
        raise ValueError(f'{trace}: empty lap trace')
    result = {'complete_laps': len(laps), 'laps': laps, 'crossings': crossings,
              'restart_events': restart_events, 'race_complete_changes': race_changes,
              'interrupted_candidates': interrupted, 'initial_state': initial_state,
              'rows': rows_count, 'require_complete_lap': require_complete_lap,
              'partial_final_candidate': None if candidate is None else {
                  'start_tick': candidate['start_tick'],
                  'ordered_segment_boundaries': candidate['boundaries'],
                  'crossing_disarmed_tick': candidate['disarm_tick']},
              'certificate': 'Two observed crossings, a complete forward segment cycle, original latch disarm, and no intervening restart.',
              'comparison_provenance': 'Caller must establish original/native agreement and capture/input identities.'}
    if require_complete_lap and not laps:
        raise ValueError(f'{trace}: no complete lap observed (requires two crossings and all {count} forward segment boundaries without restart)')
    return result


def expand_controls(source, destination):
    """Expand an explicit duration/mask timeline; never synthesize driving state."""
    document = json.loads(Path(source).read_text(encoding='utf-8'))
    if not isinstance(document, dict) or set(document) != {'provenance', 'steps'}:
        raise ValueError('Timeline must contain exactly provenance and steps')
    if not isinstance(document['provenance'], str) or not document['provenance'].strip():
        raise ValueError('Timeline provenance must describe authored or recorded input origin')
    if not isinstance(document['steps'], list) or not document['steps']:
        raise ValueError('Timeline steps must be a nonempty list')
    for step in document['steps']:
        if not isinstance(step, dict) or set(step) not in ({'ticks', 'controls'}, {'ticks', 'controls', 'quit'}):
            raise ValueError('Each timeline step must contain exactly ticks and controls, with optional quit')
        if type(step['ticks']) is not int or not 0 < step['ticks'] <= 1000000:
            raise ValueError('Each timeline duration must be 1..1000000 ticks')
        if type(step['controls']) is not int or not 0 <= step['controls'] <= 31:
            raise ValueError('Each timeline controls mask must be 0..31')
        if type(step.get('quit', 0)) is not int or step.get('quit', 0) not in (0, 1):
            raise ValueError('Each timeline quit value must be 0 or 1')
    if sum(step['ticks'] for step in document['steps']) > 1000000:
        raise ValueError('Timeline exceeds 1000000 ticks')
    destination = Path(destination)
    extended = any('quit' in step for step in document['steps'])
    with destination.open('x', encoding='ascii', newline='') as stream:
        stream.write('tick,controls,quit\n' if extended else 'tick,controls\n')
        tick = 0
        for step in document['steps']:
            for _ in range(step['ticks']):
                suffix = f',{step.get("quit", 0)}' if extended else ''
                stream.write(f'{tick},{step["controls"]}{suffix}\n')
                tick += 1
    return {'ticks': tick, 'provenance': document['provenance'],
            'timeline_sha256': hashlib.sha256(Path(source).read_bytes()).hexdigest(),
            'inputs_sha256': hashlib.sha256(destination.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    expand = commands.add_parser('expand', help='Expand duration/mask JSON into a new controls-only CSV')
    expand.add_argument('timeline', type=Path)
    expand.add_argument('output', type=Path)
    check = commands.add_parser('inspect', help='Inspect lap evidence in an already verified trace')
    check.add_argument('trace', type=Path)
    check.add_argument('chip_ram', type=Path)
    check.add_argument('--require-complete-lap', action='store_true')
    args = parser.parse_args()
    if args.command == 'expand':
        result = expand_controls(args.timeline, args.output)
    else:
        result = validate_laps(args.trace, initial_lap_state(args.chip_ram.read_bytes()), args.require_complete_lap)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
