"""My NFL Eras first-run asset builder (docs/eras/MOD-PACK-DESIGN.md 7): the assets that
need retail pixels or models, built on the player's PC from the player's own discs into the
pack's generated\\ folder. Nothing it writes is ever part of the download.

    python -I tools/eras/build_assets.py [--pack DIR] [--out DIR] [--disc DIR]
                                         [--steps office,office_1980s,logos_1980,art_1980,stadiums]
                                         [--ncaa-dir DIR] [--force] [--check]

    --pack DIR    the installed pack (default mods\\packs\\MyNFLEras); its recipes\\ are read
    --out DIR     where to write (default <pack>\\generated)
    --disc DIR    the extracted NFL 2K5 disc (default: tools/eras/eras_settings.py, original\\disc)
    --steps       the steps to run (default: every step of recipes\\build.json that is not optional,
                  plus the optional ones whose inputs are there; "all" = every step)
    --ncaa-dir    the NCAA Football 07 extractor work folder for the optional stadiums step
    --force       rebuild steps whose outputs are already recorded in build.json
    --check       compare what would be built with the files in --out (no writes)

Packaged as tools\\NFL2K5-ErasBuilder.exe (tools/build-eras-builder.ps1, PyInstaller) so players
never need Python; developers run this script.

Steps (recipes\\build.json):
  office        erasoffice.iff / erasskin.iff: byte copies of four SCNE/LAYT records of fr.iff and
                of fr31.iff from the disc archive (tools/eras/build-office-pack.py)      -> office\\files\\
  office_1980s  (optional) the IBM PC 5150 desk (coach_d80s) and its monitor atlas; needs the
                player's photo generated\\user\\ibm_pc_5150.jpg and 2K Football Mod Tools
                (tools/eras/build-office-ibmpc.py)          -> office\\files\\erasoffice.iff, offices\\1980s\\
  logos_1980    each team's 1980 logo (scorebug 512, mini 128) from the disc's logos.cdf / mini.cdf
                (recipes\\era_logos_1980.json; tools/eras/extract_era_logos.py)  -> teams\\1980\\<id>\\logos\\
  art_1980      the card chips and style logos swapped while an Eras game runs, keyed by the game's
                texture hash (recipes\\team_art_1980.json; tools/eras/build_era_team_art.py) -> teams\\1980\\art\\
  stadiums      (optional, long, resumable per venue) the historic stadium bundles from NCAA
                Football 07 (tools/eras/stadiums/ncaa_stadiums.py)           -> stadiums\\<folder>\\files\\

Every output is written to a temporary name and renamed when complete, so an interrupted build
never leaves a half-written file; build.json records the builder version, the input hashes and
every output's hash, and a step whose outputs are all present and unchanged is skipped (resume).
Progress goes to logs\\eras-build.log and <out>\\build-status.txt (the game's hub polls it).
The builder never writes outside --out and logs\\, and never touches the disc.
"""
import argparse, hashlib, importlib.util, io, json, os, shutil, struct, sys, tempfile, time, traceback

BUILDER_VERSION = '1.0.0'
HERE = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, 'frozen', False) else __file__))
FROZEN = getattr(sys, 'frozen', False)
# the tools this builder drives: beside the script, or inside the PyInstaller bundle
TOOLS = os.path.join(getattr(sys, '_MEIPASS', ''), 'eras_tools') if FROZEN else HERE
ROOT = os.path.abspath(os.path.join(HERE, '..', '..')) if not FROZEN else os.path.abspath(os.path.join(HERE, '..'))
for p in (TOOLS, os.path.join(TOOLS, 'logos'), os.path.join(ROOT, 'tools', 'team-creator', 'vendor'),
          os.path.join(TOOLS, 'vendor')):
    if os.path.isdir(p) and p not in sys.path:
        sys.path.insert(0, p)

LOG = None
STATUS = None


def log(msg):
    line = '%s %s' % (time.strftime('%H:%M:%S'), msg)
    print(line, flush=True)
    if LOG:
        with open(LOG, 'a', encoding='utf-8') as f:
            f.write(line + '\n')


def status(msg):
    if STATUS:
        tmp = STATUS + '.tmp'
        with open(tmp, 'w', encoding='utf-8') as f:
            f.write(msg + '\n')
        os.replace(tmp, STATUS)


def sha256_file(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def load_tool(name, filename):
    spec = importlib.util.spec_from_file_location(name, os.path.join(TOOLS, filename))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Writer:
    """Outputs of one step: written as <name>.part, renamed when the step's file is complete."""

    def __init__(self, out, check):
        self.out, self.check, self.files = out, check, {}

    def path(self, rel):
        return os.path.join(self.out, rel.replace('/', os.sep))

    def write(self, rel, data):
        h = hashlib.sha256(data).hexdigest()
        self.files[rel.replace('\\', '/')] = h
        p = self.path(rel)
        if self.check:
            have = sha256_file(p) if os.path.exists(p) else None
            log('  %s %s' % ('same   ' if have == h else 'MISSING' if have is None else 'DIFFERS', rel))
            return
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p + '.part', 'wb') as f:
            f.write(data)
        os.replace(p + '.part', p)

    def png(self, rel, img):
        b = io.BytesIO()
        img.save(b, format='PNG')
        self.write(rel, b.getvalue())


# ---- the disc --------------------------------------------------------------------------

def game_root():
    """The game folder: the working directory when it is one (the exe is run from the game
    folder or started by the game), else the repository / install root beside the tools."""
    cwd = os.getcwd()
    if os.path.isdir(os.path.join(cwd, 'mods')) and os.path.isdir(os.path.join(cwd, 'original')):
        return cwd
    return ROOT


def find_disc(arg):
    if arg:
        d = arg
    else:
        try:
            import eras_settings
            d = eras_settings.nfl2k5_disc()
        except Exception:
            d = None
        d = str(d) if d and not FROZEN else os.path.join(game_root(), 'original', 'disc')
    if not os.path.isdir(os.path.join(d, 'vc_53450030')):
        raise SystemExit('the NFL 2K5 disc is not at %s (it needs vc_53450030\\; give --disc)' % d)
    return d


def disc_inputs(disc):
    xbe = os.path.join(disc, 'default.xbe')
    return {'nfl2k5_default_xbe_sha256': sha256_file(xbe) if os.path.exists(xbe) else None,
            'nfl2k5_archive_bank0_size': os.path.getsize(os.path.join(disc, 'vc_53450030', '0'))}


def office_tool(disc):
    bop = load_tool('eras_build_office_pack', 'build-office-pack.py')
    bop.DISC = os.path.join(disc, 'vc_53450030')
    return bop


# ---- steps -----------------------------------------------------------------------------

def step_office(ctx, w):
    bop = office_tool(ctx['disc'])
    fr = bop.archive_file('fr.iff')
    skin = bop.archive_file('fr31.iff')
    office = b''.join(bop.pad16(r) for r in bop.pick(fr))
    w.write('office/files/erasoffice.iff', office)
    w.write('office/files/erasskin.iff', skin)
    log('  office: erasoffice.iff %d bytes (coach_desk, front_office SCNE + LAYT), erasskin.iff %d bytes' % (len(office), len(skin)))


def step_office_1980s(ctx, w):
    photo = os.path.join(ctx['out'], 'user', 'ibm_pc_5150.jpg')
    if not os.path.exists(photo):
        log('  office_1980s: skipped (no generated\\user\\ibm_pc_5150.jpg: the 1980s office keeps the retail desk)')
        return 'skipped'
    pack = w.path('office/files/erasoffice.iff')
    if not os.path.exists(pack) and not ctx['check']:
        log('  office_1980s: skipped (the office step has not run)')
        return 'skipped'
    ibm = load_tool('eras_build_office_ibmpc', 'build-office-ibmpc.py')
    tmp = tempfile.mkdtemp(prefix='eras-ibm-')
    try:
        shutil.copyfile(pack if os.path.exists(pack) else '', os.path.join(tmp, 'erasoffice.iff'))
        ibm.PACK = os.path.join(tmp, 'erasoffice.iff')
        ibm.ERA = os.path.join(tmp, '1980s')
        ibm.PHOTO = photo
        if ibm.main() != 0:
            log('  office_1980s: skipped (build-office-ibmpc.py could not run: see above)')
            return 'skipped'
        w.write('office/files/erasoffice.iff', open(ibm.PACK, 'rb').read())
        w.write('offices/1980s/computer_monitor.png', open(os.path.join(ibm.ERA, 'computer_monitor.png'), 'rb').read())
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def decode_cdf(bop, base):
    """logos.iff + logos.cdf (or mini.*) of the disc archive -> {member name: PIL RGBA image}."""
    import nfl_txtr as T
    from PIL import Image
    ix = bop.archive_file(base + '.iff')
    count, slot = struct.unpack_from('<II', ix, 0x60)
    data = bop.archive_file(base + '.cdf')
    out = {}
    for i in range(count):
        s = data[i * slot:(i + 1) * slot]
        if s[:4] != b'TXTR':
            continue
        c = T.parse_chunks(s, allow_trailing=True)[0]
        raw, _ = T.decode_chunk(s, c)
        t = T.parse_texture(raw, c)
        rgba = T.texture_to_rgba(raw, c, t)
        out[T.safe_texture_name(t.name, 's%d' % i)] = Image.frombytes('RGBA', (t.width, t.height), rgba)
    return out


def cdf_images(ctx):
    if 'cdf' not in ctx:
        bop = office_tool(ctx['disc'])
        ctx['cdf'] = {'logos': decode_cdf(bop, 'logos'), 'mini': decode_cdf(bop, 'mini')}
        log('  disc logos: %d in logos.cdf, %d in mini.cdf' % (len(ctx['cdf']['logos']), len(ctx['cdf']['mini'])))
    return ctx['cdf']


def recipe(ctx, name):
    return json.load(open(os.path.join(ctx['pack'], 'recipes', name), encoding='utf-8'))


def user_art(ctx, name):
    for ext in ('.png', '.jpg', '.jpeg'):
        p = os.path.join(ctx['out'], 'user', name + ext)
        if os.path.exists(p):
            return p
    return None


def step_logos(ctx, w):
    from PIL import Image
    ex = load_tool('eras_extract_era_logos', 'extract_era_logos.py')
    r = recipe(ctx, 'era_logos_1980.json')
    imgs = cdf_images(ctx)
    for tid, e in sorted(r['teams'].items()):
        code, style = e['code'], e['style']
        user = user_art(ctx, e['user_art']) if e.get('user_art') else None
        if user:
            w.png('teams/1980/%s/logos/scorebug.png' % tid, ex.square(Image.open(user)))
            note = 'user art %s' % os.path.basename(user)
        else:
            src = imgs['logos'].get('logo_%s_%d' % (code, style))
            if src is None:
                log('  logos_1980: %s: logo_%s_%d not on the disc' % (tid, code, style)); continue
            w.png('teams/1980/%s/logos/scorebug.png' % tid, ex.square(src))
            note = 'disc logo_%s_%d' % (code, style)
        m = imgs['mini'].get('logo_s%s_%d' % (code, style))
        if m is not None:
            w.png('teams/1980/%s/logos/mini.png' % tid, ex.square(m, 128))
        ctx.setdefault('notes', {})[tid] = note
    log('  logos_1980: %d teams' % len(r['teams']))


def step_art(ctx, w):
    from PIL import Image
    art = load_tool('eras_build_era_team_art', 'build_era_team_art.py')
    r = recipe(ctx, 'team_art_1980.json')
    hashes = r['hashes']
    imgs = cdf_images(ctx)
    n = 0

    def chip_hash(code):
        hs = sorted({v[0] for k, v in hashes.items() if k.startswith(code + '_flipchip_')})
        assert len(hs) == 1, (code, hs)
        return hs[0]

    def put(h, what, img, size, margin):
        nonlocal n
        img = img.convert('RGBA')
        img = art.fit(img, size, margin) if margin is not None else img.resize((size, size), Image.LANCZOS)
        w.png('teams/1980/art/%s_%s.png' % (h, what), img)
        n += 1

    for tid, (code, style) in sorted(r['logo_1980'].items()):
        if style is None:
            p = user_art(ctx, r['user_logo_art'].get(tid, ''))
            if not p:
                log('  art_1980: %s chip: no user art (%s); the disc chip stays' % (tid, r['user_logo_art'].get(tid)))
                continue
            src = Image.open(p)
        else:
            src = imgs['logos'].get('logo_%s_%d' % (code, style))
            if src is None:
                continue
        put(chip_hash(code), '%s_chip' % tid, src, 128, 0.04)
    for member, source in sorted(r['logo_swaps'].items()):
        src = imgs['logos' if member.startswith('logo_') and not member.startswith('logo_s') else 'mini'].get(source)
        if src is not None:
            put(hashes[member][0], member, src, hashes[member][1], None)
    for tid, members in sorted(r['user_logo'].items()):
        p = user_art(ctx, r['user_logo_art'].get(tid, ''))
        for member in members:
            if p:
                put(hashes[member][0], member, Image.open(p), hashes[member][1] * 2, 0.04)
    log('  art_1980: %d textures' % n)


def step_stadiums(ctx, w):
    """Per venue: ncaa_stadiums.py build + pack into a scratch folder, then the nine bundles
    into stadiums\\<folder>\\files\\. A venue whose bundles are all recorded is skipped."""
    ncaa = ctx.get('ncaa_dir')
    try:
        import eras_settings
        ncaa = ncaa or (str(eras_settings.ncaa07_work()) if eras_settings.ncaa07_work() else None)
    except Exception:
        pass
    if not ncaa or not os.path.isdir(os.path.join(ncaa, 'stadiums_raw')):
        log('  stadiums: skipped (no NCAA Football 07 extractor folder with stadiums_raw\\: give --ncaa-dir); '
            'Eras games use each team\'s stand-in stadium')
        return 'skipped'
    sys.path.insert(0, os.path.join(TOOLS, 'stadiums'))
    import ncaa_stadiums as NS
    NS.N.set_work(ncaa)
    venues = recipe(ctx, 'ncaa_stadiums.json')['venues']
    scratch = tempfile.mkdtemp(prefix='eras-stadiums-')
    done = ctx['prev'].get('steps', {}).get('stadiums', {}).get('venues', {})
    ctx['venues'] = dict(done)
    try:
        NS.DATA = type(NS.DATA)(scratch)
        NS.MODS = type(NS.MODS)(os.path.join(scratch, 'installed'))
        for v in venues:
            st = NS.BY_SLUG.get(v['slug'])
            folder = v['folder']
            if st is None:
                log('  stadiums: %s not in this builder' % v['slug']); continue
            rec = done.get(folder)
            if rec and not ctx['force'] and all(os.path.exists(w.path('stadiums/%s/files/%s' % (folder, f))) and
                                                 sha256_file(w.path('stadiums/%s/files/%s' % (folder, f))) == h
                                                 for f, h in rec.items()):
                log('  stadiums: %s already built' % folder)
                for f, h in rec.items():
                    w.files['stadiums/%s/files/%s' % (folder, f)] = h
                continue
            status('Building historic stadiums: %s' % st['name'])
            log('  stadiums: %s (%s) ...' % (st['name'], st['code']))
            NS.build(st, None, log=lambda s: log('    ' + s))
            NS.pack(st, log=lambda s: log('    ' + s))
            NS.install(st, log=lambda s: log('    ' + s))
            src = os.path.join(scratch, 'installed', folder, 'files')
            rec = {}
            for f in sorted(os.listdir(src)):
                data = open(os.path.join(src, f), 'rb').read()
                w.write('stadiums/%s/files/%s' % (folder, f), data)
                rec[f] = hashlib.sha256(data).hexdigest()
            ctx['venues'][folder] = rec
            write_record(ctx, w, partial=True)       # resume point after every venue
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


STEPS = {'office_pack': step_office, 'office_ibmpc': step_office_1980s, 'era_logos': step_logos,
         'era_team_art': step_art, 'ncaa_stadiums': step_stadiums}


def write_record(ctx, w, partial=False):
    if ctx['check']:
        return
    rec = {'builder': 'NFL2K5-ErasBuilder', 'builder_version': BUILDER_VERSION, 'schema': 1,
           'time': time.strftime('%Y-%m-%dT%H:%M:%S'), 'inputs': ctx['inputs'],
           'recipe_sha256': ctx['recipe_sha'], 'complete': not partial, 'steps': ctx['record']}
    if 'venues' in ctx:
        rec['steps'].setdefault('stadiums', {})['venues'] = ctx['venues']
    os.makedirs(ctx['out'], exist_ok=True)
    p = os.path.join(ctx['out'], 'build.json')
    with open(p + '.part', 'w', encoding='utf-8') as f:
        json.dump(rec, f, indent=1)
    os.replace(p + '.part', p)


def main(argv=None):
    global LOG, STATUS
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--pack', default=None)
    ap.add_argument('--out', default=None)
    ap.add_argument('--disc', default=None)
    ap.add_argument('--steps', default='')
    ap.add_argument('--ncaa-dir', default=None)
    ap.add_argument('--force', action='store_true')
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args(argv)
    game = game_root()
    pack = os.path.abspath(a.pack or os.path.join(game, 'mods', 'packs', 'MyNFLEras'))
    out = os.path.abspath(a.out or os.path.join(pack, 'generated'))
    plan_p = os.path.join(pack, 'recipes', 'build.json')
    if not os.path.exists(plan_p):
        raise SystemExit('%s: no recipes\\build.json (is My NFL Eras installed there?)' % pack)
    os.makedirs(os.path.join(game, 'logs'), exist_ok=True)
    LOG = os.path.join(game, 'logs', 'eras-build.log')
    if not a.check:
        os.makedirs(out, exist_ok=True)
        STATUS = os.path.join(out, 'build-status.txt')
    plan = json.load(open(plan_p, encoding='utf-8'))
    disc = find_disc(a.disc)
    prev = {}
    if os.path.exists(os.path.join(out, 'build.json')):
        try:
            prev = json.load(open(os.path.join(out, 'build.json'), encoding='utf-8'))
        except ValueError:
            prev = {}
    ctx = {'pack': pack, 'out': out, 'disc': disc, 'check': a.check, 'force': a.force, 'ncaa_dir': a.ncaa_dir,
           'inputs': disc_inputs(disc), 'recipe_sha': sha256_file(plan_p), 'prev': prev,
           'record': dict(prev.get('steps', {})) if prev.get('builder_version') == BUILDER_VERSION else {}}
    want = [s.strip() for s in a.steps.split(',') if s.strip()]
    log('ErasBuilder %s: pack %s -> %s, disc %s%s' % (BUILDER_VERSION, pack, out, disc, ' (check only)' if a.check else ''))
    failed = 0
    for step in plan['steps']:
        sid = step['id']
        if want and 'all' not in want and sid not in want:
            continue
        if not want and step.get('optional') and sid == 'stadiums':
            continue                                    # long: only when asked for
        old = ctx['record'].get(sid)
        if old and not a.force and not a.check and old.get('inputs') == ctx['inputs'] and old.get('files') and \
                all(os.path.exists(os.path.join(out, f.replace('/', os.sep))) and
                    sha256_file(os.path.join(out, f.replace('/', os.sep))) == h for f, h in old['files'].items()):
            log('%s: up to date (%d files)' % (sid, len(old['files'])))
            continue
        status('Building %s ...' % sid)
        log('%s:' % sid)
        w = Writer(out, a.check)
        try:
            r = STEPS[step['tool']](ctx, w)
        except Exception as e:
            traceback.print_exc()
            log('%s: FAILED %r' % (sid, e))
            failed += 1
            continue
        ctx['record'][sid] = {'inputs': ctx['inputs'], 'files': w.files, 'result': r or 'built'}
        write_record(ctx, w, partial=True)
    write_record(ctx, None)
    status('Done%s' % (' with %d failed step(s): see logs\\eras-build.log' % failed if failed else ': restart the game to use the new assets'))
    log('done%s' % (', %d step(s) failed' % failed if failed else ''))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
