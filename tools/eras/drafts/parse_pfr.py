import sys, re, json, html
src, out, year, snap = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
t = open(src, encoding='utf-8', errors='replace').read()
m = re.search(r'<table[^>]*id="drafts".*?</table>', t, re.S)
tab = m.group(0)
rows = re.findall(r'<tr[^>]*>(.*?)</tr>', tab, re.S)
def cells(r):
    d = {}
    for m in re.finditer(r'<(t[hd])[^>]*data-stat="([a-z_0-9]+)"[^>]*>(.*?)</t[hd]>', r, re.S):
        raw = m.group(3)
        txt = html.unescape(re.sub(r'<[^>]+>', '', raw)).strip()
        d[m.group(2)] = (txt, raw)
    return d
INT = {'draft_round','draft_pick','age','year_max','all_pros_first_team','pro_bowls','years_as_primary_starter','career_av','draft_av','g'}
picks = []
for r in rows:
    c = cells(r)
    if 'draft_pick' not in c or not c['draft_pick'][0].isdigit(): continue
    p = {}
    for k in ['draft_round','draft_pick','team','player','pos','age','year_max','all_pros_first_team','pro_bowls','years_as_primary_starter','career_av','draft_av','g','college_id']:
        v = c.get(k, ('', ''))[0]
        if k in INT: v = int(v) if v.lstrip('-').isdigit() else None
        p[{'draft_round':'round','draft_pick':'pick','college_id':'college','year_max':'last_season','all_pros_first_team':'ap1','years_as_primary_starter':'years_starter','g':'games'}.get(k,k)] = v
    pm = re.search(r'href="(?:https?://[^"]*?)?(/players/[A-Z]/[A-Za-z0-9.]+\.htm)"', c['player'][1])
    p['pfr_id'] = pm.group(1).split('/')[-1][:-4] if pm else None
    p['hof'] = 'HOF' in c['player'][0]
    p['player'] = p['player'].replace('HOF','').strip()
    tm = re.search(r'/teams/([a-z]{3})/', c['team'][1]); p['team_pfr'] = tm.group(1) if tm else None
    picks.append(p)
doc = {
 "schema": "nfl_draft/v1", "season": year, "draft_year": year,
 "source": {"label": "Pro-Football-Reference draft listing",
   "live_url": f"https://www.pro-football-reference.com/years/{year}/draft.htm",
   "snapshot_url": snap, "fetched": "2026-10-16",
   "notes": ["Live site returned HTTP 403 to scripted fetch; parsed from the Wayback Machine raw (id_) snapshot.",
             "Career columns (last_season, ap1, pro_bowls, years_starter, career_av, games) are PFR's values as of the snapshot date.",
             "age = PFR 'Age' column (age at draft, as of Dec 31 of draft year per PFR)."]},
 "pick_count": len(picks), "rounds": max(p['round'] for p in picks), "picks": picks}
json.dump(doc, open(out, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
from collections import Counter
print(len(picks), Counter(p['round'] for p in picks))
for p in picks[:3]+picks[7:8]: print(p)
nums=[p['pick'] for p in picks]; print('pick seq ok', nums==list(range(1,len(nums)+1)))
