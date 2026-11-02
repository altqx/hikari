const S = { sep: true };
const I = (icon, t, o = {}) => ({ icon, t, ...o });
const C = (t, on, o = {}) => ({ chk: true, on, t, ...o });
const BLOCKED = "Dock floating panels to lock the layout";

const presets = (dis) => ["Editing", "Timing", "Translation", "Typesetting", "Karaoke"].map((p, i) => C(p, i === 0, dis ? { dis: true, mark: "ren" } : {}));
const layoutMenu = (state) => {
  const locked = state === "locked";
  const lockRow = state === "blocked"
    ? I("layout-lock", "Lock layout", { chk: true, on: false, dis: true, mark: "new", hint: BLOCKED })
    : I("layout-lock", "Lock layout", { chk: true, on: locked, mark: "new", hl: locked });
  const changes = locked ? { dis: true, mark: "ren" } : {};
  return [
    ...presets(locked), S, C("QC, two screens", false, changes), S,
    I("layout-save", "Save layout as…"), I("layout-reset", "Reset this layout", changes), I("layout-manage", "Manage layouts…"),
    S, lockRow,
  ];
};

const panelMenu = (locked) => {
  const changes = locked ? { dis: true, mark: "ren" } : {};
  return [
    C("Match case", false), C("Whole words", false), S,
    I("panel-move", "Move panel…", changes), I("tab-move-left", "Move tab left", changes), I("tab-move-right", "Move tab right", changes),
    I("panel-float", "Undock", changes), S,
    I("layout-lock", "Lock layout", { chk: true, on: locked, mark: "new", hl: locked }), S,
    I("tab-close", "Close", changes),
  ];
};

const QUESTIONS = [
  {
    id: "Q1", title: "Where the lock belongs",
    rec: "To the arrangement (ADR 0020). Each Document tab, and each saved layout, keeps its own locked state. “No floating panels” holds per arrangement. The Session's Layout: line and the [Hikari] section save it with the arrangement.",
    alt: "One lock for the whole app, kept in settings. It could meet a tab whose arrangement has floating panels, and would then have to unlock or dock them.",
  },
  {
    id: "Q2", title: "Opening and closing panels while locked",
    rec: "The panel ⋯ Close and View › Panels › name › Show, Hide, Float and Dock are disabled while locked, with the lock row in the same menu to undo it. A command whose work needs its panel (Find, Check spelling, a toast's Details opening the Log) still shows that panel in the place the arrangement already keeps for it. It docks nothing new and drags nothing.",
    alt: "Allow Show and Close while locked, since they don't drag anything; only moves are refused.",
  },
  {
    id: "Q3", title: "Presets, Reset this layout and saved layouts while locked",
    rec: "Disabled until unlocked, together with Restore the previous layout and Move panel…. Save layout as… and Manage layouts… stay available. A layout saved while locked is saved locked, so applying it later locks the tab.",
    alt: "Choosing one unlocks first and shows a notice.",
  },
];

const SECTIONS = [
  {
    n: 1, id: "switcher", title: "The lock on the layout switcher",
    opens: "The layout switcher sits after the menus in HikariSub's own title bar, or at the end of the menu bar with the system title bar. It names the tab's arrangement.",
    rows: [
      { label: "Unlocked", scene: "switcher", args: [false, false] },
      { label: "Locked", scene: "switcher", args: [true, false] },
      { label: "Locked, hovered", scene: "switcher", args: [true, true] },
    ],
    notes: [
      "While the tab's layout is locked, the switcher's layout icon becomes a lock. Nothing else is added to the window.",
      "Its tooltip and accessible name gain “locked”: “Layout: Editing, locked”.",
      "Switching to a tab whose layout isn't locked shows the layout icon again (Q1).",
    ],
    recs: [
      "The lock icon on the layout switcher is the only lock indicator: no status bar item and no icon on each panel.",
      "Approve the new lock icon as drawn (16 px, the icon set's 1 px stroke, accent keyhole).",
    ],
  },
  {
    n: 2, id: "layout-menu", title: "View › Layout ▸ and the switcher's menu",
    opens: "One menu, shared by View › Layout ▸ and the layout switcher, so the row to unlock is always where the lock icon is.",
    rows: [
      { label: "Unlocked", items: layoutMenu("open") },
      { label: "Locked", items: layoutMenu("locked") },
      { label: "A panel floats", items: layoutMenu("blocked") },
    ],
    notes: [
      "Lock layout is a check row in its own group at the end of the menu. It is also in command search (hotkey command 5230, GLOBAL_TOGGLE_LOCK_LAYOUT, no default key; Settings › Hotkeys lists it).",
      "While locked, the presets, the saved layouts and Reset this layout are greyed (Q3). Save layout as… and Manage layouts… stay available.",
      "While any panel of the tab floats, Lock layout is greyed, and the reason shows under it in small text. A disabled menu row gets no hover, so a tooltip alone would never show. The same text is the row's accessible description and the command's tooltip in Settings › Hotkeys.",
      "Docking the last floating panel enables the row at once. Turning the lock on never docks anything.",
    ],
    recs: [
      "Lock layout lives in the layout menu (View › Layout ▸ and the switcher), not as a separate top-level View row.",
      "Rows the lock holds are greyed, not hidden, so the menu keeps its shape.",
      "The blocked row shows its reason under it: “Dock floating panels to lock the layout”.",
    ],
  },
  {
    n: 3, id: "panel-menu", title: "Panel ⋯ and a floating window's ⋯",
    opens: "The ⋯ in every panel header, a right-click on a tab, the Menu key and Shift+F10. Drawn for the Search panel tabbed with the Line editor.",
    rows: [
      { label: "Unlocked", items: panelMenu(false) },
      { label: "Locked", items: panelMenu(true) },
      {
        label: "Floating window ⋯", items: [
          I("panel-dock", "Dock"), S, I("layout-lock", "Lock layout", { chk: true, on: false, dis: true, mark: "new", hint: BLOCKED }), S, I("tab-close", "Close"),
        ],
      },
    ],
    notes: [
      "The panel's own rows (here Search's) work as before, as do Show and tab switching.",
      "Move panel…, Move tab left and right, Undock and Close are greyed while locked (Q2). The lock row is in the same menu to undo it.",
      "A floating window's ⋯ shows the lock row greyed, with Dock just above it as the way forward.",
      "Labels follow main today. CX6's renames (Undock → Float, Close alone after a separator) apply whichever lands first.",
    ],
    recs: [
      "Add the Lock layout check row to the panel ⋯ menu, after Undock, in its own group.",
      "Show it greyed with its reason in a floating window's ⋯.",
    ],
  },
  {
    n: 4, id: "view-menu", title: "View while locked",
    opens: "View with Panels › Search open, while the tab's layout is locked.",
    rows: [
      {
        label: "Locked", items: [
          I("layout", "Layout", { sub: true }), S,
          I("panels", "Panels", {
            open: true, hl: true,
            sub: [I("", "Video", { sub: true }), I("", "Audio", { sub: true }), I("", "Line editor", { sub: true }), I("", "Grid", { sub: true }),
              I("", "Search", { open: true, hl: true, sub: [I("show", "Show", { dis: true, mark: "ren" }), I("hide", "Hide", { dis: true, mark: "ren" }), I("panel-float", "Float", { dis: true, mark: "ren" }), I("panel-dock", "Dock", { dis: true })] }),
              I("", "Log", { sub: true })],
          }),
          I("panel-move", "Move panel…", { dis: true, mark: "ren" }), I("layout-restore", "Restore the previous layout", { dis: true, mark: "ren" }), S,
          I("log", "Log", { dis: true, mark: "ren" }), C("Hide tags", false), C("Hide tags in the line editor", false),
        ],
        note: "Hide tags and the other rows that don't touch the arrangement stay available.",
      },
    ],
    notes: [
      "Every View row that would move, add or remove a panel is greyed while locked (Q2, Q3). View › Log opens the Log panel, so it is greyed too.",
      "Find (Ctrl+F), Check spelling and a toast's Details still show their panel where the arrangement keeps it (Q2).",
    ],
    recs: ["Approve the locked View menu as drawn."],
  },
  {
    n: 5, id: "header", title: "Dragging a header, and resizing, while locked",
    opens: "A press and drag on Search's tab toward the Grid.",
    rows: [
      { label: "Unlocked", scene: "header", args: [false] },
      { label: "Locked", scene: "header", args: [true] },
    ],
    notes: [
      "Unlocked: the move cursor, then the drop highlight, as today.",
      "Locked: the arrow cursor over headers and tabs. A press switches tabs, a drag moves nothing and shows no highlight, and a double-click doesn't undock.",
      "The first refused drag in a session shows a toast, “The layout is locked”, with Unlock. Later drags stay silent; the lock icon on the switcher explains them.",
      "Separators still resize while locked, including DK5's corner resize (#658).",
    ],
    recs: [
      "No move cursor over headers while locked.",
      "One toast with Unlock on the first refused drag of a session, then silence.",
    ],
  },
];
