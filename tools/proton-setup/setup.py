#!/usr/bin/env python3
"""Set up an unmodified 2K5 Remade repack in an isolated Proton directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


def win(path):
    return 'Z:' + str(Path(path).resolve()).replace('/', '\\')


def clean_env(proton, steam, prefix, inherited=None):
    env = dict(os.environ if inherited is None else inherited)
    for key in list(env):
        if key.startswith(('NFL2K5_', 'RECOMP_')) or key in ('WINEPREFIX', 'WINEDLLOVERRIDES', 'PROTON_USE_WINED3D'):
            del env[key]
    env.update(STEAM_COMPAT_CLIENT_INSTALL_PATH=str(steam), STEAM_COMPAT_DATA_PATH=str(prefix))
    return env


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def checked(path, name):
    path = Path(path).expanduser().resolve()
    if not path.is_file():
        raise ValueError(f'{name} not found: {path}')
    if any(c in str(path) for c in '\n\r\0'):
        raise ValueError('Paths must not contain control characters')
    return path


def finish(output, game, proton, steam):
    exe = checked(game / 'NFL2K5.exe', 'Repack executable')
    checked(game / 'original/disc/default.xbe', 'Installed disc link')
    launcher = output / 'play.sh'
    # Steam launches this native shell shim. Do not force a second compatibility
    # layer on it. Preserve Steam's identity and controller environment.
    script = '#!/bin/sh\nset -eu\n'
    script += 'for key in $(env | cut -d= -f1); do case "$key" in NFL2K5_*|RECOMP_*) unset "$key";; esac; done\n'
    script += "unset WINEPREFIX WINEDLLOVERRIDES PROTON_USE_WINED3D\nexport WINEDLLOVERRIDES='d3d11,dxgi=b;d3dcompiler_47=n,b'\n"
    for key, value in {'STEAM_COMPAT_CLIENT_INSTALL_PATH':steam, 'STEAM_COMPAT_DATA_PATH':output/'prefix'}.items():
        script += f'export {key}={shlex.quote(str(value))}\n'
    script += f'cd {shlex.quote(str(game))}\n'
    script += f'exec {shlex.quote(str(proton))} runinprefix {shlex.quote(str(exe))} "$@"\n'
    launcher.write_text(script)
    launcher.chmod(0o755)
    receipt = {'format':1, 'game':str(game), 'proton':str(proton), 'steam':str(steam),
               'prefix':str(output/'prefix'), 'executable_sha256':sha(exe),
               'status':'installed-not-gameplay-verified', 'launch_mode':'runinprefix',
               'd3dcompiler_sha256':sha(game/'d3dcompiler_47.dll'),
               'presentation':'ESPN (Original)', 'render_scale':2}
    (output/'setup-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    # A .desktop file is optional convenience input for Valve's own helper.
    def quote(value):
        return '"'+str(value).replace('\\','\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$').replace('%','%%')+'"'
    desktop = output/'2K5 Remade (Repack).desktop'
    desktop.write_text('[Desktop Entry]\nType=Application\nName=2K5 Remade (Repack)\n'
                       f'Exec={quote(launcher)}\nTerminal=false\nCategories=Game;\n')
    desktop.chmod(0o755)
    return receipt


def setup(installer, disc, proton, steam, output, d3dcompiler):
    installer=checked(installer,'Upstream installer')
    d3dcompiler=checked(d3dcompiler,'Microsoft x64 shader compiler')
    proton=checked(proton,'Proton script')
    steam=Path(steam).expanduser().resolve()
    disc=Path(disc).expanduser().resolve()
    checked(disc/'default.xbe','Original Xbox executable')
    if not (disc/'vc_53450030').is_dir():
        raise ValueError('Disc folder must contain vc_53450030')
    if not (steam/'steamapps').is_dir():
        raise ValueError('Steam root must contain steamapps')
    output=Path(output).expanduser().resolve()
    if any(c in str(output) for c in '\n\r\0'):
        raise ValueError('Paths must not contain control characters')
    # Fail before mutation. Never reuse an existing Wine prefix or install folder.
    output.mkdir(parents=True,exist_ok=False)
    (output/'prefix').mkdir()
    game=output/'game'
    # Wine's cmd mklink /J can create an empty 'disc?' directory. Supply the
    # equivalent host symlink first; the stock installer validates and reuses it.
    (game/'original').mkdir(parents=True)
    (game/'original/disc').symlink_to(disc,target_is_directory=True)
    cmd=[str(proton),'run',str(installer),'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',
         '/SP-','/DIR='+win(game),'/GAMEPATH='+win(disc),'/LOG='+win(output/'installer.log')]
    (output/'input-receipt.json').write_text(json.dumps({'installer_sha256':sha(installer),
        'disc_xbe_sha256':sha(disc/'default.xbe'),'command':cmd},indent=2)+'\n')
    with (output/'setup-proton.log').open('w') as log:
        result=subprocess.run(cmd,cwd=installer.parent,env=clean_env(proton,steam,output/'prefix'),
                              stdout=log,stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f'Installer exited {result.returncode}; retained logs in {output}')
    shutil.copy2(d3dcompiler,game/'d3dcompiler_47.dll')
    # Stock settings, written only inside this new prefix before its first run.
    # ESPN Original uses the game's scorebug; HTML overlays require WebView2.
    user=output/'prefix/pfx/drive_c/users/steamuser/AppData/Local/NFL2K5-PC'
    (user/'mods').mkdir(parents=True,exist_ok=True)
    (user/'mods/presentation.ini').write_text('package = ESPN (Original)\n')
    (user/'nfl2k5_video.ini').write_text('mode = 1\nresolution = 1280x800\nscale = 2\n')
    return finish(output,game,proton,steam)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for flag in ['installer','disc','proton','steam','output','d3dcompiler']:
        p.add_argument('--'+flag,required=True,type=Path)
    p.add_argument('--add-to-steam',action='store_true',help='Invoke Valve steamos-add-to-steam after installation')
    a=p.parse_args()
    receipt=setup(a.installer,a.disc,a.proton,a.steam,a.output,a.d3dcompiler)
    output=a.output.expanduser().resolve()
    if a.add_to_steam:
        subprocess.run(['steamos-add-to-steam',str(output/'2K5 Remade (Repack).desktop')],check=True)
    print(json.dumps(receipt,indent=2))
    print(f'Launch: {output / "play.sh"}')


if __name__=='__main__':
    try: main()
    except (OSError,ValueError,RuntimeError,subprocess.CalledProcessError) as e:
        print(str(e),file=sys.stderr)
        sys.exit(1)
