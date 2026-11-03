import pathlib, re
here = pathlib.Path(__file__).resolve().parent
head = (here / "head.part").read_text()
tail = (here / "tail.part").read_text()
head = head.replace("<title>Recommended Scripts Mockup</title>", "<title>Perspective Tracking Mockup</title>")
css = """
.datafld { border: 1px solid var(--control); background: var(--field); padding: 6px 8px; width: 400px; height: 236px; overflow: hidden; white-space: pre; font-family: "JetBrains Mono", "Cascadia Mono", "DejaVu Sans Mono", monospace; font-size: 11.5px; line-height: 1.4; color: var(--text); }
.datafld.focus { border-color: var(--accent); }
.status { display: flex; gap: 6px; align-items: flex-start; font-size: 12px; width: 400px; min-height: 34px; }
.status.ok { color: var(--text); } .status.bad { color: var(--danger); } .status.warn { color: var(--warning); } .status.idle { color: var(--muted); }
.gridtbl { border: 1px solid var(--line); background: var(--field); font-size: 12px; }
.gridtbl .gr { display: grid; grid-template-columns: 34px 78px 78px 460px; min-height: 24px; align-items: center; border-bottom: 1px solid color-mix(in srgb, var(--line) 50%, transparent); }
.gridtbl .gr > span { padding: 0 6px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.gridtbl .gh { color: var(--muted); font-weight: 600; background: var(--raised); }
.gridtbl .gr.sel { background: var(--select); box-shadow: inset 3px 0 0 var(--accent); }
.num { font-variant-numeric: tabular-nums; }
"""
header = """</style>
</head>
<body>
<header class="top">
  <h1>FA13: Perspective tracking from Power-Pin data (design gate)</h1>
  <p class="intro">FA13 <a href="https://github.com/altqx/hikari/issues/570">#570</a>, the native counterpart of arch1t3cht's Aegisub Perspective-Motion 0.4.1. Each section draws one surface in Light, Dark and high contrast, with the app's theme roles and the icons from <code>src/ui/icons</code>. It adds no new icons. The data and the result lines are the third-party corpus run of the script (<code>tests/fixtures/automation-thirdparty</code>), which the native version must match. Green dots mark what is new. Answer the questions and the numbered recommendations with “yes” or a change.</p>
  <div class="controls"><label>High contrast <select id="hc"><option value="hcb">Black</option><option value="hcw">White</option></select></label>
  <span class="legend"><span class="dot new"></span> new</span></div>
</header>
<div id="main"></div>
<script>"""
head = re.sub(r"\.erow \{.*?</style>\n</head>\n<body>\n.*?<script>", lambda m: css + header, head, flags=re.S)
page = head + "\n" + (here / "scenes.js").read_text() + "\n" + tail
(here / "template.html").write_text(page)
print("template", len(page))
