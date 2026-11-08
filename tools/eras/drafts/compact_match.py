import sys, json
src, out = sys.argv[1], sys.argv[2]
d = json.load(open(src, encoding='utf-8'))
for e in d['picks']:
    ss = e.pop('seasons', None)
    if not ss:
        continue
    pk = str(e['peak']['season'])
    e['peak']['ratings'] = ss[pk]['ratings']
    e['peak']['team'] = ss[pk]['team']; e['peak']['position'] = ss[pk]['position']
    order = list(next(iter(ss.values()))['ratings'])
    e['career_max_ratings'] = {k: max(v['ratings'][k] for v in ss.values()) for k in order}
    e['season_checks'] = {y: {"team": v['team'], "position": v['position'], "college": v['college'], "years_pro": v['years_pro'],
                              "confidence": v['confidence'], "record_index": v['record_index']} for y, v in ss.items()}
d['notes'] = ["rookie = the full record in the draft season's save (when status is rookie_rated); peak = the season with the highest overall_estimate within PFR's career span, with that season's ratings",
              "career_max_ratings = per-rating maximum over every matched season (not one record)",
              "Ratings are the community save authors' (unverified); see docs/eras/research/DRAFT-CLASSES.md"]
json.dump(d, open(out, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
