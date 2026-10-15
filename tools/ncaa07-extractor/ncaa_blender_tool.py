"""Windows NCAA mesh browser. Python 3.10+, stdlib UI/pipeline.

Examples: python ncaa_blender_tool.py gui
          python ncaa_blender_tool.py export outputs/plydata_extracted work/gui_test --filter player
          python ncaa_blender_tool.py list H:/NCAA07_Extractor_Work/stadiums_raw
Optional experimental rig exports load ncaa_rig (and its dependencies) lazily.
"""
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
import tempfile
import threading
import time
import traceback
from datetime import datetime

from ncaa_formats import decode_model, export_glb, sections

ROOT = Path(__file__).resolve().parent
APP_ROOT = Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else ROOT
PREFERRED = Path("H:/NCAA07_Extractor_Work")
BLENDER = Path("C:/Program Files/Blender Foundation/Blender 5.2/blender.exe")
ARCHIVES = ("PLYDATA.DAT", "STADIUMS.DAT", "STADATA.DAT")
RIG_STATUS = "Skeleton binding / animation pending for static export; experimental rig weights are provisional."


def bundled(name):
    candidates = [ROOT / "tools" / name, APP_ROOT / "tools" / name]
    if hasattr(sys, "_MEIPASS"):
        candidates.insert(0, Path(sys._MEIPASS) / "tools" / name)
    return next((p for p in candidates if p.is_file()), candidates[0])


def default_scratch(fallback=None):
    try:
        if PREFERRED.is_dir() and shutil.disk_usage(PREFERRED).free >= 10 * 1024**3:
            return PREFERRED
    except OSError:
        pass
    return Path(fallback) if fallback else APP_ROOT / "work" / "ncaa_scratch"


def failure(path, phase):
    return {"source": str(path), "phase": phase, "exception": traceback.format_exc()}


def save_report(folder, prefix, report):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    # Unique reports retain previous failures and never serialize model['_data'].
    path = folder / f"{prefix}_{datetime.now():%Y%m%d_%H%M%S}_{time.time_ns() % 1000000000:09d}.json"
    path.write_text(json.dumps(report, indent=2, ensure_ascii=False, default=str), encoding="utf-8")
    return path


def check_cancel(cancel):
    if cancel and cancel.is_set():
        raise InterruptedError("Operation cancelled")


def catalog(source, notify=lambda s: None, cancel=None):
    source = Path(source).resolve()
    if not source.exists():
        raise FileNotFoundError(source)
    report = {"source": str(source), "models": [], "skipped": [], "errors": [], "rig_status": RIG_STATUS}
    paths = [source] if source.is_file() else sorted(source.rglob("*"))
    paths = [p for p in paths if p.is_file() and p.suffix.lower() == ".tmd"]
    for i, path in enumerate(paths):
        check_cancel(cancel)
        if i % 10 == 0:
            notify(f"Reading TMD {i + 1}/{len(paths)}: {path.name}")
        try:
            data = path.read_bytes()
            reader, table = sections(data)
            if "Geom" not in table:
                report["skipped"].append({"source": str(path), "reason": "No Geom section"})
                continue
            _, nc, _, _ = reader.unpack("<4I", table["Geom"]["offset"])
            if nc == 0:
                report["skipped"].append({"source": str(path), "reason": "Geom node count 0 (companion)"})
                continue
            model = decode_model(data, str(path))
            meshes = [{"name": m["name"], "node_index": m["node_index"],
                       "vertices": len(m["positions"]), "triangles": len(m["faces"])}
                      for m in model["meshes"] if m["positions"] and m["faces"]]
            if not meshes:
                report["skipped"].append({"source": str(path), "reason": "No supported triangle meshes", "warnings": model["warnings"]})
                continue
            report["models"].append({"source": str(path), "name": model["name"],
                                     "skeleton": bool(model.get("skeleton")),
                                     "meshes": meshes, "warnings": list(model["warnings"])})
        except Exception:
            report["errors"].append(failure(path, "catalog"))
    notify(summary(report))
    return report


def summary(report):
    models = report.get("models", [])
    return (f"{len(models)} models / {sum(len(m['meshes']) for m in models)} meshes; "
            f"{len(report.get('skipped', []))} skipped; {len(report.get('errors', []))} errors")


def matching(models, text):
    needle = text.casefold().strip()
    result = []
    for model in models:
        if not needle or needle in (model["name"] + " " + model["source"]).casefold():
            result.append(model)
        else:
            meshes = [m for m in model["meshes"] if needle in m["name"].casefold()]
            if meshes:
                result.append({**model, "meshes": meshes})
    return result


def safe_name(value):
    value = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", str(value)).strip(" .")[:120] or "model"
    if value.split(".")[0].upper() in {"CON", "PRN", "AUX", "NUL", *[f"COM{i}" for i in range(1, 10)], *[f"LPT{i}" for i in range(1, 10)]}:
        value = "_" + value
    return value


def export_models(models, output, rig=False, notify=lambda s: None, cancel=None):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {"exports": [], "errors": [], "rig_preview": rig, "rig_status": RIG_STATUS}
    for i, entry in enumerate(models):
        if cancel and cancel.is_set():
            report["cancelled"] = True
            break
        source = Path(entry["source"])
        notify(f"Exporting {i + 1}/{len(models)}: {entry['name']}")
        temp = None
        try:
            stem = safe_name(entry["name"]) + "__" + safe_name(source.stem)
            dest = output / (stem + ("_rig_preview" if rig and entry["skeleton"] else "") + ".glb")
            suffix = 2
            while dest.exists():
                dest = output / f"{stem}_{suffix}.glb"
                suffix += 1
            fd, temp_path = tempfile.mkstemp(prefix=".ncaa_", suffix=".glb", dir=output)
            os.close(fd)
            temp = Path(temp_path)
            if rig and entry["skeleton"]:
                # Rig interface exports the whole player, so never silently ignore a part selection.
                model = decode_model(source.read_bytes(), str(source))
                actual = {m["node_index"] for m in model["meshes"] if m["faces"] and m["positions"]}
                if {m["node_index"] for m in entry["meshes"]} != actual:
                    raise ValueError("Experimental rig preview requires selecting the complete player model")
                import ncaa_rig  # Lazy, but statically discoverable by PyInstaller.
                ncaa_rig.export_preview(source, temp)
                result = {"model": entry["name"], "experimental_rig": True, "warnings": ["Preview weights/deformation provisional; animation pending"]}
            else:
                model = decode_model(source.read_bytes(), str(source))
                nodes = {m["node_index"] for m in entry["meshes"]}
                model["meshes"] = [m for m in model["meshes"] if m["node_index"] in nodes and m["faces"]]
                result = export_glb(model, temp)
            with temp.open("rb") as stream:
                header = stream.read(12)
            if len(header) != 12 or struct.unpack("<4sII", header) != (b"glTF", 2, temp.stat().st_size):
                raise ValueError("Exporter did not produce a valid GLB header")
            temp.replace(dest)
            report["exports"].append({**result, "file": str(dest), "source": str(source)})
        except Exception:
            report["errors"].append(failure(source, "export"))
        finally:
            if temp and temp.exists():
                temp.unlink()
    report["report_file"] = str(save_report(output, "export_report", report))
    notify(f"Exported {len(report['exports'])}; {len(report['errors'])} errors. Report: {report['report_file']}")
    return report


def extract_archives(source, scratch, archive="ALL", quickbms=None, script=None,
                     notify=lambda s: None, cancel=None):
    exe, bms = Path(quickbms or bundled("quickbms.exe")), Path(script or bundled("madden_terf.bms"))
    for path in (exe, bms):
        if not path.is_file():
            raise FileNotFoundError(f"Required extraction tool missing: {path}. Bundle it in tools/ or pass its path.")
    source, scratch = Path(source).resolve(), Path(scratch).resolve()
    wanted = ARCHIVES if archive.upper() == "ALL" else (archive.upper(),)
    if any(n not in ARCHIVES for n in wanted):
        raise ValueError("Choose PLYDATA.DAT, STADIUMS.DAT, STADATA.DAT or ALL")
    if not source.exists():
        raise FileNotFoundError(source)
    files = [source] if source.is_file() else sorted(source.rglob("*"))
    files = [p for p in files if p.is_file() and p.name.upper() in wanted]
    if not files:
        raise FileNotFoundError(f"No {', '.join(wanted)} in {source}")
    scratch.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="ncaa_extract_", dir=scratch))
    report = {"source": str(source), "output": str(run), "archives": [], "errors": []}
    try:
        for i, path in enumerate(files):
            check_cancel(cancel)
            dest = run / f"{i + 1:02d}_{path.stem.lower()}_raw"
            dest.mkdir()
            log = run / f"{i + 1:02d}_{path.stem.lower()}.log"
            notify(f"Extracting {path.name}; log: {log}")
            try:
                # -o overwrites only within this new private directory; -Y accepts
                # QuickBMS confirmations, stdin EOF prevents interactive prompts.
                with log.open("wb") as stream:
                    proc = subprocess.Popen([str(exe.resolve()), "-o", "-Y", str(bms.resolve()), str(path), str(dest)],
                                            stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT,
                                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                    try:
                        while proc.poll() is None:
                            if cancel and cancel.wait(0.2):
                                raise InterruptedError("Extraction cancelled")
                            if cancel is None:
                                time.sleep(0.2)
                    finally:
                        if proc.poll() is None:
                            proc.kill()
                            proc.wait()
                if proc.returncode:
                    raise RuntimeError(f"QuickBMS exit {proc.returncode}; see {log}")
                report["archives"].append({"source": str(path), "output": str(dest), "log": str(log)})
            except InterruptedError:
                raise
            except Exception:
                report["errors"].append(failure(path, "extract"))
    finally:
        report["report_file"] = str(save_report(run, "extraction_report", report))
    return report


def open_blender(glb, executable=None):
    exe = Path(executable or BLENDER)
    if not exe.is_file():
        found = shutil.which("blender") if executable is None else None
        if not found:
            raise FileNotFoundError(f"Blender not found: {exe}. Choose blender.exe in the GUI or use --blender.")
        exe = Path(found)
    # JSON string is valid as a Python literal here, including escaped Windows paths.
    expression = "import bpy; bpy.ops.import_scene.gltf(filepath=" + repr(str(Path(glb).resolve())) + ")"
    subprocess.Popen([str(exe), "--python-expr", expression], stdin=subprocess.DEVNULL,
                     creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))


def launch_gui(initial=None):
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox

    root = tk.Tk()
    root.title("NCAA 07 • Mesh browser and Blender export")
    root.geometry("1120x780")
    root.minsize(850, 600)
    events = queue.Queue()
    cancel = threading.Event()
    state = {"busy": False, "catalog": {"models": []}, "rows": {}, "last": None}
    source = tk.StringVar(value=str(initial or APP_ROOT / "outputs" / "plydata_extracted"))
    scratch = tk.StringVar(value=str(default_scratch()))
    output = tk.StringVar(value=str(default_scratch() / "glb_exports"))
    blender = tk.StringVar(value=str(BLENDER))
    query = tk.StringVar()
    archive = tk.StringVar(value="ALL")
    rig = tk.BooleanVar(value=False)
    status = tk.StringVar(value="Choose a TMD folder and Load catalog.")
    totals = tk.StringVar(value=RIG_STATUS)
    panel = ttk.Frame(root, padding=12)
    panel.pack(fill="both", expand=True)
    panel.columnconfigure(1, weight=1)

    def browse(var, file=False):
        value = filedialog.askopenfilename(parent=root) if file else filedialog.askdirectory(parent=root)
        if value:
            var.set(value)

    for row, (label, var, file) in enumerate((("Source / game folder", source, False), ("Scratch folder", scratch, False),
                                              ("GLB output folder", output, False), ("Blender executable", blender, True))):
        ttk.Label(panel, text=label).grid(row=row, column=0, sticky="w", padx=(0, 8), pady=3)
        ttk.Entry(panel, textvariable=var).grid(row=row, column=1, sticky="ew")
        ttk.Button(panel, text="Browse…", command=lambda v=var, f=file: browse(v, f)).grid(row=row, column=2, padx=4)
    shortcuts = ttk.Frame(panel)
    shortcuts.grid(row=4, column=0, columnspan=3, sticky="ew", pady=6)
    for label, path in (("Players", APP_ROOT / "outputs" / "plydata_extracted"),
                        ("Stadiums", PREFERRED / "stadiums_raw"), ("Stadium props / field", PREFERRED / "stadata_raw")):
        ttk.Button(shortcuts, text=label, command=lambda p=path: source.set(str(p))).pack(side="left", padx=3)
    controls = ttk.Frame(panel)
    controls.grid(row=5, column=0, columnspan=3, sticky="ew", pady=4)
    buttons = []

    def notify(text):
        events.put(("status", text))

    def start(job, done):
        if state["busy"]:
            return
        state["busy"] = True
        cancel.clear()
        for button in buttons:
            button.configure(state="disabled")
        progress.start(12)
        def worker():
            try:
                events.put(("done", (done, job())))
            except Exception:
                events.put(("error", traceback.format_exc()))
        threading.Thread(target=worker, daemon=True).start()

    def show_catalog(report):
        state["catalog"] = report
        totals.set(summary(report) + " • " + RIG_STATUS)
        refresh()

    def load():
        src, target = source.get(), scratch.get()
        def job():
            report = catalog(src, notify, cancel)
            report["report_file"] = str(save_report(target, "catalog_report", report))
            return report
        start(job, show_catalog)

    def extract():
        src, target, name = source.get(), scratch.get(), archive.get()
        def done(report):
            log.insert("end", json.dumps(report, indent=2) + "\n")
            if report["archives"]:
                source.set(report["output"])
                status.set("Extraction complete. Click Load catalog to scan extracted meshes.")
        start(lambda: extract_archives(src, target, name, notify=notify, cancel=cancel), done)

    def add_button(parent, label, command):
        button = ttk.Button(parent, text=label, command=command)
        button.pack(side="left", padx=3)
        buttons.append(button)

    add_button(controls, "Load catalog", load)
    ttk.Combobox(controls, textvariable=archive, values=("ALL", *ARCHIVES), state="readonly", width=17).pack(side="left", padx=6)
    add_button(controls, "Extract game archives", extract)
    ttk.Label(controls, text="Requires tools/quickbms.exe + madden_terf.bms").pack(side="left", padx=8)
    filterbar = ttk.Frame(panel)
    filterbar.grid(row=6, column=0, columnspan=3, sticky="ew", pady=6)
    ttk.Label(filterbar, text="Filter model / mesh / source name:").pack(side="left")
    ttk.Entry(filterbar, textvariable=query, width=42).pack(side="left", padx=8, fill="x", expand=True)
    treeframe = ttk.Frame(panel)
    treeframe.grid(row=7, column=0, columnspan=3, sticky="nsew")
    panel.rowconfigure(7, weight=1)
    tree = ttk.Treeview(treeframe, columns=("vertices", "triangles", "kind"), selectmode="extended")
    tree.heading("#0", text="Actual model / mesh name")
    tree.column("#0", width=530)
    for key, width in (("vertices", 95), ("triangles", 95), ("kind", 200)):
        tree.heading(key, text=key.title())
        tree.column(key, width=width, stretch=False)
    scroll = ttk.Scrollbar(treeframe, orient="vertical", command=tree.yview)
    tree.configure(yscrollcommand=scroll.set)
    tree.pack(side="left", fill="both", expand=True)
    scroll.pack(side="right", fill="y")

    def refresh(*_):
        tree.delete(*tree.get_children())
        state["rows"] = {}
        for i, entry in enumerate(matching(state["catalog"]["models"], query.get())):
            key = f"m{i}"
            tree.insert("", "end", iid=key, text=entry["name"], values=(sum(m["vertices"] for m in entry["meshes"]),
                        sum(m["triangles"] for m in entry["meshes"]), "Player: skeleton metadata" if entry["skeleton"] else "Static"))
            state["rows"][key] = (entry, None)
            for j, mesh in enumerate(entry["meshes"]):
                child = f"{key}_{j}"
                tree.insert(key, "end", iid=child, text=mesh["name"], values=(mesh["vertices"], mesh["triangles"], "Mesh"))
                state["rows"][child] = (entry, mesh)

    query.trace_add("write", refresh)

    def selected_entries(all_visible=False):
        if all_visible:
            return matching(state["catalog"]["models"], query.get())
        grouped = {}
        for key in tree.selection():
            entry, mesh = state["rows"][key]
            item = grouped.setdefault(entry["source"], {**entry, "meshes": []})
            for part in ([mesh] if mesh else entry["meshes"]):
                if part not in item["meshes"]:
                    item["meshes"].append(part)
        return list(grouped.values())

    def do_export(all_visible=False, preview=False):
        entries = selected_entries(all_visible)
        if not entries:
            messagebox.showinfo("Select meshes", "Load a catalog and select a model or meshes first.", parent=root)
            return
        if preview and len(entries) != 1:
            messagebox.showinfo("Preview", "Select one model (or its mesh parts) for Blender preview.", parent=root)
            return
        dest, use_rig, exe = output.get(), rig.get(), blender.get()
        def job():
            report = export_models(entries, dest, use_rig, notify, cancel)
            if preview and report["exports"]:
                open_blender(report["exports"][0]["file"], exe)
            return report
        def done(report):
            state["last"] = report
            log.insert("end", json.dumps(report, indent=2) + "\n")
            status.set(f"Exported {len(report['exports'])} GLB(s); {len(report['errors'])} errors. {report['report_file']}")
        start(job, done)

    actions = ttk.Frame(panel)
    actions.grid(row=8, column=0, columnspan=3, sticky="ew", pady=8)
    add_button(actions, "Export selected", do_export)
    add_button(actions, "Export all filtered", lambda: do_export(True))
    add_button(actions, "Open selected in Blender", lambda: do_export(preview=True))
    ttk.Button(actions, text="Cancel", command=cancel.set).pack(side="right")
    ttk.Checkbutton(panel, text="Experimental player rig (bones + preview weights)", variable=rig).grid(row=9, column=0, columnspan=3, sticky="w")
    ttk.Label(panel, textvariable=totals, wraplength=1030).grid(row=10, column=0, columnspan=3, sticky="w", pady=4)
    progress = ttk.Progressbar(panel, mode="indeterminate")
    progress.grid(row=11, column=0, columnspan=3, sticky="ew")
    ttk.Label(panel, textvariable=status, wraplength=1030).grid(row=12, column=0, columnspan=3, sticky="w", pady=4)
    log = tk.Text(panel, height=7, wrap="word")
    log.grid(row=13, column=0, columnspan=3, sticky="ew")

    def pump():
        try:
            while True:
                kind, value = events.get_nowait()
                if kind == "status":
                    status.set(value)
                else:
                    state["busy"] = False
                    progress.stop()
                    for button in buttons:
                        button.configure(state="normal")
                    if kind == "done":
                        callback, result = value
                        callback(result)
                    else:
                        status.set(value.strip().splitlines()[-1])
                        log.insert("end", value + "\n")
                        try:
                            save_report(scratch.get(), "operation_error", {"exception": value})
                        except OSError:
                            pass
                    log.see("end")
        except queue.Empty:
            pass
        root.after(100, pump)

    def close():
        cancel.set()
        root.destroy()
    root.protocol("WM_DELETE_WINDOW", close)
    root.after(100, pump)
    root.mainloop()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command")
    gui = sub.add_parser("gui", help="Open responsive desktop mesh browser")
    gui.add_argument("source", nargs="?", type=Path)
    listing = sub.add_parser("list", help="List actual named triangle meshes; save catalog report")
    export = sub.add_parser("export", help="Export one GLB per matching source model")
    for command in (listing, export):
        command.add_argument("source", type=Path)
        command.add_argument("--filter", default="", help="Case insensitive model / mesh / source substring")
        command.add_argument("--scratch", type=Path)
    export.add_argument("output", type=Path)
    export.add_argument("--rig-preview", action="store_true", help="Experimental player bones + provisional weights; complete models only")
    export.add_argument("--open", action="store_true", help="Open first exported GLB in Blender")
    export.add_argument("--blender", type=Path)
    extract = sub.add_parser("extract", help="Extract supported DAT archives into a fresh scratch directory")
    extract.add_argument("source", type=Path)
    extract.add_argument("output", nargs="?", type=Path)
    extract.add_argument("--archive", type=str.upper, choices=("ALL", *ARCHIVES), default="ALL")
    extract.add_argument("--quickbms", type=Path)
    extract.add_argument("--script", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command in (None, "gui"):
            launch_gui(getattr(args, "source", None))
            return 0
        if args.command == "extract":
            report = extract_archives(args.source, args.output or default_scratch(), args.archive, args.quickbms, args.script, print)
            print(json.dumps(report, indent=2))
            return 1 if report["errors"] else 0
        report = catalog(args.source, print)
        report_path = save_report(args.scratch or default_scratch(getattr(args, "output", None)), "catalog_report", report)
        models = matching(report["models"], args.filter)
        if args.command == "list":
            for model in models:
                print(f"{model['name']}  [{model['source']}]")
                for mesh in model["meshes"]:
                    print(f"  {mesh['name']}: {mesh['vertices']} vertices, {mesh['triangles']} triangles")
            print(f"Catalog report: {report_path}\n{RIG_STATUS}")
            return 1 if report["errors"] else 0
        if not models:
            print(f"No matching meshes. Catalog report: {report_path}", file=sys.stderr)
            return 2
        result = export_models(models, args.output, args.rig_preview, print)
        if args.open and result["exports"]:
            open_blender(result["exports"][0]["file"], args.blender)
        return 1 if result["errors"] or report["errors"] else 0
    except KeyboardInterrupt:
        return 130
    except Exception as error:
        print(f"{type(error).__name__}: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
