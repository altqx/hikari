const THEMES = [["t-light", "Light"], ["t-dark", "Dark"], ["t-hcw", "High contrast White"], ["t-hcb", "High contrast Black"]];
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const CHECK = '<svg viewBox="0 0 10 10" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M1.5 5.2l2.3 2.3 4.7-5"/></svg>';
const ARROW = '<svg viewBox="0 0 20 20"><path d="M3 2v14l4-4 3 6 2-1-3-6h6z" fill="#fff" stroke="#000" stroke-width="1"/></svg>';
const EW = '<svg viewBox="0 0 20 20"><path d="M1 10l4-4v3h10V6l4 4-4 4v-3H5v3z" fill="#fff" stroke="#000" stroke-width=".8"/></svg>';
const MOVE = '<svg viewBox="0 0 20 20"><path d="M10 1l3 3h-2v5h5V7l3 3-3 3v-2h-5v5h2l-3 3-3-3h2v-5H4v2l-3-3 3-3v2h5V4H7z" fill="#fff" stroke="#000" stroke-width=".8"/></svg>';

function icon(role, state) {
  const svg = ICONS[role] || "";
  const acc = state === "dis" ? "var(--dis)" : state === "hl" ? "var(--on)" : "var(--accent)";
  const base = state === "dis" ? "var(--dis)" : state === "hl" ? "var(--on)" : "currentColor";
  return svg.replace("<svg ", `<svg style="color:${base}" `).replace('<g id="accent">', `<g style="color:${acc}" stroke="currentColor">`);
}

const field = (label, value) => `<div class="lbl">${esc(label)}</div><div class="field">${esc(value)}</div>`;
const PANELS = {
  Agent: { title: "Agent", icon: "ai-assistant", key: "", body: () =>
    `<div class="bubble me">Retime the signs in scene 3 to the keyframes.</div>
     <div class="bubble">I snapped 6 sign lines to the nearest keyframes. They are waiting in Review.</div>
     <div style="flex:1"></div><div class="field">Ask Hikari AI…</div>` },
  Review: { title: "Review", icon: "review-panel", body: () =>
    `<div class="bubble">6 changes from the agent</div><div class="field">Line 41: start 0:03:12.40 → 0:03:12.37</div><div class="field">Line 42: end 0:03:15.02 → 0:03:14.99</div><div><span class="btn accent">Keep all</span> <span class="btn">Undo all</span></div>` },
  Explorer: { title: "Explorer", icon: "explorer", body: () => {
    const f = ICONS["folder-open"] || "";
    const rows = [["▾ ", "show-s01", 0, 1], ["", "episode-01.ass", 1, 0, 1], ["", "episode-02.ass", 1], ["", "episode-01.mkv", 1], ["▸ ", "fonts", 1, 1], ["▸ ", "keyframes", 1, 1]];
    return `<div class="tree">${rows.map(([a, n, d, dir, sel]) => `<div class="${sel ? "sel" : ""}" style="padding-inline-start:${d * 14}px">${a}${dir ? f : ""}${esc(n)}</div>`).join("")}</div>`; } },
  Search: { title: "Find and replace", icon: "find-replace", key: "Ctrl+H", body: () =>
    `${field("Find", "Kaori")}${field("Replace with", "Kaoru")}<div><span class="btn">Find next</span> <span class="btn">Replace all</span></div>` },
  Timing: { title: "Shift times", icon: "shift-times", key: "Ctrl+I", body: () =>
    `${field("Shift by", "0:00:01.50")}<div class="lbl">◉ Forward ○ Backward</div><div><span class="btn accent">Apply</span></div>` },
  Postprocessor: { title: "Timing postprocessor", icon: "timing-postprocessor", body: () => `${field("Lead-in", "120 ms")}${field("Lead-out", "300 ms")}` },
  Styles: { title: "Styles", icon: "styles", body: () => `<div class="field">Default</div><div class="field">Sign</div><div class="field">Top</div>` },
  QcNotes: { title: "QC notes", icon: "qc-notes", body: () => `<div class="field">Line 12: typo in “Kaori”</div>` },
  Karaoke: { title: "Karaoke helper", icon: "plugins", plugin: true, body: () =>
    `<div class="lbl">Plugin panel (manifest area: left)</div>${field("Syllables", "ka·o·ri")}<div><span class="btn">Split</span></div>` },
};
const LEFT = ["Agent", "Review"];
const RIGHT = ["Explorer", "Search", "Timing", "Postprocessor", "Styles", "QcNotes"];

function strip(bar) {
  let html = "";
  bar.panels.forEach((p, i) => {
    if (bar.dropAt === i) html += '<div class="drophere"></div>';
    const cls = [p === bar.shown ? "on" : "", bar.hover === p ? "hover" : "", bar.kfocus === p ? "kfocus" : "", bar.ghost === p ? "ghost" : ""].join(" ");
    html += `<div class="sbtn ${cls}">${icon(PANELS[p].icon)}${PANELS[p].plugin ? '<span class="plug"></span>' : ""}</div>`;
  });
  if (bar.dropAt === bar.panels.length) html += '<div class="drophere"></div>';
  return `<div class="strip${bar.drop ? " droptarget" : ""}">${html}</div>`;
}

function sidebar(side, bar) {
  if (bar.hidden) return "";
  const grip = bar.grip ? '<div class="grip lit"></div>' : "";
  const w = bar.width || 250;
  if (!bar.panels.length || !bar.shown)
    return `<div class="sbar ${side}" style="width:${w}px">${grip}${strip(bar)}<div class="empty">Drag a panel here</div></div>`;
  const p = PANELS[bar.shown];
  return `<div class="sbar ${side}" style="width:${w}px">${grip}${strip(bar)}<div class="phead">${esc(p.title)}<span class="tb${bar.menuLit ? " lit" : ""}">${icon("panel-menu")}</span>${bar.focus ? '<span class="fmark"></span>' : ""}</div><div class="pbody">${p.body()}</div></div>`;
}

function middle(o) {
  const wave = Array.from({ length: 70 }, (_, i) => `<i style="left:${4 + i * 6}px;height:${8 + Math.abs(Math.sin(i * 0.7) * 22 + Math.sin(i * 1.9) * 9)}px;transform:translateY(50%)"></i>`).join("");
  const rows = [["#", "Start", "Style", "Text"], [40, "0:03:10.21", "Default", "Where did Kaori go?"], [41, "0:03:12.37", "Sign", "Station North Exit"], [42, "0:03:14.99", "Default", "She left before the rain."], [43, "0:03:17.50", "Default", "Then we wait."], [44, "0:03:20.02", "Top", "(train passing)"], [45, "0:03:22.80", "Default", "Not for long."]];
  const grid = rows.map((r, i) => `<div class="grow${i === 0 ? " h" : ""}${i === 2 ? " sel" : ""}"><span>${r[0]}</span><span class="t">${r[1]}</span><span class="t">${r[2]}</span><span>${esc(r[3])}</span></div>`).join("");
  return `<div class="mid"><div class="mp"><div class="phead">Video</div><div class="video">Station North Exit</div></div>
    <div class="mp"><div class="phead">Audio</div><div class="wave">${wave}</div><div class="phead" style="border-top:1px solid var(--line)">Line editor</div><div class="pbody"><div class="field" style="color:var(--text)">She left before the rain.</div></div></div>
    <div class="mp span2"><div class="phead tabs"><span class="ptab sel">Grid</span>${o.middleFind ? '<span class="ptab">Find and replace</span>' : ""}</div><div class="grid">${grid}</div></div></div>`;
}

const STRIP_TOP = 58;
function iconX(o, side, i) {
  const w = (side === "start" ? o.start : o.end).width || 250;
  return (side === "start" ? 0 : 1000 - w) + 5 + i * 32;
}

function frame(o) {
  const s = o.start, e = o.end;
  let body = sidebar("left", s) + middle(o) + sidebar("right", e);
  let extra = "";
  if (o.tip) {
    const t = o.tip, x = t.x ?? iconX(o, t.side, t.i), top = t.top ?? STRIP_TOP + 34;
    const left = Math.min(x, 1000 - 12 - 8 * (t.text.length + (t.k ? t.k.length + 2 : 0)));
    extra += `<div class="tip" style="top:${top}px;left:${left}px">${esc(t.text)}${t.k ? `<span class="k">${esc(t.k)}</span>` : ""}</div>`;
    extra += `<div class="cursor" style="top:${top - 18}px;left:${x + 14}px">${ARROW}</div>`;
  }
  if (o.drag) extra += `<div class="dragicon" style="top:${o.drag.top}px;left:${o.drag.x}px">${icon(PANELS[o.drag.p].icon)}</div><div class="cursor" style="top:${o.drag.top + 14}px;left:${o.drag.x + 16}px">${MOVE}</div>`;
  if (o.grip) extra += `<div class="cursor" style="top:${o.grip.top}px;right:${o.grip.x - 10}px">${EW}</div><div class="wlabel" style="top:${o.grip.top + 22}px;right:${o.grip.x - 30}px">${esc(o.grip.label)}</div>`;
  const tabs = o.home ? '<span class="dtab sel">Home</span>' : '<span class="dtab sel">episode-01.ass</span><span class="dtab">episode-02.ass</span>';
  const tg = (role, on, lit) => `<span class="tgl${on ? " on" : ""}${lit ? " hover" : ""}">${icon(role)}</span>`;
  const toggles = `<span class="toggles">${tg("side-bar-left", !s.hidden, o.litToggle === "left")}${tg("side-bar-right", !e.hidden, o.litToggle === "right")}</span>`;
  return `<div class="win"${o.rtl ? ' dir="rtl"' : ""}><div class="titlebar"><span class="m">File</span><span class="m">Edit</span><span class="m">Subtitle</span><span class="m">Timing</span><span class="m">Video</span><span class="m">Audio</span><span class="m">View</span><span class="m">Help</span><span class="switch">${icon("layout")} Editing ▾</span>${toggles}<span class="wb">–□×</span></div>
    <div class="tabsrow">${tabs}</div><div class="body">${body}</div><div class="status"><span>Line 42 of 318</span><span>0:03:14.99</span><span>show-s01</span></div>${extra}</div>`;
}

function bar(panels, shown, extra = {}) { return Object.assign({ panels: panels.slice(), shown, width: 250 }, extra); }

function row(it) {
  if (it.sep) return '<div class="sep"></div>';
  const state = it.dis ? "dis" : it.hl ? "hl" : "";
  const chk = it.chk ? `<span class="box" style="width:14px;height:14px;border:1px solid ${state === "hl" ? "var(--on)" : "var(--line)"};display:inline-flex;align-items:center;justify-content:center">${it.on ? CHECK : ""}</span>` : "";
  const mark = it.mark ? `<span class="mark ${it.mark}"></span>` : "";
  return `<div class="mi ${state}">${mark}<span class="chk">${chk}</span><span class="ic">${it.icon ? icon(it.icon, state) : ""}</span><span class="lb">${esc(it.t)}</span>${it.k ? `<span class="keys">${esc(it.k)}</span>` : ""}<span class="arrow">${it.sub ? "›" : ""}</span></div>`;
}
const menu = (cap, items, style = "") => `<div style="padding-left:16px;${style}"><div class="cap">${esc(cap)}</div><div class="menu">${items.map(row).join("")}</div></div>`;

const SIDE_MENU = [
  { t: "Move panel…", icon: "panel-move" },
  { t: "Move to the left side bar", icon: "side-bar-left", mark: "new", hl: true },
  { t: "Move out of the side bar", icon: "panel-dock", mark: "new" },
  { t: "Move left", icon: "tab-move-left", mark: "ren" },
  { t: "Move right", icon: "tab-move-right", mark: "ren" },
  { t: "Float", icon: "panel-float" },
  { sep: 1 },
  { t: "Close", icon: "tab-close", mark: "ren" },
  { sep: 1 },
  { t: "Lock layout", icon: "layout-lock", chk: 1 },
];
const MIDDLE_MENU = [
  { t: "Move panel…", icon: "panel-move" },
  { t: "Move to the left side bar", icon: "side-bar-left", mark: "new" },
  { t: "Move to the right side bar", icon: "side-bar-right", mark: "new" },
  { t: "Move tab left", icon: "tab-move-left" },
  { t: "Move tab right", icon: "tab-move-right" },
  { t: "Float", icon: "panel-float" },
  { sep: 1 },
  { t: "Close", icon: "tab-close" },
  { sep: 1 },
  { t: "Lock layout", icon: "layout-lock", chk: 1 },
];
const VIEW_MENU = [
  { t: "Layout", icon: "layout", sub: 1 },
  { sep: 1 },
  { t: "Panels", icon: "panels", sub: 1 },
  { t: "Move panel…", icon: "panel-move" },
  { t: "Restore the previous layout", icon: "layout-restore" },
  { sep: 1 },
  { t: "Left side bar", icon: "side-bar-left", chk: 1, on: 1, k: "Ctrl+Alt+B", mark: "new" },
  { t: "Right side bar", icon: "side-bar-right", chk: 1, on: 1, mark: "new", hl: true },
  { t: "Swap side bars", icon: "side-bars-swap", mark: "new" },
  { sep: 1 },
  { t: "Node effects", icon: "", chk: 1 },
  { sep: 1 },
  { t: "Log", icon: "" },
];

function placementDialog() {
  const items = ["Tab with", "Left of", "Above", "Right of", "Below", "Float", "-", "Left side bar", "Right side bar"];
  const pop = items.map((t) => t === "-" ? '<div class="psep"></div>' : `<div class="${t === "Right side bar" ? "hl" : ""}">${t.includes("side bar") ? '<span class="mark new" style="left:-14px;top:7px"></span>' : ""}${esc(t)}</div>`).join("");
  return `<div style="position:relative;padding-left:16px"><div class="cap">Move panel… with the Place list open</div><div class="dlg"><div class="dt">Move panel</div><div class="db">
    <div class="row"><span class="l">Panel</span><div class="combo" style="flex:1">Find and replace <span>▾</span></div></div>
    <div style="height:1px;background:var(--line)"></div><b>Move</b>
    <div class="row" style="position:relative"><span class="l">Place</span><div class="combo open" style="width:130px">Right side bar <span>▾</span></div><span style="color:var(--dis)">next to</span><div class="combo" style="flex:1;color:var(--dis)">Grid <span>▾</span></div>
      <div class="pop" style="top:26px;left:64px">${pop}</div></div>
    <div style="height:212px"></div>
    <div style="align-self:flex-end"><span class="btn">Move</span></div>
    <div style="height:1px;background:var(--line)"></div><b>Resize</b>
    <div class="row"><span class="l">Width</span><div class="combo" style="width:110px">320</div><span>Height</span><div class="combo" style="width:110px;color:var(--dis)">—</div><span style="color:var(--muted)">px</span></div>
    <div style="align-self:flex-end"><span class="btn accent">Close</span></div></div></div></div>`;
}

function settingsPage(rtl) {
  const nav = [["General", "settings"], ["Appearance", "appearance"], ["Audio", "settings-audio"], ["Video", "settings-video"], ["Grid", "settings-grid"]];
  return `<div style="padding-left:16px;position:relative"><div class="cap">Settings › Appearance › Window</div><div class="settings"><div class="nav">${nav.map(([n, i]) => `<div class="${n === "Appearance" ? "sel" : ""}">${icon(i)}${n}</div>`).join("")}</div>
    <div class="pg"><h4>Window</h4>
      <div class="srow"><span>Use system title bar</span><div class="combo" style="width:200px">The desktop's default <span>▾</span></div></div>
      <div class="srow"><span class="mark new" style="left:-12px;top:7px"></span><span style="display:block;max-width:270px">AI side bar<div class="d">The side the Agent and Review side bar starts on. Swap side bars in View swaps it too.</div></span><div class="combo open" style="width:200px">Left <span>▾</span></div>
        <div class="pop" style="top:32px;right:0;width:200px"><div class="hl">Left</div><div>Right</div></div></div>
      <div style="height:30px"></div></div></div></div>`;
}

const MIXED = ["Explorer", "Agent", "Search", "Timing", "Styles", "QcNotes"];
const SECTIONS = [
  { n: 1, id: "default", title: "Both side bars, defaults", meta: "First start with a Project open. Each side bar has its icon strip along its top: icons only, names on hover. Under it, the shown panel's header (its name and ⋯) and body. The AI side bar on the left shows Agent; the tools side bar on the right shows Explorer. The shown panel's icon has the selection fill and an accent line under it. Agent has the focus (the K2 triangle in its header's trailing corner). The pointer is over Find and replace. Two toggles next to the window buttons show or hide each side bar.",
    frame: () => ({ start: bar(LEFT, "Agent", { focus: 1 }), end: bar(RIGHT, "Explorer", { hover: "Search" }), tip: { side: "end", i: 1, text: "Find and replace", k: "Ctrl+H" } }) },
  { n: 2, id: "mixed", title: "The AI with the tools in one side bar", meta: "Any panel can go in either side bar, in any mix. Here Agent was moved into the right side bar between Explorer and Find and replace; Review stayed on the left. Which panels each side bar holds, their order, the shown panel and the widths are saved at once and come back after a restart.",
    frame: () => ({ start: bar(["Review"], "Review"), end: bar(MIXED, "Agent", { focus: 1 }) }) },
  { n: 3, id: "hidden", title: "Right side bar hidden", meta: "Clicking the shown panel's icon (Explorer), the right toggle in the title bar, View › Right side bar or its command hides the right side bar; the middle takes the room. The same toggle brings it back with the same panel at the same width. The pointer is on the toggle.",
    frame: () => ({ start: bar(LEFT, "Agent"), end: bar(RIGHT, "Explorer", { hidden: 1 }), litToggle: "right", tip: { x: 884, top: 32, text: "Toggle right side bar" } }) },
  { n: 4, id: "empty", title: "An empty side bar", meta: "Every tool was moved out of the right side bar. It keeps its empty icon strip and shows “Drag a panel here” at its width, so a panel can be dropped on it.",
    frame: () => ({ start: bar(LEFT, "Review"), end: bar([], null), middleFind: 1 }) },
  { n: 5, id: "ai-off", title: "AI off", meta: "With Hikari AI off, Agent and Review leave their side bar. The left side bar then has no panels, so it hides and its toggle is disabled. A panel moved into it later brings it back.",
    frame: () => ({ start: bar([], null, { hidden: 1 }), end: bar(RIGHT, "Search", { focus: 1 }) }) },
  { n: 6, id: "swapped", title: "Swapped", meta: "View › Swap side bars, or Settings › Appearance › AI side bar: Right. The contents, the shown panels, the widths and the hidden states trade places together.",
    frame: () => ({ start: bar(RIGHT, "Explorer"), end: bar(LEFT, "Agent", { focus: 1 }) }) },
  { n: 7, id: "rtl", title: "Right-to-left", meta: "In a right-to-left language the whole frame mirrors: the AI side bar starts on the right, the tools on the left, the icon strips run right to left, and the header's ⋯ and the focus triangle sit on the trailing (left) side. (The mockup keeps English text so the structure reads.)",
    frame: () => ({ rtl: 1, start: bar(LEFT, "Agent", { focus: 1 }), end: bar(RIGHT, "Explorer") }) },
  { n: 8, id: "plugin", title: "A plugin panel in the left side bar", meta: "A plugin whose manifest says area: left first shows its panel in the left side bar's strip, after the panels already there, with the plugin dot on its icon. area: right puts it in the right one; bottom stays in the middle docking.",
    frame: () => ({ start: bar(LEFT.concat(["Karaoke"]), "Karaoke", { focus: 1, hover: "Karaoke" }), end: bar(RIGHT, "Explorer"), tip: { side: "start", i: 2, text: "Karaoke helper (plugin)" } }) },
  { n: 9, id: "drag", title: "Dragging an icon to the other side bar", meta: "Find and replace is dragged from the right strip to the left one. The target strip gets the accent fill and a 2 px line where the icon will land; the icon's old place is dimmed. Dropping on the same strip reorders it.",
    frame: () => ({ start: bar(LEFT, "Agent", { drop: 1, dropAt: 2 }), end: bar(RIGHT, "Explorer", { ghost: "Search" }), drag: { p: "Search", top: STRIP_TOP + 22, x: 70 } }) },
  { n: 10, id: "width", title: "Resizing a side bar", meta: "Dragging the inner edge resizes the side bar; the size shows while dragging and is saved at once. A side bar starts at about 320 px. Dragging it under its minimum (240 px, DK3-Q8) hides it on release, as its toggle would.",
    frame: () => ({ start: bar(LEFT, "Agent", { width: 230 }), end: bar(RIGHT, "Explorer", { width: 300, grip: 1 }), grip: { top: 220, x: 300, label: "320 px" } }) },
  { n: 11, id: "panel-menu", title: "The ⋯ menu", meta: "Left: the ⋯ of Explorer in the right side bar. Right: the ⋯ of Find and replace docked in the middle, tabbed with the Grid. Rows that don't apply are hidden, as Move tab left/right already are: a side bar panel has no “Move to” its own side bar, a middle panel no “Move out”. In a side bar Move tab left/right become Move left/right along the strip, and Close takes the icon off the strip.",
    cell: () => `<div style="display:flex;gap:20px">${menu("Explorer ⋯ (right side bar)", SIDE_MENU)}${menu("Find and replace ⋯ (middle)", MIDDLE_MENU)}</div>` },
  { n: 12, id: "move-panel", title: "Move panel…", meta: "The Place list gains Left side bar and Right side bar after a separator. With either chosen, “next to” is disabled and Resize sets the side bar's width. Choosing a middle placement for a side bar panel moves it out of the side bar.",
    cell: placementDialog },
  { n: 13, id: "view-menu", title: "View menu", meta: "Two check rows and a command after Restore the previous layout: Left side bar (Ctrl+Alt+B, 5339), Right side bar (5340) and Swap side bars (5341). The hotkeys list names them “Toggle left side bar”, “Toggle right side bar” and “Swap side bars”.",
    cell: () => menu("View", VIEW_MENU) },
  { n: 14, id: "settings", title: "Settings › Appearance", meta: "One new row in the Window section, “AI side bar: Left / Right”. It applies to every window (DK3-Q1); View › Swap side bars flips the same setting, and the choice is saved.",
    cell: () => settingsPage() },
];

function render() {
  const params = new URLSearchParams(location.search);
  const only = params.get("only");
  if (only) document.body.classList.add("single");
  const main = document.getElementById("main");
  let html = "";
  for (const s of SECTIONS) {
    const cells = THEMES.map(([cls, name]) => `<div><div class="colhead">${name}</div><div class="cell ${cls}">${s.frame ? frame(s.frame()) : s.cell()}</div></div>`).join("");
    html += `<section class="sec${only === s.id ? " shown" : ""}" id="${s.id}"><h2>${s.n}. ${esc(s.title)}</h2><p class="meta">${esc(s.meta)}</p><div class="board">${cells}</div></section>`;
  }
  html += `<section class="sec" id="questions"><h2>Questions the card leaves open</h2><ol class="recs">${QUESTIONS.map((q) => `<li>${q}</li>`).join("")}</ol></section>`;
  main.innerHTML = html;
}

const QUESTIONS = [
  "<b>DK3-Q1. Which edge is app-wide, the contents per window.</b> Recommended: “AI side bar: Left / Right” is one app setting, and View › Swap side bars flips it, so every window swaps at once, as the Settings row implies. Each window keeps its own side bar contents, shown panels, widths and hidden states (Q430), and they move with the swap. <i>Alternative:</i> the edge is per window too, and the Settings row only sets it for new windows.",
  "<b>DK3-Q2. Always saved.</b> Recommended: every side bar change (a panel moved, reordered, shown or closed, a width, hide or show, a swap) is saved at once: into the window's Session entry and into <code>sidebars.json</code> beside <code>layout.json</code>, which keeps the side bars changed last. A new window and a start without a Session (a crash, Session restore off, a new install of the settings folder copied over) take that file's side bars, so the user's arrangement is never lost. Reset side bars in View › Layout returns the window to the defaults. <i>Alternative:</i> the Session only, with defaults when there is none.",
  "<b>DK3-Q3. Hiding replaces collapsing to a rail.</b> With the icons on top there is no rail left to collapse to. Recommended: clicking the shown panel's icon hides the whole side bar; the title bar's two toggles (next to the window buttons), View › Left/Right side bar and the commands bring it back with its panel and width. This amends Q426's “collapses to the rail” and Q442's “collapses”. <i>Alternative:</i> clicking the shown icon does nothing, and only the toggles hide.",
  "<b>DK3-Q4. Toggles in the title bar.</b> Recommended: two icon buttons, <code>side-bar-left</code> and <code>side-bar-right</code>, before the window buttons (with HikariSub's title bar; with the system's title bar they sit at the end of the menu bar), named “Toggle left side bar” and “Toggle right side bar”, each disabled while its side bar has no panels. <i>Alternative:</i> no buttons; View and the hotkeys only.",
  "<b>DK3-Q5. Too many icons for the strip.</b> Recommended: icons that don't fit the side bar's width go into a “More panels” chevron at the strip's end that lists them; the shown panel's icon always stays visible. <i>Alternative:</i> the strip wraps onto a second row.",
  "<b>DK3-Q6. Names in right-to-left.</b> Recommended: the setting stores the reading-order side, so in RTL “Left” (the default) puts the AI side bar on the physical right (Q437). Menu rows, commands and the hotkeys list always name the physical side: Toggle left side bar (Ctrl+Alt+B) hides whichever bar is on the left of the screen. <i>Alternative:</i> Toggle left side bar follows the AI side bar in RTL.",
  "<b>DK3-Q7. Close, order and Float in a side bar.</b> Recommended: ⋯ Close takes the icon off its strip, and View › Panels › Show (or the panel's command, e.g. Ctrl+H) brings it back to the same side bar. Dragging within a strip reorders it, and Move tab left/right become Move left/right along the strip. ⋯ Float floats the panel as today; its Dock puts it back in the side bar it came from. <i>Alternative:</i> no Close or Float in a side bar.",
  "<b>DK3-Q8. Sizes.</b> Recommended: a 240 px side bar minimum and a 320 px start (Q442). A shown side bar counts in the window's minimum at 240 px; dragging under 240 hides it on release. <i>Alternative:</i> another minimum.",
  "<b>DK3-Q9. Lock layout.</b> Recommended: while the current tab's layout is locked, moves into, out of and between side bars (menus, Move panel…, icon drags) are greyed like the other moves; showing a panel, hiding, swapping and resizing still work, as separators still resize. <i>Alternative:</i> the side bars ignore the lock.",
  "<b>DK3-Q10. No drag between a side bar and the middle docking in this card.</b> Recommended: panels go into and out of side bars by the ⋯ menu and Move panel…, and drag only between and within strips. Dragging a docked header onto a strip needs the engine's drag to cross into HikariSub's own container, which the pinned KDDW doesn't offer; it can be a follow-up card. <i>Alternative:</i> build that drag now (more native-gate risk before beta.4).",
  "<b>DK3-Q11. Commands for a side bar panel.</b> Recommended: a command that shows a panel in a hidden side bar (Ctrl+F, Ctrl+I, Show Agent) shows the side bar and that panel, with the focus in it. <i>Alternative:</i> nothing visible until the side bar is shown.",
  "<b>DK3-Q12. First start and the Explorer without a Project.</b> Recommended: the left side bar starts showing Agent; the right starts showing Explorer in a window with a Project, otherwise Find and replace. The Explorer's icon stays without a Project and its body shows PJ4's empty state. <i>Alternative:</i> the Explorer's icon shows only while the window has a Project.",
  "<b>DK3-Q13. Keyboard.</b> Recommended: F6 goes left side bar → middle panels → right side bar (mirrored in RTL); each icon strip is one Tab stop, a tab list named “Left side bar” / “Right side bar”, with Left/Right between icons, Enter or Space to show, and the Menu key for the panel's ⋯. A right click on an icon opens the same ⋯ menu. The shown panel's header gets the K2 triangle like any panel.",
  "<b>DK3-Q14. New icons.</b> Recommended: approve <code>side-bar-left</code>, <code>side-bar-right</code> and <code>side-bars-swap</code> as drawn (16 px, 1 px stroke, accent strokes), used in the title bar, View, the ⋯ menu and the hotkeys list. Strip icons are the panels' existing ones; a plugin panel's icon gets the accent plugin dot.",
];
