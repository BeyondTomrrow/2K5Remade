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

    # ---- Uniforms (granular editor + live preview) ---------------------------
    #
    # Each side (home / away) has its own uniform: colours for the jersey body,
    # collar/sleeve trim, pants, pant stripe, numbers + outline, helmet shell,
    # helmet centre stripe and facemask. The left column is the controls; the
    # right is a live 2D mock-up that redraws as colours change. "Render from
    # game textures" runs the real painter (logos.paint_*) on the base team's
    # own splayer/helmet so you can see the actual result before building.

    UNI_FIELDS = (                     # (key, label, kind)
        ("jersey_color", "Jersey", "color"),
        ("jersey_trim", "Collar / trim", "color"),
        ("pants_color", "Pants", "color"),
        ("pant_stripe", "Pant stripe", "stripe"),
        ("number_color", "Numbers", "color"),
        ("number_outline", "Number outline", "color"),
        ("helmet_color", "Helmet shell", "color"),
        ("helmet_stripe", "Helmet stripe", "stripe"),
        ("facemask_color", "Facemask", "color"),
    )

    def _default_uniform(self, away):
        pri = list(self.colors[0])
        sec = list(self.colors[1]) if len(self.colors) > 1 else [255, 255, 255]
        white = [245, 245, 245]
        body, other = (white, pri) if away else (pri, white)
        return {
            "jersey_color": list(body),
            "jersey_trim": list(other),
            "pants_color": list(other),
            "pant_stripe": {"on": True, "color": list(body), "width": 14},
            "number_color": list(other),
            "number_outline": list(body),
            "helmet_color": list(pri),
            "helmet_stripe": {"on": True, "color": list(sec), "width": 22},
            "facemask_color": [180, 180, 180],
        }

    def tab_uniforms(self, nb):
        if not hasattr(self, "uniforms"):
            self.uniforms = {"home": self._default_uniform(False), "away": self._default_uniform(True)}
        self.uni_side = tk.StringVar(value="home")
        f = ttk.Frame(nb, padding=12)
        nb.add(f, text="  Uniforms  ")

        top = ttk.Frame(f)
        top.pack(fill="x")
        ttk.Label(top, text="Editing:").pack(side="left")
        for val, txt in (("home", "Home"), ("away", "Away")):
            ttk.Radiobutton(top, text=txt, value=val, variable=self.uni_side,
                            command=self.uni_switch_side).pack(side="left", padx=4)
        ttk.Button(top, text="Reset from team colours", command=self.uni_reset).pack(side="left", padx=12)
        ttk.Button(top, text="Copy Home → Away", command=self.uni_copy_home_away).pack(side="left")

        body = ttk.Frame(f)
        body.pack(fill="both", expand=True, pady=8)
        ctl = ttk.Frame(body)
        ctl.pack(side="left", fill="y", padx=(0, 14))

        self.uni_btns = {}
        self.uni_toggle = {}
        self.uni_width = {}
        r = 0
        for key, label, kind in self.UNI_FIELDS:
            ttk.Label(ctl, text=label, width=16).grid(row=r, column=0, sticky="w", pady=3)
            b = tk.Button(ctl, text="", width=10, height=1, relief="groove",
                          command=lambda k=key: self.uni_pick(k))
            b.grid(row=r, column=1, padx=4)
            self.uni_btns[key] = b
            if kind == "stripe":
                tv = tk.BooleanVar(value=True)
                self.uni_toggle[key] = tv
                ttk.Checkbutton(ctl, text="on", variable=tv,
                                command=lambda k=key: self.uni_set_toggle(k)).grid(row=r, column=2, padx=4)
                wv = tk.IntVar(value=14)
                self.uni_width[key] = wv
                sp = ttk.Spinbox(ctl, from_=2, to=60, width=4, textvariable=wv,
                                 command=lambda k=key: self.uni_set_width(k))
                sp.grid(row=r, column=3, padx=2)
                sp.bind("<KeyRelease>", lambda e, k=key: self.uni_set_width(k))
                ttk.Label(ctl, text="px").grid(row=r, column=4, sticky="w")
            r += 1
        ttk.Separator(ctl, orient="horizontal").grid(row=r, column=0, columnspan=5, sticky="ew", pady=8)
        r += 1
        ttk.Button(ctl, text="Render from game textures", command=self.uni_render_real).grid(
            row=r, column=0, columnspan=3, sticky="w", pady=4)
        r += 1
        ttk.Button(ctl, text="Preview on player (3D)", command=self.uni_render_3d).grid(
            row=r, column=0, columnspan=3, sticky="w", pady=2)
        r += 1
        self.v_uni = tk.StringVar(value="Live mock-up. Numbers/stripes/helmet paint their regions distinctly on build.")
        ttk.Label(ctl, textvariable=self.v_uni, foreground="#557", wraplength=240,
                  justify="left").grid(row=r, column=0, columnspan=5, sticky="w", pady=6)

        self.canvas = tk.Canvas(body, bg="#2b2f36", highlightthickness=0, width=520)
        self.canvas.pack(side="left", fill="both", expand=True)
        self.prev_imgs = []
        self.uni_refresh_controls()
        self.after(60, self.uni_redraw)

    # -- state helpers --
    def _cur_uni(self):
        return self.uniforms[self.uni_side.get()]

    def uni_switch_side(self):
        self.uni_refresh_controls()
        self.uni_redraw()

    def uni_reset(self):
        side = self.uni_side.get()
        self.uniforms[side] = self._default_uniform(side == "away")
        self.uni_refresh_controls()
        self.uni_redraw()

    def uni_copy_home_away(self):
        self.uniforms["away"] = json.loads(json.dumps(self.uniforms["home"]))
        if self.uni_side.get() == "away":
            self.uni_refresh_controls()
            self.uni_redraw()

    def uni_refresh_controls(self):
        u = self._cur_uni()
        for key, label, kind in self.UNI_FIELDS:
            c = u[key]["color"] if kind == "stripe" else u[key]
            self._paint_btn(self.uni_btns[key], c)
            if kind == "stripe":
                self.uni_toggle[key].set(bool(u[key].get("on", True)))
                self.uni_width[key].set(int(u[key].get("width", 14)))

    def _paint_btn(self, btn, rgb):
        hexc = "#%02x%02x%02x" % tuple(rgb)
        fg = "#000" if sum(rgb) > 380 else "#fff"
        btn.configure(bg=hexc, fg=fg, activebackground=hexc, text=hexc.upper())

    def uni_pick(self, key):
        u = self._cur_uni()
        kind = dict((k, kd) for k, _, kd in self.UNI_FIELDS)[key]
        cur = u[key]["color"] if kind == "stripe" else u[key]
        c = colorchooser.askcolor(color="#%02x%02x%02x" % tuple(cur), title="Uniform colour")
        if not (c and c[0]):
            return
        rgb = [int(x) for x in c[0]]
        if kind == "stripe":
            u[key]["color"] = rgb
        else:
            u[key] = rgb
        self._paint_btn(self.uni_btns[key], rgb)
        self.uni_redraw()

    def uni_set_toggle(self, key):
        self._cur_uni()[key]["on"] = bool(self.uni_toggle[key].get())
        self.uni_redraw()

    def uni_set_width(self, key):
        try:
            self._cur_uni()[key]["width"] = max(2, min(60, int(self.uni_width[key].get())))
        except (ValueError, tk.TclError):
            return
        self.uni_redraw()

    # -- serialise to the build / apply_logo format --
    def uniforms_to_spec(self):
        out = {}
        for side, u in self.uniforms.items():
            d = {k: list(u[k]) for k in ("jersey_color", "jersey_trim", "pants_color",
                                         "number_color", "number_outline", "helmet_color", "facemask_color")}
            ps = u["pant_stripe"]
            d["pant_stripe"] = {"color": list(ps["color"]), "width": int(ps["width"]),
                                "style": "solid" if ps.get("on", True) else "none"}
            hs = u["helmet_stripe"]
            d["helmet_stripe"] = {"color": list(hs["color"]), "width": int(hs["width"])} if hs.get("on", True) else None
            out[side] = d
        return out

    # -- live 2D mock-up --
    @staticmethod
    def _hx(rgb):
        return "#%02x%02x%02x" % tuple(int(v) for v in rgb)

    def uni_redraw(self):
        if not hasattr(self, "canvas"):
            return
        cv = self.canvas
        cv.delete("all")
        u = self._cur_uni()
        W = cv.winfo_width() or 520
        cx = W // 2
        # ---- Helmet (top, side profile) ----
        hx = self._hx
        shell = u["helmet_color"]
        cv.create_text(cx, 10, text=self.uni_side.get().upper() + " UNIFORM", fill="#cdd", font=("Segoe UI", 10, "bold"))
        hy = 30
        # shell dome
        cv.create_arc(cx - 70, hy, cx + 70, hy + 120, start=0, extent=180, fill=hx(shell), outline="#111", width=2)
        cv.create_rectangle(cx - 70, hy + 58, cx + 70, hy + 60, fill=hx(shell), outline="")
        # center stripe
        hs = u["helmet_stripe"]
        if hs.get("on", True):
            w = max(4, int(hs["width"]) // 2)
            cv.create_arc(cx - w, hy, cx + w, hy + 120, start=0, extent=180, fill=hx(hs["color"]), outline="")
        # facemask
        cv.create_arc(cx + 40, hy + 40, cx + 120, hy + 110, start=200, extent=110,
                      style="arc", outline=hx(u["facemask_color"]), width=5)
        # logo dot
        cv.create_oval(cx - 34, hy + 34, cx - 6, hy + 62, fill=hx(u["jersey_color"]), outline="#fff")
        # ---- Jersey ----
        jy = 170
        jc, tc = hx(u["jersey_color"]), hx(u["jersey_trim"])
        # torso
        cv.create_polygon(cx - 55, jy + 25, cx - 55, jy + 120, cx + 55, jy + 120, cx + 55, jy + 25,
                          fill=jc, outline="#111", width=2)
        # sleeves
        cv.create_polygon(cx - 55, jy + 25, cx - 90, jy + 55, cx - 78, jy + 80, cx - 55, jy + 60, fill=jc, outline="#111")
        cv.create_polygon(cx + 55, jy + 25, cx + 90, jy + 55, cx + 78, jy + 80, cx + 55, jy + 60, fill=jc, outline="#111")
        # sleeve trim stripes
        cv.create_line(cx - 86, jy + 52, cx - 66, jy + 64, fill=tc, width=5)
        cv.create_line(cx + 86, jy + 52, cx + 66, jy + 64, fill=tc, width=5)
        # collar
        cv.create_arc(cx - 20, jy + 10, cx + 20, jy + 44, start=180, extent=180, style="arc", outline=tc, width=6)
        # number
        nc, no = hx(u["number_color"]), hx(u["number_outline"])
        cv.create_text(cx, jy + 74, text="00", font=("Arial Black", 34), fill=no)
        cv.create_text(cx, jy + 73, text="00", font=("Arial Black", 30), fill=nc)
        # ---- Pants ----
        py = jy + 128
        pc = hx(u["pants_color"])
        cv.create_polygon(cx - 46, py, cx + 46, py, cx + 42, py + 95, cx + 8, py + 95,
                          cx, py + 40, cx - 8, py + 95, cx - 42, py + 95, fill=pc, outline="#111", width=2)
        ps = u["pant_stripe"]
        if ps.get("on", True):
            sw = max(2, int(ps["width"]) // 2)
            scol = hx(ps["color"])
            cv.create_line(cx - 30, py + 4, cx - 26, py + 90, fill=scol, width=sw)
            cv.create_line(cx + 30, py + 4, cx + 26, py + 90, fill=scol, width=sw)

    def uni_render_real(self):
        """Run the actual region painters on the base team's own textures so the
        user sees the real result, not only the mock-up."""
        if not self.arc or Image is None:
            messagebox.showinfo("Team Creator", "Rendering needs the game's disc files and Pillow.")
            return
        import logos
        base = self.base_team()
        side = self.uni_side.get()
        spec_u = self.uniforms_to_spec()[side]
        bc = [tuple(c) for c in base_colors(base[1])]
        self.v_uni.set("Rendering from game textures...")
        self.update_idletasks()
        data = self.arc.read(f"{base[0]}{'a' if side == 'away' else 'h'}0.iff")
        texs = {}
        for c in G.T.parse_chunks(data, allow_trailing=True):
            if c.kind != "TXTR" or not c.compressed:
                continue
            dec, _ = G.T.decode_chunk(data, c)
            try:
                info = G.T.parse_texture(dec, c)
            except Exception:
                continue
            if info.format_code == 0x0B and info.name in ("splayer", "helmet00", "48"):
                texs[info.name] = Image.frombytes("RGBA", (info.width, info.height),
                                                  G.T.texture_to_rgba(dec, c, info))
        self.canvas.delete("all")
        self.prev_imgs.clear()
        x = 10
        if "splayer" in texs:
            im = logos.paint_splayer(texs["splayer"], spec_u, bc, side == "away").resize((320, 160))
            self._show_tk(im, x, 30, "On-model jersey + pants")
            x += 332
        if "helmet00" in texs:
            hs = logos.paint_helmet_shell(texs["helmet00"], spec_u["helmet_color"])
            if spec_u.get("helmet_stripe"):
                hs = logos.draw_helmet_stripe(hs, spec_u["helmet_stripe"])
            self._show_tk(hs.resize((160, 160)), x, 30, "Helmet")
            x = 10
        if "48" in texs:
            self._show_tk(logos.paint_number(texs["48"], spec_u).resize((96, 96)), 342, 210, "Number")
        self.v_uni.set("Real textures painted from the base cut. Click a swatch to adjust and re-render.")

    def _show_tk(self, im, x, y, label):
        tkim = ImageTk.PhotoImage(im)
        self.prev_imgs.append(tkim)
        self.canvas.create_image(x, y, image=tkim, anchor="nw")
        self.canvas.create_text(x + im.width // 2, y - 12, text=label, fill="#ddd")

    # -- real 3D player-model preview (the actual game mesh + generated textures) --
    def uni_render_3d(self):
        """Render the DESIGNED uniform on the real NFL 2K5 player model (body +
        helmet), textured with the textures the Team Creator generates, and show
        front + back. The mesh is decoded from original\\disc once and cached."""
        if not self.arc or Image is None:
            messagebox.showinfo("Team Creator", "The 3D preview needs the game's disc files and Pillow.")
            return
        if getattr(self, "_rendering_3d", False):
            return
        try:
            import model_preview  # noqa: F401
        except Exception as e:
            messagebox.showerror("Team Creator", f"3D preview module could not load:\n{e}")
            return
        self._rendering_3d = True
        side = self.uni_side.get()
        base = self.base_team()
        spec_side = self.uniforms_to_spec()[side]
        self.v_uni.set("Decoding the real player model and rendering... (a few seconds)")
        self.update_idletasks()
        self._render3d_box = []
        threading.Thread(target=self._render3d_worker, args=(base[0], spec_side, side), daemon=True).start()
        self.after(120, self._poll_3d)

    def _render3d_worker(self, base_code, spec_side, side):
        try:
            import model_preview
            if getattr(self, "_player_model", None) is None:
                self._player_model = model_preview.PlayerModel(GAME / "original" / "disc")
            textures = model_preview.textures_for_uniform(self.arc, base_code, spec_side, side == "away")
            imgs = model_preview.render_player(self._player_model, textures, w=235, h=350,
                                               views=("front", "back"))
            out = {v: Image.fromarray(a, "RGB") for v, a in imgs.items()}
            self._render3d_box.append(("ok", side, out))
        except Exception as e:
            import traceback
            self._render3d_box.append(("err", side, f"{e}\n{traceback.format_exc()}"))

    def _poll_3d(self):
        if not self._render3d_box:
            self.after(150, self._poll_3d)
            return
        self._rendering_3d = False
        status, side, payload = self._render3d_box[0]
        if status == "err":
            self.v_uni.set("3D preview failed (see dialog).")
            messagebox.showerror("Team Creator", f"3D preview failed:\n{payload}")
            return
        self.canvas.delete("all")
        self.prev_imgs.clear()
        x = 12
        for v in ("front", "back"):
            if v in payload:
                self._show_tk(payload[v], x, 24, f"{side.title()} - {v}")
                x += payload[v].width + 16
        self.v_uni.set("Real NFL 2K5 player model (bind pose) in your " + side + " uniform. "
                       "Body uses the generated splayer texture; helmet the helmet00 texture. "
                       "Switch Home/Away and click again.")

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
                "roster": self.roster, "logo": self.v_logo.get(),
                "uniforms": self.uniforms_to_spec() if hasattr(self, "uniforms") else None}
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
