"""Summarize existing native replay evidence without starting/altering a game.

This checks the recorded ownership/paint/camera chain, not visual similarity
or absence of defects outside the captured interval. Video still needs review.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('review', type=Path)
args = parser.parse_args()
review = args.review.resolve()
log = review / 'stderr.log'
lines = log.read_text(encoding='utf-8', errors='replace').splitlines()
owners = {}
pending_ready = []
errors, native_phases, lifecycle, host_stops = [], [], [], []
for lineno, line in enumerate(lines, 1):
    if '[REPLAY-OWNER]' in line:
        token = int(re.search(r'\btoken=(\d+)', line)[1])
        o = owners.setdefault(token, {'token': token, 'actions': [], 'evidence_lines': []})
        o['evidence_lines'].append(lineno)
        tick = int(re.search(r'\bt=(\d+)', line)[1])
        if ' prepare ' in line:
            o.update(direction=re.search(r'direction=(in|out)', line)[1], created_at=tick)
        elif ' page ' in line:
            action = re.search(r'action=(\w+)', line)[1]
            o['actions'].append(action)
            o[action.lower() + '_at'] = tick
        elif ' camera ' in line:
            o['actions'].append('HANDOFF')
            o['handoff_at'] = tick
        elif ' release ' in line:
            o['released_at'] = tick
            o['release_reason'] = re.search(r'reason=([^ ]+)', line)[1]
            o['complete'] = all(re.search(r'\b' + key + '=1', line)
                                for key in ('painted', 'covered', 'handed', 'done'))
    if '[REPLAY-READY]' in line:
        pending_ready.append({'line': lineno, 'text': line})
    if '[REPLAY-FSM]' in line:
        native_phases.append({'line': lineno, 'phase': int(re.search(r'\bphase=(\d+)', line)[1]),
                              'tick': int(re.search(r'\bt=(\d+)', line)[1])})
    if '[PRES-MATCH]' in line:
        lifecycle.append({'line': lineno, 'text': line})
    if '[HTMLPRES] host stopped' in line:
        host_stops.append(lineno)
    if any(key in line for key in ('[CRASH]', 'handler error:', 'CBS_REPLAY_FALLBACK:')):
        errors.append({'line': lineno, 'text': line})

expected = ['READY', 'START', 'COVER', 'HANDOFF', 'DONE']
for o in owners.values():
    o['ordered_handshake'] = o['actions'] == expected
    if o.get('complete'):
        assert o['ordered_handshake'], o
        assert o['cover_at'] <= o['handoff_at'] <= o['done_at'] <= o['released_at'], o
    o['host_stopped_while_owned'] = any(o['evidence_lines'][0] < n < o['evidence_lines'][-1]
                                       for n in host_stops)
    if 'start_at' in o and 'done_at' in o:
        o['start_to_done_ms'] = o['done_at'] - o['start_at']
videos = [{'name': path.name, 'bytes': path.stat().st_size,
           'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
          for path in sorted(review.glob('*.mp4')) if not path.name.endswith('.video.mp4')]
result = {'review': str(review), 'log_sha256': hashlib.sha256(log.read_bytes()).hexdigest(),
          'native_lifecycle': lifecycle, 'owners': list(owners.values()),
          'complete_transitions': sum(bool(o.get('complete')) for o in owners.values()),
          'fallback_transitions': sum(not bool(o.get('complete')) for o in owners.values()),
          'native_initialized_records': pending_ready, 'native_phases': native_phases,
          'host_stops': host_stops, 'crash_handler_or_page_fallback_lines': errors, 'videos': videos,
          'scope': 'Trace handshake verification only; visual review is separate.'}
out = review / 'replay-verification.json'
out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(f"{result['complete_transitions']} completed transitions; {result['fallback_transitions']} fallback transitions; "
      f"{len(errors)} crash/handler/page-fallback lines; {len(videos)} actual MP4s")
print(out)
