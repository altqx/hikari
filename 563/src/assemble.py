import pathlib
here = pathlib.Path(__file__).resolve().parent
src = (here.parent / "pl4-mockup" / "template.html").read_text().split("\n")
head = "\n".join(src[0:178])
helpers = "\n".join(src[189:243])
tail = "\n".join(src[439:])
css = """
.erow { position: relative; padding: 6px 8px 7px 20px; display: flex; flex-direction: column; gap: 1px; }
.erow.sel { background: var(--select); }
.erow.sel::before { content: ""; position: absolute; left: 0; top: 0; bottom: 0; width: 3px; background: var(--accent); }
.btn.small { height: 24px; padding: 0 8px; font-size: 12px; }
.toast { width: 360px; display: flex; gap: 10px; align-items: flex-start; padding: 10px 12px; background: var(--field); border: 1px solid var(--line); box-shadow: 2px 3px 0 color-mix(in srgb, var(--shadow) 25%, transparent); color: var(--text); }
"""
header = """</style>
</head>
<body>
<header class="top">
  <h1>FA6: Recommended scripts in the Automation panel (design gate)</h1>
  <p class="intro">FA6 <a href="https://github.com/altqx/hikari/issues/563">#563</a>. Each section draws one surface in Light, Dark and high contrast, with the app's theme roles and the icons from <code>src/ui/icons</code>. It adds no new icons. The six entries are arch1t3cht's scripts, which the card names, and each has a recorded run in the third-party corpus. Green dots mark what is new. Answer the questions and the numbered recommendations with “yes” or a change.</p>
  <div class="controls"><label>High contrast <select id="hc"><option value="hcb">Black</option><option value="hcw">White</option></select></label>
  <span class="legend"><span class="dot new"></span> new</span></div>
</header>
<div id="main"></div>
<script>"""
head = head.replace("<title>Plugin Panels Mockup</title>", "<title>Recommended Scripts Mockup</title>")
page = head + css + header + "\n" + helpers + "\n/*SCENES*/\n" + tail
page = page.replace("/*SCENES*/", (here / "scenes.js").read_text())
(here / "template.html").write_text(page)
print("template", len(page))
