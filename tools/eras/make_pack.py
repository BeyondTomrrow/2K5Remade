"""Assemble the My NFL Eras feature pack (docs/eras/MOD-PACK-DESIGN.md 4, 5, 8).

    python -I tools/eras/make_pack.py --install [--dest DIR]     the full pack for THIS PC
    python -I tools/eras/make_pack.py --dist [--dest DIR]        the redistributable pack + zip
    python -I tools/eras/make_pack.py --check-zip FILE.zip       the redistribution check only

--install (default dest mods\\packs\\MyNFLEras) writes the shipped files, then fills generated\\
from this PC's current development folders, so the game keeps playing exactly as today:
  league\\1980\\rosters, depth\\        the community-roster ratings (never redistributed)
  generated\\office\\files\\*.iff        the office packages built from the disc
  generated\\teams\\1980\\...            the era logos and card art from the disc (+ the user's Bengals logo)
  generated\\stadiums\\<F>\\files\\       the stadium bundles (NCAA 07 + disc), hard links when on one volume
  generated\\offices\\1980s\\computer_monitor.png   crops of the user's IBM PC photo
  generated\\user\\                      the user's own art (IBM photo, Bengals logo, the narration)
  generated\\draft\\                     the draft show's files and audio (user-supplied)
An existing generated\\ file is kept (a builder run's output is never overwritten); --refresh replaces.

--dist (default dest build\\eras-pack) writes MyNFLEras-<version>\\ and MyNFLEras-<version>.zip with
only the content the redistribution checklist (MOD-PACK-DESIGN.md 8) clears: the facts of the 1980
league (teams, schedule, coaches, provenance), team.json identities, the drawn office art and
era.ini files, stadium.json definitions, presentations.ini, the build recipes, pack.json, README
and CREDITS. Everything flagged stays out and is listed in MISSING\\SUPPLY.txt. The zip is then
checked (--check-zip): no .iff/.cdf/.tmd/.glb/.max/.psu/.mp4/.mp3/.wav/.jpg, every image one of
the drawn office pictures and not byte- or pixel-equal to any disc-derived or user image of this
PC, every file listed with its sha256 in pack.json.
"""
import argparse, hashlib, json, os, shutil, sys, zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

PACK_ID = 'my-nfl-eras'
VERSION = '0.3.0'
MIN_GAME = '0.7.0'
SEASON = 1980
RETAIL_XBE = '73105b17a3161c546fea792a1c84ce37f9966a67c416f474cdbfab74b911a4a9'

MODS = os.path.join(ROOT, 'mods', 'eras')
RUNTIME = os.path.join(ROOT, 'data', 'eras', 'runtime', str(SEASON))
# the drawn office pictures (tools/eras/make-office-art.py; shapes and Windows fonts only)
DRAWN = ['offices/1980s/skin_team_logo.png', 'offices/1980s/office_atlas.png', 'offices/1980s/carpet.png',
         'offices/1980s/desk_metal.png', 'offices/1980s/wood_panel.png', 'offices/1980s/coffee_table_wood.png',
         'offices/1990s/computer_screen.png', 'offices/1990s/wall_tv_picture.png', 'offices/2010s/skin_team_logo.png',
         'offices/2020s/computer_screen.png', 'offices/2020s/wall_tv_picture.png']
FORBIDDEN = ('.iff', '.cdf', '.tmd', '.glb', '.gltf', '.max', '.psu', '.mp4', '.mp3', '.wav', '.ogg', '.jpg', '.jpeg', '.dds', '.bin')
IMAGES = ('.png', '.bmp', '.gif', '.tga', '.webp')


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def pixels_hash(p):
    try:
        from PIL import Image
        im = Image.open(p).convert('RGBA')
        return hashlib.sha256(b'%dx%d' % im.size + im.tobytes()).hexdigest()
    except Exception:
        return None


# ---- what ships ----------------------------------------------------------------------------

def shipped():
    """(pack-relative path, source path) of every redistributable file."""
    out = []
    for f in ('teams.csv', 'schedule.csv', 'coaches.csv', 'PROVENANCE.txt'):
        out.append(('league/%d/%s' % (SEASON, f), os.path.join(RUNTIME, f)))
    tdir = os.path.join(MODS, 'teams', str(SEASON))
    for tid in sorted(os.listdir(tdir)):
        p = os.path.join(tdir, tid, 'team.json')
        if os.path.isfile(p):
            out.append(('teams/%d/%s/team.json' % (SEASON, tid), p))
    odir = os.path.join(MODS, 'offices')
    for dp, dn, fn in os.walk(odir):
        dn[:] = [d for d in dn if d != 'reference']
        for f in sorted(fn):
            rel = os.path.relpath(os.path.join(dp, f), MODS).replace('\\', '/')
            if f.lower().endswith(('.ini', '.txt')) or rel in DRAWN:
                out.append((rel, os.path.join(dp, f)))
    sdir = os.path.join(MODS, 'stadiums')
    for d in sorted(os.listdir(sdir)):
        p = os.path.join(sdir, d, 'stadium.json')
        if os.path.isfile(p):
            out.append(('stadiums/%s/stadium.json' % d, p))
    out.append(('presentations.ini', os.path.join(MODS, 'presentations.ini')))
    return out


def local_only():
    """(pack-relative path, source path, what) of this PC's files that never ship."""
    out = []
    for sub in ('rosters', 'depth'):
        d = os.path.join(RUNTIME, sub)
        for f in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            out.append(('league/%d/%s/%s' % (SEASON, sub, f), os.path.join(d, f), 'community roster'))
    g = 'generated/'
    for d, rel in ((os.path.join(MODS, 'office', 'files'), 'office/files'),):
        for f in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            out.append((g + rel + '/' + f, os.path.join(d, f), 'disc'))
    tdir = os.path.join(MODS, 'teams', str(SEASON))
    for tid in sorted(os.listdir(tdir)):
        for dp, dn, fn in os.walk(os.path.join(tdir, tid)):
            for f in sorted(fn):
                src = os.path.join(dp, f)
                rel = os.path.relpath(src, os.path.join(MODS)).replace('\\', '/')
                if f == 'team.json':
                    continue
                if tid == 'bengals' and f == 'logo.png' and dp == os.path.join(tdir, tid):
                    out.append((g + 'user/bengals_1980.png', src, 'user'))
                elif tid == 'bengals' and f == 'source.jpg':
                    out.append((g + 'user/bengals_1980_source.jpg', src, 'user'))
                else:
                    out.append((g + rel, src, 'disc'))
    sdir = os.path.join(MODS, 'stadiums')
    for d in sorted(os.listdir(sdir)):
        fd = os.path.join(sdir, d, 'files')
        for f in sorted(os.listdir(fd)) if os.path.isdir(fd) else []:
            out.append((g + 'stadiums/%s/files/%s' % (d, f), os.path.join(fd, f), 'disc+ncaa'))
    cm = os.path.join(MODS, 'offices', '1980s', 'computer_monitor.png')
    if os.path.isfile(cm):
        out.append((g + 'offices/1980s/computer_monitor.png', cm, 'user photo'))
    ph = os.path.join(MODS, 'offices', '1980s', 'reference', 'ibm_pc_5150.jpg')
    if os.path.isfile(ph):
        out.append((g + 'user/ibm_pc_5150.jpg', ph, 'user photo'))
    for dp, dn, fn in os.walk(os.path.join(MODS, 'eraselect')):
        for f in sorted(fn):
            src = os.path.join(dp, f)
            out.append((g + os.path.relpath(src, MODS).replace('\\', '/'), src, 'user'))
    for dp, dn, fn in os.walk(os.path.join(MODS, 'draft')):
        for f in sorted(fn):
            src = os.path.join(dp, f)
            out.append((g + os.path.relpath(src, MODS).replace('\\', '/'), src, 'user'))
    return out


# ---- generated text ------------------------------------------------------------------------

def recipes():
    import importlib.util

    def tool(name, fn):
        spec = importlib.util.spec_from_file_location(name, os.path.join(ROOT, 'tools', 'eras', fn))
        m = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(m)
        return m
    logos = tool('eel', 'extract_era_logos.py')
    art = tool('beta', 'build_era_team_art.py')
    era_logos = {'schema': 1, 'season': SEASON, 'source': 'the disc archive logos.cdf / mini.cdf (docs/eras/research/ERA-LOGOS.md)',
                 'teams': {tid: dict(code=c, style=s, primary=p, secondary=q, **({'user_art': 'bengals_1980'} if tid == 'bengals' else {}))
                           for tid, (c, s, p, q) in sorted(logos.LOOK.items())}}
    team_art = {'schema': 1, 'season': SEASON,
                'note': 'texture content hash -> member, as the game uploads it (tools/eras/logos/hashes.json); numbers only, no pixels',
                'hashes': json.load(open(os.path.join(ROOT, 'tools', 'eras', 'logos', 'hashes.json'), encoding='utf-8')),
                'logo_1980': {k: list(v) for k, v in sorted(art.LOGO_1980.items())}, 'logo_swaps': art.LOGO_SWAPS,
                'user_logo': art.USER_LOGO, 'user_logo_art': {'bengals': 'bengals_1980'}}
    venues = []
    try:
        sys.path.insert(0, os.path.join(ROOT, 'tools', 'eras', 'stadiums'))
        import ncaa_stadiums as NS
        venues = [{'slug': s['slug'], 'folder': s['folder'], 'code': s['code'], 'name': s['name'], 'ncaa': s['ncaa'],
                   'entries': list(s['entries']), 'tool': 'ncaa_stadiums'} for s in NS.STADIUMS]
    except Exception as e:                       # numpy missing: the venue list from the stadium folders
        print('make_pack: ncaa_stadiums.py not importable (%r); venue list from stadium.json only' % e)
    venues.append({'slug': 'la_coliseum', 'folder': 'LA_Coliseum', 'code': 's60', 'name': 'LA MEMORIAL COLISEUM',
                   'tool': 'coliseum_build', 'note': 'tools/eras/stadiums/coliseum_build.py (not in the builder yet)'})
    build = {'schema': 1, 'steps': [
        {'id': 'office', 'needs': ['nfl2k5_xbox_disc'], 'tool': 'office_pack', 'out': 'office/files/'},
        {'id': 'office_1980s', 'needs': ['office', 'user_art:ibm_pc_5150'], 'tool': 'office_ibmpc', 'optional': True},
        {'id': 'logos_1980', 'needs': ['nfl2k5_xbox_disc'], 'tool': 'era_logos', 'args': {'map': 'recipes/era_logos_1980.json'}},
        {'id': 'art_1980', 'needs': ['nfl2k5_xbox_disc'], 'tool': 'era_team_art',
         'args': {'map': 'recipes/team_art_1980.json', 'user_logos': {'bengals': 'user_art:bengals_1980'}}},
        {'id': 'stadiums', 'needs': ['nfl2k5_xbox_disc', 'ncaa07_xbox_disc'], 'tool': 'ncaa_stadiums', 'optional': True,
         'per_item': 'recipes/ncaa_stadiums.json'}]}
    return {'recipes/build.json': build, 'recipes/era_logos_1980.json': era_logos,
            'recipes/team_art_1980.json': team_art, 'recipes/ncaa_stadiums.json': {'schema': 1, 'venues': venues}}


README = """My NFL Eras %(v)s
=================

A historical franchise mode for NFL2K5-PC: the real 1980 NFL season (28 teams, the real
schedule), played in a copy of Franchise mode's office.

Install
  1. Unzip this folder into the game's mods\\packs\\ folder, so that this file is
     mods\\packs\\MyNFLEras\\README.txt.
  2. Supply what the download cannot contain (MISSING\\SUPPLY.txt).
  3. Build your assets once from your own NFL 2K5 disc: run tools\\NFL2K5-ErasBuilder.exe
     from the game folder (developers: python -I tools\\eras\\build_assets.py). It writes
     generated\\ in this folder: the 3D office, the era logos and card badges, and, if you
     give it your NCAA Football 07 disc (--ncaa-dir), the historic stadiums.
  4. Start the game. "My NFL Eras" is in the Main Menu.

The clean game only: with an XBE mod pack (SOFTDRINK) selected My NFL Eras stays off;
launch with --retail to play it. --vanilla starts the game with no packs at all.
Switch it off without uninstalling: mods\\features.ini, a line  my-nfl-eras=0

Uninstall: delete this folder. League saves stay in %%LOCALAPPDATA%%\\NFL2K5-PC\\eras\\.
"""

CREDITS = """My NFL Eras - credits, sources and licences
============================================

Not affiliated with or endorsed by the NFL, its teams, 2K, Visual Concepts or EA.
Team names, cities, abbreviations and colours are used as historical facts; this pack
contains no NFL shield, team logo or wordmark files.

League facts (league\\1980\\teams.csv, schedule.csv, coaches.csv)
  Teams, divisions, the 1980 schedule, scores, head coaches and stadiums are historical
  facts. Sources: Pro-Football-Reference.com (via the Internet Archive's Wayback Machine),
  Wikipedia (facts only; no prose is copied), the NFL Record & Fact Book.
  league\\1980\\PROVENANCE.txt says where each part came from and what is unverified.

Built on your PC, never shipped
  The office, the era logos and card badges come from your own NFL 2K5 disc, the historic
  stadiums from your own NCAA Football 07 disc (the asset builder, NFL2K5-ErasBuilder).

Tools
  The asset builder ships with NFL2K5-PC and uses 2K Football Mod Tools (MIT licence,
  Noah and contributors; tools\\licenses\\LICENSE-2k-football-mod-tools.txt), numpy (BSD)
  and Pillow (HPND).

Office art
  The drawn office pictures (offices\\*\\*.png) are original work made with
  tools\\eras\\make-office-art.py (shapes and text only).

Not in this download (MISSING\\SUPPLY.txt)
  The 1980 rosters' ratings, names, bodies and faces come from community PS2 roster saves
  whose authors are not recorded; they are not redistributed without their permission.
  The IBM PC 5150 photo, the 1980 Bengals logo image and the era-select narration
  (ElevenLabs) are user-supplied and their redistribution is not cleared.
  Broadcast packages (e.g. "NFL on CBS") are never part of this pack.
"""

SUPPLY = """What this download does not contain, and how to supply it
==========================================================

My NFL Eras needs these files that cannot be redistributed. Without the first one the Main
Menu row opens a hub that says what is missing.

REQUIRED
  league\\1980\\rosters\\<team>.csv (and .flags.txt), league\\1980\\depth\\<team>.csv
      The 1980 rosters: real players (Pro-Football-Reference membership) rated from community
      PS2 roster saves. Make them from your own copy of those saves with the repository's
      tools (tools\\eras\\make_runtime_package.py --season 1980 --out <this pack>\\league\\1980),
      or copy them from a full development install.
%(missing)s
OPTIONAL (put them in generated\\user\\, then run the asset builder)
  generated\\user\\ibm_pc_5150.jpg      a photo of an IBM PC 5150: the 1980s office desk computer
                                     (without it the 1980s office keeps the retail desk)
  generated\\user\\bengals_1980.png     the 1980 Cincinnati Bengals logo (without it, the disc's)
  generated\\eraselect\\voice\\1980s.mp3 the era-select narration (without it, silent)
  NCAA Football 07 (Xbox) extracted with tools\\ncaa07-extractor: the historic stadiums
                                     (without them each team plays at its stand-in stadium)

Left out of this download (with the reason):
%(left)s
"""


def pack_json(files):
    return {
        'id': PACK_ID, 'kind': 'feature', 'name': 'My NFL Eras', 'display_name': 'My NFL Eras', 'version': VERSION,
        'author': 'Matthew', 'description': 'Historical franchise mode: the real 1980 NFL season, schedule and teams.',
        'homepage': '', 'manifest_version': 1, 'data_schema': 1, 'min_game_version': MIN_GAME, 'plugin_api': '1.0',
        'compatible_bases': [{'name': 'ESPN NFL 2K5 (retail)', 'xbe_sha256': RETAIL_XBE}],
        'module': {'builtin': 'eras', 'dll': None, 'dll_sha256': None, 'signature': None},
        'league': {'packages': [{'season': SEASON, 'dir': 'league/%d' % SEASON, 'teams': 28, 'games': 224}],
                   'default_season': SEASON},
        'assets': {'builder_min_version': '1.0.0', 'recipes': 'recipes/build.json',
                   'required_inputs': ['nfl2k5_xbox_disc'], 'optional_inputs': ['ncaa07_xbox_disc', 'user_art']},
        'labels': {'rosters': 'unverified community roster'},
        'files': [{'path': p, 'sha256': h} for p, h in sorted(files.items())],
        'credits': 'CREDITS.txt', 'licence': 'data: see CREDITS.txt; code ships with NFL2K5-PC'}


def put(dest, rel, src=None, data=None, link=False, keep=False):
    p = os.path.join(dest, rel.replace('/', os.sep))
    if keep and os.path.exists(p):
        return 'kept'
    os.makedirs(os.path.dirname(p), exist_ok=True)
    if data is not None:
        with open(p + '.part', 'wb') as f:
            f.write(data)
        os.replace(p + '.part', p)
        return 'written'
    if os.path.exists(p):
        os.remove(p)
    if link:
        try:
            os.link(src, p)
            return 'linked'
        except OSError:
            pass
    shutil.copy2(src, p)
    return 'copied'


def text_files(missing_lines, left_lines):
    return {'README.txt': README % {'v': VERSION},
            'CREDITS.txt': CREDITS,
            'MISSING/SUPPLY.txt': SUPPLY % {'missing': missing_lines, 'left': left_lines}}


def assemble(dest, dist, refresh=False):
    files, counts = {}, {}
    for rel, src in shipped():
        put(dest, rel, src)
        files[rel] = sha256(src)
    for rel, obj in recipes().items():
        data = (json.dumps(obj, indent=1, sort_keys=True) + '\n').encode('utf-8')
        put(dest, rel, data=data)
        files[rel] = hashlib.sha256(data).hexdigest()
    lo = local_only()
    left = {}
    for rel, src, what in lo:
        left.setdefault(what, []).append(rel)
    left_lines = '\n'.join('  %-12s %4d files, e.g. %s' % (w, len(v), v[0]) for w, v in sorted(left.items()))
    missing = '' if not dist else ''
    for name, t in text_files(missing, left_lines).items():
        data = t.replace('\n', '\r\n').encode('utf-8')
        if name.startswith('MISSING/') and not dist:
            continue
        put(dest, name, data=data)
        files[name] = hashlib.sha256(data).hexdigest()
    if not dist:
        for rel, src, what in lo:
            keep = rel.startswith('generated/') and not refresh
            if keep:
                # M16: a builder run's output is kept, but a file this PC's development folders hold
                # newer and different (the draft / office files copied by hand) replaces a stale one
                p = os.path.join(dest, rel.replace('/', os.sep))
                if os.path.exists(p) and os.path.getmtime(src) > os.path.getmtime(p) + 1 and \
                        (os.path.getsize(src) != os.path.getsize(p) or sha256(src) != sha256(p)):
                    keep = False
            r = put(dest, rel, src, link=os.path.getsize(src) > (1 << 20), keep=keep)
            counts[r] = counts.get(r, 0) + 1
        put(dest, 'generated/build.json', data=(json.dumps({
            'builder': 'tools/eras/make_pack.py --install', 'builder_version': 'dev', 'schema': 1, 'complete': True,
            'note': 'assembled from this PC\'s development folders (mods\\eras, data\\eras\\runtime)',
            'steps': {}}, indent=1) + '\n').encode('utf-8'), keep=not refresh)
    pj = (json.dumps(pack_json(files), indent=1) + '\n').encode('utf-8')
    put(dest, 'pack.json', data=pj)
    return files, counts, lo


# ---- the redistribution check ----------------------------------------------------------------

def forbidden_images():
    """sha256 and pixel hashes of every disc-derived or user image on this PC."""
    shas, pix, n = set(), set(), 0
    roots = [os.path.join(MODS, 'teams'), os.path.join(MODS, 'office'), os.path.join(MODS, 'stadiums'),
             os.path.join(MODS, 'offices', '1980s', 'reference'), os.path.join(MODS, 'eraselect'),
             os.path.join(MODS, 'draft'), os.path.join(ROOT, 'data', 'eras', 'stadiums'),
             os.path.join(ROOT, 'mods', 'packs', 'MyNFLEras', 'generated')]
    single = [os.path.join(MODS, 'offices', '1980s', 'computer_monitor.png')]
    for r in roots:
        for dp, dn, fn in os.walk(r):
            for f in fn:
                single.append(os.path.join(dp, f))
    for p in single:
        # the identity and definition files beside the art (team.json, stadium.json, ...) ship
        if not os.path.isfile(p) or p.lower().endswith(('.json', '.ini', '.txt', '.csv', '.md')):
            continue
        shas.add(sha256(p))
        if p.lower().endswith(IMAGES + ('.jpg', '.jpeg')) and os.path.getsize(p) < (32 << 20):
            h = pixels_hash(p)
            if h:
                pix.add(h)
        n += 1
    return shas, pix, n


def check_zip(path):
    import tempfile
    bad = []
    shas, pix, n = forbidden_images()
    with zipfile.ZipFile(path) as z:
        names = [i.filename for i in z.infolist() if not i.is_dir()]
        prefix = os.path.commonprefix(names).split('/')[0] + '/' if names else ''
        pj = json.loads(z.read(prefix + 'pack.json'))
        listed = {f['path']: f['sha256'] for f in pj['files']}
        tmp = tempfile.mkdtemp(prefix='eras-zipcheck-')
        try:
            images = 0
            for name in names:
                rel = name[len(prefix):]
                data = z.read(name)
                h = hashlib.sha256(data).hexdigest()
                low = rel.lower()
                if low.endswith(FORBIDDEN):
                    bad.append('%s: a %s file (retail/NCAA data or unlicensed media)' % (rel, os.path.splitext(low)[1]))
                if h in shas:
                    bad.append('%s: byte-equal to a disc-derived or user file of this PC' % rel)
                if low.endswith(IMAGES):
                    images += 1
                    if rel not in DRAWN:
                        bad.append('%s: an image that is not one of the drawn office pictures' % rel)
                    p = os.path.join(tmp, 'x' + os.path.splitext(low)[1])
                    open(p, 'wb').write(data)
                    ph = pixels_hash(p)
                    if ph in pix:
                        bad.append('%s: pixel-equal to a disc-derived or user image of this PC' % rel)
                if rel != 'pack.json':
                    if rel not in listed:
                        bad.append('%s: not listed in pack.json files' % rel)
                    elif listed[rel] != h:
                        bad.append('%s: sha256 differs from pack.json' % rel)
            for rel in listed:
                if prefix + rel not in names:
                    bad.append('%s: listed in pack.json but not in the zip' % rel)
            for need in ('CREDITS.txt', 'README.txt', 'MISSING/SUPPLY.txt'):
                if prefix + need not in names:
                    bad.append('%s missing' % need)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    print('check %s: %d files, %d images (all drawn office art), compared with %d disc-derived/user files of this PC'
          % (os.path.basename(path), len(names), images, n))
    for b in bad:
        print('  FAIL ' + b)
    print('redistribution check: %s' % ('PASS' if not bad else 'FAIL (%d)' % len(bad)))
    return 0 if not bad else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--install', action='store_true')
    g.add_argument('--dist', action='store_true')
    g.add_argument('--check-zip')
    ap.add_argument('--dest')
    ap.add_argument('--refresh', action='store_true', help='--install: replace generated\\ files too')
    a = ap.parse_args()
    if a.check_zip:
        return check_zip(a.check_zip)
    if a.install:
        dest = os.path.abspath(a.dest or os.path.join(ROOT, 'mods', 'packs', 'MyNFLEras'))
        files, counts, lo = assemble(dest, dist=False, refresh=a.refresh)
        print('installed %s %s at %s: %d shipped files, %d local-only (%s)' % (PACK_ID, VERSION, dest, len(files), len(lo),
              ', '.join('%d %s' % (v, k) for k, v in sorted(counts.items()))))
        return 0
    base = os.path.abspath(a.dest or os.path.join(ROOT, 'build', 'eras-pack'))
    folder = os.path.join(base, 'MyNFLEras-%s' % VERSION, 'MyNFLEras')
    if os.path.exists(os.path.dirname(folder)):
        shutil.rmtree(os.path.dirname(folder))
    files, counts, lo = assemble(folder, dist=True)
    zp = os.path.join(base, 'MyNFLEras-%s.zip' % VERSION)
    with zipfile.ZipFile(zp + '.part', 'w', zipfile.ZIP_DEFLATED) as z:
        for dp, dn, fn in os.walk(folder):
            dn.sort()
            for f in sorted(fn):
                p = os.path.join(dp, f)
                z.write(p, 'MyNFLEras/' + os.path.relpath(p, folder).replace('\\', '/'))
    os.replace(zp + '.part', zp)
    print('dist: %s (%d files, %d bytes); %d local-only files left out (MISSING\\SUPPLY.txt)' %
          (zp, len(files) + 1, os.path.getsize(zp), len(lo)))
    return check_zip(zp)


if __name__ == '__main__':
    sys.exit(main())
