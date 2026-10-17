"""Measure the draft-show voice clips: duration, leading/trailing silence, loudness and peak.

    python tools/eras/draftshow_clip_timing.py

Writes mods\eras\draft\audio\clip_timing.json (next to clips.json) and prints a summary plus
any clip that needs trimming or normalising. The show uses lead/tail silence to butt the
chime, the "With the Nth pick" clip and the team clip together without gaps or overlaps.
Needs ffmpeg/ffprobe on PATH.
"""
import glob
import json
import os
import re
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
AUDIO = os.path.join(ROOT, 'mods', 'eras', 'draft', 'audio')


def probe(path):
    dur = float(subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration',
                                '-of', 'csv=p=0', path], capture_output=True, text=True).stdout.strip() or 0)
    err = subprocess.run(['ffmpeg', '-hide_banner', '-nostats', '-i', path,
                          '-af', 'silencedetect=noise=-40dB:d=0.05,ebur128=peak=true', '-f', 'null', '-'],
                         capture_output=True, text=True, encoding='utf-8', errors='replace').stderr
    starts = [float(x) for x in re.findall(r'silence_start: ([\d.]+)', err)]
    ends = [float(x) for x in re.findall(r'silence_end: ([\d.]+)', err)]
    lead = ends[0] if starts and ends and starts[0] < 0.02 else 0.0
    tail = dur - starts[-1] if starts and (len(starts) > len(ends) or starts[-1] > ends[-1]) else 0.0
    lufs = re.findall(r'I:\s+(-?[\d.]+) LUFS', err)
    peak = re.findall(r'Peak:\s+(-?[\d.]+) dBFS', err)
    return dict(duration=round(dur, 2), lead_silence=round(lead, 2), tail_silence=round(tail, 2),
                lufs=float(lufs[-1]) if lufs else None, peak_dbfs=float(peak[-1]) if peak else None)


def main():
    files = [os.path.join(AUDIO, 'pick_is_in_chime.mp3')]
    files += sorted(glob.glob(os.path.join(AUDIO, 'numbers', '*.mp3')))
    files += sorted(glob.glob(os.path.join(AUDIO, 'teams', '*.mp3')))
    files += sorted(glob.glob(os.path.join(AUDIO, 'teams', 'Alternate_team_names', '*.mp3')))
    out = {os.path.relpath(f, AUDIO).replace(os.sep, '/'): probe(f) for f in files}
    json.dump(out, open(os.path.join(AUDIO, 'clip_timing.json'), 'w'), indent=1)
    print('chime', out['pick_is_in_chime.mp3'])
    for name in ('numbers', 'teams'):
        g = [v for k, v in out.items() if k.startswith(name)]
        print('%s n=%d dur %.2f-%.2f  lead max %.2f  tail max %.2f  LUFS %.1f..%.1f  peak max %.1f' % (
            name, len(g), min(x['duration'] for x in g), max(x['duration'] for x in g),
            max(x['lead_silence'] for x in g), max(x['tail_silence'] for x in g),
            min(x['lufs'] for x in g), max(x['lufs'] for x in g), max(x['peak_dbfs'] for x in g)))
    flag = [(k, v) for k, v in out.items()
            if v['lead_silence'] > 0.3 or v['tail_silence'] > 0.4 or (v['peak_dbfs'] or -99) > -0.5
            or (v['lufs'] is not None and abs(v['lufs'] + 16) > 4)]
    for k, v in flag:
        print('FLAG', k, 'lead %.2f tail %.2f lufs %s peak %s' % (v['lead_silence'], v['tail_silence'], v['lufs'], v['peak_dbfs']))
    if not flag:
        print('no clip needs trimming or normalising')


def normalize():
    """Loudness-matched copies: normalized/<same path>.wav at -18 LUFS, true peak -1.5 dBFS,
    48 kHz stereo PCM; leading silence trimmed on the voice clips (not the chime)."""
    files = [os.path.join(AUDIO, 'pick_is_in_chime.mp3')]
    files += sorted(glob.glob(os.path.join(AUDIO, 'numbers', '*.mp3')))
    files += sorted(glob.glob(os.path.join(AUDIO, 'teams', '*.mp3')))
    files += sorted(glob.glob(os.path.join(AUDIO, 'teams', 'Alternate_team_names', '*.mp3')))
    index = {}
    for f in files:
        rel = os.path.relpath(f, AUDIO).replace(os.sep, '/')
        out = os.path.join(AUDIO, 'normalized', rel[:-4] + '.wav')
        os.makedirs(os.path.dirname(out), exist_ok=True)
        trim = '' if rel.startswith('pick') else 'silenceremove=start_periods=1:start_threshold=-40dB:start_silence=0.02,'
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', f, '-af', trim + 'loudnorm=I=-18:TP=-1.5:LRA=11',
                        '-ar', '48000', '-ac', '2', '-c:a', 'pcm_s16le', out], check=True)
        m = probe(out)
        index[rel] = dict(file='normalized/' + rel[:-4] + '.wav', **m)
    json.dump(index, open(os.path.join(AUDIO, 'normalized', 'index.json'), 'w'), indent=1)
    lufs = [v['lufs'] for v in index.values()]
    peak = [v['peak_dbfs'] for v in index.values()]
    print('%d clips normalised: LUFS %.1f..%.1f, peak max %.1f dBFS' % (len(index), min(lufs), max(lufs), max(peak)))
    print('chime', index['pick_is_in_chime.mp3'])
    print('number_28', index['numbers/number_28.mp3'])


if __name__ == '__main__':
    import sys
    normalize() if '--normalize' in sys.argv else main()
