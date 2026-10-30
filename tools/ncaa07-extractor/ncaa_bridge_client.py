"""Send models to the Blender that is already open (blender_addon/ncaa07_bridge).

The add-on writes %LOCALAPPDATA%\\NCAA07Bridge\\alive.json every second while
Blender runs and imports any request dropped into ...\\NCAA07Bridge\\inbox.
If no Blender is listening, one is started with the add-on loaded, once;
every later request goes to that same window.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time
import uuid
from pathlib import Path

ROOT = Path(os.environ.get('LOCALAPPDATA', str(Path.home()))) / 'NCAA07Bridge'
HERE = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent))
ADDON = HERE / 'blender_addon' / 'ncaa07_bridge'
DEFAULT_BLENDER = Path(r'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe')


def find_blender(preferred: Path | None = None) -> Path | None:
    for candidate in (preferred, DEFAULT_BLENDER):
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    found = shutil.which('blender')
    if found:
        return Path(found)
    base = Path(r'C:\Program Files\Blender Foundation')
    if base.is_dir():
        exes = sorted(base.glob('Blender*/blender.exe'), reverse=True)
        if exes:
            return exes[0]
    return None


def alive(max_age: float = 4.0) -> dict | None:
    try:
        info = json.loads((ROOT / 'alive.json').read_text())
        if time.time() - info.get('time', 0) <= max_age:
            return info
    except (OSError, ValueError):
        pass
    return None


def _launch(blender: Path, log=print):
    log('Starting Blender with the NCAA 07 bridge (only this once)...')
    subprocess.Popen([str(blender), '--python', str(ADDON / '__init__.py')],
                     creationflags=getattr(subprocess, 'DETACHED_PROCESS', 0))
    for _ in range(120):
        time.sleep(0.5)
        if alive():
            return True
    raise RuntimeError('Blender started but the NCAA 07 bridge did not answer within 60 s.')


def send(request: dict, blender: Path | None = None, wait: float = 120.0, log=print) -> dict:
    """Queue `request` for the open Blender (starting one if needed) and wait for its result."""
    if not alive():
        exe = find_blender(blender)
        if not exe:
            raise FileNotFoundError('Blender was not found. Install Blender or choose blender.exe.')
        _launch(exe, log)
    inbox = ROOT / 'inbox'
    inbox.mkdir(parents=True, exist_ok=True)
    name = f'{time.strftime("%Y%m%d_%H%M%S")}_{uuid.uuid4().hex[:8]}.json'
    tmp = inbox / (name + '.tmp')
    tmp.write_text(json.dumps(request, indent=1), encoding='utf-8')
    tmp.replace(inbox / name)                          # atomic: the add-on never reads half a file
    done = ROOT / 'done' / name
    deadline = time.time() + wait
    while time.time() < deadline:
        if done.is_file():
            result = json.loads(done.read_text(encoding='utf-8'))
            try:
                done.unlink()
            except OSError:
                pass
            if not result.get('ok'):
                raise RuntimeError('Blender could not import it: ' + result.get('error', '?'))
            return result
        time.sleep(0.25)
    raise TimeoutError('Blender did not finish the import in time (it may still be working).')


def install_addon(blender: Path | None = None, log=print) -> Path:
    """Install and enable the add-on in the user's Blender, so every Blender
    window opened later listens for the extractor."""
    exe = find_blender(blender)
    if not exe:
        raise FileNotFoundError('Blender was not found.')
    code = ("import bpy,shutil,addon_utils,pathlib;"
            "d=pathlib.Path(bpy.utils.user_resource('SCRIPTS',path='addons',create=True))/'ncaa07_bridge';"
            f"shutil.rmtree(d,ignore_errors=True);shutil.copytree(r'{ADDON}',d);"
            "addon_utils.enable('ncaa07_bridge',default_set=True,persistent=True);"
            "bpy.ops.wm.save_userpref();print('NCAA07_ADDON',d)")
    # the user's own preferences are loaded (no --factory-startup) so saving
    # them only adds the enabled add-on
    done = subprocess.run([str(exe), '-b', '--python-expr', code],
                          capture_output=True, text=True,
                          creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    for line in done.stdout.splitlines():
        if line.startswith('NCAA07_ADDON'):
            path = Path(line.split(' ', 1)[1])
            log(f'Blender add-on installed: {path}')
            return path
    raise RuntimeError('Add-on install failed:\n' + (done.stdout + done.stderr)[-1500:])
