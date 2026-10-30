"""Export the supplied wide Ringside outlines without changing the font file."""
import json
import math
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

root = Path(__file__).resolve().parents[2]
font = TTFont(root / 'mods/presentations/NBC_SNF/fonts/RingsideWide-BlackItalic-TD.woff')
glyph_set = font.getGlyphSet()
cmap = font.getBestCmap()
# Font coordinates are y-up; remove the font's rightward italic slant,
# then reflect y for SVG. The source WOFF remains byte-for-byte intact.
shear = math.tan(math.radians(font['post'].italicAngle))
transform = (1, 0, shear, -1, 0, 0)
glyphs = {}
for ch in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789- .:':
    name = cmap.get(ord(ch))
    if not name:
        raise ValueError(f'Missing glyph: {ch!r}')
    path = SVGPathPen(glyph_set)
    bounds = BoundsPen(glyph_set)
    glyph_set[name].draw(TransformPen(path, transform))
    glyph_set[name].draw(TransformPen(bounds, transform))
    glyphs[ch] = {'path': path.getCommands(), 'bounds': bounds.bounds,
                  'advance': font['hmtx'].metrics[name][0]}
target = root / 'mods/presentations/NBC_SNF/wide-glyph-data.js'
target.write_text('/* Outlines from the user-supplied Ringside Wide Black Italic; upright geometry. */\n'
                  + 'window.SNFWideGlyphData=' + json.dumps(glyphs, separators=(',', ':')) + ';\n',
                  encoding='utf-8')
print(f'Exported {len(glyphs)} supplied glyphs to {target}')
