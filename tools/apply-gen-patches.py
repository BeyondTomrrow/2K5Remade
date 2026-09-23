"""Re-apply this project's hand patches to regenerated code in src/recomp/gen.

`tools/analyze.ps1 -Recompile` rewrites every src/recomp/gen/*.c file from
scratch, silently wiping any edit made there. Through 2026-09-22 that lost
every NFL2K5_FORCE_UNBLOCK_* patch except STATE9_READY (which lives in
src/recomp_manual.c) across two regenerations, while the CMake options stayed
ON and compiled to nothing -- so several "still blocked" measurements after
the toolchain update were taken without the bypasses they assumed were active.

Each patch is anchored on a function's `void sub_XXXXXXXX(void)` line plus
the first occurrence of an exact generated line after it (labels are guest
addresses, so they survive regeneration). Idempotent: a patch whose marker is
already present is skipped. Fails loudly if an anchor can't be found, so a
toolchain change that moves the code is noticed instead of silently dropping
the patch again. tools/build.ps1 -Game runs this before every build.

The AC97 reset ack is included too; if the pre-existing hand-applied copy is
already there (no marker), the anchor check below skips re-inserting it.
"""
import glob
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "src", "recomp", "gen")

# (id, function, anchor line (stripped match), insert 'before' or 'after', code)
PATCHES = [
    ("AC97_RESET_ACK", "sub_004542EA", "MEM8(eax + -20971253) = 2;", "after", """\
    { extern void nfl2k5_ack_ac97_reset(uint32_t address); nfl2k5_ack_ac97_reset(eax + 0xFEC0010Bu); }
"""),
    ("B09584", "sub_000432C0", "esp += 4; return; /* ret */", "before", """\
#ifdef NFL2K5_FORCE_UNBLOCK_B09584
    /* EXPERIMENTAL, 2026-09-21: MEM32(0xB09584) gets stuck at 1 forever on
     * native, permanently blocking sub_00074180's poll loop and so the main
     * game loop. Forces the wait to always succeed. See PROJECT_STATUS.md. */
    eax = 1;
#endif
"""),
    ("TASK42200_CAPTURE", "sub_00042210", "eax = esp;", "after", """\
#ifdef NFL2K5_FORCE_UNBLOCK_TASK42200
    static uint32_t nfl2k5_task42200_flag; /* completion flag address, see below */
    nfl2k5_task42200_flag = eax;
#endif
"""),
    ("TASK42200", "sub_00042210", "loc_0004223E: ;", "after", """\
#ifdef NFL2K5_FORCE_UNBLOCK_TASK42200
    /* EXPERIMENTAL, 2026-09-21: async task submitted via sub_0003B1B0 whose
     * completion flag (captured above) is never set; the poll loop at
     * loc_00042246 spins forever. Same root cause as TASK42440. */
    if (eax) MEM32(nfl2k5_task42200_flag) = 0;
#endif
"""),
    ("TASK42440", "sub_00042820", "loc_00042861: ;", "after", """\
#ifdef NFL2K5_FORCE_UNBLOCK_TASK42440
    /* EXPERIMENTAL, 2026-09-21: async boot task type 0x42440 (submitted via
     * sub_0003BE40 just above, completion flag at MEM32(esp+4)) never
     * completes, so archive/font registration (sub_00044D00) is never
     * reached. Force the flag to "complete". See PROJECT_STATUS.md. */
    if (eax) MEM32(esp + 4) = 0;
#endif
"""),
    # Anchored on the retry loop's label, not on "eax = MEM32(esi + 8);" --
    # that line appears first in the entry check at loc_00033668, and a patch
    # there leaves the loop below it spinning forever (2026-09-23).
    ("33660_DRAIN_LOOP", "sub_00033660", "loc_0003367A: ;", "after", """\
#ifdef NFL2K5_FORCE_UNBLOCK_33660_DRAIN
    /* EXPERIMENTAL, 2026-09-22: MEM32(esi+8) frozen at 1 across tens of
     * thousands of retries (live cdb). Force after 200 retries so a real
     * short wait still resolves on its own. See PROJECT_STATUS.md. */
    if (MEM32(esi + 8)) {
        static volatile long retries;
        if (++retries > 200)
            MEM32(esi + 8) = 0;
    }
#endif
"""),
    ("NETPOLL_1", "sub_00045E60", "loc_00045E93: ;", "after", """\
#ifdef NFL2K5_FORCE_UNBLOCK_NETPOLL
    /* EXPERIMENTAL, 2026-09-22: XNetStartup's readiness poll (sub_00484EAB)
     * returns 0 forever -- no network hardware behind it. After 50 retries
     * report 2 ("not found", not 1 = true success) so the function's own
     * no-link fallback runs. See PROJECT_STATUS.md. */
    {
        static volatile long retries;
        if (eax == 0 && ++retries > 50)
            eax = 2;
    }
#endif
"""),
    ("NETPOLL_2", "sub_00045E60", "loc_00045EE4: ;", "after", """\
#ifdef NFL2K5_FORCE_UNBLOCK_NETPOLL
    /* Second instance of the same poll (sub_00484EB6), see above. */
    {
        static volatile long retries;
        if (eax == 0 && ++retries > 50)
            eax = 2;
    }
#endif
"""),
    ("AUDIO_LOCK", "sub_0044BB44", "loc_0044BCAB: ;", "before", """\
#ifdef NFL2K5_FORCE_UNBLOCK_AUDIO_LOCK
    /* EXPERIMENTAL, 2026-09-21: the busy-wait at loc_0044BCAB spins on a
     * DirectSound-buffer completion counter the stubbed APU never
     * acknowledges. Clear it immediately. See PROJECT_STATUS.md. */
    MEM32(ebx) = 0;
#endif
"""),
]


def marker(pid):
    return "/* NFL2K5-GENPATCH:%s */" % pid


def find_function(files, func):
    sig = "void %s(void)" % func
    for path in files:
        with open(path, encoding="utf-8", newline="") as f:
            lines = f.read().split("\n")
        for i, line in enumerate(lines):
            if line.strip() == sig:
                return path, lines, i
    return None, None, None


def main():
    check_only = "--check" in sys.argv
    files = sorted(glob.glob(os.path.join(GEN, "recomp_*.c")))
    applied, present, failed = [], [], []
    for pid, func, anchor, where, code in PATCHES:
        path, lines, start = find_function(files, func)
        if path is None:
            failed.append("%s: function %s not found" % (pid, func))
            continue
        # The function ends at the next top-level closing brace.
        end = next((j for j in range(start + 1, len(lines)) if lines[j].rstrip("\r") == "}"), len(lines))
        body = lines[start:end]
        if any(marker(pid) in l for l in body):
            present.append(pid)
            continue
        hit = next((j for j in range(start, end) if lines[j].strip() == anchor), None)
        if hit is None:
            failed.append("%s: anchor %r not found in %s" % (pid, anchor, func))
            continue
        nxt = lines[hit + 1].strip() if where == "after" and hit + 1 < end else ""
        if nxt and nxt == code.strip().splitlines()[0].strip():
            present.append(pid)
            continue
        if check_only:
            failed.append("%s: not applied" % pid)
            continue
        eol = "\r" if lines[start].endswith("\r") else ""  # generated files are CRLF
        block = [l + eol for l in ["    " + marker(pid)] + code.rstrip("\n").split("\n")]
        at = hit + 1 if where == "after" else hit
        # 'before' a label: keep the label's preceding blank line above the patch.
        if where == "before" and lines[hit].strip().endswith(": ;") and lines[hit - 1].strip() == "":
            at = hit - 1
        lines[at:at] = block
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write("\n".join(lines))
        applied.append("%s -> %s" % (pid, os.path.basename(path)))
    for a in applied:
        print("[gen-patch] applied %s" % a)
    if present:
        print("[gen-patch] already present: %s" % ", ".join(present))
    for msg in failed:
        print("[gen-patch] FAILED %s" % msg, file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
