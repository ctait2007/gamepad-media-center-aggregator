"""Instance NuvioTV's variable UI fonts into the four static cuts borealis
loads (Regular/Medium/SemiBold/Bold), pinning every other axis at its default
so the result is the face the reference actually renders.

The PUA cmap entries are stripped for the same reason GMCA's own inter.ttf has
them stripped: borealis draws the bottom bar's controller glyphs from U+E0E0..,
and a text font that also maps that range wins the lookup and draws digits.
"""
import sys
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

SRC = "/home/user/nuviomedia/nuviotv/app/src/main/res/font"
OUT = "resources/font"
WEIGHTS = {"": 400, "_medium": 500, "_semibold": 600, "_bold": 700}

def strip_pua(font):
    removed = 0
    for table in font["cmap"].tables:
        for cp in [c for c in table.cmap if 0xE000 <= c <= 0xF8FF]:
            del table.cmap[cp]
            removed += 1
    return removed

for src, stem in (("dm_sans_variable", "dmsans"), ("opensans_variable", "opensans")):
    for suffix, wght in WEIGHTS.items():
        f = TTFont(f"{SRC}/{src}.ttf")
        axes = {a.axisTag: a.defaultValue for a in f["fvar"].axes}
        axes["wght"] = wght
        instancer.instantiateVariableFont(f, axes, inplace=True, updateFontNames=True)
        n = strip_pua(f)
        path = f"{OUT}/{stem}{suffix}.ttf"
        f.save(path)
        print(f"{path:38} wght={wght} pua_stripped={n}")
