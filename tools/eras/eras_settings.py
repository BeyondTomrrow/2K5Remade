"""Local settings for the My NFL Eras tools: where things are on this PC.

Nothing here is hard-coded to one machine. Each setting is found, in order:
  1. the environment variable;
  2. the settings file (first that exists): $NFL2K5_ERAS_SETTINGS,
     <repo>\\eras-tools.ini (gitignored), %LOCALAPPDATA%\\NFL2K5-PC\\eras-tools.ini;
     an INI file with plain `key = value` lines;
  3. discovery (listed per setting).

  ncaa07_work   (NFL2K5_NCAA07_WORK)  the NCAA Football 07 extractor work folder: the one holding
                stadiums_raw\\<entry>.tmd (tools/ncaa07-extractor). Discovery: <drive>:\\NCAA07_Extractor_Work
                on every drive, <repo>\\build\\ncaa07, %USERPROFILE%\\NCAA07_Extractor_Work.
  nfl2k5_disc   (NFL2K5_DISC)         the extracted NFL 2K5 Xbox disc (default.xbe + vc_53450030\\).
                Discovery: <repo>\\original\\disc, then the game folder's original\\disc.

    python -I tools/eras/eras_settings.py        print what is found
"""
import os
import string
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
_cache = None


def _files():
    out = []
    if os.environ.get('NFL2K5_ERAS_SETTINGS'):
        out.append(Path(os.environ['NFL2K5_ERAS_SETTINGS']))
    out.append(ROOT / 'eras-tools.ini')
    if os.environ.get('LOCALAPPDATA'):
        out.append(Path(os.environ['LOCALAPPDATA']) / 'NFL2K5-PC' / 'eras-tools.ini')
    return out


def _ini():
    global _cache
    if _cache is None:
        _cache = {}
        for p in _files():
            if p.is_file():
                for line in p.read_text(encoding='utf-8', errors='replace').splitlines():
                    line = line.split(';', 1)[0].split('#', 1)[0]
                    if '=' in line and not line.strip().startswith('['):
                        k, v = line.split('=', 1)
                        _cache.setdefault(k.strip().lower(), v.strip().strip('"'))
                break
    return _cache


def _get(key, env, candidates, test):
    v = os.environ.get(env) or _ini().get(key)
    if v:
        return Path(v)
    for c in candidates():
        try:
            if test(c):
                return c
        except OSError:
            pass
    return None


def ncaa07_work():
    """The NCAA 07 extractor work folder, or None."""
    def cands():
        yield ROOT / 'build' / 'ncaa07'
        for d in string.ascii_uppercase:
            p = Path('%s:\\NCAA07_Extractor_Work' % d)
            yield p
        if os.environ.get('USERPROFILE'):
            yield Path(os.environ['USERPROFILE']) / 'NCAA07_Extractor_Work'
    return _get('ncaa07_work', 'NFL2K5_NCAA07_WORK', cands, lambda p: (p / 'stadiums_raw').is_dir())


def nfl2k5_disc():
    """The extracted NFL 2K5 disc folder, or None."""
    def cands():
        yield ROOT / 'original' / 'disc'
    return _get('nfl2k5_disc', 'NFL2K5_DISC', cands,
                lambda p: (p / 'default.xbe').is_file() and (p / 'vc_53450030').is_dir())


if __name__ == '__main__':
    print('settings files :', ', '.join(str(p) for p in _files()))
    print('ncaa07_work    :', ncaa07_work() or '(not found: set NFL2K5_NCAA07_WORK or ncaa07_work in eras-tools.ini)')
    print('nfl2k5_disc    :', nfl2k5_disc() or '(not found: set NFL2K5_DISC)')
