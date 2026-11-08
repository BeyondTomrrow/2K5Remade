/* Offline check of src/nfl2k5_eras_playoffs.c (M14): the seeding of a results file against the real
 * 1980 field. Stubs the league API with the package's teams.csv / schedule.csv and a results.csv.
 *
 *   tools\eras\season\test_playoffs.cmd   (builds with cl and runs on logs\season\home-real)
 *
 * Expected for the real 1980 results (Wikipedia, 1980 NFL season): AFC 1 Chargers, 2 Browns, 3 Bills,
 * 4 Raiders, 5 Oilers; NFC 1 Falcons, 2 Eagles, 3 Vikings, 4 Cowboys, 5 Rams. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../../../src/nfl2k5_eras.h"
#include "../../../src/nfl2k5_eras_playoffs.h"

static ErasTeam s_t[32]; static int s_nt;
static ErasGame s_g[320]; static int s_ng, s_nreg;
static int s_played[320], s_hp[320], s_ap[320];
static char s_pkg[MAX_PATH], s_league[MAX_PATH];

int nfl2k5_eras_team_count(void) { return s_nt; }
const ErasTeam *nfl2k5_eras_team(int i) { return i >= 0 && i < s_nt ? &s_t[i] : NULL; }
int nfl2k5_eras_team_find(const char *id) { int i; for (i = 0; i < s_nt; i++) if (!strcmp(s_t[i].id, id)) return i; return -1; }
int nfl2k5_eras_game_count(void) { return s_ng; }
int nfl2k5_eras_regular_game_count(void) { return s_nreg; }
const ErasGame *nfl2k5_eras_game(int i) { return i >= 0 && i < s_ng ? &s_g[i] : NULL; }
int nfl2k5_eras_game_find(const char *id) { int i; for (i = 0; i < s_ng; i++) if (!strcmp(s_g[i].id, id)) return i; return -1; }
int nfl2k5_eras_result(int g, int *h, int *a) { if (g < 0 || g >= s_ng || !s_played[g]) return 0; if (h) *h = s_hp[g]; if (a) *a = s_ap[g]; return 1; }
int nfl2k5_eras_league_loaded(void) { return 1; }
int nfl2k5_eras_season(void) { return 1980; }
uint32_t nfl2k5_eras_league_seed(void) { return 12345u; }
const char *nfl2k5_eras_package_dir(void) { return s_pkg; }
int nfl2k5_eras_regular_complete(void) { int i; for (i = 0; i < s_nreg; i++) if (!s_played[i]) return 0; return 1; }
int nfl2k5_eras_season_complete(void) { int i; for (i = s_nreg; i < s_ng; i++) if (s_g[i].round == 4 && s_played[i]) return 1; return 0; }
int nfl2k5_eras_add_game(const ErasGame *g) { if (s_ng >= 320) return -1; s_g[s_ng] = *g; s_played[s_ng] = 0; return s_ng++; }
char *nfl2k5_eras_league_path(const char *rel, char *out, unsigned n) { snprintf(out, n, "%s\\%s", s_league, rel); return out; }
char *nfl2k5_eras_league_peek(const char *rel, char *out, unsigned n) { return nfl2k5_eras_league_path(rel, out, n); }
void nfl2k5_eras_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); printf("[ERAS] "); vprintf(fmt, ap); printf("\n"); va_end(ap); }
int nfl2k5_eras_divisions(const char *conference, const char *out[4])
{
    int i, j, n = 0;
    for (i = 0; i < s_nt && n < 4; i++) {
        if (strcmp(s_t[i].conference, conference)) continue;
        for (j = 0; j < n; j++) if (!strcmp(out[j], s_t[i].division)) break;
        if (j == n) out[n++] = s_t[i].division;
    }
    return n;
}
void nfl2k5_eras_standings(ErasRecord out[32])
{
    int i;
    memset(out, 0, sizeof(ErasRecord) * 32);
    for (i = 0; i < s_ng; i++) {
        ErasRecord *h = &out[s_g[i].home], *a = &out[s_g[i].away];
        if (!s_played[i]) continue;
        h->pf += s_hp[i]; h->pa += s_ap[i]; a->pf += s_ap[i]; a->pa += s_hp[i];
        if (s_hp[i] > s_ap[i]) { h->w++; a->l++; } else if (s_ap[i] > s_hp[i]) { a->w++; h->l++; } else { h->t++; a->t++; }
    }
}

static int split(char *line, char **f, int max) { int n = 0; char *p = line; line[strcspn(line, "\r\n")] = 0; while (n < max) { f[n++] = p; p = strchr(p, ','); if (!p) break; *p++ = 0; } return n; }

int main(int argc, char **argv)
{
    char path[MAX_PATH], line[1024], *f[20];
    FILE *fp;
    int row, i, c;
    snprintf(s_pkg, sizeof s_pkg, "%s", argc > 1 ? argv[1] : "data\\eras\\runtime\\1980");
    snprintf(s_league, sizeof s_league, "%s", argc > 2 ? argv[2] : "logs\\season\\home-real\\NFL2K5-PC\\eras\\league1");
    snprintf(path, sizeof path, "%s\\teams.csv", s_pkg);
    fp = fopen(path, "rb"); if (!fp) { printf("no %s\n", path); return 1; }
    for (row = 0; fgets(line, sizeof line, fp); row++) {
        int n = split(line, f, 20);
        if (row == 0 || n < 9) continue;
        snprintf(s_t[s_nt].id, 24, "%s", f[0]); snprintf(s_t[s_nt].city, 32, "%s", f[1]); snprintf(s_t[s_nt].name, 32, "%s", f[2]);
        snprintf(s_t[s_nt].abbr, 8, "%s", f[3]); snprintf(s_t[s_nt].conference, 4, "%s", f[4]); snprintf(s_t[s_nt].division, 16, "%s", f[5]);
        s_nt++;
    }
    fclose(fp);
    snprintf(path, sizeof path, "%s\\schedule.csv", s_pkg);
    fp = fopen(path, "rb"); if (!fp) { printf("no %s\n", path); return 1; }
    for (row = 0; fgets(line, sizeof line, fp); row++) {
        int n = split(line, f, 20);
        if (row == 0 || n < 5) continue;
        memset(&s_g[s_ng], 0, sizeof s_g[0]);
        snprintf(s_g[s_ng].id, 48, "%s", f[0]); s_g[s_ng].week = atoi(f[1]); snprintf(s_g[s_ng].date, 12, "%s", f[2]);
        s_g[s_ng].away = nfl2k5_eras_team_find(f[3]); s_g[s_ng].home = nfl2k5_eras_team_find(f[4]);
        s_ng++;
    }
    fclose(fp);
    s_nreg = s_ng;
    snprintf(path, sizeof path, "%s\\results.csv", s_league);
    fp = fopen(path, "rb"); if (!fp) { printf("no %s\n", path); return 1; }
    while (fgets(line, sizeof line, fp)) {
        int n = split(line, f, 8), g;
        if (n < 6) continue;
        g = nfl2k5_eras_game_find(f[0]);
        if (g < 0 || s_played[g]) continue;
        s_played[g] = 1; s_hp[g] = atoi(f[1]); s_ap[g] = atoi(f[2]);
    }
    fclose(fp);
    printf("%d teams, %d games, regular complete %d\n", s_nt, s_ng, nfl2k5_eras_regular_complete());
    /* remove a stale bracket so the seeding runs afresh */
    snprintf(path, sizeof path, "%s\\seeds.csv", s_league); DeleteFileA(path);
    snprintf(path, sizeof path, "%s\\playoffs.csv", s_league); DeleteFileA(path);
    for (c = 0; c < 2; c++) {
        ErasSeed pic[32];
        int n = nfl2k5_eras_playoff_picture(c, pic, 32, 4);
        for (i = 0; i < n; i++)
            printf("%s %s %d %-12s %s%s\n", c ? "NFC" : "AFC", pic[i].seed ? "seed" : "hunt", pic[i].seed, s_t[pic[i].team].name,
                   pic[i].division_winner ? "(div) " : "", pic[i].how);
    }
    nfl2k5_eras_playoffs_update();
    for (i = s_nreg; i < s_ng; i++) printf("game %s week %d label %s site '%s'\n", s_g[i].id, s_g[i].week, s_g[i].label, s_g[i].site);
    return 0;
}
