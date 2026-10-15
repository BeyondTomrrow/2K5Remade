"""Windows NCAA 07 model browser and Blender GLB exporter."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import queue
import re
import shutil
import struct
import subprocess
import sys
import threading

from ncaa_formats import sections, decode_model, export_glb

GAME = Path(r'H:\07\NCAA Football 07  (axekin.com)\data')
HERE = Path(__file__).resolve().parent
BASE = Path(sys.executable).parent if getattr(sys, 'frozen', False) else HERE
SCRATCH = Path(r'H:\NCAA07_Extractor_Work') if Path('H:/').exists() else BASE / 'workspace'
BLENDER = Path(r'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe')
ARCHIVES = {'Players': 'PLYDATA.DAT', 'Stadiums': 'STADIUMS.DAT',
            'Stadium props': 'STADATA.dat', 'Field layers': 'FLDDATA.DAT', 'UI models': 'UIS_MODL.DAT',
            'Animations': 'ANIMDATA.dat'}
FOLDERS = {'Players': 'players_raw', 'Stadiums': 'stadiums_raw',
           'Stadium props': 'stadata_raw', 'Field layers': 'field_raw', 'UI models': 'ui_raw',
           'Animations': 'animdata_raw'}


def safe(value):
    return re.sub(r'[^A-Za-z0-9_.-]+', '_', value)


def catalog(folder: Path):
    result, errors = [], []
    for p in sorted(folder.rglob('*.tmd')):
        try:
            data = p.read_bytes()
            r, table = sections(data)
            if 'Geom' not in table:
                continue
            np, nc, sp, sc = r.unpack('<4I', table['Geom']['offset'])
            if not nc:
                continue
            name = r.string(table['Info']['offset']) if 'Info' in table else p.stem
            bones = r.unpack('<I', table['Skel']['offset'])[0] if 'Skel' in table else 0
            from ncaa_names import friendly
            materials = None
            if bones and 'Matl' in table:
                try:
                    from ncaa_player import _materials
                    materials = set(_materials(p)[0].values())
                except Exception:
                    materials = None
            label = friendly(name, materials)
            result.append({'name': label if label == name else f'{label}   [{name}]', 'source_name': name,
                           'label': label, 'file': str(p.resolve()), 'nodes': nc,
                           'bones': bones, 'textures': 'Text' in table, 'archive_index': p.stem})
        except Exception as e:
            errors.append({'file': str(p), 'error': str(e)})
    return result, errors


def extraction_tools():
    for folder in (BASE / 'tools', HERE / 'tools'):
        if (folder / 'quickbms.exe').is_file() and (folder / 'madden_terf.bms').is_file():
            return folder / 'quickbms.exe', folder / 'madden_terf.bms'
    raise FileNotFoundError('Keep the tools folder beside the application.')


def extract(game: Path, category: str, scratch: Path, log=print):
    archive = game / ARCHIVES[category]
    if not archive.is_file():
        raise FileNotFoundError(f'Game archive not found: {archive}')
    destination = scratch / FOLDERS[category]
    destination.mkdir(parents=True, exist_ok=True)
    exe, script = extraction_tools()
    log(f'Extracting {archive.name} to {destination} ...')
    creation = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
    done = subprocess.run([str(exe), '-o', str(script), str(archive), str(destination)],
                          capture_output=True, text=True, creationflags=creation)
    if done.returncode:
        raise RuntimeError((done.stdout + '\n' + done.stderr)[-2500:])
    log(f'Extraction complete: {destination}')
    return destination


def player_textures(folder: Path, game: Path | None, scratch: Path | None, uniform: int, log=print,
                    historic: int = 0x2A0):
    """Runtime texture banks for players (gear, team uniform, player art)."""
    from ncaa_player import Textures
    if game and scratch:
        try:
            from ncaa_assemble import ensure_assets
            ensure_assets(game, scratch, player=True, log=log)
        except Exception as e:                      # textures are optional; geometry still exports
            log(f'Player textures unavailable: {e}')
    scratch = Path(scratch) if scratch else folder.parent
    uniform_file = scratch / 'uniform_raw' / f'{uniform:08x}.3ds'
    if not uniform_file.is_file() and (scratch / 'uniform_raw').is_dir():
        log(f'Uniform entry {uniform} not found; using the first one')
        uniform_file = next(iter(sorted((scratch / 'uniform_raw').glob('*.3ds'))), uniform_file)
    return Textures(gear=folder / '00000001.3ds', uniform=uniform_file,
                    art=scratch / 'player_art_raw' / '00000000.3ds', coach=folder / '00000022.3ds',
                    historic=scratch / 'uniform_raw' / f'{historic:08x}.3ds')


def convert(rows, destination: Path, preview=False, log=print, game=None, scratch=None, uniform=9,
            variants=True, historic=0x2A0):
    destination.mkdir(parents=True, exist_ok=True)
    records = []
    textures = None
    for i, row in enumerate(rows):
        path = Path(row['file'])
        output = destination / f"{safe(row.get('label', row['name']))}_{safe(path.stem)}.glb"
        original = output
        number = 2
        while output.exists():
            output = original.with_name(original.stem + f'_{number}' + original.suffix)
            number += 1
        try:
            from ncaa_player import is_player, export_player, face_for
            if row.get('kind') == 'animations':
                # the configured player on its skeleton, every clip of the group as an action
                from ncaa_anim import groups, library_clips, body_rig, body_rig_file
                folder = path.parent
                players = Path(scratch) / 'players_raw' if scratch else None
                if not players or not (players / '00000004.tmd').is_file():
                    raise FileNotFoundError('Load Players once first (the animations play on the player model).')
                payloads = groups(folder)[path]
                rig = body_rig(body_rig_file(folder))
                log(f"Decoding {row['nodes']} clips ...")
                clips = library_clips(path, payloads, rig)
                if textures is None:
                    textures = player_textures(players, game, scratch, uniform, log, historic)
                report = export_player(players / '00000004.tmd', output, players / '00000003.tmd',
                                       textures=textures, include_variants=False, animations=clips)
                report['kind'] = 'player'
            elif row['bones'] and is_player(path):
                # every part placed with the skeleton; the configured player
                # visible, every other option tagged for the hidden Variants
                if textures is None:
                    textures = player_textures(path.parent, game, scratch, uniform, log, historic)
                face = None if row.get('source_name', row['name']).startswith('fm') else face_for(path)
                report = export_player(path, output, face, textures=textures, include_variants=variants)
                report['kind'] = 'player'
            elif preview and row['bones']:
                from ncaa_rig import export_preview
                report = export_preview(path, output)
            elif path.parent.name == 'stadiums_raw' and game and scratch:
                # the stadium with its own field (turf, logos, end zones) and props
                from ncaa_assemble import ensure_assets
                from ncaa_stadium import build_stadium
                ensure_assets(Path(game), Path(scratch), log=log)
                report = build_stadium(path, game, scratch, output, log=log)
            else:
                model = decode_model(path.read_bytes(), str(path))
                report = export_glb(model, output)
                report['kind'] = 'stadium'
                if model['skeleton']:
                    output.with_suffix('.skeleton.json').write_text(
                        json.dumps(model['skeleton'], indent=2), encoding='utf-8')
            report['file'] = str(output.resolve())
            report['status'] = 'exported'
            records.append(report)
            log(f"{i+1}/{len(rows)}  {row['name']}")
        except Exception as e:
            records.append({'file': str(path), 'status': 'failed', 'error': str(e)})
            log(f"Could not export {row['name']}: {e}")
    # Each run gets its own report, preserving previous export results.
    import time
    name = 'export_report_' + time.strftime('%Y%m%d_%H%M%S') + f'_{time.time_ns() % 1000000000:09d}.json'
    (destination / name).write_text(json.dumps(records, indent=2), encoding='utf-8')
    return records


def launch():
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    window = tk.Tk()
    window.title('NCAA 07 → Blender')
    window.geometry('1180x780')
    window.minsize(900, 620)
    events = queue.Queue()
    rows, exported = [], []
    busy = False
    game = tk.StringVar(value=str(GAME))
    category = tk.StringVar(value='Stadiums')
    scratch = tk.StringVar(value=str(SCRATCH))
    output = tk.StringVar(value=str(SCRATCH / 'blender_exports'))
    search = tk.StringVar()
    preview = tk.BooleanVar(value=False)
    uniform = tk.IntVar(value=9)
    historic = tk.IntVar(value=0x2A0)
    send_after = tk.BooleanVar(value=True)
    variants = tk.BooleanVar(value=True)
    status = tk.StringVar(value='Choose a category, then Load models. Existing extracted files are reused.')
    frame = ttk.Frame(window, padding=14)
    frame.pack(fill='both', expand=True)
    ttk.Label(frame, text='NCAA 07 → Blender', font=('Segoe UI', 20, 'bold')).pack(anchor='w')
    ttk.Label(frame, text='Textured stadiums • player meshes • source bones • GLB export').pack(anchor='w', pady=(0, 12))

    def folder_row(label, variable):
        row = ttk.Frame(frame)
        row.pack(fill='x', pady=3)
        ttk.Label(row, text=label, width=19).pack(side='left')
        ttk.Entry(row, textvariable=variable).pack(side='left', fill='x', expand=True)
        def browse():
            value = filedialog.askdirectory(initialdir=variable.get() if Path(variable.get()).is_dir() else None)
            if value: variable.set(value)
        ttk.Button(row, text='Browse…', command=browse).pack(side='left', padx=(8, 0))
    folder_row('Game data folder', game)
    folder_row('Extraction workspace', scratch)
    folder_row('Blender export folder', output)
    bar = ttk.Frame(frame)
    bar.pack(fill='x', pady=10)
    ttk.Combobox(bar, textvariable=category, values=list(ARCHIVES), state='readonly', width=18).pack(side='left')
    actions = []

    def log(text): events.put(('log', str(text)))
    def start(job):
        nonlocal busy
        if busy: return
        busy = True
        for button in actions: button.configure(state='disabled')
        def worker():
            try: job()
            except Exception as e: events.put(('error', str(e)))
            finally: events.put(('done', None))
        threading.Thread(target=worker, daemon=True).start()

    def load_models():
        selected_category, game_path, scratch_path = category.get(), Path(game.get()), Path(scratch.get())
        def job():
            folder = scratch_path / FOLDERS[selected_category]
            if selected_category == 'Players' and not folder.exists() and (HERE / 'outputs/plydata_extracted').exists():
                folder = HERE / 'outputs/plydata_extracted'
            if selected_category == 'Animations':
                if not folder.exists() or not any(folder.glob('*.vap')):
                    folder = extract(game_path, selected_category, scratch_path, log)
                from ncaa_anim import catalog as anim_catalog
                data, errors = anim_catalog(folder), []
                events.put(('catalog', data))
                log(f'Loaded {len(data)} animation groups ({sum(r["nodes"] for r in data)} clips) from {folder}.')
                return
            if not folder.exists() or not any(folder.glob('*.tmd')):
                folder = extract(game_path, selected_category, scratch_path, log)
            data, errors = catalog(folder)
            events.put(('catalog', data))
            log(f'Loaded {len(data)} models from {folder}. {len(errors)} unreadable files.')
        start(job)

    def open_folder():
        folder = filedialog.askdirectory(title='Choose extracted TMDL folder')
        if folder:
            def job():
                data, errors = catalog(Path(folder))
                events.put(('catalog', data)); log(f'Loaded {len(data)} models. {len(errors)} errors.')
            start(job)
    for text, command in [('Load models', load_models), ('Open extracted folder…', open_folder)]:
        button = ttk.Button(bar, text=text, command=command)
        button.pack(side='left', padx=6); actions.append(button)
    ttk.Label(bar, text='Search').pack(side='left', padx=(14, 5))
    ttk.Entry(bar, textvariable=search).pack(side='left', fill='x', expand=True)
    columns = ('model', 'bones', 'textures', 'file')
    tree = ttk.Treeview(frame, columns=columns, show='headings', selectmode='extended', height=16)
    for col, title, width in [('model', 'Model / stadium', 350), ('bones', 'Source bones', 95),
                              ('textures', 'Textures', 90), ('file', 'Archive entry', 120)]:
        tree.heading(col, text=title); tree.column(col, width=width)
    tree.pack(fill='both', expand=True)

    def show_rows(*_):
        tree.delete(*tree.get_children())
        term = search.get().lower()
        for i, row in enumerate(rows):
            if term in row['name'].lower() or term in row['archive_index'].lower():
                tree.insert('', 'end', iid=str(i), values=(row['name'], row['bones'] or '—',
                            'Embedded' if row['textures'] else 'External / none', row['archive_index']))
    search.trace_add('write', show_rows)
    options = ttk.Frame(frame)
    options.pack(fill='x', pady=(10, 4))
    ttk.Checkbutton(options, text='Send to Blender after export', variable=send_after).pack(side='left')
    ttk.Checkbutton(options, text='Players: keep alternative helmets/hair/facemasks (hidden)', variable=variants).pack(side='left', padx=(14, 0))
    ttk.Label(options, text='Uniform (UNIFORM.DAT entry)').pack(side='left', padx=(14, 4))
    ttk.Spinbox(options, from_=0, to=1205, textvariable=uniform, width=6).pack(side='left')

    def pick_uniform():
        """Every UNIFORM.DAT entry as jersey + helmet thumbnails; click one."""
        from ncaa_assemble import uniform_thumbnail, ensure_assets
        cache = Path(scratch.get())
        folder = cache / 'uniform_raw'
        if not folder.is_dir():
            try:
                ensure_assets(Path(game.get()), cache, player=True, log=log)
            except Exception as e:
                messagebox.showerror('Uniforms', str(e)); return
        count = len(list(folder.glob('*.3ds')))
        top = tk.Toplevel(window); top.title('Choose a uniform (UNIFORM.DAT)'); top.geometry('1180x760')
        canvas = tk.Canvas(top, bg='#2b2b2b'); bar = ttk.Scrollbar(top, orient='vertical', command=canvas.yview)
        canvas.configure(yscrollcommand=bar.set); bar.pack(side='right', fill='y'); canvas.pack(fill='both', expand=True)
        grid = ttk.Frame(canvas); canvas.create_window((0, 0), window=grid, anchor='nw')
        images = []
        cols = 12
        def choose(i):
            from ncaa_assemble import TextureBank
            try:
                names = TextureBank(folder / f'{i:08x}.3ds').names
            except Exception:
                names = {}
            if 'TORSO' in names:          # historic-team entry: dresses the old-era bodies
                historic.set(i); log(f'Historic uniform {i} selected (old-era players)')
            else:
                uniform.set(i); log(f'Uniform {i} selected (modern players)')
            top.destroy()
        def fill(start=0):
            for i in range(start, min(start + 6, count)):
                try:
                    img = tk.PhotoImage(file=str(uniform_thumbnail(cache, i)))
                except Exception:
                    continue
                images.append(img)
                b = tk.Button(grid, image=img, text=str(i), compound='top', command=lambda i=i: choose(i),
                              bg='#3a3a3a', fg='white', relief='flat')
                b.grid(row=i // cols, column=i % cols, padx=2, pady=2)
            grid.update_idletasks(); canvas.configure(scrollregion=canvas.bbox('all'))
            if start + 6 < count and top.winfo_exists():
                top.after(1, lambda: fill(start + 6))
        canvas.bind_all('<MouseWheel>', lambda e: canvas.yview_scroll(-e.delta // 120, 'units'))
        fill()
    ttk.Button(options, text='Pick uniform...', command=pick_uniform).pack(side='left', padx=(6, 0))
    ttk.Label(options, text='Historic').pack(side='left', padx=(10, 4))
    ttk.Spinbox(options, from_=0x221, to=0x2E0, textvariable=historic, width=5).pack(side='left')
    ttk.Label(frame, text='Players are exported on their skeleton: the configured player is visible, every other helmet, facemask, hair style and hand pose sits in a hidden Variants collection (Blender sidebar > NCAA 07 swaps them). Models go into the Blender you already have open.',
              foreground='#6a4b17', wraplength=1050).pack(anchor='w', pady=3)

    def export_selection(all_visible=False):
        ids = tree.get_children() if all_visible else tree.selection()
        if not ids:
            messagebox.showinfo('Choose models', 'Select one or more models first.'); return
        chosen = [rows[int(i)] for i in ids]
        destination, rig = Path(output.get()), preview.get()
        game_path, scratch_path, team = Path(game.get()), Path(scratch.get()), uniform.get()
        keep_variants, send, old_team = variants.get(), send_after.get(), historic.get()
        def job():
            result = convert(chosen, destination, rig, log, game_path, scratch_path, team, keep_variants, old_team)
            events.put(('exports', result))
            log(f"Finished: {sum(r['status']=='exported' for r in result)} exported, {sum(r['status']=='failed' for r in result)} failed. {destination}")
            if send:
                to_blender(result)
        start(job)

    def to_blender(records):
        """Into the Blender that is already open (one is started only if none is)."""
        from ncaa_bridge_client import send
        done = [r for r in records if r.get('status') == 'exported']
        for r in done[:12]:
            action = 'open_blend' if r.get('blend') else r.get('kind', 'stadium')
            target = r.get('blend') or r['file']
            log('Sending to Blender: ' + Path(target).name)
            send({'action': action, 'file': target, 'name': Path(target).stem}, BLENDER, log=log)
        if len(done) > 12:
            log(f'{len(done) - 12} more exports are in the folder; only the first 12 were sent to Blender.')

    def open_blender():
        if not [x for x in exported if x.get('status') == 'exported']:
            messagebox.showinfo('Export a model first', 'Export a selected model, then click Send to Blender.'); return
        records = list(exported)
        start(lambda: to_blender(records))

    def install_addon():
        def job():
            from ncaa_bridge_client import install_addon as install
            install(BLENDER, log)
            log('Every Blender you open now listens for the extractor (sidebar > NCAA 07).')
        start(job)

    controls = ttk.Frame(frame)
    controls.pack(fill='x', pady=8)
    for text, command in [('Export selected', lambda: export_selection(False)),
                           ('Export all shown', lambda: export_selection(True)), ('Send to Blender', open_blender),
                           ('Install Blender add-on', install_addon)]:
        button = ttk.Button(controls, text=text, command=command)
        button.pack(side='left', padx=(0, 8)); actions.append(button)
    def assembled(player=False):
        selected=tree.selection()
        if not player and (len(selected)!=1 or category.get()!='Stadiums'):
            messagebox.showinfo('Select stadium','Load Stadiums and select one stadium.');return
        source=rows[int(selected[0])]['file'] if not player else None
        game_path,cache,destination=Path(game.get()),Path(scratch.get()),Path(output.get())
        team=uniform.get()
        def job():
            from ncaa_assemble import ensure_assets, fresh_output
            from ncaa_bridge_client import send
            ensure_assets(game_path,cache,player=player,log=log)
            if player:
                from ncaa_player import export_player
                players=cache/'players_raw'
                target=fresh_output(destination,f'Player_uniform_{team}').with_suffix('.glb')
                tex=player_textures(players,None,cache,team,log)
                report=export_player(players/'00000004.tmd',target,players/'00000003.tmd',textures=tex)
                report.update(file=str(target),status='exported',kind='player')
                send({'action':'player','file':str(target),'name':target.stem},BLENDER,log=log)
            else:
                from ncaa_stadium import build_stadium
                stem=decode_model(Path(source).read_bytes(),str(source))['name'].replace('.ea3','')
                target=fresh_output(destination,stem+'_Complete').with_suffix('.glb')
                report=build_stadium(source,game_path,cache,target,log=log)
                report.update(status='exported')
                send({'action':'stadium','file':str(target),'name':target.stem},BLENDER,log=log)
            events.put(('exports',[report]));log('In Blender.')
        start(job)
    builders=ttk.Frame(frame);builders.pack(fill='x',pady=(0,5))
    for label,command in [('Complete stadium to Blender',lambda:assembled(False)),('Build configured player',lambda:assembled(True))]:
        button=ttk.Button(builders,text=label,command=command);button.pack(side='left',padx=(0,8));actions.append(button)
    ttk.Button(controls, text='Open export folder', command=lambda: os.startfile(output.get()) if Path(output.get()).is_dir() else None).pack(side='left')
    ttk.Label(frame, textvariable=status, wraplength=1080).pack(anchor='w', pady=3)
    logbox = tk.Text(frame, height=5, state='disabled', wrap='word', font=('Consolas', 9))
    logbox.pack(fill='x')

    def poll():
        nonlocal rows, exported, busy
        try:
            while True:
                kind, value = events.get_nowait()
                if kind == 'catalog': rows = value; show_rows()
                elif kind == 'exports': exported = value
                elif kind == 'done':
                    busy = False
                    for button in actions: button.configure(state='normal')
                elif kind == 'error':
                    status.set('Could not finish: ' + value)
                    messagebox.showerror('NCAA 07 → Blender', value)
                elif kind == 'log':
                    status.set(value); logbox.configure(state='normal'); logbox.insert('end', value+'\n')
                    logbox.see('end'); logbox.configure(state='disabled')
        except queue.Empty: pass
        window.after(100, poll)
    poll()
    window.mainloop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', type=Path, help=argparse.SUPPRESS)
    sub = parser.add_subparsers(dest='command')
    sub.add_parser('gui')
    ex = sub.add_parser('export')
    ex.add_argument('source', type=Path); ex.add_argument('output', type=Path)
    ex.add_argument('--filter', default=''); ex.add_argument('--rig-preview', action='store_true')
    ca = sub.add_parser('catalog'); ca.add_argument('source', type=Path)
    for name in ('build-player','build-stadium'):
        command=sub.add_parser(name)
        if name=='build-stadium':command.add_argument('source',type=Path)
        command.add_argument('output',type=Path)
        command.add_argument('--game',type=Path,default=GAME)
        command.add_argument('--cache',type=Path,default=SCRATCH)
    a = parser.parse_args()
    if a.self_test:
        import tkinter as tk
        import numpy
        test_window = tk.Tk(); test_window.withdraw(); test_window.update(); test_window.destroy()
        exe, script = extraction_tools()
        a.self_test.write_text(json.dumps({'tkinter': True, 'numpy': numpy.__version__,
                                          'quickbms': exe.is_file(), 'script': script.is_file(),
                                          'assembly_script': (HERE/'ncaa_blender_build.py').is_file(),
                                          'frozen': bool(getattr(sys, 'frozen', False))}, indent=2), encoding='utf-8')
        return 0
    if a.command == 'catalog':
        data, errors = catalog(a.source); print(json.dumps({'models': data, 'errors': errors}, indent=2)); return
    if a.command in ('build-player','build-stadium'):
        from ncaa_assemble import build_clean_player,ensure_assets,fresh_output
        if a.command=='build-player':result=build_clean_player(a.game,a.cache,a.output,BLENDER)
        else:
            from ncaa_stadium import build_stadium
            ensure_assets(a.game,a.cache)
            result=build_stadium(a.source,a.game,a.cache,fresh_output(a.output,a.source.stem+'_Complete').with_suffix('.glb'))
        (a.output/'assembly_report.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        return 0
    if a.command == 'export':
        data, errors = catalog(a.source)
        data = [row for row in data if a.filter.lower() in row['name'].lower()]
        result = convert(data, a.output, a.rig_preview)
        print(json.dumps({'exported': sum(r['status']=='exported' for r in result), 'results': result}, indent=2)); return
    launch()


if __name__ == '__main__':
    raise SystemExit(main())
