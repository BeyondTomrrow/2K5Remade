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
# --gen DIR: another generated tree (a mod pack's build). --lenient: a patch
# whose function or anchor the pack changed is reported, not fatal.
if "--gen" in sys.argv:
    GEN = os.path.abspath(sys.argv[sys.argv.index("--gen") + 1])
LENIENT = "--lenient" in sys.argv

# (id, function, anchor line (stripped match), insert 'before' or 'after', code)
PATCHES = [
    ("CELEBRATION_PREVIEW_CUSTOM", "sub_001B6B40", "loc_001B6B40: ;", "after", """\
    if (ecx >= 37u) { extern uint32_t nfl2k5_celebration_resource(uint32_t); eax = nfl2k5_celebration_resource(ecx); esp += 4; return; }
"""),
    ("CELEBRATION_SELECT_SAVE", "sub_001B6B50", "loc_001B6BD7: ;", "after", """\
    uint32_t nfl2k5_custom_selector_row = 0;
    if (eax >= 37u) { extern uint32_t nfl2k5_celebration_selector_row(uint32_t); nfl2k5_custom_selector_row = nfl2k5_celebration_selector_row(eax); }
"""),
    ("CELEBRATION_SELECT_APPLY", "sub_001B6B50", "eax = eax * 4 + 0x50CFC8;", "after", """\
    if (nfl2k5_custom_selector_row) {
        eax = nfl2k5_custom_selector_row;
        ecx = MEM32(eax);
        _fa = ecx; _fb = ecx; _fas = (int32_t)ecx; _fbs = (int32_t)ecx;
    }
"""),
    # Team touchdown celebrations: sub_001B68F0 rolls rand % 24 -- 0-1 two
    # dancers, 2-18 three, 19-23 four -- then draws a clip of that size. The
    # custom duos (My World) replace both two-dancer clips; force_duo makes
    # every team celebration a duo (custom-animations\\team.ini).
    ("TEAM_CELEBRATION_FORCE_DUO", "sub_001B68F0", "_fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp edx, 2 (32-bit) */", "after", """\
    { extern int nfl2k5_team_force_duo(void); if (nfl2k5_team_force_duo()) { _fas = 0; _fbs = 2; } }
"""),
    ("CELEBRATION_VISIBLE_SAVE", "sub_00369650", "loc_0036967B: ;", "after", """\
    uint32_t nfl2k5_custom_visible_remaining = edi;
"""),
    ("CELEBRATION_VISIBLE_CUSTOM", "sub_00369650", "eax = 0x25;", "after", """\
    { extern uint32_t nfl2k5_celebration_after_retail(uint32_t); eax = nfl2k5_celebration_after_retail(nfl2k5_custom_visible_remaining); }
"""),
    ("CELEBRATION_COUNT_CUSTOM", "sub_00369A50", "eax = edi;", "before", """\
    edi += 4u;
"""),
    ("CELEBRATION_LABEL_PROFILE", "sub_00369A00", "eax = MEM32(eax * 8 + 0x5858D4);", "before", """\
    uint32_t nfl2k5_custom_label_profile_index = eax;
"""),
    ("CELEBRATION_LABEL_PROFILE_DONE", "sub_00369A00", "eax = MEM32(eax * 8 + 0x5858D4);", "after", """\
    if (nfl2k5_custom_label_profile_index >= 37u) { extern uint32_t nfl2k5_celebration_label(uint32_t); eax = nfl2k5_celebration_label(nfl2k5_custom_label_profile_index); }
"""),
    ("CELEBRATION_LABEL_LIST", "sub_00369AB0", "eax = MEM32(eax * 8 + 0x5858D4);", "before", """\
    uint32_t nfl2k5_custom_label_list_index = eax;
"""),
    ("CELEBRATION_LABEL_LIST_DONE", "sub_00369AB0", "eax = MEM32(eax * 8 + 0x5858D4);", "after", """\
    if (nfl2k5_custom_label_list_index >= 37u) { extern uint32_t nfl2k5_celebration_label(uint32_t); eax = nfl2k5_celebration_label(nfl2k5_custom_label_list_index); }
"""),
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
    # sub_002FA9C0: the same test/cmp join before a je at 0x002FAD42. The
    # fallback never branched, so the loop kept the wrong index (SOFTDRINK
    # Play Now crashed on a freed object right after, 2026-10-08).
    ("JOIN_2FAD42_TEST", "sub_002FA9C0", "goto loc_002FAD42;", "before", """    if (TEST_Z(_fa, _fb)) goto loc_002FAD46;
    goto loc_002FAD44;
"""),
    ("JOIN_2FAD42_CMP", "sub_002FA9C0", "loc_002FAD42: ;", "after", """    if (CMP_EQ(_fa, _fb)) goto loc_002FAD46;
"""),
    ("JOIN_2F7CA4_TEST", "sub_002F7C50", "goto loc_002F7CA4;", "before", """\
    if (TEST_Z(_fa, _fb)) goto loc_002F7CE3;
    goto loc_002F7CA6;
"""),
    ("JOIN_2F7CA4_CMP", "sub_002F7C50", "loc_002F7CA4: ;", "after", """\
    if (CMP_EQ(_fa, _fb)) goto loc_002F7CE3;
"""),
    # Press Start/front-end path (2026-10-06): both predecessors have already
    # populated _fa/_fb (test eax,eax or cmp edi,eax), so the joined `jne`
    # can use the preserved operands directly.  The generic fallback always
    # returned false and sent the title state machine down the wrong path.
    ("JOIN_3D4CFB_NE", "sub_003D4C02", "if (RECOMP_FLAGS_FALLBACK(0x003D4CFBu, _flags) /* jne: not equal / not zero */) goto loc_003D4D01;", "before", """\
    if (CMP_NE(_fa, _fb)) goto loc_003D4D01;
    goto loc_003D4CFD;
"""),
    # Defensive bound for the title's UTF-16 strlen helper.  A malformed
    # front-end/save-library string currently has no terminator and otherwise
    # monopolises the guest CPU forever immediately after Press Start.  Log
    # the guest pointer/caller so the producer can be corrected separately.
    ("WSTRLEN_BOUND_INIT", "sub_00030A60", "loc_00030A60: ;", "after", """\
    uint32_t nfl2k5_wstrlen_start = ecx, nfl2k5_wstrlen_count = 0;
"""),
    ("WSTRLEN_BOUND_LOOP", "sub_00030A60", "loc_00030A70: ;", "after", """\
    if (++nfl2k5_wstrlen_count >= 0x10000u) {
        extern int __cdecl fprintf(void *, const char *, ...);
        extern void *__cdecl __acrt_iob_func(unsigned);
        fprintf(__acrt_iob_func(2), "  [WSTRLEN] unterminated ptr=%08X caller=%08X; capped at %u chars\\n",
                nfl2k5_wstrlen_start, MEM32(esp), nfl2k5_wstrlen_count);
        eax = nfl2k5_wstrlen_start + nfl2k5_wstrlen_count * 2u;
        goto loc_00030A79;
    }
"""),
    # SD2K28 Press Start: sub_0016AC40 asks this provider for a 48-byte
    # front-end record, then interprets dword +0x24 as a UTF-16 name pointer.
    # Log the provider and returned record only for that exact caller so we
    # can compare its layout with the working retail PC build without spam.
    ("DIAG_RECORD_3A960", "sub_0003A960", "loc_0003A9BE: ;", "after", """\
    if (MEM32(esp + 8) == 0x0016ACC2u) {
        static int nfl2k5_record_logs;
        if (nfl2k5_record_logs++ < 40) {
            extern int __cdecl fprintf(void *, const char *, ...);
            extern void *__cdecl __acrt_iob_func(unsigned);
            uint32_t nfl2k5_record_base = MEM32(ebx + 0x1A0);
            uint32_t nfl2k5_record_src = nfl2k5_record_base + edx * 48u;
            fprintf(__acrt_iob_func(2),
                    "  [RECORD3A960] provider=%08X count=%u base=%08X index=%u src=%08X dst=%08X "
                    "words=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X\\n",
                    ebx, MEM32(ebx + 0x19C), nfl2k5_record_base, edx,
                    nfl2k5_record_src, eax,
                    MEM32(nfl2k5_record_src + 0x00), MEM32(nfl2k5_record_src + 0x04),
                    MEM32(nfl2k5_record_src + 0x08), MEM32(nfl2k5_record_src + 0x0C),
                    MEM32(nfl2k5_record_src + 0x10), MEM32(nfl2k5_record_src + 0x14),
                    MEM32(nfl2k5_record_src + 0x18), MEM32(nfl2k5_record_src + 0x1C),
                    MEM32(nfl2k5_record_src + 0x20), MEM32(nfl2k5_record_src + 0x24),
                    MEM32(nfl2k5_record_src + 0x28), MEM32(nfl2k5_record_src + 0x2C));
        }
    }
"""),
    # Save list capacity (docs/SAVES-UNLIMITED-20261021.md). The Load/Save
    # screens copy each save of the hard disk into a static table of 256
    # 24-byte rows (0xBDC1C8..0xBDD9C8) plus a name buffer, and the stock
    # code treats 256 saves as "Save Game Limit Reached" (sub_0016B610) and
    # zeroes the disk's free space (sub_0004C8E0) -- which reads as "not
    # enough free blocks". 255 rows keeps every one of those inert while
    # listing as many saves as the table holds. (It was 15 while the fill
    # pass stopped at the first save it could not open; the save library's
    # repair pass and SAVESCAN_SKIP_BAD below removed that.)
    ("SAVE_ENUM_CAP_255", "sub_0003A950", "eax = MEM32(ecx + 0x19C);", "after", """\
    if (ecx == 0x00A7DF38u && eax > 255u) {
        static int nfl2k5_save_cap_logged;
        if (!nfl2k5_save_cap_logged++) {
            extern int __cdecl fprintf(void *, const char *, ...);
            extern void *__cdecl __acrt_iob_func(unsigned);
            fprintf(__acrt_iob_func(2),
                    "  [SAVE-LIST] %u saves: the Load/Save list holds 255 (the game's 256-row table)\\n", eax);
        }
        eax = 255u;
    }
"""),
    # The save list's fill pass opens every save by name and stopped at the
    # first one that failed, leaving the count at the directory total and
    # every later row unfilled (garbage on the Load screen). Skip that save
    # instead: one fewer row, the next save fills this one.
    ("SAVESCAN_SKIP_BAD", "sub_0004C8E0", "if (TEST_Z(_fa, _fb)) goto loc_0004CB7E; /* je: equal / zero */", "before", """\
    if (eax == 0) {
        { extern void nfl2k5_savescan_skipped(uint32_t); nfl2k5_savescan_skipped(MEM32(esp + 0x10)); }
        MEM32(esp + 0x10) = MEM32(esp + 0x10) - 1;
        ecx = MEM32(esp + 0xC);
        eax = esp + 0x90;
        PUSH32(esp, eax);
        PUSH32(esp, ecx);
        g_ebp = ebp;
        g_seh_ebp = ebp;
        PUSH32(esp, 0x0004CB72u); RECOMP_ABI_CALL(0x00379536u, sub_00379536); /* XFindNextSaveGame */
        if (eax != 0) goto loc_0004CA35;
        goto loc_0004CB7A;
    }
"""),
    # The save list's name buffer: 8 KB is 256 names of 15 characters. 32 KB
    # holds 255 names of 63 characters without running past it.
    ("SAVE_NAMES_32K", "sub_0016B060", "edx = 0x2000;", "after", """\
    edx = 0x8000;
"""),
    # Free blocks vs. what all of the game's save types need together (the
    # "Your Xbox doesn't have enough free blocks" check at boot): logged.
    ("SAVE_SPACE_LOG", "sub_0016B610", "loc_0016B679: ;", "after", """\
    { extern void nfl2k5_save_space_log(uint32_t, uint32_t); nfl2k5_save_space_log(edi, eax); }
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
    # Player cards (2026-10-02): script op 133 shows a pregame card; the
    # context (eax after sub_000DCFA0) says which kind (+0x48) and whose.
    ("PLAYERCARD_PROBE", "sub_002604D0", "loc_002604D5: ;", "after", """\
    { extern void nfl2k5_playercard_probe(uint32_t ctx); nfl2k5_playercard_probe(eax); }
"""),
    # The after-kickoff lineup ticker (2026-10-02): sub_000FFBA0 has filled
    # its 11 lines; report the starters (src/nfl2k5_presentation.cpp).
    ("LINEUP_TICKER", "sub_000FFBA0", "loc_000FFD54: ;", "after", """\
    { extern void nfl2k5_lineup_ticker(uint32_t sp); nfl2k5_lineup_ticker(esp); }
"""),
    # sub_000FFD60 shows the ticker once it is filled; skip only the show
    # (sub_001775D0 on the ticker layout) when the package draws the lineup.
    ("LINEUP_NATIVE_HIDE", "sub_000FFD60", "loc_000FFD8B: ;", "after", """    { extern int nfl2k5_lineup_hide_native(void); if (nfl2k5_lineup_hide_native()) { esp += 4; return; } }
"""),
    # Frame interval (2026-10-03): sub_00027880 stores how many vblanks a
    # frame lasts (1 = 60 fps, 2 = 30). The game asks for 2 in its wide and
    # presentation camera modes (table at 0x9F5B4 via sub_0009F570) and while
    # loading. src/main.c nfl2k5_frame_interval can lock 60 or 30 instead.
    ("FRAME_INTERVAL", "sub_00027880", "loc_00027880: ;", "after", """\
    { extern uint32_t nfl2k5_frame_interval(uint32_t requested, uint32_t caller); ecx = nfl2k5_frame_interval(ecx, MEM32(esp)); }
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
    # Music player commands (2026-10-01): who plays / stops a music track.
    # RECOMP_MUSIC_LOG=1. Menu music kept restarting every 1-5 s.
    # Custom soundtrack (WMA) player: decoder create result and failure exit.
    ("DIAG_WMACREATE", "sub_00328130", "loc_003281FA: ;", "after", """    { extern void nfl2k5_diag_wma(const char *what, uint32_t v); nfl2k5_diag_wma("decoder create", eax); }
"""),
    ("DIAG_WMAFAIL", "sub_00328130", "loc_0032835B: ;", "after", """    { extern void nfl2k5_diag_wma(const char *what, uint32_t v); nfl2k5_diag_wma("song start failed", eax); }
"""),
    ("DIAG_WMAREADREQ", "sub_00327F70", "loc_00327F70: ;", "after", """    { extern void nfl2k5_diag_wma3(const char *what, uint32_t a, uint32_t b, uint32_t c); nfl2k5_diag_wma3("read req off/size/ctx", MEM32(esp + 8), MEM32(esp + 0xC), MEM32(esp + 4)); }
"""),
    ("DIAG_WMAREADRET", "sub_00327F70", "loc_00327F92: ;", "after", """    { extern void nfl2k5_diag_wma3(const char *what, uint32_t a, uint32_t b, uint32_t c); nfl2k5_diag_wma3("read ret bytes/first4", eax, MEM32(0xC95E20), 0); }
"""),
    ("DIAG_WMA_00327ED2", "sub_00327E20", "loc_00327ED2: ;", "after", """    { extern void nfl2k5_diag_wma3(const char *what, uint32_t a, uint32_t b, uint32_t c); nfl2k5_diag_wma3("E20 at 00327ED2 esi/esp/eax", esi, esp, eax); }
"""),
    ("DIAG_WMA_00327F06", "sub_00327E20", "loc_00327F06: ;", "after", """    { extern void nfl2k5_diag_wma3(const char *what, uint32_t a, uint32_t b, uint32_t c); nfl2k5_diag_wma3("E20 at 00327F06 esi/esp/eax", esi, esp, eax); }
"""),
    ("DIAG_WMA_00327F09", "sub_00327E20", "loc_00327F09: ;", "after", """    { extern void nfl2k5_diag_wma3(const char *what, uint32_t a, uint32_t b, uint32_t c); nfl2k5_diag_wma3("E20 at 00327F09 esi/esp/eax", esi, esp, eax); }
"""),
    ("DIAG_WMA_00327F4C", "sub_00327E20", "loc_00327F4C: ;", "after", """    { extern void nfl2k5_diag_wma3(const char *what, uint32_t a, uint32_t b, uint32_t c); nfl2k5_diag_wma3("E20 at 00327F4C esi/esp/eax", esi, esp, eax); }
"""),
    ("MEMCPY_NATIVE_145B0", "sub_000145B0", "loc_000145B0: ;", "after", """    /* MSVC memcpy (memmove semantics) run natively. Its misaligned-
     * destination paths dispatch through jump tables the disassembler could
     * not recover (LeadUpVec's unused slot 0, TrailUpVec reached with a
     * negative index), so every copy to an address that is not 4-byte
     * aligned jumped into nowhere and returned with esi and eax wrong --
     * the WMA decoder crashed on its first ring-buffer wrap (2026-10-01). */
    { uint32_t _d = MEM32(esp + 4), _s = MEM32(esp + 8), _n = MEM32(esp + 0xC);
      if (_n) memmove(XBOX_PTR(_d), XBOX_PTR(_s), _n);
      eax = _d; esp += 4; return; }
"""),
    ("MEMCPY_NATIVE_3739F0", "sub_003739F0", "loc_003739F0: ;", "after", """    /* MSVC memcpy (memmove semantics) run natively. Its misaligned-
     * destination paths dispatch through jump tables the disassembler could
     * not recover (LeadUpVec's unused slot 0, TrailUpVec reached with a
     * negative index), so every copy to an address that is not 4-byte
     * aligned jumped into nowhere and returned with esi and eax wrong --
     * the WMA decoder crashed on its first ring-buffer wrap (2026-10-01). */
    { uint32_t _d = MEM32(esp + 4), _s = MEM32(esp + 8), _n = MEM32(esp + 0xC);
      if (_n) memmove(XBOX_PTR(_d), XBOX_PTR(_s), _n);
      eax = _d; esp += 4; return; }
"""),
    ("DIAG_JUKEBOXART", "sub_0032A8A0", "loc_0032A911: ;", "after", """    { extern void nfl2k5_diag_txtr(uint32_t res, uint32_t album); nfl2k5_diag_txtr(eax, MEM32(0xCB69F4)); }
"""),
    # Local music: NOW PLAYING banner when a soundtrack song opens (with the
    # player object, whose "finished" flag R3 sets to skip), and the current
    # song cleared when the player shuts down (nfl2k5_local_music.cpp).
    ("LOCALMUSIC_OPEN2", "sub_00328130", "loc_00328176: ;", "after", """    { extern void nfl2k5_local_music_opening(uint32_t song_id, uint32_t player); nfl2k5_local_music_opening(MEM32(esp + 0x18), esi); }
"""),
    ("LOCALMUSIC_CLOSE", "sub_00327FA0", "loc_00327FA0: ;", "after", """    { extern void nfl2k5_local_music_closed(void); nfl2k5_local_music_closed(); }
"""),
    ("DIAG_SCHEDTABLE", "sub_00038CD0", "loc_00038CEB: ;", "after", """    { extern void nfl2k5_diag_sched(uint32_t idx); uint32_t _f = MEM32(esi * 8 + 0xB04D24); if (_f < 0x10000u || _f >= 0x01000000u) nfl2k5_diag_sched(esi); }
"""),
    # Franchise sim crash (2026-10-02): the random player pick in
    # sub_00204930 divides by its candidate count, which came up 0 (team
    # 0xB75A40 + n*0x13390, entries 0x60 bytes at +0x60, count +0x38) and
    # killed the game thread. Log the list and fall back to any active entry.
    # Commentary line validation (sub_001A9840 via sub_001A9A80): all 270
    # entries failed in the port (no bit 31), so random line picks were empty.
    ("DIAG_COMMVALID", "sub_001A9A80", "loc_001A9A88: ;", "after", """    { extern void nfl2k5_diag_commvalid(uint32_t entry, uint32_t ret); nfl2k5_diag_commvalid(esi, eax); }
"""),
    ("DIAG_PLAYVALID_FAIL", "sub_001A9840", "loc_001A98AF: ;", "after", """    { extern void nfl2k5_diag_playfail(uint32_t entry, uint32_t esi_, uint32_t ebp_, uint32_t ebx_, uint32_t edi_, uint32_t s10, uint32_t s14, uint32_t s18, uint32_t eax_); nfl2k5_diag_playfail(MEM32(esp + 0x1C), esi, ebp, ebx, edi, MEM32(esp + 0x10), MEM32(esp + 0x14), MEM32(esp + 0x18), eax); }
"""),
    ("DIAG_PLAYVALID_A", "sub_001A9840", "loc_001A9924: ;", "after", """    { extern void nfl2k5_diag_playstep(int which, uint32_t eax_, uint32_t esi_, uint32_t ebp_); nfl2k5_diag_playstep(1, eax, esi, ebp); }
"""),
    ("DIAG_PLAYVALID_B", "sub_001A9840", "loc_001A993A: ;", "after", """    { extern void nfl2k5_diag_playstep(int which, uint32_t eax_, uint32_t esi_, uint32_t ebp_); nfl2k5_diag_playstep(2, eax, esi, ebp); }
"""),
    ("DIAG_SLOT_9238", "sub_001A91A0", "loc_001A9238: ;", "after", """    { extern void nfl2k5_diag_slot(const char *where, uint32_t a, uint32_t b, uint32_t c, uint32_t d); nfl2k5_diag_slot("9238 icall ret/node/vt", eax, esi, MEM32(esi), MEM32(esi + 4)); }
"""),
    ("DIAG_SLOT_959C", "sub_001A91A0", "loc_001A959C: ;", "after", """    { extern void nfl2k5_diag_slot(const char *where, uint32_t a, uint32_t b, uint32_t c, uint32_t d); nfl2k5_diag_slot("959C 1A8FB0 ret/s10/s14/s18", eax, MEM32(esp + 0x10), MEM32(esp + 0x14), MEM32(esp + 0x18)); }
"""),
    ("DIAG_SLOT_95D5", "sub_001A91A0", "loc_001A95D5: ;", "after", """    { extern void nfl2k5_diag_slot(const char *where, uint32_t a, uint32_t b, uint32_t c, uint32_t d); nfl2k5_diag_slot("95D5 word/s18/ebx/-", MEM32(ebp), MEM32(esp + 0x18), ebx, 0); }
"""),
    ("DIAG_SLOT_93AE", "sub_001A91A0", "loc_001A93AE: ;", "after", """    { extern void nfl2k5_diag_slot(const char *where, uint32_t a, uint32_t b, uint32_t c, uint32_t d); nfl2k5_diag_slot("93AE fail eax/ebx/loop/edi", eax, ebx, MEM32(esp + 0x28), edi); }
"""),
    ("SIM_PICK_EMPTY", "sub_00204930", "loc_002049F2: ;", "after", """    if (!edi) { extern uint32_t nfl2k5_sim_pick_empty(uint32_t team, uint32_t want, uint32_t arg2, uint32_t mask); uint32_t _p = nfl2k5_sim_pick_empty(ebx, MEM32(esp + 0x8C), MEM32(esp + 0x90), ebp); MEM32(esp + 0x10) = _p; edi = 1; }
"""),
    ("DIAG_MUSICPLAY", "sub_0003CEA0", "loc_0003CEA0: ;", "after", """    { extern void nfl2k5_diag_music(int what, uint32_t handle_ptr, uint32_t esp_); nfl2k5_diag_music(1, ecx, esp); }
"""),
    ("DIAG_MUSICSTOP", "sub_0003CEE0", "loc_0003CEE0: ;", "after", """    { extern void nfl2k5_diag_music(int what, uint32_t handle_ptr, uint32_t esp_); nfl2k5_diag_music(0, ecx, esp); }
"""),
    # The music manager's end-of-track test (sub_00040870): log the values it
    # compared when it decides a track has finished. RECOMP_MUSIC_LOG=1.
    ("DIAG_MUSICEND", "sub_00040870", "loc_000408D8: ;", "after", """    { extern void nfl2k5_diag_music_end(uint32_t mgr, uint32_t rec, uint32_t delta, uint32_t chan); nfl2k5_diag_music_end(ebx, esi, ecx, ebp); }
"""),
    ("DIAG_MUSICFILL", "sub_00040870", "loc_000408DF: ;", "after", """    { extern void nfl2k5_diag_music_fill(uint32_t mgr, uint32_t rec, uint32_t delta, uint32_t chan); nfl2k5_diag_music_fill(ebx, esi, ecx, ebp); }
"""),
    # R3 for original-disc music is consumed on the retail player's own guest
    # frame. Stop its active stream, then use the game's real advance routine;
    # this selects a new index and lets the normal state-2 dispatcher start it.
    ("LOCALMUSIC_RETAIL_NEXT", "sub_00280620", "loc_00280620: ;", "after", """    { extern int nfl2k5_retail_music_take_next(void); if (nfl2k5_retail_music_take_next()) {
        uint32_t keep_eax = eax, keep_ecx = ecx, keep_edx = edx;
        PUSH32(esp, 0x00280620u); RECOMP_ABI_CALL(0x0027FEC0u, sub_0027FEC0);
        PUSH32(esp, 0x00280620u); RECOMP_ABI_CALL(0x0027F040u, sub_0027F040);
        eax = keep_eax; ecx = keep_ecx; edx = keep_edx;
    } }
"""),
    # Track cursor read in the music state machine (sub_0003DBC0): the play
    # cursor DirectSound returned and the limit it must be under, else the
    # track position stays 0. RECOMP_MUSIC_LOG=1.
    ("DIAG_MUSICCURSOR", "sub_0003DBC0", "loc_0003DD4F: ;", "after", """    { extern void nfl2k5_diag_music_cursor(uint32_t slot_off, uint32_t cursor, uint32_t limit); nfl2k5_diag_music_cursor(ebx, MEM32(esp + 0x84), MEM32(esp + 0x24)); }
"""),
    # MUSIC TRACK POSITION RACE (2026-10-01). Each tick the music state
    # machine zeroes a track's position (0xA6D8E0), asks DirectSound for the
    # play cursor, then stores it. The music manager reads that field from
    # another guest thread; on the single-core Xbox it practically never saw
    # the transient 0, but with host threads in parallel it saw it all the
    # time, took "cursor 0 behind the write cursor" as the end of the song and
    # skipped to the next one (menu music fast-forwarding, worse while moving
    # through menus; announcer streams cut short). Keep the old value during
    # the update and store the result once.
    ("MUSICPOS_KEEP", "sub_0003DBC0", "MEM32(ebx + 0xA6D8E0) = 0;", "before", """    uint32_t nfl2k5_pos_old = MEM32(ebx + 0xA6D8E0); int nfl2k5_pos_set = 0;
"""),
    ("MUSICPOS_RESTORE", "sub_0003DBC0", "MEM32(ebx + 0xA6D8E0) = 0;", "after", """    MEM32(ebx + 0xA6D8E0) = nfl2k5_pos_old;   /* MUSICPOS: no transient 0 */
"""),
    ("MUSICPOS_SET", "sub_0003DBC0", "MEM32(ebx + 0xA6D8E0) = eax;", "after", """    nfl2k5_pos_set = 1;
"""),
    ("MUSICPOS_FINAL", "sub_0003DBC0", "loc_0003DD62: ;", "after", """    if (!nfl2k5_pos_set) MEM32(ebx + 0xA6D8E0) = 0;   /* MUSICPOS: final value, stored once */
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
    # SOFTDRINK 2K28's menu shuffle plays through the Crib's stream player at
    # 0xC3CC10, but nothing opens that player's stream outside the Crib, so
    # every song request failed silently -- in xemu too (2026-10-08). Open it
    # the way the loading-music player is opened (sub_000F5410: edx = 0,
    # jmp sub_000CF0F0) the first time a bank is set on it. Pack builds only.
    ("PACK_MENU_MUSIC_PLAYER", "sub_000CEF80", "loc_000CEF80: ;", "after", """\
    { extern int nfl2k5_pack_build;
      if (nfl2k5_pack_build && ecx == 0x00C3CC10u && MEM32(0x00C3CC10u) == 0 && MEM32(0x00C3CC14u)) {
        uint32_t keep_eax = eax, keep_ecx = ecx, keep_edx = edx;
        edx = 0;
        PUSH32(esp, 0x000CEF80u); RECOMP_ABI_CALL(0x000CF0F0u, sub_000CF0F0);
        eax = keep_eax; ecx = keep_ecx; edx = keep_edx;
      } }
"""),
    # Controller Setup's fourth layout, Custom Control (src/nfl2k5_controller_setup.c).
    ("CONTROLLER_SETUP_NEXT", "sub_00357400", "loc_0035741D: ;", "after", """\
    { extern int nfl2k5_cs_pages(void); if (nfl2k5_cs_pages() == 4) { MEM32(0xAEA034) = MEM32(0xAEA034) >= 3u ? 0u : MEM32(0xAEA034) + 1u; g_seh_ebp = ebp; sub_00357370(); return; } }
"""),
    ("CONTROLLER_SETUP_PREV", "sub_00357450", "loc_0035746D: ;", "after", """\
    { extern int nfl2k5_cs_pages(void); if (nfl2k5_cs_pages() == 4) { MEM32(0xAEA034) = MEM32(0xAEA034) == 0u ? 3u : MEM32(0xAEA034) - 1u; g_seh_ebp = ebp; sub_00357370(); return; } }
"""),
    ("CONTROLLER_SETUP_LABELS", "sub_00357350", "loc_00357350: ;", "after", """\
    if (eax == 3u) { extern uint32_t nfl2k5_cs_labels(void); uint32_t _cs = nfl2k5_cs_labels(); if (_cs) { eax = _cs; esp += 4; return; } }
"""),
    ("CONTROLLER_SETUP_NAME", "sub_00357360", "loc_00357360: ;", "after", """\
    if (eax == 3u) { extern uint32_t nfl2k5_cs_name(void); uint32_t _cs = nfl2k5_cs_name(); if (_cs) { eax = _cs; esp += 4; return; } }
"""),
    ("CONTROLLER_SETUP_PORT_LAYOUT", "sub_00077230", "loc_00077230: ;", "after", """\
    { extern volatile long nfl2k5_cs_custom_ports; uint32_t _cs = MEM32(ecx * 0x1Cu + 0xE5FE90u);
      if (_cs > 2u || (ecx < 4u && ((nfl2k5_cs_custom_ports >> ecx) & 1))) { extern uint32_t nfl2k5_cs_port_layout(uint32_t, uint32_t, uint32_t); uint32_t _p = ecx; ecx = ecx * 0x1Cu; eax = nfl2k5_cs_port_layout(_p, _cs, MEM32(esp)); esp += 4; return; } }
"""),
    ("CONTROLLER_SETUP_APPLY", "sub_00357370", "loc_00357370: ;", "after", """\
    { extern void nfl2k5_cs_applied(uint32_t, uint32_t); nfl2k5_cs_applied(MEM32(0xAEA030), MEM32(0xAEA034)); }
"""),
    ("CONTROLLER_SETUP_ROW_COLOR", "sub_00357530", "edx = 0xFFE0E0E0u;", "after", """\
    { extern uint32_t nfl2k5_cs_row_color(uint32_t, uint32_t); edx = nfl2k5_cs_row_color(esi, edx); }
"""),
    # Team Select steps into every team: historic and added ones (src/nfl2k5_teams.c).
    ("TEAM_SELECT_HOME_NEXT", "sub_002C2240", "PUSH32(esp, 0x002C22D5u); RECOMP_ABI_CALL(0x002C18D0u, sub_002C18D0); /* call 0x002C18D0 */", "before", """\
    { extern int nfl2k5_ts_step(int, int, int); int _ts; RECOMP_REGS_OUT(); _ts = nfl2k5_ts_step(0, 1, 0); RECOMP_REGS_IN(); if (_ts) goto loc_002C22D5; }
"""),
    ("TEAM_SELECT_AWAY_NEXT", "sub_002C2240", "PUSH32(esp, 0x002C2285u); RECOMP_ABI_CALL(0x002C1910u, sub_002C1910); /* call 0x002C1910 */", "before", """\
    { extern int nfl2k5_ts_step(int, int, int); int _ts; RECOMP_REGS_OUT(); _ts = nfl2k5_ts_step(1, 1, 0); RECOMP_REGS_IN(); if (_ts) goto loc_002C2285; }
"""),
    ("TEAM_SELECT_HOME_PREV", "sub_002C2310", "PUSH32(esp, 0x002C2397u); RECOMP_ABI_CALL(0x002C18F0u, sub_002C18F0); /* call 0x002C18F0 */", "before", """\
    { extern int nfl2k5_ts_step(int, int, int); int _ts; RECOMP_REGS_OUT(); _ts = nfl2k5_ts_step(0, -1, 0); RECOMP_REGS_IN(); if (_ts) goto loc_002C2397; }
"""),
    ("TEAM_SELECT_AWAY_PREV", "sub_002C2310", "PUSH32(esp, 0x002C234Eu); RECOMP_ABI_CALL(0x002C1930u, sub_002C1930); /* call 0x002C1930 */", "before", """\
    { extern int nfl2k5_ts_step(int, int, int); int _ts; RECOMP_REGS_OUT(); _ts = nfl2k5_ts_step(1, -1, 0); RECOMP_REGS_IN(); if (_ts) goto loc_002C234E; }
"""),
    ("TEAM_SELECT_HOME_RANDOM", "sub_002C1AC0", "PUSH32(esp, 0x002C1B07u); RECOMP_ABI_CALL(0x002C18D0u, sub_002C18D0); /* call 0x002C18D0 */", "before", """\
    { extern int nfl2k5_ts_step(int, int, int); int _ts; RECOMP_REGS_OUT(); _ts = nfl2k5_ts_step(0, 1, 1); RECOMP_REGS_IN(); if (_ts) goto loc_002C1B07; }
"""),
    ("TEAM_SELECT_AWAY_RANDOM", "sub_002C1B10", "PUSH32(esp, 0x002C1B57u); RECOMP_ABI_CALL(0x002C1910u, sub_002C1910); /* call 0x002C1910 */", "before", """\
    { extern int nfl2k5_ts_step(int, int, int); int _ts; RECOMP_REGS_OUT(); _ts = nfl2k5_ts_step(1, 1, 1); RECOMP_REGS_IN(); if (_ts) goto loc_002C1B57; }
"""),
    # Asset file names: log them, and serve added teams' files (src/nfl2k5_team_files.c).
    ("ASSET_NAME_HOOK", "sub_00049090", "loc_00049090: ;", "after", """\
    { extern uint32_t nfl2k5_asset_name(uint32_t); ecx = nfl2k5_asset_name(ecx); }
"""),
    ("ASSET_HASH_HOOK", "sub_00038650", "loc_00038650: ;", "after", """\
    { extern uint32_t nfl2k5_asset_hash_name(uint32_t); ecx = nfl2k5_asset_hash_name(ecx); }
"""),
    ("ARCHIVE_EXTRA_PACK", "sub_00042450", "loc_000425DD: ;", "after", """\
    { extern void nfl2k5_archive_extend(void); RECOMP_REGS_OUT(); nfl2k5_archive_extend(); RECOMP_REGS_IN(); }
"""),
    # Added teams (codes Z?/Q?) also count as created teams, so Roster Manager and the
    # other lists of created teams show them while they are loaded (src/nfl2k5_teams.c).
    ("TEAM_FILTER_ADDED", "sub_000E2E50", "loc_000E2E50: ;", "after", """\
    { extern int nfl2k5_team_filter(uint32_t, uint32_t, uint32_t); int _tf = nfl2k5_team_filter(ecx, edx, MEM32(esp)); if (_tf >= 0) { eax = (uint32_t)_tf; esp += 4; return; } }
"""),
    # The case-sensitive name CRC (0x38600): logos.cdf's per-team art (Team Select
    # cards unif_/helm_, logo_ backgrounds) is found through it -- an added team's
    # names hash as its base team's until it has art of its own there.
    ("ASSET_HASH_HOOK_CS", "sub_00038600", "loc_00038600: ;", "after", """\
    { extern uint32_t nfl2k5_asset_hash_name(uint32_t); ecx = nfl2k5_asset_hash_name(ecx); }
"""),
    # Team colours: the front end's lookups of the 80-entry colour table at 0x4E7FE0
    # start at an added team's own entry (team.json colours; src/nfl2k5_team_files.c).
    ("TEAM_COLOR_ENTRY_1", "sub_00068D70", "esi = 0x4E7FE0;", "after", """\
    { extern uint32_t nfl2k5_team_color_entry(uint32_t); uint32_t _e = nfl2k5_team_color_entry(ebx); if (_e) { esi = _e; edi = (_e - 0x4E7FE0u) / 0x1Cu; } }
"""),
    ("TEAM_COLOR_ENTRY_2", "sub_00068DC0", "esi = 0x4E7FE0;", "after", """\
    { extern uint32_t nfl2k5_team_color_entry(uint32_t); uint32_t _e = nfl2k5_team_color_entry(ebx); if (_e) { esi = _e; edi = (_e - 0x4E7FE0u) / 0x1Cu; } }
"""),
    ("TEAM_COLOR_ENTRY_3", "sub_00068E10", "esi = 0x4E7FE0;", "after", """\
    { extern uint32_t nfl2k5_team_color_entry(uint32_t); uint32_t _e = nfl2k5_team_color_entry(ebx); if (_e) { esi = _e; edi = (_e - 0x4E7FE0u) / 0x1Cu; } }
"""),
    ("TEAM_COLOR_ENTRY_4", "sub_00068E60", "esi = 0x4E7FE0;", "after", """\
    { extern uint32_t nfl2k5_team_color_entry(uint32_t); uint32_t _e = nfl2k5_team_color_entry(ebx); if (_e) { esi = _e; edi = (_e - 0x4E7FE0u) / 0x1Cu; } }
"""),
    ("TEAM_COLOR_ENTRY_5", "sub_00068EB0", "esi = 0x4E7FE0;", "after", """\
    { extern uint32_t nfl2k5_team_color_entry(uint32_t); uint32_t _e = nfl2k5_team_color_entry(ebx); if (_e) { esi = _e; edi = (_e - 0x4E7FE0u) / 0x1Cu; } }
"""),
    ("TEAM_COLOR_ENTRY_6", "sub_00068F10", "esi = 0x4E7FE0;", "after", """\
    { extern uint32_t nfl2k5_team_color_entry(uint32_t); uint32_t _e = nfl2k5_team_color_entry(ebx); if (_e) { esi = _e; edi = (_e - 0x4E7FE0u) / 0x1Cu; } }
"""),
    ("TEAM_COLOR_ENTRY_7", "sub_0006B5B0", "esi = 0x4E7FE0;", "after", """\
    { extern uint32_t nfl2k5_team_color_entry(uint32_t); uint32_t _e = nfl2k5_team_color_entry(ebx); if (_e) { esi = _e; edi = (_e - 0x4E7FE0u) / 0x1Cu; } }
"""),
    # Front-end logos of added teams: extra slots of the logos.cdf / flipchip.cdf
    # caches -- an index past the end when the sorted CRC search misses, and the read
    # of such a slot served from mods\\teams\\<folder>\\cache (src/nfl2k5_team_files.c).
    ("CACHE_EXTRA_INDEX", "sub_00042D50", "loc_00042DDC: ;", "after", """\
    { extern int nfl2k5_cache_extra_index(uint32_t, uint32_t, uint32_t); if (nfl2k5_cache_extra_index(esi, edi, MEM32(esp + 0xC))) { POP32(esp, esi); POP32(esp, ebx); eax = 1; esp += 8; return; } }
"""),
    ("CACHE_EXTRA_READ", "sub_000442F0", "PUSH32(esp, 0x000443A3u); RECOMP_ABI_CALL(0x00048FF0u, sub_00048FF0); /* call 0x00048FF0 */", "before", """\
    { extern int nfl2k5_cache_extra_read(uint32_t, uint32_t); int _r; RECOMP_REGS_OUT(); _r = nfl2k5_cache_extra_read(esi, edx); RECOMP_REGS_IN(); if (_r) { esp += 20; eax = 1; goto loc_000443A3; } }
"""),
    # Feature-module hooks (src/nfl2k5_plugin_host.c; My NFL Eras is the one
    # module, docs/eras/MOD-PACK-DESIGN.md 6.2). Each shim is one load and a
    # branch while no module is on. sub_000A2910 is the final whistle: only the
    # state-0x0A entry callback sub_000AED90 reaches it, with ecx = home points,
    # edx = away points, and [0xE602B8] still the old state. sub_000645D0 is
    # match exit (franchise commit in modes 5-7). (docs/eras/research/RESULT-CAPTURE.md)
    ("HOOK_GAME_FINAL", "sub_000A2910", "loc_000A2910: ;", "after", """\
    { extern void nfl2k5_hook_game_final(uint32_t, uint32_t); nfl2k5_hook_game_final(ecx, edx); }
"""),
    ("HOOK_MATCH_EXIT", "sub_000645D0", "loc_000645D0: ;", "after", """\
    { extern void nfl2k5_hook_match_exit(void); nfl2k5_hook_match_exit(); }
"""),
    # After a game, Coach Match Up's msg 3 (sub_0034C1F0) pops to the Main Menu
    # 0x515660; a module may name another descriptor (Eras M4b: its office, when
    # an Eras game was launched and the office is on the FSM stack). Registers
    # are not published here: the hook reads memory only (no guest calls).
    ("HOOK_RETURN", "sub_0034C1F0", "edx = 0x515660;", "after", """\
    { extern uint32_t nfl2k5_hook_return_desc(uint32_t); edx = nfl2k5_hook_return_desc(edx); }
"""),
    # M4 review, the halftime hang (src/nfl2k5_eras_launch.c): NFL2K5_WALK_DIAG=1
    # keeps sub_000561F0's arguments per call and logs a walk that never ends.
    # Off by default: one load and a branch.
    ("WALK_DIAG_ENTER", "sub_000561F0", "loc_000561F0: ;", "after", """\
    { extern int nfl2k5_walk_diag_on; extern void nfl2k5_walk_enter(uint32_t, uint32_t, uint32_t, uint32_t); if (nfl2k5_walk_diag_on) nfl2k5_walk_enter(ecx, edx, esi, esp); }
"""),
    ("WALK_DIAG_STEP", "sub_000561F0", "loc_000561F3: ;", "after", """\
    { extern int nfl2k5_walk_diag_on; extern void nfl2k5_walk_step(uint32_t); if (nfl2k5_walk_diag_on) nfl2k5_walk_step(ecx); }
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


# Patches replaced by others: (id, function, code). Their block (marker line and
# exactly this code) is taken out again, so a renamed patch never runs twice.
RETIRED = [
    ("SAVE_ENUM_CAP_15", "sub_0003A950", """\
    if (ecx == 0x00A7DF38u && eax > 15u) {
        static int nfl2k5_save_cap_logged;
        if (!nfl2k5_save_cap_logged++) {
            extern int __cdecl fprintf(void *, const char *, ...);
            extern void *__cdecl __acrt_iob_func(unsigned);
            fprintf(__acrt_iob_func(2),
                    "  [SAVE-LIST] capped %u records to the menu's 15-row capacity\\n", eax);
        }
        eax = 15u;
    }
"""),
    ("ERAS_GAME_FINAL", "sub_000A2910", "    { extern void nfl2k5_eras_game_final(uint32_t, uint32_t); nfl2k5_eras_game_final(ecx, edx); }\n"),
    ("ERAS_MATCH_EXIT", "sub_000645D0", "    { extern void nfl2k5_eras_match_exit(void); nfl2k5_eras_match_exit(); }\n"),
    ("ERAS_RETURN", "sub_0034C1F0", "    { extern uint32_t nfl2k5_eras_return_desc(uint32_t); edx = nfl2k5_eras_return_desc(edx); }\n"),
]


def retire(files, check_only, failed):
    removed = []
    for pid, func, code in RETIRED:
        path, lines, start = find_function(files, func)
        if path is None:
            continue
        end = next((j for j in range(start + 1, len(lines)) if lines[j].rstrip("\r") == "}"), len(lines))
        hit = next((j for j in range(start, end) if marker(pid) in lines[j]), None)
        if hit is None:
            continue
        body = code.rstrip("\n").split("\n")
        if [l.rstrip("\r") for l in lines[hit + 1:hit + 1 + len(body)]] != body:
            failed.append("%s: retired patch found with other code; left in place" % pid)
            continue
        if check_only:
            failed.append("%s: retired patch still present" % pid)
            continue
        del lines[hit:hit + 1 + len(body)]
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write("\n".join(lines))
        removed.append("%s <- %s" % (pid, os.path.basename(path)))
    return removed


def main():
    check_only = "--check" in sys.argv
    files = sorted(glob.glob(os.path.join(GEN, "recomp_*.c")))
    applied, present, failed = [], [], []
    for r in retire(files, check_only, failed):
        print("[gen-patch] retired %s" % r)
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
        print("[gen-patch] FAILED %s" % msg, file=sys.stdout if LENIENT else sys.stderr)
    return 1 if failed and not LENIENT else 0


if __name__ == "__main__":
    sys.exit(main())
