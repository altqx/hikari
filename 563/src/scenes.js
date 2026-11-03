const ENTRIES = [
  { group: "Typesetting", name: "Derive Perspective Track", line: "Makes a power-pin track from the lines’ perspective, for Perspective-Motion.", by: "arch1t3cht · MIT" },
  { group: "Typesetting", name: "Focus Lines", line: "Draws moving focus lines, frame by frame.", by: "arch1t3cht · MIT" },
  { group: "Typesetting", name: "Split Tag Sections", line: "Splits a line at its tags, one line per section.", by: "arch1t3cht · MIT" },
  { group: "Editing", name: "Rewriting Tools", line: "Keeps earlier rewrites of a line and switches between them.", by: "arch1t3cht · MIT", installed: true },
  { group: "QC", name: "Git Signs", line: "Marks the lines changed since a git commit.", by: "arch1t3cht · MIT · Needs Git", installed: true, sel: true },
  { group: "Karaoke", name: "Fix KFX Boilerplate", line: "Fixes old KFX templates so Aegisub 3.5 runs them too.", by: "arch1t3cht · MIT" },
];

const chev = (folded) => `<span style="display:inline-flex;width:12px;height:12px;${folded ? "transform:rotate(-90deg)" : ""}"><svg viewBox="0 0 12 12" width="12" height="12" fill="none" stroke="currentColor" stroke-width="1.25" stroke-linecap="round" stroke-linejoin="round"><path d="M3 4.5l3 3 3-3"/></svg></span>`;
const sectionHead = (title, count) => `<div class="row" style="gap:4px;height:28px;padding:6px 12px 0"><span style="color:var(--text)">${chev(false)}</span><span class="h">${esc(title)}</span><span class="muted" style="font-size:11px">${count}</span></div>`;
const installedMark = () => `<span class="row small" style="gap:4px;color:var(--text)">${icon("run-applied", "", 14)}Installed</span>`;
const spinner = () => icon("run-busy", "", 14);

function entryRow(e, state, opts = {}) {
  const st = state || (e.installed ? "installed" : "install");
  let action = "";
  if (st === "install") action = btn("Install", "small", "plugin-install");
  else if (st === "installing") action = `<span class="row small muted" style="gap:4px">${spinner()}Installing…</span>`;
  else if (st === "failed") action = btn("Try again", "small", "restart");
  else action = `${installedMark()}${ibtn("panel-menu", opts.menuOpen ? "on" : "")}`;
  const failed = st === "failed"
    ? `<div class="small" style="color:var(--danger);padding:2px 0 0">${icon("notice-error", "danger", 12)} Couldn’t read the feed: no network connection. <span class="link">Details</span></div>` : "";
  return `<div class="erow${opts.sel ? " sel" : ""}">`
    + `<div class="row" style="gap:6px;min-height:26px"><span class="h" style="flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis">${esc(e.name)}</span>${action}</div>`
    + `<div style="padding-right:8px">${esc(e.line)}</div>`
    + `<div class="small muted">${esc(e.by)}</div>${failed}</div>`;
}

function toolbar(view) {
  const seg = ["Scripts", "Runs 3", "Recommended"].map((t, i) => `<span${i === view ? ' class="on"' : ""}>${t}</span>`).join("");
  return `<div class="row" style="gap:4px;padding:8px 8px 0"><span class="seg">${seg}</span><span style="flex:1"></span>${ibtn("automation")}${ibtn("refresh")}${ibtn("rerun-macro")}${ibtn("repeat-run")}</div>`;
}

const panelDock = (body, h) => `<div class="dock" style="width:400px;height:${h}px"><div class="dhead">Automation<span class="menub">${icon("panel-menu", "")}</span><span class="focusmark"></span></div><div style="display:flex;flex-direction:column;flex:1;min-height:0">${body}</div></div>`;

const SCENES = {
  panel() {
    const groups = ["Typesetting", "Editing", "QC", "Karaoke"];
    let list = "";
    for (const g of groups) {
      const members = ENTRIES.filter((e) => e.group === g);
      list += sectionHead(g, members.length) + members.map((e) => entryRow(e, "", { sel: e.sel })).join("");
    }
    const body = toolbar(2)
      + `<div style="padding:8px 8px 0">${fld("~Filter scripts", 382)}</div>`
      + `<div class="small muted" style="padding:6px 10px 2px">Installs from each author’s feed through DependencyControl. A script runs its code when it loads.</div>`
      + `<div style="flex:1;overflow:hidden">${list}</div>`;
    return `<div style="position:relative">${panelDock(body, 760)}<span class="anno" style="left:-14px;top:52px"></span></div>`;
  },
  rowState(kind) {
    const e = kind === "manual"
      ? { name: "Focus Lines", line: "Draws moving focus lines, frame by frame.", by: "arch1t3cht · MIT" }
      : { name: "Git Signs", line: "Marks the lines changed since a git commit.", by: "arch1t3cht · MIT · Needs Git" };
    const state = { install: "install", installing: "installing", failed: "failed", menu: "installed", manual: "installed" }[kind];
    const row = `<div style="width:400px;background:var(--panel);border:1px solid var(--line)">${entryRow(e, state, { sel: true, menuOpen: kind === "menu" || kind === "manual" })}</div>`;
    if (kind !== "menu" && kind !== "manual") return row;
    const items = [
      { t: "Show in Scripts", icon: "automation" },
      { t: "Open project page", icon: "open-link" },
      { sep: true },
      { t: "Uninstall…", icon: "delete", k: "Delete", dis: kind === "manual", hl: kind === "menu", hint: kind === "manual" ? "Not installed by DependencyControl. Remove the file from your autoload folder." : "" },
    ];
    return `<div class="row" style="align-items:flex-start;gap:0">${row}<div style="margin-top:28px;margin-left:-40px">${menuCascade(items)}</div></div>`;
  },
  toast() {
    return `<div class="toast">${icon("notice-info", "", 16)}<div style="flex:1">Git Signs is installed. Its macros are in Automation › Git Signs.</div><span class="link">Show</span>${icon("window-close", "muted", 12)}</div>`;
  },
  scripts() {
    const script = (name, sel, newMark) => `<div style="position:relative"><div class="row" style="gap:4px;padding:4px 8px 0 20px">${box(true)}<span class="h" style="flex:1">${esc(name)}</span>${ibtn("refresh")}${ibtn("panel-menu")}</div>${newMark ? '<span class="anno" style="left:-14px;top:10px"></span>' : ""}</div>`;
    const macro = (name, sel) => `<div class="lr${sel ? " sel" : ""}" style="padding-left:40px;height:28px">${esc(name)}</div>`;
    const body = toolbar(0)
      + `<div style="padding:8px">${fld("~Filter macros", 382)}</div>`
      + sectionHead("Autoload", 3)
      + script("DependencyControl Toolbox") + macro("DependencyControl/Install Script")
      + script("Git Signs", true, true) + macro("Git Signs/Show Diff", true) + macro("Git Signs/Configure") + macro("Git Signs/Clear Markers")
      + script("Rewriting Tools") + macro("Rewriting Tools/Prepare Rewrite");
    return panelDock(body, 430);
  },
  uninstall() {
    const body = `<div class="row" style="gap:12px;align-items:flex-start">${icon("notice-warning", "warning", 32)}<div class="col" style="gap:6px"><div class="h">Uninstall Git Signs?</div><div class="muted">DependencyControl deletes its files from your user folder. The modules it brought stay, for other scripts.</div></div></div>`;
    return win("Uninstall script", body, `<span class="grow"></span>${btn("Uninstall", "")}${btn("Cancel", "primary ring")}`, 440);
  },
  emptyScripts() {
    const body = toolbar(0)
      + `<div style="padding:8px">${fld("~Filter macros", 382)}</div>`
      + `<div class="empty"><div>No scripts loaded</div>${btn("Load script…", "", "automation")}<span class="link" style="position:relative">Browse recommended scripts<span class="anno" style="left:-16px;top:5px"></span></span></div>`;
    return panelDock(body, 330);
  },
};
