"""Regenerates the embedded font headers from the google/fonts originals.

    pip install fonttools skia-pathops
    python3 fonts_gen.py SRC.ttf OUT.h C_NAME WGHT|-

Pins a variable font at the given weight (overlaps merged), subsets it to
printable ASCII plus the middle dot, multiplication sign and bullet, keeps
only kerning from the layout tables, drops hinting, and writes the result as
a C byte array. The shipped headers were made with:

    fonts_gen.py "Fredoka[wdth,wght].ttf" font_display.h g_font_display 700
    fonts_gen.py "Nunito[wght].ttf"       font_ui.h      g_font_ui      800

from https://github.com/google/fonts (ofl/fredoka, ofl/nunito). Both are SIL
Open Font License 1.1; the licence texts sit beside this file.
"""
import io
import sys

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

src, out, cname, wght = sys.argv[1:5]
font = TTFont(src)
if 'fvar' in font:
    loc = {a.axisTag: a.defaultValue for a in font['fvar'].axes}
    if wght != '-':
        loc['wght'] = float(wght)
    font = instancer.instantiateVariableFont(font, loc, overlap=instancer.OverlapMode.REMOVE)

opts = subset.Options()
opts.layout_features = ['kern']
opts.hinting = False
opts.desubroutinize = True
opts.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]   # keep copyright and licence
opts.notdef_outline = False
opts.glyph_names = False
opts.drop_tables += ['DSIG', 'STAT', 'MVAR', 'HVAR', 'gasp', 'prep', 'fpgm', 'cvt ', 'GDEF']
sub = subset.Subsetter(opts)
sub.populate(unicodes=list(range(0x20, 0x7F)) + [0xB7, 0xD7, 0x2022])
sub.subset(font)

buf = io.BytesIO()
font.save(buf)
data = buf.getvalue()
name = src.replace('\\', '/').split('/')[-1]
rows = [',' .join('0x%02x' % b for b in data[i:i + 20]) + ',' for i in range(0, len(data), 20)]
with open(out, 'w') as f:
    f.write('/* %s, wght %s, subset to printable Latin by fonts_gen.py.\n' % (name, wght))
    f.write('   SIL Open Font License 1.1: see the OFL text beside this file. */\n')
    f.write('static const unsigned char %s[%d] = {\n' % (cname, len(data)))
    f.write('\n'.join('    ' + r for r in rows))
    f.write('\n};\n')
print(out, len(data), 'bytes')
