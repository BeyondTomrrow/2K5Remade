"""Read the owned XBE's one-byte script registry without executing the title.

Addresses are evidence, not invented names for unknown presentation commands.
Optional existing logs add exact observed opcode counts; no nearest-event
correlation is used. Game files and generated code are never changed.
"""
import argparse
import collections
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from xbe_peek import Xbe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=pathlib.Path, default=ROOT / 'original/disc/default.xbe')
    parser.add_argument('--log', type=pathlib.Path, action='append', default=[])
    parser.add_argument('--out', type=pathlib.Path, default=ROOT / 'outputs/presentation-audit/command-registry-20261018')
    args = parser.parse_args()
    image = Xbe(str(args.xbe))
    dispatch = (ROOT / 'src/recomp/gen/recomp_dispatch.c').read_text(encoding='utf-8')
    compiled = {int(v, 16) for v in re.findall(r'\{\s*0x([0-9A-Fa-f]+)u,\s*\(recomp_func_t\)', dispatch)}
    observations = collections.Counter()
    pattern = re.compile(r'\[SCRIPT\] ctx=([0-9A-F]+) op=\s*(\d+) (start|poll)\s+type=(\d+) start=([0-9A-F]+) poll=([0-9A-F]+)')
    mismatches, logfiles = [], []
    for path in args.log:
        path = path.resolve()
        logfiles.append(str(path))
        with path.open(encoding='utf-8', errors='replace') as log:
            for lineno, line in enumerate(log, 1):
                match = pattern.search(line)
                if not match:
                    continue
                ctx, opcode, phase, type_, start, poll = match.groups()
                opcode = int(opcode)
                if not 0 <= opcode <= 255:
                    mismatches.append({'file': str(path), 'line': lineno, 'opcode': opcode})
                    continue
                expected = tuple(image.u32(0xA8F7B8 + opcode * 12 + i * 4) for i in range(3))
                actual = (int(type_), int(start, 16), int(poll, 16))
                if actual != expected:
                    mismatches.append({'file': str(path), 'line': lineno, 'opcode': opcode,
                        'reason': 'runtime table differs from owned XBE initializer'})
                else:
                    observations[(opcode, phase)] += 1
    rows = []
    for opcode in range(256):
        type_, start, poll = (image.u32(0xA8F7B8 + opcode * 12 + i * 4) for i in range(3))
        # sub_DBC10 indexes this table by a byte and validates types0..32.
        if type_ > 32:
            raise RuntimeError(f'Unexpected operand-dispatch type {type_} at opcode{opcode}')
        rows.append({'opcode': opcode, 'type': type_, 'start': f'{start:08X}', 'poll': f'{poll:08X}',
            'populated': bool(start or poll),
            'start_recompiled': start in compiled if start else None,
            'poll_recompiled': poll in compiled if poll else None,
            'observed_starts': observations[(opcode, 'start')],
            'observed_polls': observations[(opcode, 'poll')]})
    active = [r for r in rows if r['populated']]
    metadata = {'source_sha256': hashlib.sha256(image.data).hexdigest(), 'table': '00A8F7B8',
        'dispatch': '000DBC10', 'slot_count': 256, 'populated_count': len(active),
        'distinct_operand_types': sorted({r['type'] for r in active}),
        'log_files': logfiles, 'runtime_mismatches': mismatches, 'commands': rows}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.with_suffix('.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    md = ['# Native script command registry', '',
        'Read directly from the user-owned, read-only XBE. No title or assets are bundled.', '',
        f'One-byte dispatcher: **{len(active)} populated commands / 256 slots**, '
        f'**{len(metadata["distinct_operand_types"])} operand-dispatch types**. '
        'These are commands throughout the title scripting system, not255 identified graphics.', '',
        '`sub_000DBC10` reads type/start/poll at `A8F7B8 + opcode*12`. '
        'A handler address or poll pointer alone does not prove when an animation paints, finishes or is skipped. '
        'Unknown meanings remain unclassified. Runtime observations below count only exact table-matching log records.', '',
        f'Existing logs inspected: {len(logfiles)}; mismatched records: {len(mismatches)}.', '',
        '| Op | Type | Start | Poll | Recompiled start/poll | Starts/polls observed |',
        '|---:|---:|---|---|---|---:|']
    for r in active:
        status = lambda value: '-' if value is None else 'yes' if value else 'NO'
        md.append(f'|{r["opcode"]}|{r["type"]}|`{r["start"]}`|`{r["poll"]}`|'
            f'{status(r["start_recompiled"])}/{status(r["poll_recompiled"])}|'
            f'{r["observed_starts"]}/{r["observed_polls"]}|')
    args.out.with_suffix('.md').write_text('\n'.join(md) + '\n', encoding='utf-8')
    missing = [r['opcode'] for r in active if r['start_recompiled'] is False or r['poll_recompiled'] is False]
    print(f'{len(active)}/256 populated; {len(metadata["distinct_operand_types"])} types; '
        f'{len(missing)} commands with a missing compiled target; runtime mismatches={len(mismatches)}')
    print(args.out.with_suffix('.md'))


if __name__ == '__main__':
    main()
