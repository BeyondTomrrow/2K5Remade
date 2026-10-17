"""My NFL Eras: the game's match statistic selectors, read from the retail XBE.

    python tools/eras/result_capture/stat_selectors.py

Read-only. Input: original/disc/default.xbe (sha256 checked). Output:
    data/eras/stat_selectors_2k5.json

What it reads (all evidence in docs/eras/research/RESULT-CAPTURE.md):
  * the base selector table at .data 0xA8A510, 0x1C bytes per selector 0..0xB6:
      +0x00 type (0..3), +0x04 player live getter, +0x08 team getter,
      +0x0C ROST career-history field id (-1 none), +0x10..+0x18 not decoded
    consumed by sub_000CB240 (player, scope 0..7 -> sub_000CA470 -> sub_000C9510)
    and sub_000CB2D0 (team, scope 0..7 -> sub_000CB080 -> sub_000CAA00);
  * the derived selector table at .data 0xA8B918, 24 bytes per selector 0xB8..0xE8,
    consumed by sub_000CB0C0 (op 0 add, 1 subtract, 2 average, 3 ratio, 4 sum of
    four, 5 passer rating via sub_000CA880, 6 times f32[0x4E4184] = 0.5);
  * the game's own labelled stat-screen descriptors in .data: 32-byte records
    {fmt, kind, label (UTF-16 in .string_), formatter, selector, scope, selector2,
    scope2}; formatter 0x329400 calls sub_000CB240 (player), 0x329460 calls
    sub_000CB2D0 (team).
Career field names: 2K Football Mod Tools mod_editor/core/nfl2k5_career_stats.py FIELDS.
"""
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
XBE = ROOT / 'original' / 'disc' / 'default.xbe'
OUT = ROOT / 'data' / 'eras' / 'stat_selectors_2k5.json'
RETAIL_SHA256 = '73105b17a3161c546fea792a1c84ce37f9966a67c416f474cdbfab74b911a4a9'

BASE_TABLE, BASE_STRIDE, BASE_COUNT = 0xA8A510, 0x1C, 0xB7
DERIVED_TABLE, DERIVED_STRIDE, DERIVED_FIRST, DERIVED_LAST = 0xA8B918, 24, 0xB8, 0xE8
PLAYER_FORMATTER, TEAM_FORMATTER = 0x329400, 0x329460
DERIVED_OPS = {0: 'a+b', 1: 'a-b', 2: 'a/b (average, b as integer)', 3: 'a/b (ratio)',
               4: 'a+b+c+d', 5: 'passer rating(a..e) via sub_000CA880', 6: 'a*0.5'}
# nfl2k5_career_stats.py FIELDS: history field id -> name (2K Football Mod Tools)
CAREER_FIELD_NAMES = {
    0: 'games', 1: 'rushing_attempts', 2: 'rushing_yards', 3: 'rushing_touchdowns',
    6: 'passing_attempts', 7: 'passing_completions', 8: 'passing_yards', 9: 'passing_touchdowns',
    10: 'passing_interceptions', 12: 'receptions', 13: 'receiving_yards', 14: 'receiving_touchdowns',
    18: 'defensive_tackles', 19: 'defensive_sacks_half_units', 20: 'defensive_interceptions',
    21: 'forced_fumbles', 55: 'interception_return_yards', 25: 'extra_points_made',
    26: 'extra_points_attempted', 32: 'punts', 33: 'punting_yards', 34: 'punts_inside_20',
    71: 'punt_touchbacks', 61: 'field_goals_made_1_29', 62: 'field_goals_attempted_1_29',
    63: 'field_goals_made_30_39', 64: 'field_goals_attempted_30_39', 65: 'field_goals_made_40_49',
    66: 'field_goals_attempted_40_49', 67: 'field_goals_made_50_plus', 68: 'field_goals_attempted_50_plus',
}
SCOPES = {0: 'this match', 1: 'Q1', 2: 'Q2', 3: 'Q3', 4: 'Q4', 5: 'OT (inferred)', 6: '1st half',
          7: '2nd half', 11: 'season'}


class Xbe:
    def __init__(self, path):
        self.data = path.read_bytes()
        base = struct.unpack_from('<I', self.data, 0x104)[0]
        count, table = struct.unpack_from('<II', self.data, 0x11C)
        self.sections = []
        for i in range(count):
            h = table - base + i * 56
            _flags, va, vsize, raw, rsize, name_va = struct.unpack_from('<6I', self.data, h)
            no = name_va - base
            name = self.data[no:self.data.index(b'\0', no)].decode('ascii', 'replace')
            self.sections.append((name, va, vsize, raw, rsize))

    def section(self, name):
        return next(s for s in self.sections if s[0] == name)

    def read(self, va, n):
        for _name, sva, vsize, raw, rsize in self.sections:
            if sva <= va < sva + vsize:
                off = va - sva
                out = bytearray(n)
                avail = max(0, min(n, rsize - off))
                out[:avail] = self.data[raw + off:raw + off + avail]
                return bytes(out)
        raise KeyError(hex(va))

    def u32(self, va):
        return struct.unpack('<I', self.read(va, 4))[0]

    def wide(self, va, limit=64):
        s = self.read(va, limit * 2).decode('utf-16le', 'replace')
        return s.split('\0')[0]


def in_section(sec, va):
    return sec[1] <= va < sec[1] + sec[2]


def main():
    xbe = Xbe(XBE)
    digest = hashlib.sha256(xbe.data).hexdigest()
    if digest != RETAIL_SHA256:
        raise SystemExit(f'unexpected default.xbe sha256 {digest}')

    base = []
    for sel in range(BASE_COUNT):
        w = struct.unpack('<7I', xbe.read(BASE_TABLE + BASE_STRIDE * sel, 28))
        hist = w[3] if w[3] != 0xFFFFFFFF else None
        base.append({
            'selector': sel, 'hex': f'0x{sel:02X}', 'type': w[0],
            'per_period_scopes': w[0] in (2, 3),
            'player_getter': f'0x{w[1]:08X}' if w[1] else None,
            'team_getter': f'0x{w[2]:08X}' if w[2] else None,
            'history_field': hist,
            'career_field_name': CAREER_FIELD_NAMES.get(hist) if hist is not None else None,
            'raw_tail': [f'0x{x:08X}' for x in w[4:]],
            'labels': [],
        })

    derived = []
    for sel in range(DERIVED_FIRST, DERIVED_LAST + 1):
        w = struct.unpack('<6I', xbe.read(DERIVED_TABLE + DERIVED_STRIDE * (sel - DERIVED_FIRST), 24))
        n = {0: 2, 1: 2, 2: 2, 3: 2, 4: 4, 5: 5, 6: 1}.get(w[0], 0)
        derived.append({'selector': sel, 'hex': f'0x{sel:02X}', 'op': w[0],
                        'op_meaning': DERIVED_OPS.get(w[0]),
                        'inputs': [f'0x{x:02X}' for x in w[1:1 + n]], 'labels': []})

    data_sec, str_sec, text_sec = xbe.section('.data'), xbe.section('.string_'), xbe.section('.text')
    _n, sva, _vs, raw, rsize = data_sec
    blob = xbe.data[raw:raw + rsize]
    labels = []
    for off in range(0, len(blob) - 32, 4):
        fmt, kind, lab, fn, sel, scope, sel2, scope2 = struct.unpack_from('<8I', blob, off)
        if fn not in (PLAYER_FORMATTER, TEAM_FORMATTER) or not in_section(str_sec, lab):
            continue
        if sel > 0x100 or scope > 0x20:
            continue
        text = xbe.wide(lab, 48)
        if not text:
            continue
        rec = {'record_va': f'0x{sva + off:08X}', 'label': text,
               'entity': 'player' if fn == PLAYER_FORMATTER else 'team',
               'selector': f'0x{sel:02X}', 'scope': scope, 'scope_meaning': SCOPES.get(scope),
               'selector2': f'0x{sel2:02X}' if sel2 else None, 'scope2': scope2 if sel2 else None,
               'fmt_dword': fmt, 'kind_dword': kind}
        labels.append(rec)
        target = base[sel] if sel < BASE_COUNT else (
            derived[sel - DERIVED_FIRST] if DERIVED_FIRST <= sel <= DERIVED_LAST else None)
        if target is not None:
            tag = f"{rec['entity']}:{text}@{scope}"
            if tag not in target['labels']:
                target['labels'].append(tag)

    out = {
        'schema': 'nfl2k5_eras_stat_selectors/v1',
        'source': {'xbe': str(XBE), 'sha256': digest,
                   'base_table': f'0x{BASE_TABLE:08X}', 'derived_table': f'0x{DERIVED_TABLE:08X}',
                   'doc': 'docs/eras/research/RESULT-CAPTURE.md'},
        'accessors': {
            'player': 'sub_000CB240: ecx=player record, edx=selector, [esp+4]=scope; result in x87 ST0',
            'team': 'sub_000CB2D0: ecx=match team-data record (0xB30864 home, 0xB30A58 away), '
                    'edx=selector, [esp+4]=scope; result in x87 ST0',
        },
        'scopes': {str(k): v for k, v in SCOPES.items()},
        'base_selectors': base,
        'derived_selectors': derived,
        'stat_screen_labels': labels,
    }
    OUT.write_text(json.dumps(out, indent=1), encoding='utf-8')
    print(f'{OUT}: {len(base)} base, {len(derived)} derived, {len(labels)} labelled descriptors')


if __name__ == '__main__':
    main()
