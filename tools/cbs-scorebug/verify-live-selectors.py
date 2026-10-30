"""Check the live collector's field/schema fingerprint against XBE evidence.

Read only: never builds, starts the game, modifies generated code, or writes
game data. Metadata is data/eras/stat_selectors_2k5.json, decoded separately
from the user's executable. --xbe additionally verifies the owned executable
and its actual selector/position tables. --self-test checks that deliberately
corrupted in-memory source variants are rejected by this validator.

    python tools/cbs-scorebug/verify-live-selectors.py --self-test --xbe
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Interface field -> independently documented guest selector, wire key, format.
# Source: RESULT-CAPTURE.md sections4.4/4.6/8.1, original Gamecast labels.
FIELDS = {
    'PASS_COMPLETIONS': (0x04, 'completions', 0),
    'PASS_ATTEMPTS': (0x23, 'attempts', 0),
    'PASS_YARDS': (0x4C, 'passingYards', 0),
    'PASS_TOUCHDOWNS': (0x40, 'passingTouchdowns', 0),
    'PASS_INTERCEPTIONS': (0x16, 'interceptions', 0),
    'RUSH_ATTEMPTS': (0x03, 'carries', 0),
    'RUSH_YARDS': (0x50, 'rushingYards', 0),
    'RUSH_TOUCHDOWNS': (0x3F, 'rushingTouchdowns', 0),
    'RECEPTIONS': (0x2E, 'receptions', 0),
    'RECEIVE_YARDS': (0x4F, 'receivingYards', 0),
    'RECEIVE_TOUCHDOWNS': (0x3E, 'receivingTouchdowns', 0),
    'TACKLES': (0x32, 'tackles', 0),
    'SACKS': (0xB8, 'sacks', 1),
    'DEF_INTERCEPTIONS': (0x17, 'defensiveInterceptions', 0),
    'FIELD_GOALS_MADE': (0xC8, 'fieldGoalsMade', 0),
    'FIELD_GOALS_ATTEMPTED': (0xC7, 'fieldGoalsAttempted', 0),
    'LONGEST_FIELD_GOAL': (0x1C, 'longestFieldGoal', 0),
    'FIRST_DOWNS': (0x0E, 'firstDowns', 0),
    'POSSESSIONS': (0x5D, 'possessions', 0),
    'TOTAL_YARDS': (0xD0, 'totalYards', 0),
    'TIME_OF_POSSESSION': (0x5F, 'timeOfPossession', 2),
    'FORCED_FUMBLES': (0x11, 'forcedFumbles', 0),
    'FUMBLES_RECOVERED': (0x12, 'fumblesRecovered', 0),
    'TACKLES_FOR_LOSS': (0x34, 'tacklesForLoss', 0),
    'LONGEST_RUSH': (0x1A, 'longestRush', 0),
    'LONGEST_RECEPTION': (0x1B, 'longestReception', 0),
    'LONGEST_PASS': (0x1F, 'longestPass', 0),
    'TURNOVERS': (0x42, 'turnovers', 0),
    'PENALTIES': (0x24, 'penalties', 0),
    'PASS_YARDS_ALLOWED': (0x46, 'passingYardsAllowed', 0),
    'RUSH_YARDS_ALLOWED': (0x45, 'rushingYardsAllowed', 0),
    'TOTAL_YARDS_ALLOWED': (0xDE, 'totalYardsAllowed', 0),
}
DERIVED = {0xB8: (6, [0x2F]), 0xC8: (4, [0xAD, 0xAE, 0xAF, 0xB0]),
           0xC7: (4, [0xA9, 0xAA, 0xAB, 0xAC]),
           0xD0: (0, [0x50, 0x4C]), 0xDE: (0, [0x45, 0x46])}
POSITIONS = ['QB', 'K', 'P', 'WR', 'CB', 'FS', 'SS', 'HB', 'FB', 'TE',
             'LB', 'LB', 'C', 'G', 'T', 'DT', 'DE']


class Invalid(ValueError):
    pass


def require(value, message):
    if not value:
        raise Invalid(message)


def body(source, pattern, label):
    match = re.search(pattern, source, re.S)
    require(match is not None, 'Cannot locate ' + label)
    return match.group(1)


def c_number(value):
    return int(value.rstrip('uUlL'), 0)


def validate(manual, header, presentation, metadata):
    enum = body(header, r'typedef\s+enum\s+Nfl2k5LiveStatField\s*\{(.*?)\}', 'field enum')
    fields = re.findall(r'\bNFL2K5_LIVE_([A-Z_]+)\b', enum)
    require(fields and fields[-1] == 'FIELD_COUNT', 'The field count must terminate the enum')
    fields = fields[:-1]
    require(len(fields) == len(FIELDS) and set(fields) == set(FIELDS), 'Field enum has missing, duplicate or unknown entries')
    require(len(fields) <= 64, 'Fields exceed the validity-mask width')
    table = body(manual, r'k_live_selectors\s*\[NFL2K5_LIVE_FIELD_COUNT\]\s*=\s*\{(.*?)\n\};', 'selector table')
    rows = re.findall(r'\{\s*(0x[\da-fA-F]+),\s*(\d+),\s*(0x[\da-fA-F]+u|0),\s*(0x[\da-fA-F]+u|0),\s*(\d+)\s*\}', table)
    require(len(rows) == len(fields), 'Selector table length does not match the field enum')
    base = {entry['selector']: entry for entry in metadata['base_selectors']}
    derived = {entry['selector']: entry for entry in metadata['derived_selectors']}
    for field, raw in zip(fields, rows):
        selector, kind, player, team, fmt = map(c_number, raw)
        expected, _key, expected_format = FIELDS[field]
        require(selector == expected, f'{field}: selector {selector:#x} should be {expected:#x}')
        require(fmt == expected_format, f'{field}: integer/half-credit/seconds format changed')
        if selector in DERIVED:
            require(kind == 4 and player == team == 0, f'{field}: derived marker must not impersonate a base getter')
            source = derived.get(selector)
            require(source is not None, f'{field}: derived source missing')
            expected_op, expected_inputs = DERIVED[selector]
            require(source['op'] == expected_op and [int(v, 16) for v in source['inputs']] == expected_inputs,
                    f'{field}: derived source operation/inputs differ from the verified meaning')
        else:
            source = base.get(selector)
            require(source is not None, f'{field}: base source missing')
            require(kind == source['type'], f'{field}: descriptor type differs from the XBE')
            require(player == int(source['player_getter'] or '0', 0), f'{field}: player callback differs from the XBE')
            require(team == int(source['team_getter'] or '0', 0), f'{field}: team callback differs from the XBE')
    names = body(presentation, r'static\s+std::string\s+live_fields_json\([^)]*\).*?names\[NFL2K5_LIVE_FIELD_COUNT\]\s*=\s*\{(.*?)\};', 'JSON field names')
    wire = re.findall(r'"([A-Za-z]+)"', names)
    require(wire == [FIELDS[field][1] for field in fields], 'JSON wire-key order no longer matches the shared field enum')
    positions = body(manual, r'\bpositions\[17\]\s*=\s*\{(.*?)\}', 'native position table')
    require(re.findall(r'"([A-Z]+)"', positions) == POSITIONS, 'Native position IDs were remapped incorrectly')
    owner = body(manual, r'static\s+int\s+nfl2k5_live_read_owner\([^)]*\)\s*\{(.*?)\n\}', 'owner reader')
    require('nfl2k5_live_playbook_code' in owner, 'Owner abbreviation must use the real playbook team-code reader')
    require('0x10C' not in owner.upper().replace('0X', '0x'), 'Numeric roster+10C IDs are not team abbreviations')
    playbook = body(manual, r'static\s+int\s+nfl2k5_live_playbook_code\([^)]*\)\s*\{(.*?)\n\}', 'playbook-code reader')
    require(re.search(r'names\[2\]\s*=\s*\{\s*0x00B30810u,\s*0x00B307D0u\s*\}', playbook) is not None,
            'Away/home playbook addresses differ from read_state')
    require('uint16_t words[8]' in playbook and "words[i] != '-'" in playbook,
            'Playbook code must read the bounded UTF-16 prefix, not a numeric roster ID')
    require('uint64_t valid_fields' in header and 'double values[NFL2K5_LIVE_FIELD_COUNT]' in header,
            'Snapshot schema must preserve validity and half-credit values')
    require('sample.values[i] = NAN' in manual, 'Unknown snapshot values must not be fabricated as zero')
    require('sample.time_of_possession_unit = NFL2K5_TOP_SECONDS' in manual,
            'Verified possession-time units must be explicit')
    accessor = body(manual, r'static\s+int\s+nfl2k5_live_value\([^)]*\)\s*\{(.*?)\n\}', 'live value reader')
    require(re.search(r'k_live_selectors\[field\]\.selector,\s*0u,\s*&value', accessor) is not None,
            'Collector must request scope zero, not season/career data')
    return fields


class Xbe:
    def __init__(self, file):
        self.data = file.read_bytes()
        base = struct.unpack_from('<I', self.data, 0x104)[0]
        count, table = struct.unpack_from('<II', self.data, 0x11C)
        self.sections = []
        for i in range(count):
            _flags, va, size, raw, raw_size = struct.unpack_from('<5I', self.data, table - base + 56 * i)
            self.sections.append((va, size, raw, raw_size))

    def read(self, va, count):
        for start, size, raw, raw_size in self.sections:
            if start <= va and va + count <= start + size:
                offset = va - start
                require(offset + count <= raw_size, f'XBE bytes absent at {va:#x}')
                return self.data[raw + offset:raw + offset + count]
        raise Invalid(f'XBE VA {va:#x} is outside the mapped sections')

    def u32(self, va):
        return struct.unpack('<I', self.read(va, 4))[0]

    def wide(self, va):
        return self.read(va, 32).decode('utf-16le').split('\0', 1)[0]


def verify_xbe(file, metadata):
    xbe = Xbe(file)
    require(hashlib.sha256(xbe.data).hexdigest() == metadata['source']['sha256'], 'Owned XBE hash differs from the source metadata')
    for row in metadata['base_selectors']:
        words = struct.unpack('<7I', xbe.read(0xA8A510 + row['selector'] * 28, 28))
        require(words[:3] == (row['type'], int(row['player_getter'] or '0', 0), int(row['team_getter'] or '0', 0)),
                f'Actual XBE base selector {row["hex"]} differs from its metadata')
    for selector, (op, inputs) in DERIVED.items():
        words = struct.unpack('<6I', xbe.read(0xA8B918 + (selector - 0xB8) * 24, 24))
        require(words[0] == op and list(words[1:1 + len(inputs)]) == inputs, f'Actual derived selector {selector:#x} differs')
    require(struct.unpack('<f', xbe.read(0x4E4184, 4))[0] == 0.5, 'Sack half-credit multiplier differs')
    require([xbe.wide(xbe.u32(0x4F2688 + i * 4)) for i in range(17)] == POSITIONS,
            'Actual XBE position names differ')


def self_test(manual, header, presentation, metadata):
    mutants = [
        ('wrong receiving selector', manual.replace('{ 0x2E, 2,', '{ 0x03, 2,', 1), header, presentation),
        ('wrong tackle getter', manual.replace('0x0024EED0u', '0x0024E230u', 1), header, presentation),
        ('half sacks rounded as integers', manual.replace('{ 0xB8, 4, 0, 0, 1 }', '{ 0xB8, 4, 0, 0, 0 }', 1), header, presentation),
        ('scope changed to season', manual.replace('k_live_selectors[field].selector, 0u, &value', 'k_live_selectors[field].selector, 11u, &value', 1), header, presentation),
        ('wire field order changed', manual, header, presentation.replace('"carries", "rushingYards"', '"receptions", "rushingYards"', 1)),
        ('field enum order changed', manual, header.replace('NFL2K5_LIVE_RUSH_ATTEMPTS,', 'NFL2K5_LIVE_RUSH_ATTEMPTS_TEMP,', 1), presentation),
        ('numeric team code substituted', manual.replace('nfl2k5_live_playbook_code(side, owner->abbreviations[side], sizeof owner->abbreviations[side]);', 'nfl2k5_guest_record_text(owner->rosters[side] + 0x10Cu, owner->abbreviations[side], sizeof owner->abbreviations[side]);', 1), header, presentation),
    ]
    for label, c, h, cpp in mutants:
        require((c, h, cpp) != (manual, header, presentation), 'Mutation did not modify source: ' + label)
        try:
            validate(c, h, cpp, metadata)
        except Invalid:
            continue
        raise Invalid('Validator failed to reject ' + label)
    return len(mutants)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', nargs='?', const=str(ROOT / 'original/disc/default.xbe'), type=Path,
                        help='Also verify the user-owned XBE; default path is original/disc/default.xbe')
    parser.add_argument('--self-test', action='store_true', help='Reject seven in-memory corrupted source variants')
    args = parser.parse_args()
    try:
        manual = (ROOT / 'src/recomp_manual.c').read_text(encoding='utf-8-sig')
        header = (ROOT / 'src/nfl2k5_broadcast.h').read_text(encoding='utf-8-sig')
        presentation = (ROOT / 'src/nfl2k5_presentation.cpp').read_text(encoding='utf-8-sig')
        metadata = json.loads((ROOT / 'data/eras/stat_selectors_2k5.json').read_text(encoding='utf-8-sig'))
        fields = validate(manual, header, presentation, metadata)
        print(f'PASS {len(fields)} field fingerprints, getter types/addresses, derived meanings, wire schema, positions, scope and ownership source')
        if args.self_test:
            print(f'PASS {self_test(manual, header, presentation, metadata)} corrupted-source rejection checks')
        if args.xbe:
            verify_xbe(args.xbe, metadata)
            print('PASS owned-XBE SHA256, base/derived tables, half-sack factor and all17 native position names')
    except (Invalid, OSError, KeyError, ValueError, struct.error) as error:
        parser.exit(1, 'FAIL ' + str(error) + '\n')


if __name__ == '__main__':
    main()
