# SV1 #365 crops sheet: real before shots (qt 6c0d80daa build) vs. the proposal rendered with libass at screen size.
import pathlib
OUT = pathlib.Path(__file__).resolve().parent
ROWS = [
    ("full-sign", 1, 640, "Fullscreen, 100 % scale (1920×1080 screen, 640×360 video: 3×)"),
    ("full-sign", 2, 640, "Fullscreen, 200 % scale (3840×2160 screen: 6×)"),
    ("panel-sign", 1, 600, "Video panel, 100 % scale (picture 736×414)"),
    ("panel-sign", 2, 600, "Video panel, 200 % scale (picture 1472×828 device pixels)"),
    ("panel-line", 1, 800, "Video panel, 100 %, the bottom line"),
    ("panel-line", 2, 800, "Video panel, 200 %, the bottom line"),
]
css = """*{box-sizing:border-box;margin:0;padding:0}
body{background:#171B20;color:#E8EDF2;font:13px 'Liberation Sans',Arial,sans-serif;padding:24px;width:1880px}
h1{font-size:18px;font-weight:600;margin:0 0 8px} h2{font-size:13px;font-weight:650;margin:22px 0 8px}
p{color:#A5B1BD;font-size:12.5px;line-height:1.45;max-width:1700px;margin:0 0 6px}
.r{display:flex;gap:24px} .c{display:flex;flex-direction:column;gap:4px}
.k{font-size:11.5px;color:#A5B1BD} img{image-rendering:pixelated;display:block;outline:1px solid #414B57}"""
body = ["<h1>SV1 #365 — before / after crops</h1>",
        "<p><b>Before</b>: real screenshots of today’s app (qt 6c0d80daa build), under Xvfb with QT_SCALE_FACTOR 1 and 2, "
        "Text Z rotation tool on the sign line. <b>After</b>: the proposal, not the app: libass draws the same script at the "
        "picture’s on-screen pixel size with its storage size kept at the video’s (ffmpeg’s libass filter, "
        "original_size=640x360), and the tool’s ring is drawn at legacy’s sizes in logical pixels (so 2× the "
        "device pixels at 200 %). Every crop is shown at the same on-page size; 100 % crops are magnified with "
        "nearest-neighbour so pixels stay visible.</p>"]
for name, s, w, title in ROWS:
    body.append(f"<h2>{title}</h2><div class='r'>")
    for side, label in (("before", "Before — Video resolution (today)"), ("after", "After — Screen size")):
        body.append(f"<div class='c'><span class='k'>{label}</span><img src='crops/{name}-s{s}-{side}.png' width='{w}'></div>")
    body.append("</div>")
(OUT / "sv1-crops.html").write_text(f"<!doctype html><html><head><meta charset='utf-8'><style>{css}</style></head><body>{''.join(body)}</body></html>")
