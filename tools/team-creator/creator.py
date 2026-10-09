"""NFL 2K5 Team Creator: make a completely new team for the game.

    python tools\\team-creator\\creator.py        (or "Team Creator.cmd")

Team     -- city / school, nickname, abbreviation, colours, the uniform cut
            and crowd it starts from (an NFL team's), colours from a roster
Uniforms -- the new home and away uniforms, previewed from the player's own
            disc with the new colours
Roster   -- generated, or imported from a Madden or NCAA Football roster
            file (pick the team); 53 players, best per position
Build    -- writes mods\\teams\\<Team>; restart the game and the team is in
            Quick Game Team Select, after the historic teams

Needs the extracted game (original\\disc) beside this tool's game folder.
"""
from __future__ import annotations

import json
import queue
import threading
import tkinter as tk
from pathlib import Path
from tkinter import colorchooser, filedialog, messagebox, ttk

HERE = Path(__file__).resolve().parent
GAME = HERE.parent.parent
import sys  # noqa: E402
sys.path.insert(0, str(HERE))
import build_team  # noqa: E402
import game_assets as G  # noqa: E402
import rosters  # noqa: E402

try:
    from PIL import Image, ImageTk
except ImportError:            # previews need Pillow; everything else works without
    Image = ImageTk = None

NFL = [t for t in json.loads((HERE / "teams_2k5.json").read_text()) if t[0].isdigit() and int(t[0]) < 38
       and t[1] not in ("NFL", "AFC", "NFC", "USER1", "USER2")]


def parse_hex(s):
    s = s.lstrip("#")
    return [int(s[i:i + 2], 16) for i in (0, 2, 4)]


def base_colors(abbr):
    p = GAME / "mods" / "teams" / abbr / "team.json"
    try:
        c = json.loads(p.read_text(encoding="utf-8"))["colors"]
        return [parse_hex(c["primary"]), parse_hex(c["secondary"])]
    except Exception:
        return [[170, 0, 0], [255, 255, 255]]


class Creator(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("NFL 2K5 Team Creator")
        self.geometry("980x700")
        self.minsize(900, 620)
        self.colors = [[28, 28, 30], [211, 188, 141], [255, 255, 255]]
        self.roster: list[dict] = []
        self.ea: rosters.EARoster | None = None
        self.arc = None
        self.msgs: queue.Queue = queue.Queue()
        style = ttk.Style(self)
        style.configure("Big.TButton", font=("Segoe UI", 12, "bold"), padding=8)
        nb = ttk.Notebook(self)
        nb.pack(fill="both", expand=True, padx=10, pady=10)
        self.tab_team(nb)
        self.tab_uniforms(nb)
        self.tab_roster(nb)
        self.tab_build(nb)
        self.after(150, self.pump)
        self.after(50, self.open_disc)

    # ---- disc -----------------------------------------------------------
    def open_disc(self):
        try:
            self.arc = G.Archive(GAME / "original" / "disc")
        except Exception as e:
            messagebox.showwarning("Team Creator", f"The game's disc files were not found ({e}).\n"
                                   "Uniform previews and building need original\\disc.")

    # ---- Team -------------------------------------------------------------
    def tab_team(self, nb):
        f = ttk.Frame(nb, padding=16)
        nb.add(f, text="  Team  ")
        self.v_city = tk.StringVar(value="")
        self.v_nick = tk.StringVar(value="")
        self.v_abbr = tk.StringVar(value="")
        self.v_base = tk.StringVar(value="SF  San Francisco 49ers")
        row = 0
        for label, var, w in (("City or school", self.v_city, 32), ("Nickname", self.v_nick, 32),
                              ("Abbreviation (2-4 letters)", self.v_abbr, 8)):
            ttk.Label(f, text=label).grid(row=row, column=0, sticky="w", pady=6)
            ttk.Entry(f, textvariable=var, width=w, font=("Segoe UI", 11)).grid(row=row, column=1, sticky="w")
            row += 1
        ttk.Label(f, text="Uniform cut and crowd from").grid(row=row, column=0, sticky="w", pady=6)
        cb = ttk.Combobox(f, textvariable=self.v_base, state="readonly", width=36,
                          values=[f"{t[1]}  {t[2]} {t[3]}" for t in sorted(NFL, key=lambda t: t[2])])
        cb.grid(row=row, column=1, sticky="w")
        row += 1
        ttk.Label(f, text="Colours").grid(row=row, column=0, sticky="nw", pady=10)
        cf = ttk.Frame(f)
        cf.grid(row=row, column=1, sticky="w", pady=10)
        self.swatches = []
        for i, name in enumerate(("Primary", "Secondary", "Third")):
            b = tk.Button(cf, text=name, width=12, height=2, relief="groove",
                          command=lambda i=i: self.pick_color(i))
            b.grid(row=0, column=i, padx=6)
            self.swatches.append(b)
        self.paint_swatches()
        row += 1
        ttk.Label(f, text="Logo").grid(row=row, column=0, sticky="nw", pady=6)
        lf = ttk.Frame(f)
        lf.grid(row=row, column=1, sticky="w", pady=6)
        self.v_logo = tk.StringVar(value="")
        ttk.Button(lf, text="Choose logo image...", command=self.pick_logo).pack(side="left")
        ttk.Label(lf, textvariable=self.v_logo, foreground="#555").pack(side="left", padx=8)
        self.logo_lbl = ttk.Label(f)
        self.logo_lbl.grid(row=row, column=2, rowspan=3, padx=20, sticky="n")
        row += 1
        ttk.Label(f, text="The primary colour replaces the base team's first colour, the secondary its second.\n"
                          "Whites, greys and blacks keep their shading. Import a roster to take its colours.",
                  foreground="#555").grid(row=row, column=1, sticky="w")

    def pick_logo(self):
        p = filedialog.askopenfilename(title="Team logo (PNG with transparency works best)",
                                       filetypes=[("Images", "*.png *.jpg *.jpeg *.bmp *.gif"), ("All", "*.*")])
        if not p:
            return
        self.v_logo.set(p)
        if Image is not None:
            im = Image.open(p).convert("RGBA")
            im.thumbnail((160, 160))
            self.logo_tk = ImageTk.PhotoImage(im)
            self.logo_lbl.configure(image=self.logo_tk)

    def pick_color(self, i):
        c = colorchooser.askcolor(color="#%02x%02x%02x" % tuple(self.colors[i]), title="Team colour")
        if c and c[0]:
            self.colors[i] = [int(x) for x in c[0]]
            self.paint_swatches()

    def paint_swatches(self):
        for b, c in zip(self.swatches, self.colors):
            fg = "#000" if sum(c) > 380 else "#fff"
            b.configure(bg="#%02x%02x%02x" % tuple(c), fg=fg, activebackground="#%02x%02x%02x" % tuple(c))

    def base_team(self):
        abbr = self.v_base.get().split()[0]
        return next(t for t in NFL if t[1] == abbr)

    # ---- Uniforms ------------------------------------------------------------
    def tab_uniforms(self, nb):
        f = ttk.Frame(nb, padding=16)
        nb.add(f, text="  Uniforms  ")
        top = ttk.Frame(f)
        top.pack(fill="x")
        ttk.Button(top, text="Preview uniforms", command=self.preview).pack(side="left")
        self.v_prev = tk.StringVar(value="Shows the jersey and pants textures with your colours (home, then away).")
        ttk.Label(top, textvariable=self.v_prev, foreground="#555").pack(side="left", padx=12)
        self.canvas = tk.Canvas(f, bg="#20262b", highlightthickness=0)
        self.canvas.pack(fill="both", expand=True, pady=10)
        self.prev_imgs = []

    def preview(self):
        if not self.arc or Image is None:
            messagebox.showinfo("Team Creator", "Previews need the game's disc files and Pillow (pip install pillow).")
            return
        base = self.base_team()
        self.v_prev.set("Working...")
        self.update_idletasks()
        mapper = G.make_mapper([tuple(c) for c in base_colors(base[1])], [tuple(c) for c in self.colors[:2]])
        self.canvas.delete("all")
        self.prev_imgs.clear()
        x = 10
        for side in ("h", "a"):
            data = self.arc.read(f"{base[0]}{side}0.iff")
            if not data:
                continue
            new = G.recolor_iff(data, mapper)
            for w, h, rgba in G.texture_previews(new, 2):
                im = Image.frombytes("RGBA", (w, h), rgba).resize((220, 220))
                tkim = ImageTk.PhotoImage(im)
                self.prev_imgs.append(tkim)
                self.canvas.create_image(x, 30, image=tkim, anchor="nw")
                self.canvas.create_text(x + 110, 15, text="Home" if side == "h" else "Away", fill="#ddd")
                x += 232
        self.v_prev.set(f"Built on the {base[2]} {base[3]} cut. Your logo goes on the helmet when the team is built.")

    # ---- Roster ------------------------------------------------------------------
    def tab_roster(self, nb):
        f = ttk.Frame(nb, padding=16)
        nb.add(f, text="  Roster  ")
        bar = ttk.Frame(f)
        bar.pack(fill="x")
        ttk.Label(bar, text="Generate:").pack(side="left")
        self.v_strength = tk.IntVar(value=75)
        ttk.Scale(bar, from_=40, to=99, variable=self.v_strength, length=140).pack(side="left", padx=6)
        ttk.Button(bar, text="Generate roster", command=self.generate).pack(side="left", padx=6)
        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=12)
        ttk.Button(bar, text="Open Madden / NCAA roster...", command=self.open_roster).pack(side="left")
        self.v_team = tk.StringVar()
        self.teams_cb = ttk.Combobox(bar, textvariable=self.v_team, state="readonly", width=34)
        self.teams_cb.pack(side="left", padx=6)
        ttk.Button(bar, text="Import team", command=self.import_team).pack(side="left")
        self.v_rinfo = tk.StringVar(value="No roster yet.")
        ttk.Label(f, textvariable=self.v_rinfo, foreground="#555").pack(anchor="w", pady=6)
        cols = ("pos", "num", "first", "last", "ovr", "ht", "wt", "spd")
        self.tree = ttk.Treeview(f, columns=cols, show="headings", height=22)
        for c, w, t in zip(cols, (50, 50, 140, 160, 50, 60, 60, 50),
                           ("Pos", "#", "First", "Last", "OVR", "Height", "Weight", "Speed")):
            self.tree.heading(c, text=t)
            self.tree.column(c, width=w, anchor="w")
        self.tree.pack(fill="both", expand=True)
        self.tree.bind("<Double-1>", self.edit_player)

    def show_roster(self, source):
        self.tree.delete(*self.tree.get_children())
        for i, p in enumerate(self.roster):
            self.tree.insert("", "end", iid=str(i), values=(
                rosters.POSITIONS[p["pos"]], p["number"], p["first"], p["last"], p["ovr"],
                f"{p['height'] // 12}'{p['height'] % 12}\"", p["weight"], p["ratings"][0]))
        self.v_rinfo.set(f"{len(self.roster)} players ({source}). Double-click a player to rename or renumber.")

    def generate(self):
        self.roster = rosters.pick_53(rosters.generate(int(self.v_strength.get())))
        self.show_roster(f"generated, team strength {int(self.v_strength.get())}")

    def open_roster(self):
        p = filedialog.askopenfilename(title="Madden or NCAA Football roster")
        if not p:
            return
        try:
            self.ea = rosters.EARoster(p)
        except Exception as e:
            messagebox.showerror("Team Creator", f"Not a roster this tool reads:\n{e}")
            return
        self.ea_teams = self.ea.team_list()
        self.teams_cb.configure(values=[t[1] for t in self.ea_teams])
        if self.ea_teams:
            self.teams_cb.current(0)
        self.v_rinfo.set(f"{self.ea.kind} roster: {len(self.ea_teams)} teams. Pick one and Import team.")

    def import_team(self):
        if not self.ea or not self.v_team.get():
            return
        tgid = next(t[0] for t in self.ea_teams if t[1] == self.v_team.get())
        info = self.ea.team_info(tgid)
        self.roster = rosters.pick_53(self.ea.team_players(tgid))
        if not self.v_city.get():
            self.v_city.set(info["city"])
        if not self.v_nick.get():
            self.v_nick.set(info["nickname"])
        if not self.v_abbr.get():
            self.v_abbr.set(info["abbreviation"][:4])
        if any(info["primary"]) or any(info["secondary"]):
            self.colors[0], self.colors[1] = list(info["primary"]), list(info["secondary"])
            self.paint_swatches()
        self.show_roster(f"{self.ea.kind}: {self.v_team.get().split('  (')[0]}")

    def edit_player(self, _e):
        sel = self.tree.selection()
        if not sel:
            return
        p = self.roster[int(sel[0])]
        w = tk.Toplevel(self)
        w.title("Player")
        vs = {}
        for r, (k, label) in enumerate((("first", "First name"), ("last", "Last name"), ("number", "Number"))):
            ttk.Label(w, text=label).grid(row=r, column=0, padx=8, pady=4, sticky="w")
            vs[k] = tk.StringVar(value=str(p[k]))
            ttk.Entry(w, textvariable=vs[k]).grid(row=r, column=1, padx=8)

        def ok():
            p["first"], p["last"] = vs["first"].get()[:15], vs["last"].get()[:15]
            try:
                p["number"] = max(0, min(99, int(vs["number"].get())))
            except ValueError:
                pass
            self.show_roster("edited")
            w.destroy()
        ttk.Button(w, text="OK", command=ok).grid(row=3, column=1, pady=8, sticky="e")

    # ---- Build ---------------------------------------------------------------------
    def tab_build(self, nb):
        f = ttk.Frame(nb, padding=16)
        nb.add(f, text="  Build  ")
        ttk.Label(f, text="Builds the team into mods\\teams. It takes about a minute: every uniform\n"
                          "style, helmet and crowd file is made for your team.",
                  font=("Segoe UI", 10)).pack(anchor="w")
        ttk.Button(f, text="Build team", style="Big.TButton", command=self.build).pack(anchor="w", pady=12)
        self.log = tk.Text(f, height=20, bg="#111", fg="#cfe", font=("Consolas", 10))
        self.log.pack(fill="both", expand=True)

    def build(self):
        if not self.v_city.get().strip() or not self.v_nick.get().strip():
            messagebox.showinfo("Team Creator", "Give the team a city or school and a nickname first.")
            return
        if not self.roster:
            self.generate()
        base = self.base_team()
        spec = {"city": self.v_city.get().strip()[:15], "nickname": self.v_nick.get().strip()[:15],
                "abbreviation": (self.v_abbr.get().strip() or self.v_nick.get()[:3]).upper()[:4],
                "base": base[0], "base_colors": base_colors(base[1]), "colors": self.colors,
                "roster": self.roster, "logo": self.v_logo.get()}
        self.log.delete("1.0", "end")
        threading.Thread(target=self.run_build, args=(spec,), daemon=True).start()

    def run_build(self, spec):
        try:
            out = build_team.build(spec, GAME, progress=lambda m: self.msgs.put(m))
            self.msgs.put(f"\nBuilt in {out}.\nRestart the game: the team is in Quick Game Team Select after the historic teams.")
        except Exception as e:
            self.msgs.put(f"\nFailed: {e}")

    def pump(self):
        while not self.msgs.empty():
            self.log.insert("end", self.msgs.get() + "\n")
            self.log.see("end")
        self.after(150, self.pump)


if __name__ == "__main__":
    Creator().mainloop()
