# Writes the WS2 (#216) compatibility fixtures: each script without and
# with the [Hikari] section HikariSub writes, for renderers and other
# editors to read both and compare.
# - plain.ass / plain-hikari.ass: a script ending with its Events; the
#   section goes at the end of the file after a blank line.
# - fonts.ass / fonts-hikari.ass: the same with an attached font in [Fonts]
#   at the end, where legacy HikariSub writes attachments
#   (SubsGrid::SaveFile); the section goes before [Fonts], since libass reads
#   lines after [Fonts] as font data.
# layout.txt holds the "Layout=" value: a real arrangement's file form
# (WorkspaceArrangement::serialiseForFile, captured from the shell).
# hikari_core_hikari_section_tests checks that the rewrite writes exactly
# the -hikari files from the others. Run it from any directory; it writes
# beside itself.
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ATTACHMENTS = os.path.join(HERE, "..", "attachments")
FAMILY = "HikariAttachedTest"


def uuencode(data):
    # As tests/fixtures/attachments/make-fixtures.py (Aegisub's encoding).
    out = []
    for i in range(0, len(data), 3):
        chunk = data[i:i + 3]
        n = len(chunk)
        b = chunk + bytes(3 - n)
        v = [b[0] >> 2, ((b[0] & 3) << 4) | (b[1] >> 4), ((b[1] & 15) << 2) | (b[2] >> 6), b[2] & 63]
        out.extend(chr(x + 33) for x in v[:n + 1])
    text = "".join(out)
    return [text[i:i + 80] for i in range(0, len(text), 80)]


def script():
    return [
        "[Script Info]",
        "; HikariSub test script: the [Hikari] section's compatibility (make-fixtures.py)",
        "Title: WS2 compatibility",
        "ScriptType: v4.00+",
        "PlayResX: 640",
        "PlayResY: 360",
        "WrapStyle: 0",
        "ScaledBorderAndShadow: yes",
        "YCbCr Matrix: TV.709",
        "",
        "[V4+ Styles]",
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
        "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
        "MarginR, MarginV, Encoding",
        "Style: Default,Arial,36,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,1,2,20,20,20,1",
        "Style: Sign,Arial,28,&H0000FFFF,&H000000FF,&H00400000,&H00000000,1,0,0,0,100,100,0,0,1,3,0,8,10,10,10,1",
        "Style: Attached," + FAMILY + ",60,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1",
        "",
        "[Events]",
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text",
        "Dialogue: 0,0:00:00.00,0:00:04.00,Default,,0,0,0,,Bottom line one\\Nand a second row",
        "Dialogue: 1,0:00:00.50,0:00:03.50,Sign,,0,0,0,,{\\pos(320,60)\\frz8\\c&H3060FF&}Rotated sign",
        "Comment: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a comment line",
        "Dialogue: 0,0:00:01.00,0:00:04.00,Default,,0,0,0,,{\\an7\\pos(20,120)\\p1\\c&H00FF00&}m 0 0 l 80 0 80 40 0 40{\\p0}",
        "Dialogue: 0,0:00:00.00,0:00:04.00,Attached,,0,0,0,,{\\pos(460,140)}HH",
        "Dialogue: 0,0:00:02.00,0:00:04.00,Default,,0,0,0,,{\\an5\\pos(320,200)\\k50}Ka{\\k50}ra{\\k50}o{\\k50}ke",
    ]


def fonts():
    font = open(os.path.join(ATTACHMENTS, FAMILY + ".ttf"), "rb").read()
    return ["[Fonts]", "fontname: " + FAMILY + "_0.ttf"] + uuencode(font)


def write(name, lines):
    with open(os.path.join(HERE, name), "w", newline="") as f:
        f.write("\r\n".join(lines) + "\r\n")


def main():
    layout = open(os.path.join(HERE, "layout.txt")).read().strip()
    hikari = ["[Hikari]", "Layout=" + layout]
    write("plain.ass", script())
    write("plain-hikari.ass", script() + [""] + hikari)
    write("fonts.ass", script() + [""] + fonts())
    write("fonts-hikari.ass", script() + [""] + hikari + [""] + fonts())


if __name__ == "__main__":
    main()
