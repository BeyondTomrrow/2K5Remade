"""Button icon assets for the Controller Setup work (2026-10-09).

    python tools/controller-art/render.py          # sheets and keys
    python tools/controller-art/photos.py          # controller pictures

Writes assets/controls/:
  sheet-<style>.png   512x512 button sheets in the game's 4x4 layout (Kenney
                      "Input Prompts", CC0) for xbox, ps (DualSense) and ps4
  keys/vkXX.png       keyboard key glyphs by Windows virtual-key code, for
                      keyboard prompts; keys/arrows.png for the d-pad

KENNEY_INPUT_PROMPTS points at the unpacked pack (kenney.nl/assets/input-prompts;
default build/downloads/kenney-input-prompts/x). The controller pictures
come from photos (photos.py).
"""
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, "assets", "controls")
KENNEY = os.environ.get("KENNEY_INPUT_PROMPTS",
                        os.path.join(ROOT, "build", "downloads", "kenney-input-prompts", "x"))


# The game's 128x128 sheet: 4x4 cells of 32x32.
#   A      Y      X      B
#   START  BACK   L-stk  (!)
#   L-trg  R-trg  White  Black
#   D-pad  L3     R3     R-stk
# This port: White = LB, Black = RB (src/nfl2k5_controls.c).
SHEETS = {
    "xbox": ["Xbox Series/Double/xbox_button_color_a.png", "Xbox Series/Double/xbox_button_color_y.png",
             "Xbox Series/Double/xbox_button_color_x.png", "Xbox Series/Double/xbox_button_color_b.png",
             "Xbox Series/Double/xbox_button_menu.png", "Xbox Series/Double/xbox_button_view.png",
             "Xbox Series/Double/xbox_stick_l.png", None,
             "Xbox Series/Double/xbox_lt.png", "Xbox Series/Double/xbox_rt.png",
             "Xbox Series/Double/xbox_lb.png", "Xbox Series/Double/xbox_rb.png",
             "Xbox Series/Double/xbox_dpad.png", "Xbox Series/Double/xbox_ls.png",
             "Xbox Series/Double/xbox_rs.png", "Xbox Series/Double/xbox_stick_r.png"],
    "ps": ["PlayStation Series/Double/playstation_button_color_cross.png",
           "PlayStation Series/Double/playstation_button_color_triangle.png",
           "PlayStation Series/Double/playstation_button_color_square.png",
           "PlayStation Series/Double/playstation_button_color_circle.png",
           "PlayStation Series/Double/playstation5_button_options.png",
           "PlayStation Series/Double/playstation5_button_create.png",
           "PlayStation Series/Double/playstation_stick_l.png", None,
           "PlayStation Series/Double/playstation_trigger_l2.png",
           "PlayStation Series/Double/playstation_trigger_r2.png",
           "PlayStation Series/Double/playstation_trigger_l1.png",
           "PlayStation Series/Double/playstation_trigger_r1.png",
           "PlayStation Series/Double/playstation_dpad.png",
           "PlayStation Series/Double/playstation_button_l3.png",
           "PlayStation Series/Double/playstation_button_r3.png",
           "PlayStation Series/Double/playstation_stick_r.png"],
}
# DualShock 4: the same, with its OPTIONS / SHARE buttons
SHEETS["ps4"] = list(SHEETS["ps"])
SHEETS["ps4"][4] = "PlayStation Series/Double/playstation4_button_options.png"
SHEETS["ps4"][5] = "PlayStation Series/Double/playstation4_button_share.png"


def icon(path, size):
    im = Image.open(os.path.join(KENNEY, path)).convert("RGBA")
    bb = im.getchannel("A").getbbox()
    im = im.crop(bb)
    s = size / max(im.size)
    return im.resize((max(1, round(im.size[0] * s)), max(1, round(im.size[1] * s))), Image.LANCZOS)


def sheets():
    for style, cells in SHEETS.items():
        sheet = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
        for i, p in enumerate(cells):
            if p is None:
                continue
            im = icon(p, 112)
            cx, cy = (i % 4) * 128 + 64, (i // 4) * 128 + 64
            sheet.alpha_composite(im, (cx - im.size[0] // 2, cy - im.size[1] // 2))
        sheet.save(os.path.join(OUT, "sheet-%s.png" % style))
        print("sheet", style)


# Keyboard glyphs by Windows virtual-key code.
KEYS = {}
for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789":
    KEYS[ord(c)] = c.lower()
KEYS.update({0x0D: "enter", 0x08: "backspace", 0x20: "space", 0x09: "tab", 0x1B: "escape", 0x10: "shift",
             0x11: "ctrl", 0x12: "alt", 0x26: "arrow_up", 0x28: "arrow_down", 0x25: "arrow_left",
             0x27: "arrow_right", 0x14: "capslock", 0x2D: "insert", 0x2E: "delete", 0x24: "home",
             0x23: "end", 0x21: "page_up", 0x22: "page_down", 0xBC: "comma", 0xBE: "period",
             0xBF: "slash_forward", 0xBA: "semicolon", 0xDE: "apostrophe", 0xDB: "bracket_open",
             0xDD: "bracket_close", 0xDC: "slash_back", 0xBD: "minus", 0xBB: "equals", 0xC0: "tilde",
             0xA0: "shift", 0xA1: "shift", 0xA2: "ctrl", 0xA3: "ctrl", 0xA4: "alt", 0xA5: "alt"})
for i in range(1, 13):
    KEYS[0x6F + i] = "f%d" % i


def keys():
    kd = os.path.join(OUT, "keys")
    os.makedirs(kd, exist_ok=True)
    done = 0
    for vk, name in KEYS.items():
        p = "Keyboard & Mouse/Double/keyboard_%s.png" % name
        if not os.path.exists(os.path.join(KENNEY, p)):
            print("no glyph for", name)
            continue
        icon(p, 112).save(os.path.join(kd, "vk%02X.png" % vk))
        done += 1
    icon("Keyboard & Mouse/Double/keyboard_arrows.png", 112).save(os.path.join(kd, "arrows.png"))
    for name in ("mouse_left", "mouse_right", "mouse_scroll"):
        p = "Keyboard & Mouse/Double/%s.png" % name
        if os.path.exists(os.path.join(KENNEY, p)):
            icon(p, 112).save(os.path.join(kd, name + ".png"))
    print("keys", done)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    what = sys.argv[1:] or ["sheets", "keys"]
    if "sheets" in what:
        sheets()
    if "keys" in what:
        keys()
