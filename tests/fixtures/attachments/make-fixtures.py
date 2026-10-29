# Needs fontTools and Pillow. Run it from any directory; it writes beside itself.
import os

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
FAMILY = "HikariAttachedTest"


def make_font(path):
    fb = FontBuilder(1000, isTTF=True)
    fb.setupGlyphOrder([".notdef", "space", "box"])
    cmap = {ord(" "): "space"}
    for c in list(range(ord("A"), ord("Z") + 1)) + list(range(ord("a"), ord("z") + 1)):
        cmap[c] = "box"
    fb.setupCharacterMap(cmap)
    pen = TTGlyphPen(None)
    pen.moveTo((50, 0))
    pen.lineTo((50, 700))
    pen.lineTo((950, 700))
    pen.lineTo((950, 0))
    pen.closePath()
    fb.setupGlyf({".notdef": TTGlyphPen(None).glyph(), "space": TTGlyphPen(None).glyph(), "box": pen.glyph()})
    fb.setupHorizontalMetrics({".notdef": (1000, 0), "space": (500, 0), "box": (1000, 50)})
    fb.setupHorizontalHeader(ascent=800, descent=-200)
    fb.setupNameTable({"familyName": FAMILY, "styleName": "Regular",
                       "uniqueFontIdentifier": FAMILY + "-Regular", "fullName": FAMILY,
                       "psName": FAMILY + "-Regular", "version": "Version 1.000",
                       "licenseDescription": "Public domain test font"})
    fb.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200, achVendID="HKRI")
    fb.setupPost()
    fb.save(path)


def uuencode(data):
    out = []
    for i in range(0, len(data), 3):
        chunk = data[i:i + 3]
        n = len(chunk)
        b = chunk + bytes(3 - n)
        v = [b[0] >> 2, ((b[0] & 3) << 4) | (b[1] >> 4), ((b[1] & 15) << 2) | (b[2] >> 6), b[2] & 63]
        out.extend(chr(x + 33) for x in v[:n + 1])
    text = "".join(out)
    return [text[i:i + 80] for i in range(0, len(text), 80)]


def main():
    font_path = os.path.join(HERE, "HikariAttachedTest.ttf")
    png_path = os.path.join(HERE, "red.png")
    make_font(font_path)
    Image.new("RGBA", (16, 16), (255, 0, 0, 255)).save(png_path, optimize=True)
    font = open(font_path, "rb").read()
    png = open(png_path, "rb").read()
    lines = [
        "[Script Info]",
        "; HikariSub test script: an attached font and an attached image (make-fixtures.py)",
        "ScriptType: v4.00+",
        "PlayResX: 640",
        "PlayResY: 360",
        "WrapStyle: 0",
        "ScaledBorderAndShadow: yes",
        "",
        "[V4+ Styles]",
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
        "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
        "MarginR, MarginV, Encoding",
        "Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1",
        "Style: Attached," + FAMILY + ",100,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1",
        "",
        "[Fonts]",
        "fontname: " + FAMILY + "_0.ttf",
    ] + uuencode(font) + [
        "",
        "[Graphics]",
        "filename: red.png",
    ] + uuencode(png) + [
        "",
        "[Events]",
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text",
        "Dialogue: 0,0:00:00.00,0:00:10.00,Attached,,0,0,0,,{\\pos(40,40)}HH",
        "Dialogue: 0,0:00:00.00,0:00:10.00,Default,,0,0,0,,{\\pos(400,40)\\1img(red.png)\\p1}m 0 0 l 100 0 100 100 0 100{\\p0}",
    ]
    with open(os.path.join(HERE, "attached.ass"), "w", newline="") as f:
        f.write("\r\n".join(lines) + "\r\n")


if __name__ == "__main__":
    main()
