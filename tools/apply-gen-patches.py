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
    ("SKIP_INTRO", "sub_00178150", "loc_00178150: ;", "after", """\
    /* Runtime option NFL2K5_SKIP_INTRO=1 (2026-09-23): sub_00178150 plays one
     * intro movie (espn_videogames, vc, espn_game_sound, intro -- the table at
     * 0x4E9730) and returns 2 when the player skips it, which ends the intro
     * loop at 0x74BC0. There is no input yet, and with the guest on one core
     * the movies run far below real time, so this does what pressing a button
     * would. */
    {
        /* Only the boot intro: returning 2 ends that loop after its first
         * movie. Later calls (anything played after the front end, 2026-09-24)
         * run normally and are logged with their caller. */
        extern char *__cdecl getenv(const char *);
        extern int __cdecl fprintf(void *, const char *, ...);
        extern void *__cdecl __acrt_iob_func(unsigned);
        static int skip = -1, calls;
        if (skip < 0) { const char *v = getenv("NFL2K5_SKIP_INTRO"); skip = v && *v && *v != '0'; }
        fprintf(__acrt_iob_func(2), "  [MOVIE] sub_00178150 call %d from %08X arg %08X%s\\n", calls,
                MEM32(esp), MEM32(esp + 4), skip && calls == 0 ? " (skipped: NFL2K5_SKIP_INTRO)" : "");
        if (skip && calls++ == 0) { eax = 2; esp += 8; return; /* ret 4 */ }
        calls++;
    }
"""),
    # XInput HLE entry points (XPP section): see src/nfl2k5_input_hle.c.
    ("XINPUT_XGETDEVICES", "sub_004DBC0A", "loc_004DBC0A: ;", "after", """\n    { extern int nfl2k5_hle_XGetDevices(void); if (nfl2k5_hle_XGetDevices()) return; } /* src/nfl2k5_input_hle.c */
"""),
    ("XINPUT_XGETDEVICECHANGES", "sub_004DBC2C", "loc_004DBC2C: ;", "after", """\n    { extern int nfl2k5_hle_XGetDeviceChanges(void); if (nfl2k5_hle_XGetDeviceChanges()) return; } /* src/nfl2k5_input_hle.c */
"""),
    ("XINPUT_XINPUTOPEN", "sub_004DBC99", "loc_004DBC99: ;", "after", """\n    { extern int nfl2k5_hle_XInputOpen(void); if (nfl2k5_hle_XInputOpen()) return; } /* src/nfl2k5_input_hle.c */
"""),
    ("XINPUT_XINPUTCLOSE", "sub_004DBCEF", "loc_004DBCEF: ;", "after", """\n    { extern int nfl2k5_hle_XInputClose(void); if (nfl2k5_hle_XInputClose()) return; } /* src/nfl2k5_input_hle.c */
"""),
    ("XINPUT_XINPUTGETCAPABILITIES", "sub_004DBCFB", "loc_004DBCFB: ;", "after", """\n    { extern int nfl2k5_hle_XInputGetCapabilities(void); if (nfl2k5_hle_XInputGetCapabilities()) return; } /* src/nfl2k5_input_hle.c */
"""),
    ("XINPUT_XINPUTGETSTATE", "sub_004DBED3", "loc_004DBED3: ;", "after", """\n    { extern int nfl2k5_hle_XInputGetState(void); if (nfl2k5_hle_XInputGetState()) return; } /* src/nfl2k5_input_hle.c */
"""),
    ("XINPUT_XINPUTSETSTATE", "sub_004DBF46", "loc_004DBF46: ;", "after", """\n    { extern int nfl2k5_hle_XInputSetState(void); if (nfl2k5_hle_XInputSetState()) return; } /* src/nfl2k5_input_hle.c */
"""),
    # Diagnostics for the pushbuffer overrun after START (2026-09-23).
    ("DIAG_PBRESET", "sub_000331A0", "loc_000331A0: ;", "after", """\
    { extern void nfl2k5_diag_pbreset(uint32_t dev); nfl2k5_diag_pbreset(ecx); }
"""),
    ("DIAG_PBBEGIN", "sub_0002C940", "loc_0002C940: ;", "after", """\
    { extern void nfl2k5_diag_pbbegin(void); nfl2k5_diag_pbbegin(); }
"""),
    # 0x167707 `jne` is reached from `cmp ecx,[eax+4]` (fall-through) and from
    # `test [esi+0x20],ecx` at 0x167817 (jmp); the lifter cannot merge a cmp
    # and a test, so it compiled as never taken ([FLAGS] reported it running,
    # 2026-09-25). Thread the jump: the test path decides its own branch before
    # jumping, and the label keeps the cmp's condition.
    ("JOIN_167707_TEST", "sub_001675E0", "goto loc_00167707;", "before", """\
    if (TEST_NZ(_fa, _fb)) goto loc_00167673;
    goto loc_0016770D;
"""),
    ("JOIN_167707_CMP", "sub_001675E0", "loc_00167707: ;", "after", """\
    if (CMP_NE(_fa, _fb)) goto loc_00167673;
"""),
    # Flag joins reached during the kickoff (2026-09-25): a je where one path
    # arrives from a cmp and the other from a test, which the lifter cannot
    # merge (RECOMP_FLAGS_FALLBACK, never taken). Resolve each path before the
    # join, like JOIN_167707.
    ("JOIN_221ABB_CMP", "sub_00221A30", "goto loc_00221ABB;", "before", """\
    if (CMP_EQ(_fa, _fb)) goto loc_00221ACD;
    goto loc_00221ABD;
"""),
    ("JOIN_221ABB_TEST", "sub_00221A30", "loc_00221ABB: ;", "after", """\
    if (TEST_Z(_fa, _fb)) goto loc_00221ACD;
"""),
    ("JOIN_2F7CA4_TEST", "sub_002F7C50", "goto loc_002F7CA4;", "before", """\
    if (TEST_Z(_fa, _fb)) goto loc_002F7CE3;
    goto loc_002F7CA6;
"""),
    ("JOIN_2F7CA4_CMP", "sub_002F7C50", "loc_002F7CA4: ;", "after", """\
    if (CMP_EQ(_fa, _fb)) goto loc_002F7CE3;
"""),
    # Texture bound with address 0 (2026-09-25, black field / bodies): log
    # the D3D texture object sub_00031FA0 is writing (esi) and the caller.
    ("DIAG_TEXZERO", "sub_00031FA0", "ebx = edx + 0x81B00;", "before", """\
    if (eax == 0) { extern void nfl2k5_diag_texzero(uint32_t tex, uint32_t ret); nfl2k5_diag_texzero(esi, MEM32(esp + 0x10)); }
"""),
    # Archive resource load/release callbacks (2026-09-25): textures drawn
    # after their release (Data back to 0) -> black field and uniforms.
    ("DIAG_REG_00043E10", "sub_00043E10", "loc_00043E10: ;", "after", """    { extern void nfl2k5_diag_reg(uint32_t fn, uint32_t edx_, uint32_t a0, uint32_t a1, uint32_t obj); nfl2k5_diag_reg(0x00043E10, edx, MEM32(esp), MEM32(esp + 4), ecx); }
"""),
    ("DIAG_REG_00043E30", "sub_00043E30", "loc_00043E30: ;", "after", """    { extern void nfl2k5_diag_reg(uint32_t fn, uint32_t edx_, uint32_t a0, uint32_t a1, uint32_t obj); nfl2k5_diag_reg(0x00043E30, edx, MEM32(esp), MEM32(esp + 4), ecx); }
"""),
    ("DIAG_REG_00043E50", "sub_00043E50", "loc_00043E50: ;", "after", """    { extern void nfl2k5_diag_reg(uint32_t fn, uint32_t edx_, uint32_t a0, uint32_t a1, uint32_t obj); nfl2k5_diag_reg(0x00043E50, edx, MEM32(esp), MEM32(esp + 4), ecx); }
"""),
    ("DIAG_REG_00043E70", "sub_00043E70", "loc_00043E70: ;", "after", """    { extern void nfl2k5_diag_reg(uint32_t fn, uint32_t edx_, uint32_t a0, uint32_t a1, uint32_t obj); nfl2k5_diag_reg(0x00043E70, edx, MEM32(esp), MEM32(esp + 4), ecx); }
"""),
    ("DIAG_RES43D20_IN", "sub_00043D20", "loc_00043D20: ;", "after", """\
    { extern void nfl2k5_diag_res43(uint32_t obj, uint32_t load, uint32_t fre, uint32_t where); nfl2k5_diag_res43(esi, ebx, eax, MEM32(esp)); }
"""),
    ("DIAG_RES43D20_CALL", "sub_00043D20", "loc_00043D67: ;", "after", """\
    { extern void nfl2k5_diag_res43(uint32_t obj, uint32_t load, uint32_t fre, uint32_t where); nfl2k5_diag_res43(esi, MEM32(esi + 0x18), MEM32(esi + 0x1C), 1); }
"""),
    ("DIAG_RESLOAD", "sub_000450B0", "loc_000450B0: ;", "after", """\
    { extern void nfl2k5_diag_res(uint32_t res, int load, uint32_t ret, uint32_t r2, uint32_t r3); nfl2k5_diag_res(ecx, 1, MEM32(esp), MEM32(esp + 8), MEM32(esp + 0x18)); }
"""),
    ("DIAG_RESFREE", "sub_000450D0", "loc_000450D0: ;", "after", """\
    { extern void nfl2k5_diag_res(uint32_t res, int load, uint32_t ret, uint32_t r2, uint32_t r3); nfl2k5_diag_res(ecx, 0, MEM32(esp), MEM32(esp + 8), MEM32(esp + 0x18)); }
"""),
    # Game state-machine pushes/pops (2026-09-24): see nfl2k5_diag_fsm in src/main.c.
    ("DIAG_FSMPUSH", "sub_0006E390", "loc_0006E3E6: ;", "after", """\
    { extern void nfl2k5_diag_fsm(uint32_t obj, uint32_t desc, int push); nfl2k5_diag_fsm(esi, edi, 1); }
"""),
    # Popups (2026-09-25, coin toss): show copies the popup at ecx into its
    # slot (sub_0008ACF0); sub_0008A340(slot) closes it, and on the second
    # call (fade done) runs the slot's callback at +0xF14.
    ("DIAG_POPUPSHOW", "sub_0008ACF0", "loc_0008ACF0: ;", "after", """\
    { extern void nfl2k5_diag_popup(uint32_t obj, int what); nfl2k5_diag_popup(ecx, 0); }
"""),
    ("DIAG_POPUPCLOSE", "sub_0008A340", "loc_0008A340: ;", "after", """\
    { extern void nfl2k5_diag_popup(uint32_t obj, int what); nfl2k5_diag_popup(ecx, 1); }
"""),
    ("DIAG_POPUPCB", "sub_0008A340", "loc_0008A3E1: ;", "after", """\
    { extern void nfl2k5_diag_popup(uint32_t obj, int what); nfl2k5_diag_popup(esi, 2); }
"""),
    # Presentation script interpreter (2026-09-25): opcode byte in eax, script
    # context in esi, [esp+0x24] nonzero = start the command, 0 = poll it.
    ("DIAG_SCRIPTOP", "sub_000DBC10", "loc_000DBC20: ;", "after", """\
    { extern void nfl2k5_diag_script(uint32_t ctx, uint32_t op, uint32_t start); nfl2k5_diag_script(esi, eax & 0xFF, MEM32(esp + 0x24)); }
"""),
    ("DIAG_FSMPOP", "sub_0006E400", "loc_0006E439: ;", "after", """\
    { extern void nfl2k5_diag_fsm(uint32_t obj, uint32_t desc, int push); nfl2k5_diag_fsm(esi, 0, 0); }
"""),
    # D3D KickOff: process the submission the moment DMA_PUT is written, as
    # the GPU does (2026-09-24). See xbox_Nv2aKick in xbox_memory_layout.c.
    ("NV2A_KICK_426110", "sub_00426110", "MEM32(ecx + 0x40) = edx;", "after", """\
    { extern void xbox_Nv2aKick(void); xbox_Nv2aKick(); }
"""),
    ("NV2A_KICK_4261C0", "sub_004261C0", "MEM32(ecx + 0x40) = esi;", "after", """\
    { extern void xbox_Nv2aKick(void); xbox_Nv2aKick(); }
"""),
    ("STOPWAIT_3CAF0", "sub_0003CAF0", "loc_0003CB1E: ;", "after", """\
#ifdef NFL2K5_FORCE_UNBLOCK_STOPWAIT
    /* EXPERIMENTAL, 2026-09-24: sub_0003CAF0 stops a DirectSound buffer and
     * spins on GetStatus until its PLAYING bit clears. Only the APU finishing
     * the voice-off clears it (voice+0x12 state), and the APU is a stub, so
     * after Start Game the audio task spun here forever and loading stopped
     * (task 3E910 permanently BUSY, no disc reads). Same root cause as
     * AUDIO_LOCK below. */
    {
        static volatile long spins;
        if (!(MEM8(esp + 4) & 1))
            spins = 0;
        else if (++spins > 100) {
            spins = 0;
            goto loc_0003CB24;
        }
    }
#endif
"""),
    ("AUDIO_LOCK", "sub_0044BB44", "loc_0044BCAB: ;", "before", """\
#ifdef NFL2K5_FORCE_UNBLOCK_AUDIO_LOCK
    /* The busy-wait at loc_0044BCAB spins until the APU's interrupt path (a
     * DPC) counts a DirectSound operation down to zero. On the console the
     * interrupt preempts this loop; here DPCs only run where the kernel
     * drains them, so the loop spun forever. Until 2026-09-26 the counter was
     * simply cleared -- DirectSound then freed buffers the APU was still
     * playing, the title reused the memory, and the menu music played
     * garbage (static). Now: wait for the real completion, draining DPCs as
     * the interrupt would; clear it only after 2 s as a last resort. */
    {
        extern void xbox_bridge_drain_guest_dpcs(void);
        extern unsigned long __stdcall GetTickCount(void);
        extern void __stdcall Sleep(unsigned long);
        unsigned long t0 = GetTickCount();
        while (MEM32(ebx) != 0 && GetTickCount() - t0 < 2000u) {
            xbox_bridge_drain_guest_dpcs();
            Sleep(0);
        }
        if (MEM32(ebx) != 0) {
            static volatile long timeouts;
            timeouts++;
            MEM32(ebx) = 0;
        }
    }
#endif
"""),
    ("DIAG_MUSICSTREAM", "sub_0003F860", "loc_0003F860: ;", "after", """    { extern void nfl2k5_diag_stream(uint32_t dst, uint32_t src, uint32_t len, uint32_t eax_, uint32_t arg); nfl2k5_diag_stream(ebx, edx, edi, eax, MEM32(esp + 4)); }
"""),
    # Gamecast Live table rows (2026-09-28). Capture each distinct row-render
    # callback once so live stat record layouts can be mapped without a
    # per-frame log or any changes to generated sources outside this patcher.
    ("DIAG_GAMECAST_ROW", "sub_00171910", "edx = ebx;", "before", """\
    { extern void nfl2k5_gamecast_row_probe(uint32_t list, uint32_t row, uint32_t index, uint32_t callback); nfl2k5_gamecast_row_probe(edi, ebx, ebp, MEM32(ebx + 0x20)); }
"""),
    ("DIAG_GAMECAST_TEXT", "sub_00173840", "loc_00173897: ;", "after", """\
    { extern void nfl2k5_gamecast_text_probe(uint32_t widget, uint32_t value); nfl2k5_gamecast_text_probe(ebx, eax); }
"""),
    ("DIAG_GAMECAST_STATCTX", "sub_003639D0", "loc_003639D0: ;", "after", """\
    { extern void nfl2k5_gamecast_stat_context(uint32_t context, uint32_t selector); nfl2k5_gamecast_stat_context(ecx, edx); }
"""),
    ("DIAG_GAMECAST_STATVALUE", "sub_003636B0", "MEMF(esp + 4) = (float)fp_top(); fp_pop(); /* fstp */", "after", """\
    { extern void nfl2k5_gamecast_stat_value(uint32_t table_slot, uint32_t field, uint32_t value_bits); nfl2k5_gamecast_stat_value(esi, edx, MEM32(esp + 4)); }
"""),
    ("SCOREBUG_HIDE_LAYOUT1", "sub_000FC200", "loc_000FC2A6: ;", "after", """    /* Re-hide the ESPN bug after the game lays it out for a new mode. */
    { extern void nfl2k5_scorebug_hide(void); nfl2k5_scorebug_hide(); }
"""),
    ("SCOREBUG_HIDE_LAYOUT2", "sub_000FC200", "loc_000FC329: ;", "after", """    { extern void nfl2k5_scorebug_hide(void); nfl2k5_scorebug_hide(); }
"""),
    ("SCOREBUG_HIDE_UPDATE", "sub_000FCE70", "loc_000FCE70: ;", "after", """    { extern void nfl2k5_scorebug_hide(void); nfl2k5_scorebug_hide(); }
"""),
    ("LIVE_QB_SAMPLE", "sub_000FCE70", "loc_000FCE70: ;", "after", """    /* Sample both title-maintained QB records on the guest execution thread. */
    { extern void nfl2k5_live_qb_guest_tick(void); nfl2k5_live_qb_guest_tick(); }
"""),
    ("SCOREBUG_SKIP_TEST", "sub_000FCE70", "loc_000FCE70: ;", "after", """    /* Experiment (NFL2K5_HIDE_SKIP=1): skip the ESPN scorebug update while a
     * custom broadcast package is on, to find out whether it drives the bar. */
    { extern int nfl2k5_scorebug_skip_update(void); if (nfl2k5_scorebug_skip_update()) { esp += 8; return; } }
"""),
    ("SCOREBUG_NATIVE_HIDE2", "sub_000FCE70", "loc_000FD178: ;", "before", """    /* Custom broadcast presentations (src/nfl2k5_presentation.cpp): hide the
     * ESPN bug by rewriting its root matrix (esi) after the game places it. */
    { extern void nfl2k5_scorebug_native_place(uint32_t matrix); nfl2k5_scorebug_native_place(esi); }
"""),
    ("SCOREBUG_NATIVE_HOOK", "sub_000FCE70", "loc_000FCFA7: ;", "after", """    /* Custom broadcast presentations (src/nfl2k5_presentation.cpp): note
     * whether the game shows its scorebug this frame, and hide the ESPN one
     * when another package draws its own. */
    { extern void nfl2k5_scorebug_native_hook(void); nfl2k5_scorebug_native_hook(); }
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
