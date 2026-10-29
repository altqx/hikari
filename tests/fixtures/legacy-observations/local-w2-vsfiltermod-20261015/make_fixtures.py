"""The scripts and the video of the legacy VSFilterMod captures.

    python make_fixtures.py <dir>            # scripts/*.ass and captures.json
    python make_fixtures.py <dir> --video    # also probe/media/w2-gradient.mkv (ffmpeg)
"""
import json
import struct
import subprocess
import sys
import zlib
from pathlib import Path

HEADER = (
    "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n{extra}"
    "\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
    "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, "
    "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,36,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,2,2,20,20,20,1\n"
    "{styles}"
)
EVENTS = "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"


def ass_time(ms):
    cs = ms // 10
    return "%d:%02d:%02d.%02d" % (cs // 360000, cs // 6000 % 60, cs // 100 % 60, cs % 100)


def png(w, h, pix):
    raw = b"".join(b"\0" + bytes(c for x in range(w) for c in pix(x, y)) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def quad(x, y):
    return [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 0, 255)][(x >= 32) + 2 * (y >= 32)]


def uuencode(data):
    out, line = [], ""
    for pos in range(0, len(data), 3):
        src = data[pos:pos + 3] + b"\0" * (3 - len(data[pos:pos + 3]))
        dst = [src[0] >> 2, ((src[0] & 3) << 4) | (src[1] >> 4), ((src[1] & 15) << 2) | (src[2] >> 6), src[2] & 63]
        for i in range(min(len(data) - pos + 1, 4)):
            line += chr(dst[i] + 33)
            if len(line) == 80 and pos + 3 < len(data):
                out.append(line)
                line = ""
    if line:
        out.append(line)
    return out


def dialogue(text, end="0:00:05.00"):
    return "Dialogue: 0,0:00:00.00,%s,Default,,0,0,0,,%s\n" % (end, text)


BOX = r"{\an7\pos(%d,%d)\bord0\shad0%s\p1}m 0 0 l 128 0 128 128 0 128{\p0}"

FIXTURES = {
    "positions": ("", "", [
        dialogue(r"{\an1}an1"),
        dialogue(r"{\an5}centre"),
        dialogue(r"{\an9}an9"),
        dialogue(r"{\pos(100,80)}pos"),
        dialogue(r"{\move(100,300,500,300,0,4000)}move"),
        "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,40,0,60,,margins\n",
    ], None, [0, 1000, 2500, 4000]),
    "styles": ("",
               "Style: Sign,Arial,48,&H0030A0E0,&H000000FF,&H00402010,&H00000000,-1,-1,-1,-1,120,80,4,10,1,3,4,8,"
               "10,10,10,1\nStyle: Box,Arial,30,&H4000FF00,&H000000FF,&H00FF0000,&H80000000,0,0,0,0,100,100,0,0,3,"
               "4,0,5,10,10,10,1\n", [
                   "Dialogue: 0,0:00:00.00,0:00:05.00,Sign,,0,0,0,,Bold italic underline\n",
                   "Dialogue: 0,0:00:00.00,0:00:05.00,Box,,0,0,0,,Opaque box\n",
                   dialogue(r"{\bord5\shad0\blur2\c&H0000FF&\3c&HFFFFFF&\alpha&H40&}blur "
                            r"{\be1\fscx150\frz15}be rotate"),
               ], None, [1000]),
    "clip": ("", "", [
        dialogue(r"{\an5\pos(320,120)\fs80\clip(250,90,390,140)}CLIPPED"),
        dialogue(r"{\an5\pos(320,200)\fs80\iclip(250,170,390,220)}INVERSE"),
        dialogue(r"{\an5\pos(320,290)\fs80\clip(m 220 250 l 420 250 320 330)}VECTOR"),
    ], None, [1000]),
    "karaoke": ("", "", [
        dialogue(r"{\an8\k50}ka{\k50}ra{\k100}o{\k100}ke", "0:00:04.00"),
        dialogue(r"{\an5\kf100}fill{\kf100}sweep", "0:00:04.00"),
        dialogue(r"{\an2\ko100}out{\ko100}line", "0:00:04.00"),
    ], None, [0, 250, 750, 1500, 2600]),
    "matrix-none": ("", "", [dialogue(r"{\c&H3080E0&}matrix")], None, [1000]),
    "matrix-tv601": ("YCbCr Matrix: TV.601\n", "", [dialogue(r"{\c&H3080E0&}matrix")], None, [1000]),
    "matrix-tv709": ("YCbCr Matrix: TV.709\n", "", [dialogue(r"{\c&H3080E0&}matrix")], None, [1000]),
    "img-attached": ("", "", [
        dialogue(BOX % (60, 60, r"\1img(w2quad.png)")),
        dialogue(BOX % (260, 60, r"\1img(w2quad.png,32,16)")),
        dialogue(BOX % (460, 60, r"\1img(w2quad.png)\t(0,2000,\1img(w2quad.png,32,32))")),
        dialogue(r"{\an2}attached image"),
    ], [("w2quad.png", png(64, 64, quad))], [0, 1000]),
}


def script_text(name):
    extra, styles, lines, graphics, times = FIXTURES[name]
    body = HEADER.format(extra=extra, styles=styles)
    if graphics:
        body += "\n[Graphics]\n"
        for fname, data in graphics:
            body += "filename: " + fname + "\n" + "\n".join(uuencode(data)) + "\n"
    body += EVENTS + "".join(lines)
    for ms in times:
        body += "Comment: 0,%s,%s,Default,,0,0,0,,w2 capture %d ms\n" % (ass_time(ms), ass_time(ms + 50), ms)
    return body


def main():
    out = Path(sys.argv[1])
    (out / "scripts").mkdir(parents=True, exist_ok=True)
    captures = []
    for name, (_, _, lines, _, times) in FIXTURES.items():
        (out / "scripts" / (name + ".ass")).write_bytes(script_text(name).encode("utf-8-sig"))
        for i, ms in enumerate(times):
            captures.append({"fixture": name, "ms": ms, "activeLine": len(lines) + i})
    (out / "captures.json").write_text(json.dumps(captures, indent=1) + "\n")
    if "--video" in sys.argv:
        media = out / "probe" / "media"
        media.mkdir(parents=True, exist_ok=True)
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "lavfi", "-i",
                        "nullsrc=s=640x360:r=20:d=6,geq=r='X*255/W':g='Y*255/H':b='128+64*sin(X/40)'",
                        "-c:v", "libx264", "-qp", "0", "-pix_fmt", "yuv420p", "-g", "1",
                        str(media / "w2-gradient.mkv")], check=True)


if __name__ == "__main__":
    main()
