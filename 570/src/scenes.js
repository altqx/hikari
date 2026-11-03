const PIN = (corner, xs, y) => `Effects\tCC Power Pin #1\tCC Power Pin-${corner}\n\tFrame\tX pixels\tY pixels\n` + xs.map((x, i) => `\t${i}\t${x}\t${y}`).join("\n");
const DATA = ["Adobe After Effects 6.0 Keyframe Data", "", "\tUnits Per Second\t23.976", "\tSource Width\t1920", "\tSource Height\t1080", "",
  PIN("0002", [600, 610, 620, 630, 640], 300), "", PIN("0003", [1000, 1010, 1020, 1030, 1040], 300), "…"].join("\n");
const DATA_720 = DATA.replace("Width\t1920", "Width\t1280").replace("Height\t1080", "Height\t720");

const STATUS = {
  ok: ["ok", "run-applied", "", "5 frames at 23.976 fps, tracked at 1920 × 1080. Covers the selection’s 5 frames, 335–339."],
  idle: ["idle", "notice-info", "muted", "Paste the Power-Pin data exported from After Effects or Blender. Apply reads the four CC Power Pin corners."],
  length: ["bad", "notice-error", "danger", "The data has 120 frames, but the selected lines cover 5 (335–339). Select lines that span the tracked frames."],
  invalid: ["bad", "notice-error", "danger", "This isn’t Power-Pin data: no CC Power Pin corners were found."],
  size: ["warn", "notice-warning", "warning", "Tracked at 1280 × 720, but the video is 1920 × 1080. The corners are scaled to the video."],
  overlap: ["bad", "notice-error", "danger", "Lines 9 and 10 overlap, but line 10 isn’t shown at frame 1. Pick a frame both lines show, or apply them one at a time."],
};

function trackingDialog(kind) {
  const data = { idle: "", invalid: "Effects\tADBE Corner Pin #1\tADBE Corner Pin-0001\n\tFrame\tX pixels\tY pixels\n\t0\t600\t300\n…", size: DATA_720 }[kind] ?? DATA;
  const [cls, ic, st, text] = STATUS[kind];
  const ready = cls === "ok" || cls === "warn";
  const left = `<div class="col" style="gap:6px"><div class="small h">Power-Pin data</div>`
    + `<div class="datafld${kind === "idle" ? " focus" : ""}">${data ? esc(data) : ""}</div>`
    + `<div class="status ${cls}">${icon(ic, st, 14)}<span>${esc(text)}</span></div></div>`;
  const spin = `${fld("1", 64, "num")}<span class="small muted" style="white-space:nowrap">of 5 · video frame 335</span>`;
  const right = `<div class="col" style="gap:10px;padding-top:22px;min-width:300px">`
    + `<div class="grid2"><span>Relative to frame</span><span class="row" style="gap:6px">${spin}</span>`
    + `<span>Origin</span><span>${combo("Keep", 120)}</span></div>`
    + `<div class="hr"></div>`
    + `<div class="col" style="gap:8px">${check("Fit flat lines to the plane first", false)}${check("Move clips with the plane", true)}${check("Scale outline and shadow", true)}</div>`
    + `<div class="small muted" style="max-width:270px">The plane is saved with each line, for the Perspective tool and Derive Perspective Track.</div></div>`;
  const body = `<div class="row" style="gap:24px;align-items:flex-start">${left}${right}</div>`;
  const foot = `<span class="small muted">${kind === "overlap" ? "Lines 9–10" : "Line 8"} · frames 335–339</span><span class="grow"></span>${btn("Apply", ready ? "primary ring" : "dis")}${btn("Cancel")}`;
  return `<div style="position:relative">${win("Perspective tracking", body, foot, 760)}<span class="anno" style="left:-14px;top:12px"></span></div>`;
}

const RESULT = [
  ["14.000", "14.210", "{\\fad(80,0)\\pos(800,400)\\frx10\\fry-5}fbf"],
];
const TRACKED = [
  ["13.993", "14.035", "{\\pos(800,400)\\org(800,400)\\fscx100\\fscy100\\frz0\\fry-5\\frx10\\fax0\\xbord1\\ybord1\\alpha&HD3&}fbf"],
  ["14.035", "14.076", "{\\pos(810.01,400)\\org(800,400)\\fscx99.443\\fscy99.728\\frz0\\fry-5.014\\frx10.027\\fax-0.005\\xbord0.99\\ybord1\\alpha&H50&}fbf"],
  ["14.076", "14.118", "{\\pos(819.965,400)\\org(800,400)\\fscx98.888\\fscy99.457\\frz0\\fry-5.028\\frx10.055\\fax-0.011\\xbord0.99\\ybord0.99}fbf"],
  ["14.118", "14.160", "{\\pos(829.864,400)\\org(800,400)\\fscx98.334\\fscy99.185\\frz0\\fry-5.042\\frx10.082\\fax-0.017\\xbord0.98\\ybord0.99}fbf"],
  ["14.160", "14.201", "{\\pos(839.707,400)\\org(800,400)\\fscx97.781\\fscy98.914\\frz0\\fry-5.056\\frx10.11\\fax-0.022\\xbord0.98\\ybord0.99}fbf"],
];

function gridOf(rows, first, sel) {
  const head = `<div class="gr gh"><span>#</span><span>Start</span><span>End</span><span>Text</span></div>`;
  return `<div class="gridtbl">${head}${rows.map((r, i) => `<div class="gr${sel ? " sel" : ""}"><span class="num muted">${first + i}</span><span class="num">0:00:${r[0]}</span><span class="num">0:00:${r[1]}</span><span class="mono">${esc(r[2])}</span></div>`).join("")}</div>`;
}

function historyList() {
  const rows = [["Perspective tracking", true], ["Visual perspective tool", false], ["Change text", false], ["Open subtitles", false]];
  return `<div class="col" style="gap:6px"><div class="small h">Edit › History</div><div class="lst" style="width:260px">${rows.map(([t, s]) => `<div class="lr${s ? " sel" : ""}">${icon(s ? "tool-perspective" : "history", "", 14)}${esc(t)}</div>`).join("")}</div></div>`;
}

const VIDEO_MENU = (hasVideo) => [
  { t: "Insert start time from video", icon: "set-start-time", dis: !hasVideo },
  { t: "Insert end time from video", icon: "set-end-time", dis: !hasVideo },
  { t: "Shift by frames to video", icon: "shift-times", sub: [], dis: !hasVideo },
  { t: "Perspective tracking…", icon: "tool-perspective", mark: true, hl: hasVideo, dis: !hasVideo, hint: hasVideo ? "" : "Open a video first: tracking data is per frame." },
  { sep: true },
  { t: "Previous frame", icon: "frame-previous", k: "Left", dis: !hasVideo },
  { t: "Next frame", icon: "frame-next", k: "Right", dis: !hasVideo },
];

const SCENES = {
  dialog: (kind) => trackingDialog(kind),
  before: () => `<div class="col" style="gap:6px"><div class="small h">Before: line 8, selected</div>${gridOf(RESULT, 8, true)}</div>`,
  after: () => `<div class="col" style="gap:14px;align-items:flex-start"><div class="col" style="gap:6px"><div class="small h">After: lines 8–12, selected</div>${gridOf(TRACKED, 8, true)}</div>${historyList()}</div>`,
  menu: (hasVideo) => menuCascade(VIDEO_MENU(hasVideo)),
};
