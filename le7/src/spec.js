const THEMES = [["t-light", "Light"], ["t-dark", "Dark"], ["t-hcw", "High contrast White"], ["t-hcb", "High contrast Black"]];
const icon = (name, size = 16) => (ICONS[name] || "").replace(/width="16" height="16"/, `width="${size}" height="${size}"`);
const esc = s => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;");

const WIDTH = { wide: 640, medium: 560, narrow: 450 };
const ROWS = {
  wide: [["start", "end", "duration", "style"], ["comment", "layer", "actor", "effect", "margins"]],
  medium: [["start", "end", "duration"], ["style", "actor"], ["layer", "effect", "comment", "margins"]],
  narrow: [["start", "end", "duration"], ["style", "actor"], ["layer", "effect", "comment"], ["margins"]],
};
const LINE = "{\\an8\\fad(120,0)}The last train leaves at noon, so if we want to catch it we should go now and run. Mika, grab the bags and the tickets; I will meet you by the gate at platform four.";
const LINE_RTL = "{\\an8}القطار الأخير يغادر عند الظهر، لذا إذا أردنا اللحاق به فعلينا الذهاب الآن والركض.";
const TRANSLATED = "Ostatni pociąg odjeżdża w południe, więc jeśli chcemy zdążyć, musimy iść teraz i biec.";

function minHeight(o) {
  const n = ROWS[o.step].length;
  let h = 28 + 8 + 26 + 6 + 50 + 6 + n * 26 + (n - 1) * 6 + 8 + 2;
  if (o.tr) h += 6 + 50 + 4 + 4;
  if (o.error) h += 6 + 38;
  return h;
}

function strip(o) {
  const b = (n, cls = "") => `<span class="ib ${cls}">${icon(n)}</span>`;
  const tags = ["tag-bold", "tag-italic", "tag-underline", "tag-strikeout"].map(n => b(n)).join("");
  const look = `<span class="rule"></span>${b("tag-font")}${["colour-primary", "colour-secondary", "colour-outline", "colour-shadow"].map(n => b(n)).join("")}`;
  const tr = `<span class="rule"></span>${["paste-translation", "paste-selection-translation", "comment-original", "unconfirmed", "moving-tags"].map(n => b(n)).join("")}`;
  const align = `<span class="rule"></span><span class="align">${icon("alignment-8")}${icon("chevron-down", 12)}</span>`;
  const custom = `<span class="rule"></span><span class="tagbtn">\\blur2</span><span class="tagbtn">\\fad</span>${b("add")}`;
  const shown = {
    wide: o.tr ? [look, tr] : [look, align, custom],
    medium: o.tr ? [look] : [look, align],
    narrow: [],
  }[o.step];
  const folded = !(o.step === "wide" && !o.tr);
  const more = folded ? b("menu-more", o.moreOpen ? "down" : "") : "";
  const ctr = `<span class="ctr">Chars <b>${o.rtl ? 78 : 84}</b></span><span class="ctr">CPS <b>${o.rtl ? 19.5 : 21.0}</b></span>`;
  return `<div class="strip">${tags}${shown.join("")}${more}<span class="spacer"></span>${ctr}${b("ai-assistant")}</div>`;
}

function textField(o, which) {
  const body = which === 1 ? TRANSLATED : (o.rtl ? LINE_RTL : LINE);
  const m = body.match(/^(\{[^}]*\})(.*)$/);
  const html = m ? `<span class="tags">${esc(m[1])}</span>${esc(m[2])}` : esc(body);
  const legend = o.tr ? `<span class="legend">${which === 1 ? "Translated text" : "Original text"}</span>` : "";
  const focus = (o.focus ?? 0) === which ? " focus" : "";
  return `<div class="text${o.tr ? " cap" : ""}${focus}">${legend}<div class="inner">${html}</div><span class="vbar"><i style="top:0;height:62%"></i></span></div>`;
}

function slot(name, o) {
  const fill = o.step !== "wide";
  const lab = t => `<span class="lab">${t}</span>`;
  const pend = f => (o.pending || []).includes(f) ? " pend" : "";
  switch (name) {
  case "start": return `<div class="slot${fill ? " grow fill" : ""}">${lab("Start")}<div class="box time${pend("start")}">0:01:12.48</div></div>`;
  case "end": return `<div class="slot${fill ? " grow fill" : ""}">${lab("End")}<div class="box time${o.error ? " warn" : ""}">0:01:16.02</div></div>`;
  case "duration": return `<div class="slot${fill ? " grow fill" : ""}">${lab("Duration")}<div class="box time">0:00:03.54</div></div>`;
  case "style": {
    const name = o.longStyle ? "Signs – Episode 12 cold open title card (top, 1080p)" : "Default";
    return `<div class="slot grow4" style="position:relative">${lab("Style")}<div class="box combo"><span class="v">${name}</span>${icon("chevron-down", 12)}</div><span class="ib">${icon("edit")}</span></div>`;
  }
  case "actor": return `<div class="slot grow3">${lab("Actor")}<div class="box combo edit"><span class="v">${o.rtl ? "Mika" : "Mika"}</span>${icon("chevron-down", 12)}</div></div>`;
  case "effect": return `<div class="slot grow3">${lab("Effect")}<div class="box combo edit"><span class="v"></span>${icon("chevron-down", 12)}</div></div>`;
  case "layer": return `<div class="slot">${lab("Layer")}<div class="box num">0</div></div>`;
  case "comment": return `<div class="slot"><span class="chk"><i></i>Comment</span></div>`;
  case "margins": {
    const m = (ic, v, hint, f) => `<div class="box marg${pend(f)}">${icon(ic)}<span class="${hint ? "hint" : ""}">${v}</span></div>`;
    const ml = o.pending && o.pending.includes("ml") ? m("margin-left", "40", false, "ml") : m("margin-left", "20", true, "ml");
    return `<div class="slot${fill ? " grow fill" : ""}" style="gap:5px">${lab("Margins")}<div style="display:flex;gap:2px;flex:1 1 auto;min-width:0">${ml}${m("margin-right", "20", true, "mr")}${m("margin-vertical", "15", true, "mv")}</div></div>`;
  }
  }
  return "";
}

function rows(o) {
  return `<div class="rows">${ROWS[o.step].map(r => `<div class="row">${r.map(s => slot(s, o)).join("")}</div>`).join("")}</div>`;
}

function message(o) {
  if (!o.error)
    return "";
  return `<div class="msg">${icon("notice-error", 12)}<div class="col"><span>The end time is before the start time.</span><span>Not applied: End 0:01:10.00</span></div></div>`;
}

function dock(o) {
  const w = o.width ?? WIDTH[o.step];
  const h = o.height ?? minHeight(o);
  const title = "Line editor: episode-12.ass";
  const head = `<div class="dhead"><span class="grow">${title}</span><span class="tb">${icon("menu-more")}</span></div>`;
  const texts = textField(o, 0) + (o.tr ? textField(o, 1) : "");
  const extra = (o.overlays || []).join("");
  return `<div class="dock" ${o.rtl ? 'dir="rtl"' : ""} style="width:${w}px;height:${h}px">${head}<div class="dbody">${strip(o)}${texts}${message(o)}${rows(o)}${extra}</div></div><div class="size">${w} × ${h} px${o.height ? "" : ", the step's minimum"}</div>`;
}

function today(o) {
  const w = WIDTH.narrow, h = minHeight({ step: "narrow", tr: o.tr });
  const ts = (l, v, cls = "") => `<div class="tslot ${cls}"><span class="lab">${l}</span><div>${v}</div></div>`;
  const grid = `<div class="tgrid">${[
    ts("Start", `<div class="box time">0:01:12.48</div>`), ts("End", `<div class="box time">0:01:16.02</div>`),
    ts("Duration", `<div class="box time">0:00:03.54</div>`), ts("Layer", `<div class="box num">0</div>`),
    ts("Style", `<div class="box combo"><span class="v">Default</span>${icon("chevron-down", 12)}</div><span class="ib">${icon("edit")}</span>`, "span"),
    ts("Actor", `<div class="box combo edit"><span class="v">Mika</span>${icon("chevron-down", 12)}</div>`),
    ts("Effect", `<div class="box combo edit"><span class="v"></span>${icon("chevron-down", 12)}</div>`),
    ts("Margins", ["margin-left", "margin-right", "margin-vertical"].map(i => `<div class="box marg">${icon(i)}<span class="hint">20</span></div>`).join("")),
    ts("", `<span class="chk"><i></i>Comment</span>`),
  ].join("")}</div>`;
  const txt = t => `<div class="text" style="flex:none;min-height:0;height:${t ? 32 : 32}px"><div class="inner">${esc(LINE)}</div></div>`;
  const caps = o.tr ? `<div class="caption">Original text</div>${txt()}<div class="caption">Translated text</div>${txt(1)}` : txt();
  return `<div class="dock today" style="width:${w}px;height:${h}px"><div class="dhead"><span class="grow">Line editor: episode-12.ass</span><span class="tb">${icon("menu-more")}</span></div><div class="dbody" style="padding-inline-end:16px"><div class="clipper" style="padding:8px 16px 8px 8px;display:flex;flex-direction:column;gap:6px">${strip({ step: "narrow", tr: o.tr })}${caps}${grid}</div><span class="vbar" style="top:32px"><i style="top:0;height:${o.tr ? 42 : 55}%"></i></span></div></div><div class="size">${w} × ${h} px today: the panel scrolls, ${o.tr ? "every field is" : "most fields are"} below the edge</div>`;
}

function board(make, opts = {}) {
  const cells = THEMES.map(([cls, name]) => `<div><div class="colhead">${name}</div><div class="cell ${cls}">${make(cls)}</div></div>`).join("");
  return `<div class="board${opts.four ? " four" : ""}">${cells}</div>`;
}

const tooltipStyle = `<div class="tip" style="top:170px;left:8px">Signs – Episode 12 cold open title card (top, 1080p)<br><span style="color:var(--muted)">Line style</span></div>`;
const moreMenu = (top, start) => `<div class="menu" style="top:${top}px;inset-inline-start:${start}px">
  <div class="mi">${icon("tag-font")}<span class="lb">Choose font…</span></div>
  <div class="mi hover">${icon("colour-primary")}<span class="lb">Primary colour</span></div>
  <div class="mi">${icon("colour-secondary")}<span class="lb">Secondary colour for karaoke</span></div>
  <div class="mi">${icon("colour-outline")}<span class="lb">Border colour</span></div>
  <div class="mi">${icon("colour-shadow")}<span class="lb">Shadow colour</span></div>
  <div class="msep"></div>
  <div class="mi">${icon("alignment-8")}<span class="lb">Alignment</span><span class="k">›</span></div>
  <div class="msep"></div>
  <div class="mi"><span style="width:16px"></span><span class="lb">\\blur2</span></div>
  <div class="mi"><span style="width:16px"></span><span class="lb">\\fad</span></div>
  <div class="mi">${icon("add")}<span class="lb">Add a tag button</span></div></div>`;

const SECTIONS = [
  { n: 1, id: "today", title: "Today, for comparison: the narrow Line editor at the new narrow minimum",
    meta: "main 26f206268 at 450 × 226 and 450 × 286. Labels sit above their fields, so the fields take about ten text rows and the whole panel scrolls. This is what LE7 removes.",
    body: () => board(() => today({ tr: false })) + "<div style='height:14px'></div>" + board(() => today({ tr: true })) },
  { n: 2, id: "wide", title: "Wide, two rows (as today)",
    meta: "Start, End, Duration, Style; then Comment, Layer, Actor, Effect, Margins. Every tag button fits, so there is no More. The Line text shrinks to 2 lines and scrolls inside itself (the thin bar inside the field).",
    body: () => board(() => dock({ step: "wide" })) },
  { n: 3, id: "wide-tr", title: "Wide, Translation mode",
    meta: "Both text fields are 2 lines at the minimum. The captions “Original text” and “Translated text” sit on each field's top border, so they take no row of their own (Q447). The translation buttons show; alignment and the custom tag buttons fold into More.",
    body: () => board(() => dock({ step: "wide", tr: true, focus: 1 })) },
  { n: 4, id: "medium", title: "Medium, three rows",
    meta: "Start, End, Duration; Style, Actor; Layer, Effect, Comment, Margins. This is the narrow step with its last two rows on one line (LE7-Q1). The time and margin fields share the row's width.",
    body: () => board(() => dock({ step: "medium" })) },
  { n: 5, id: "medium-tr", title: "Medium, Translation mode",
    meta: "The translation buttons, alignment and custom tag buttons fold into More; the four tag buttons, font and colours stay.",
    body: () => board(() => dock({ step: "medium", tr: true, focus: 1 })) },
  { n: 6, id: "narrow", title: "Narrow, four rows (Q446)",
    meta: "Start, End, Duration; Style, Actor; Layer, Effect, Comment; the three margins with their icons. Only Bold, Italic, Underline and Strikeout stay on the strip; everything else is in More. This width is the Line editor's new minimum width (LE7-Q3).",
    body: () => board(() => dock({ step: "narrow" })) },
  { n: 7, id: "narrow-tr", title: "Narrow, Translation mode",
    meta: "The tallest minimum: two 2-line text fields and four rows of fields.",
    body: () => board(() => dock({ step: "narrow", tr: true, focus: 1 })) },
  { n: 8, id: "long-style", title: "A long Style name, narrow",
    meta: "The Style box takes the room Actor leaves (4 to 3, as today) and cuts the name with “…”. Hovering shows the full name; the list shows full names too. The Edit style button never moves off the row.",
    body: () => board(() => dock({ step: "narrow", longStyle: true, overlays: [tooltipStyle] })) },
  { n: 9, id: "pending", title: "Pending changes and the style's margins, medium",
    meta: "Start was typed and the left margin stepped but neither is applied yet: amber text and an amber border, as today, with “Changed, not applied to the line yet” in the tooltip and the accessible description. The right and vertical margins are 0, so they show the style's values in muted text (“0: the style's (20)” in the tooltip).",
    body: () => board(() => dock({ step: "medium", pending: ["start", "ml"] })) },
  { n: 10, id: "error", title: "An error message, narrow, Translation mode",
    meta: "The message sits between the text and the fields, as today. While it shows, the panel's minimum counts it (LE7-Q5), so no field is pushed out of view.",
    body: () => board(() => dock({ step: "narrow", tr: true, error: true, focus: 0 })) },
  { n: 11, id: "more", title: "More, narrow",
    meta: "Everything that doesn't fit on the strip is in More, in strip order, with the same icons and names: Choose font, the four colours, alignment, the custom tag buttons and Add a tag button. In Translation mode the five translation buttons come before alignment.",
    body: () => board(() => dock({ step: "narrow", moreOpen: true, height: 410, overlays: [moreMenu(36, 108)] })) },
  { n: 12, id: "rtl", title: "Right to left, medium, Translation mode",
    meta: "The whole panel mirrors: labels on the right of their fields, the strip from the right, More and the counters on the left. Times and numbers keep their left-to-right digits; the Line text follows its own direction.",
    body: () => board(() => dock({ step: "medium", tr: true, rtl: true, focus: 0 })) },
];

const QUESTIONS = [
  ["Medium order.", "Medium is narrow with its last two rows joined: Start, End, Duration; Style, Actor; Layer, Effect, Comment, Margins. Going from medium to narrow only moves Margins to a row of its own. Yes?", "Yes. The other order that fits (Style, Actor, Effect; Comment, Layer, Margins) is as narrow as the narrow step, so narrow would never show."],
  ["Wide stays as today.", "Wide keeps today's two rows, with Comment first on row 2, although medium and narrow put Comment after Effect. Yes?", "Yes, Q446 says wide is as today, and Comment is a check box, not a field."],
  ["New minimum width.", "The narrowest Line editor gets wider: about 450 px against about 320 px today, because Start, End and Duration share a row with their labels beside them (Q446). Docks you set narrower than that open at the new minimum. Yes?", "Yes. The tests measure the real number with the app's font."],
  ["Captions on the border.", "In Translation mode, “Original text” and “Translated text” sit on the top border of their fields, like a group box title, at every size and not only when the panel is small. Yes?", "Yes: no row of its own, and small looks like full size. The other way, inside the field's start like the margin icons, takes about 90 px of every text line."],
  ["Messages count.", "While an error or “Not applied” message shows, the panel's minimum height counts it, as it counts the AI review strip today. A dock already tall enough doesn't move; a dock at its minimum grows by the message's lines. Yes?", "Yes. Otherwise the message would push the bottom row out of view, which Q444 rules out."],
  ["Labels per row.", "In medium and narrow, each label sits right beside its own field; labels are not lined up in columns across rows. Yes?", "Yes. The rows hold 3, 2, 3 and 1 fields, so columns would leave gaps."],
  ["Tab order.", "Tab goes through the fields in each step's reading order, row by row, as it does in both of today's arrangements. Yes?", "Yes. Accessible names don't change."],
  ["The Original text panel.", "With the Original text in its own panel, that panel's minimum also becomes 2 lines, and it scrolls inside the field the same way. Yes?", "Yes, it is the same text field."],
];

function render() {
  const only = new URLSearchParams(location.search).get("only");
  const main = document.getElementById("main");
  let html = "";
  for (const s of SECTIONS) {
    if (only && only !== s.id)
      continue;
    html += `<section class="sec" id="${s.id}"><h2>${s.n}. ${s.title}</h2><p class="meta">${s.meta}</p>${s.body()}</section>`;
  }
  if (!only || only === "questions") {
    html += `<section class="sec" id="questions"><h2>Steps</h2><table class="steps"><tr><th>Step</th><th>Rows</th><th>Minimum, mockup font</th><th>Minimum in Translation mode</th></tr>`
      + ["wide", "medium", "narrow"].map(st => `<tr><td>${st[0].toUpperCase() + st.slice(1)}</td><td>${ROWS[st].map(r => r.map(x => x[0].toUpperCase() + x.slice(1)).join(", ")).join("<br>")}</td><td>${WIDTH[st]} × ${minHeight({ step: st })}</td><td>${WIDTH[st]} × ${minHeight({ step: st, tr: true })}</td></tr>`).join("")
      + `</table><p class="meta">A step is used when every row fits at its fields' minimum widths; otherwise the next narrower step. The panel's minimum width is the narrow step's; its minimum height is the current step's.</p>`
      + `<h2>Questions</h2><ul class="recs" style="list-style:none;padding-left:0">${QUESTIONS.map((q, i) => `<li><b class="num">LE7-Q${i + 1}</b><b>${q[0]}</b> ${q[1]} <i>Recommendation: ${q[2]}</i></li>`).join("")}</ul></section>`;
  }
  main.innerHTML = html;
}
