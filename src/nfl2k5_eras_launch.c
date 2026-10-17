/* My NFL Eras: launching a league game and coming back (docs/eras/PHASE2-PLAN.md M4).
 *
 * "Play Next Game" (League screen row 0, src/nfl2k5_eras_menu.c) runs the
 * retail Quick Game plumbing from our own screen ("Option A",
 * docs/eras/research/MATCH-LAUNCH.md section 5):
 *
 *   1. refuse, touching nothing, when USER1/USER2 holds a Create-a-Team team;
 *   2. snapshot the user's settings: the 0x2E0-byte block 0xE5FF80..0xE60260,
 *      stadium / home / away / last home / last away / playbooks
 *      (0xE5FE64..0xE5FE7C), uniform slots 0xE60210/14 and the port side words
 *      0xE5FE88 + port * 0x1C;
 *   3. stage home -> side 0 (USER1), away -> side 1 (USER2) (src/nfl2k5_teams.c, M2);
 *   4. mode [0xE5FF80] = 4 (exhibition: no franchise commit at match exit),
 *      First Person Football [0xE5FFE4] = 0;
 *   5. every port to side 0 (CPU), except the port that pressed A, which gets
 *      1 (home) or 2 (away) when the league has a user team in this game;
 *   6. the teams through the individual setters, as Franchise does
 *      (sub_0015DB50), not set_home (its Team Select texture lookup):
 *      sub_00077AE0 home, sub_00077B20 away, the home stadium sub_00077470,
 *      uniform slots sub_000E3000 / sub_000E30E0 from team +0x192. The home
 *      stadium is the era's (teams.csv stadium, resolved by name among the
 *      roster's stadium records) when it is found, else the template's;
 *   7. arm the result capture (src/nfl2k5_eras_capture.c, M1) for the fixture,
 *      mark it "launched" in league.ini, then sub_0034CA40 and push the Coach
 *      Match Up prep state 0x55D088: the flow-1 arm of Team Select's 0x2C0E70.
 *
 * The user presses Start Game in the retail Coach Match Up; the rest is retail.
 * The result is written at the final whistle (M1). After End Of Game -> Exit,
 * Coach Match Up's msg 3 pops to the Main Menu (0x34C1F0); the gen patch
 * ERAS_RETURN (M4b) makes that the League screen instead when it is on the
 * FSM stack. The clean-up -- commit (log) the result, release both slots,
 * restore the snapshot -- runs from the front-end FSM pop as soon as the
 * League screen, the hub or the Main Menu is on top again, after 0x55D088's
 * msg 2 has put [0xE5FE68]/[0xE5FE6C] back on the slots, and again
 * (idempotently) from the League and hub screens' msg 3. Backing out of Coach
 * Match Up, or quitting from the pause menu, ends the same way with the
 * fixture left unplayed.
 *
 * NFL2K5_ERAS_RETURN=0 turns the M4b redirect off (the M4a Main Menu return). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "nfl2k5_plugin_api.h"
#include "nfl2k5_eras.h"
#include "nfl2k5_eras_capture.h"
#include "nfl2k5_eras_launch.h"
#include "nfl2k5_teams.h"
#include "nfl2k5_eras_depth.h"

extern uint32_t nfl2k5_eras_menu_header(int which);   /* 0 hub, 1 the league screen (the office, M3-office) */
extern void nfl2k5_eras_menu_game_rows(int eras);
/* M9 (src/nfl2k5_presentation.cpp): the scorebug's Eras identity and package for a launched game */

#define FSM             0x00A84B18u
#define MAIN_MENU       0x00515660u
#define PREP_STATE      0x0055D088u   /* Coach Match Up prep (no screen); its msg 3 pushes Coach Match Up */
#define SETTINGS        0x00E5FF80u   /* the saved settings block, 0x2E0 bytes */
#define SETTINGS_SIZE   0x2E0u
#define GAME_MODE       0x00E5FF80u
#define FPF_FLAG        0x00E5FFE4u
#define SEL_STADIUM     0x00E5FE64u   /* stadium, home, away, last home, last away, home / away playbook */
#define UNIFORMS        0x00E60210u   /* home / away uniform slot */
#define PORT_SIDE       0x00E5FE88u   /* + port * 0x1C: 1 home, 2 away, 0 neither (CPU) */
#define PORTS           8
#define T_STADIUM       0x114u
#define T_UNIFORM       0x192u

static struct {
    int active;                     /* launched, not cleaned up */
    int game;                       /* fixture index */
    int port;                       /* the port that pressed A */
    uint8_t block[SETTINGS_SIZE];
    uint32_t sel[7], uniforms[2], sides[PORTS];
} s_l;
static int s_test_used;          /* NFL2K5_ERAS_TEST_USER_TEAM applied (once per process) */

static uint32_t gcall(void (*fn)(void), uint32_t ecx, uint32_t edx)
{
    g_ecx = ecx; g_edx = edx;
    PUSH32(g_esp, 0x0BADC0DEu);
    fn();
    return g_eax;
}

static void snapshot_take(void)
{
    uint32_t i;
    for (i = 0; i < SETTINGS_SIZE; i++) s_l.block[i] = MEM8(SETTINGS + i);
    for (i = 0; i < 7; i++) s_l.sel[i] = MEM32(SEL_STADIUM + 4u * i);
    for (i = 0; i < 2; i++) s_l.uniforms[i] = MEM32(UNIFORMS + 4u * i);
    for (i = 0; i < PORTS; i++) s_l.sides[i] = MEM32(PORT_SIDE + 0x1Cu * i);
}

static void snapshot_restore(void)
{
    uint32_t i;
    for (i = 0; i < SETTINGS_SIZE; i++) MEM8(SETTINGS + i) = s_l.block[i];
    for (i = 0; i < 7; i++) MEM32(SEL_STADIUM + 4u * i) = s_l.sel[i];
    for (i = 0; i < 2; i++) MEM32(UNIFORMS + 4u * i) = s_l.uniforms[i];
    for (i = 0; i < PORTS; i++) MEM32(PORT_SIDE + 0x1Cu * i) = s_l.sides[i];
}

/* What the restore is about to roll back: the byte ranges of the settings
 * block that differ from the snapshot (mode and First Person Football are the
 * launch's own), logged so a field the match writes would show. */
static void block_diff(void)
{
    char line[512];
    size_t o = 0;
    uint32_t i = 0, ranges = 0;
    line[0] = 0;
    while (i < SETTINGS_SIZE) {
        uint32_t j;
        if (MEM8(SETTINGS + i) == s_l.block[i]) { i++; continue; }
        for (j = i; j < SETTINGS_SIZE && MEM8(SETTINGS + j) != s_l.block[j]; j++) {}
        ranges++;
        if (o + 48 < sizeof line)
            o += (size_t)snprintf(line + o, sizeof line - o, " %08X..%08X (%08X -> %08X)", SETTINGS + i, SETTINGS + j - 1,
                                  MEM32(SETTINGS + (i & ~3u)), *(const uint32_t *)(s_l.block + (i & ~3u)));
        i = j;
    }
    nfl2k5_eras_log("clean-up: %u changed range(s) in the settings block, restored:%s", ranges, ranges ? line : " none");
}

/* M13: the package's roster, or the league's rosters\<season>\<id>.csv (the draft's rookies) when
 * NFL2K5_ERAS_ROSTER_SEASON names a later season (nfl2k5_eras_roster_path). */
static void roster_path(const ErasTeam *t, char *out, size_t n)
{
    int team = nfl2k5_eras_team_find(t->id);
    if (nfl2k5_eras_roster_path(team, out, n))
        nfl2k5_eras_log("roster of %s for season %d missing: %s", t->id, nfl2k5_eras_roster_season(), out);
}

int nfl2k5_eras_launch_active(void) { return s_l.active; }

int nfl2k5_eras_launch(uint32_t fsm, char *msg, size_t n)
{
    const ErasGame *e;
    const ErasTeam *home, *away;
    char roster[2][MAX_PATH], box[MAX_PATH], rel[96], csv[MAX_PATH];
    uint32_t slot[2], stadium;
    int g, s, rc, u, user_side = 0, stadium_id = -1;
    msg[0] = 0;
    if (s_l.active) { snprintf(msg, n, "A league game is still in progress"); return -1; }
    if (nfl2k5_eras_depth_editing()) { snprintf(msg, n, "A depth chart is still open"); return -1; }
    if (!nfl2k5_eras_league_loaded()) { snprintf(msg, n, "No league loaded"); return -1; }
    nfl2k5_eras_refresh();
    g = nfl2k5_eras_next_game();
    if (g < 0) { snprintf(msg, n, "Season complete"); return -1; }
    e = nfl2k5_eras_game(g);
    home = nfl2k5_eras_team(e->home);
    away = nfl2k5_eras_team(e->away);

    /* 1. Refuse before anything is touched. NFL2K5_ERAS_TEST_USER_TEAM=1|2 (test
     * only, T5.6) makes an empty USER1/USER2 slot look like a Create-a-Team team
     * with one player for the length of the check. */
    for (s = 0; s < 2; s++) {
        const char *t = getenv("NFL2K5_ERAS_TEST_USER_TEAM");
        uint32_t fake = 0;
        if (t && atoi(t) == s + 1 && !s_test_used) {
            s_test_used = 1;   /* the first Play Next Game of the process only */
            fake = g_eras_svc->spare_slot(s);
            if (fake && MEM32(fake + 0x128u) == 2 && MEM8(fake + 0x11Cu) == 0) MEM8(fake + 0x11Cu) = 1;
            else fake = 0;
        }
        rc = g_eras_svc->stage_check(s);
        if (fake) {
            MEM8(fake + 0x11Cu) = 0;
            nfl2k5_eras_log("test: USER%d slot %08X shown as a one-player Create-a-Team team for the check (NFL2K5_ERAS_TEST_USER_TEAM)",
                            s + 1, fake);
        }
        if (rc != NFL2K5_STAGE_OK) {
            /* short: the row clips at about 40 characters */
            snprintf(msg, n, rc == NFL2K5_STAGE_USER_TEAM ? "Can't play: Create-a-Team in USER%d"
                             : "Can't play: USER%d slot not ready", s + 1);
            nfl2k5_eras_log("launch %s refused: USER%d: %s (nothing changed)", e->id, s + 1, g_eras_svc->stage_message(rc));
            return -2;
        }
    }
    roster_path(home, roster[0], sizeof roster[0]);
    roster_path(away, roster[1], sizeof roster[1]);

    /* 2-3. Snapshot, then stage. */
    snapshot_take();
    for (s = 0; s < 2; s++) {
        const ErasTeam *t = s ? away : home;
        rc = g_eras_svc->stage(s, t->template_entry, roster[s], t->city, t->name, t->abbr);
        if (rc != NFL2K5_STAGE_OK) {
            g_eras_svc->release(0);
            g_eras_svc->release(1);
            snapshot_restore();
            snprintf(msg, n, "Can't play: %s not loaded", t->name);
            nfl2k5_eras_log("launch %s refused: staging %s (USER%d) failed: %s; slots released, settings restored",
                            e->id, t->id, s + 1, g_eras_svc->stage_message(rc));
            return -3;
        }
        slot[s] = g_eras_svc->staged_slot(s);
        /* M9: the season's uniform style before the uniform slots are set (step 6) */
        nfl2k5_eras_apply_style(slot[s], s ? e->away : e->home);
        /* M6: the league's depth chart (else the community roster's), and the 1980 head coach */
        nfl2k5_eras_depth_apply(s, s ? e->away : e->home);
        nfl2k5_eras_coach_apply(s, s ? e->away : e->home, nfl2k5_eras_team_game_number(s ? e->away : e->home, g));
    }

    /* 4. Exhibition mode. */
    MEM32(GAME_MODE) = 4;
    MEM32(FPF_FLAG) = 0;

    /* 5. Sides: CPU everywhere, the pressing port on the user team's side. */
    s_l.port = (int)gcall(sub_000F3580, fsm, 0);
    if (s_l.port < 0 || s_l.port >= PORTS) s_l.port = 0;
    u = nfl2k5_eras_user_team();
    if (u >= 0 && u == e->home) user_side = 1;
    else if (u >= 0 && u == e->away) user_side = 2;
    for (s = 0; s < PORTS; s++) gcall(sub_00077200, (uint32_t)s, s == s_l.port ? (uint32_t)user_side : 0u);

    /* 6. Teams, stadium, uniforms. */
    stadium = home->stadium[0] ? g_eras_svc->stadium_find(home->stadium, &stadium_id) : 0;
    if (!stadium && home->stadium[0]) stadium = g_eras_svc->stadium_borrow(home->stadium, &stadium_id);
    if (stadium) MEM32(slot[0] + T_STADIUM) = stadium;
    else if (home->stadium[0]) nfl2k5_eras_log("launch %s: stadium '%s' not found among the roster's stadiums; the template's is used",
                                               e->id, home->stadium);
    gcall(sub_00077AE0, slot[0], 0);
    gcall(sub_00077B20, slot[1], 0);
    gcall(sub_00077470, MEM32(slot[0] + T_STADIUM), 0);
    gcall(sub_000E3000, MEM8(slot[0] + T_UNIFORM), 0);
    gcall(sub_000E30E0, MEM8(slot[1] + T_UNIFORM), 0);

    /* 7. Arm, mark, push. */
    snprintf(rel, sizeof rel, "box\\%s.json", e->id);
    nfl2k5_eras_league_path(rel, box, sizeof box);
    nfl2k5_eras_league_path("results.csv", csv, sizeof csv);
    nfl2k5_eras_capture_arm(e->id, box, csv);
    nfl2k5_eras_capture_set_roster_source("unverified community roster");
    nfl2k5_eras_set_launched(e->id);
    s_l.game = g;
    s_l.active = 1;
    nfl2k5_eras_menu_game_rows(1);
    /* M9: era card badges / logos on, and the scorebug's Eras identity and package */
    nfl2k5_eras_team_art_update();
    g_eras_svc->pres_eras_begin(NFL2K5_ERAS_SEASON, e->week, home->id, away->id);
    nfl2k5_eras_log("launch %s (week %d): %s %s (USER1 %08X, home) vs %s %s (USER2 %08X, away); "
                    "mode 4; port %d side %d (%s); stadium %s (record %08X id %d); uniforms %u/%u; "
                    "unverified community rosters",
                    e->id, e->week, home->city, home->name, slot[0], away->city, away->name, slot[1],
                    s_l.port, user_side, user_side ? "user team" : "CPU vs CPU",
                    stadium ? home->stadium : "(template)", MEM32(slot[0] + T_STADIUM), stadium_id,
                    MEM8(slot[0] + T_UNIFORM), MEM8(slot[1] + T_UNIFORM));
    gcall(sub_0034CA40, 0, 0);
    gcall(sub_0006E390, fsm, PREP_STATE);
    return 0;
}

/* A home / away / last home / last away word from before the launch (or a
 * depth chart edit) can be the USER1/USER2 slot itself (a historic or added
 * team picked in Quick Game, which the staging replaced). That slot is empty
 * now: the word gets a club instead, or Team Select would open on a team
 * without players. */
void nfl2k5_eras_repoint_released(uint32_t slot0, uint32_t slot1)
{
    int k;
    for (k = 0; k < 4; k++) {
        uint32_t a = SEL_STADIUM + 4u + 4u * (uint32_t)k, v = MEM32(a);
        if (v && (v == slot0 || v == slot1)) {
            uint32_t club = g_eras_svc->resident(k & 1);
            if (club == MEM32(SEL_STADIUM + 4u + 4u * (uint32_t)(k ^ 1))) club = g_eras_svc->resident(!(k & 1));
            if (club) MEM32(a) = club;
            nfl2k5_eras_log("clean-up: [%08X] was %08X, a released slot -> %08X (ident %u)",
                            a, v, MEM32(a), club ? MEM16(club + 0x118u) : 0u);
        }
    }
}

void nfl2k5_eras_after_game(const char *why)
{
    Nfl2k5ErasResult r;
    const ErasGame *e;
    uint32_t slot[2];
    int k;
    if (!s_l.active) {
        const char *l = nfl2k5_eras_launched();
        if (l && *l) {
            /* the save says a game was launched, but this process never launched
             * one: the last session ended between launch and clean-up */
            int g = nfl2k5_eras_game_find(l), hp, ap;
            char id[48];
            snprintf(id, sizeof id, "%s", l);
            nfl2k5_eras_refresh();
            nfl2k5_eras_log("launch %s was not cleaned up in the last session (crash or exit during the game); %s",
                            id, g >= 0 && nfl2k5_eras_result(g, &hp, &ap) == NFL2K5_ERAS_PLAYED
                                ? "its result is in results.csv" : "no result was written: the fixture stays unplayed");
            nfl2k5_eras_set_launched("");
        }
        return;
    }
    s_l.active = 0;
    nfl2k5_eras_team_art_update();       /* M9: retail art again */
    g_eras_svc->pres_eras_end();
    e = nfl2k5_eras_game(s_l.game);
    nfl2k5_eras_capture_result(&r);
    if (r.status == NFL2K5_ERAS_CAPTURE_FINAL && !strcmp(r.game_id, e->id)) {
        for (k = 0; k < 300 && r.written == 0; k++) { Sleep(10); nfl2k5_eras_capture_result(&r); }
        nfl2k5_eras_log("committed %s: %s %d at %s %d, period %d%s, end state 0x%02X, mode %d; box score %s",
                        e->id, nfl2k5_eras_team(e->away)->name, r.away_points, nfl2k5_eras_team(e->home)->name,
                        r.home_points, r.final_period, r.final_period >= 5 ? " (overtime)" : "", r.end_state, r.mode,
                        r.written > 0 ? "and results row written" : r.written < 0 ? "NOT WRITTEN (see stderr)" : "still being written");
    } else if (r.status == NFL2K5_ERAS_CAPTURE_ABANDONED) {
        nfl2k5_eras_log("abandoned %s: the game was quit before the final whistle (period %d); the fixture stays unplayed",
                        e->id, r.final_period);
    } else {
        if (r.status == NFL2K5_ERAS_CAPTURE_ARMED) nfl2k5_eras_capture_disarm();
        nfl2k5_eras_log("launch %s ended with no game played (capture status %d); the fixture stays unplayed",
                        e->id, r.status);
    }
    for (k = 0; k < 2; k++) slot[k] = g_eras_svc->staged_slot(k);
    nfl2k5_eras_menu_game_rows(0);
    block_diff();
    nfl2k5_eras_coach_restore(0);
    nfl2k5_eras_coach_restore(1);
    g_eras_svc->release(0);
    g_eras_svc->release(1);
    g_eras_svc->stadium_restore();
    snapshot_restore();
    nfl2k5_eras_repoint_released(slot[0], slot[1]);
    nfl2k5_eras_set_launched("");
    nfl2k5_eras_refresh();
    {
        int next = nfl2k5_eras_next_game();
        nfl2k5_eras_log("clean-up (%s): both slots released, settings restored (mode %u, quarter %u min, home %08X away %08X); "
                        "%d of %d games played, next %s",
                        why, MEM32(GAME_MODE), MEM32(0xE6000Cu), MEM32(SEL_STADIUM + 4u), MEM32(SEL_STADIUM + 8u),
                        nfl2k5_eras_games_played(), nfl2k5_eras_game_count(),
                        next >= 0 ? nfl2k5_eras_game(next)->id : "(season complete)");
    }
}

static int on_stack(uint32_t desc)
{
    uint32_t depth = MEM32(FSM + 0x100u), i;
    if (!desc || depth >= 0x20u) return 0;
    for (i = 0; i <= depth; i++)
        if (MEM32(FSM + 8u * i) == desc) return 1;
    return 0;
}

void nfl2k5_eras_fsm_pop(uint32_t fsm, uint32_t top)
{
    if (fsm != FSM) return;
    if (nfl2k5_eras_depth_editing() && (top == MAIN_MENU || top == nfl2k5_eras_menu_header(1) || top == nfl2k5_eras_menu_header(0)))
        nfl2k5_eras_depth_edit_end("left the depth chart");
    if (!s_l.active) return;
    if (top == MAIN_MENU) nfl2k5_eras_after_game("Main Menu on top");
    else if (top == nfl2k5_eras_menu_header(1)) nfl2k5_eras_after_game("office on top");
    else if (top == nfl2k5_eras_menu_header(0)) nfl2k5_eras_after_game("hub on top");
}

uint32_t nfl2k5_eras_return_desc(uint32_t edx)
{
    static int on = -1;
    uint32_t league = nfl2k5_eras_menu_header(1);
    if (on < 0) { const char *v = getenv("NFL2K5_ERAS_RETURN"); on = !(v && *v == '0'); }
    if (!s_l.active || edx != MAIN_MENU) return edx;
    if (!on) { nfl2k5_eras_log("return: NFL2K5_ERAS_RETURN=0, to the Main Menu (M4a)"); return edx; }
    if (!on_stack(league)) {
        nfl2k5_eras_log("return: the office %08X is not on the FSM stack; to the Main Menu", league);
        return edx;
    }
    nfl2k5_eras_log("return: to the office %08X instead of the Main Menu", league);
    /* the office's room, freed for the match, is loaded again at the office's msg 3
     * (this runs inside generated code without the registers published: memory only) */
    return league;
}
