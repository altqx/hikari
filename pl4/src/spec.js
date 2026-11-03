const QUESTIONS = [
  {
    id: "Q1", title: "The plugin manager's shape",
    rec: "One resizable window, list on the left and the chosen plugin's details on the right: what it is, who signed it, its permissions and reasons, what it adds, its settings, and On, Update from file…, Open data folder and Uninstall…. It opens at the plugin a command came from (About Line tools…, a toast's Details).",
    alt: "Keep today's single list, and open each plugin's details in a second window.",
  },
  {
    id: "Q2", title: "Where a plugin's settings are edited",
    rec: "In its details in the plugin manager, as a form HikariSub builds from the manifest's contributes.settings (check box, number, text, choice). A change is kept at once, as hikari.settings.set keeps it.",
    alt: "A Plugins page in Settings, one group per plugin.",
  },
  {
    id: "Q3", title: "A saved layout that names a missing plugin's panel",
    rec: "The panel keeps its place and shows why, with the one command that helps: Open Plugins… when the plugin isn't installed, Turn on when it is off. Closing the placeholder, or View › Panels › name › Remove from the layout, drops it for good. When the plugin comes back, its panel fills the place.",
    alt: "Hide the place silently and remember it, so nothing shows until the plugin is back.",
  },
  {
    id: "Q4", title: "Telling a plugin's panel from HikariSub's own",
    rec: "The header shows the panel's title only, like every panel. The plugin's name is in View › Panels after the title, in the header's tooltip (“Timing check, from Line tools”) and in the ⋯ menu as About Line tools…, which opens the plugin manager at it.",
    alt: "Put the plugin's 16 px icon before the title in the header and tab.",
  },
  {
    id: "Q5", title: "Colours a plugin may use",
    rec: "Widgets take the theme only. The canvas draws with the theme's role names (accent, text, muted, line, danger, warning, success, panel, field) so it follows every theme and high contrast; literal colours are allowed only for subtitle content (a style's colour, a swatch), and HC draws them with a text-coloured outline.",
    alt: "Allow any colour anywhere on the canvas.",
  },
  {
    id: "Q6", title: "Where a new plugin panel first appears",
    rec: "Closed until the user shows it from View › Panels or the plugin's command calls panels.show. The first show docks it in the area the manifest names (left, right or bottom; right when it names none), tabbed with the panel already there. After that it goes where the user left it, per arrangement (ADR 0020).",
    alt: "Show a plugin's panels as soon as it is installed.",
  },
];

const SECTIONS = [
  {
    n: 1, id: "manager", title: "The plugin manager",
    opens: "Automation › Plugins… (GLOBAL_PLUGINS 5231), About <plugin>… in a plugin panel's ⋯, and Details in a plugin's toast. A DG1 window, modeless, 760 × 520 at first and resizable.",
    rows: [
      { label: "A plugin chosen", scene: "manager", args: ["ok"] },
      { label: "One that didn't start", scene: "manager", args: ["broken"] },
      { label: "Nothing installed", scene: "managerEmpty" },
    ],
    notes: [
      "Each row: the package's icon (its manifest icon; the plugins icon when it has none), name, version, trust, and a state at the trailing edge only when it isn't simply running: Turned off, Didn't start, Stopped, Full trust.",
      "The filter matches name, author and description. Up and Down move through the list; Space turns the chosen plugin on or off; Delete asks to uninstall, as today.",
      "Details: name, version, authors and the home page link from the manifest; trust; the description; Permissions with each reason in quotes (or “Only the active subtitle file.”); Adds, listing the commands with their menu path and the panels; Settings; then the actions.",
      "On is the same switch as the row's check box today, moved into the details so the list stays quiet. Turning a plugin off stops it, and its commands and panels leave (panels become the placeholder in section 6).",
      "Update from file… is today's Install from file… flow for this plugin: it asks Update “Line tools” from 1.2.0 to 1.3.0?, and refuses a package with another id. PL6 adds checking the marketplace.",
      "A plugin that didn't start shows the error with its traceback in the details, with Start again and Copy. A crash or a time-limit stop shows the same block with its reason.",
      "Uninstall… keeps today's question, with Uninstall, keep its data and Uninstall and remove its data.",
      "Empty: one muted line and the command that fills it.",
    ],
    recs: [
      "Approve the master–detail window as drawn (Q1).",
      "Approve plugin settings as a generated form in the details (Q2).",
      "Approve the row states (Turned off, Didn't start, Stopped, Full trust) and the error block with Start again.",
      "Approve Update from file… and Open data folder as the per-plugin actions, with Uninstall… outlined in danger at the trailing edge.",
    ],
  },
  {
    n: 2, id: "panel", title: "A plugin's panel",
    opens: "Declared in hikari-plugin.json as contributes.panels [{id, title, area}], filled by hikari.panels.render(id, tree). Its dock id is plugin:<plugin id>/<panel id>, outside the numbered registry. Shown from View › Panels or by the plugin.",
    rows: [
      { label: "Docked, with ⋯", scene: "panel" },
      { label: "Tabbed", scene: "tabbed" },
      { label: "Floating", scene: "floating" },
    ],
    notes: [
      "A plugin panel is a PanelDock like every other: it docks, tabs, floats, takes the focus mark, saves in layouts and in each arrangement, and follows Lock layout (DK4).",
      "Its body is the plugin's widget tree (section 4), with the panels' 4-pixel margin and 8-pixel spacing. An event (a click, an edit, a choice) goes to the plugin with the widget's id; the plugin answers with changes to the tree, never pixels.",
      "The ⋯ menu starts with the panel's own rows from the tree (Check again with its shortcut, a check row), then the usual Move panel…, Undock and Close, and About Line tools….",
      "A panel's shortcuts (F5 here) work while the panel has the focus, like the Search panel's, and show in Settings › Shortcuts under Plugin shortcuts.",
      "Drawn: the Timing check panel of Line tools, a list of problems with severity icons, a two-way filter, and a primary action.",
    ],
    recs: [
      "Approve plugin panels as ordinary docks, with title-only headers (Q4).",
      "Approve the ⋯ menu order: the panel's own rows, the docking rows, About <plugin>…, Close.",
      "Approve first placement from the manifest's area, closed until shown (Q6).",
    ],
  },
  {
    n: 3, id: "view-panels", title: "View › Panels",
    opens: "Plugin panels follow HikariSub's own, after a separator, sorted by title, each with its plugin's name in muted text after it.",
    rows: [
      { label: "A running plugin", scene: "panelsMenu", args: [false] },
      { label: "A missing plugin", scene: "panelsMenu", args: [true] },
    ],
    notes: [
      "Each plugin panel gets the same Show, Hide, Float and Dock rows as a built-in one.",
      "A panel the current layout keeps for a missing or turned-off plugin is listed too, so it can be removed: Show is greyed, and Remove from the layout drops the placeholder.",
      "Move panel… lists plugin panels with the same suffix.",
    ],
    recs: [
      "Approve the group after a separator, titles with the plugin's name as a muted suffix.",
      "Approve Remove from the layout for a missing plugin's panel (Q3).",
    ],
  },
  {
    n: 4, id: "widgets", title: "The widget tree",
    opens: "What a restricted plugin can put in a panel or a dialog. Each kind maps to the control HikariSub already uses for it (HikariStyle, PK1, the script dialogs), so it follows the theme, the focus ring, accessibility and high contrast without the plugin doing anything.",
    rows: [
      { label: "Input", scene: "widgetsInput" },
      { label: "Display and layout", scene: "widgetsShow" },
    ],
    notes: [
      "A node is {kind, id, …}: label, button (normal, primary, danger), icon_button (an icon role from src/ui/icons, with its hover name required), text, text_area, number (with unit, minimum, maximum, step), time, check, choice (a segmented row of 2–4), radio, dropdown, colour (opens the colour popover; ASS &HAABBGGRR), slider, progress, list (rows with icon, text, trailing text, paged on request), tree, tabs, group (a heading over its children), link, image (from the package), row, column, grid, empty (one muted line), and canvas (section 5).",
      "Labels come from the plugin's i18n catalogue. Every input has an accessible name: its label, or the name the node gives.",
      "No free HTML or rich text, no fonts or sizes beyond heading, body, muted and small, no colours (Q5).",
      "Lists and trees are virtualised and ask the plugin for the rows they show, so a panel can list every line of a long file.",
    ],
    recs: [
      "Approve the kinds as listed, mapped onto the existing controls.",
      "Approve that icon buttons use HikariSub's icon roles only, each with a required hover name.",
      "Approve plain text only, with heading, muted and small as the only text styles.",
    ],
  },
  {
    n: 5, id: "canvas", title: "The canvas",
    opens: "A canvas node takes a draw list: rect, line, polyline, path, ellipse, text and image, in the canvas's own pixels, redrawn when the plugin sends a new list. Pointer events come back with coordinates.",
    rows: [
      { label: "A reading-speed chart", scene: "canvas" },
    ],
    notes: [
      "Drawn: a reading-speed bar chart for the selected lines; bars over the limit in warning with a text outline, the limit as a dashed danger line, the hovered bar's outline thicker with a tooltip the plugin supplies.",
      "Colour is never the only cue: in high contrast black, accent and warning are both yellow, so the outline is what sets the bars apart. docs/plugins.md will say so for every canvas.",
      "Colours are theme role names, so the chart follows Light, Dark and both high-contrast themes (Q5).",
      "Click, drag and hover come back as {x, y, buttons, modifiers}. The canvas has an accessible name and a text description the plugin gives, since a chart isn't readable to a screen reader.",
      "It suits 2D charts and overlays. It isn't meant for per-frame video work.",
    ],
    recs: [
      "Approve the canvas operations as listed.",
      "Approve theme role names as the canvas's colours, with literal colours only for subtitle content (Q5).",
    ],
  },
  {
    n: 6, id: "placeholders", title: "A panel without its plugin",
    opens: "What a plugin panel's place shows when the layout names it but the plugin can't fill it. The same component covers all three states.",
    rows: [
      { label: "Not installed", scene: "placeholder", args: ["missing"] },
      { label: "Turned off", scene: "placeholder", args: ["off"] },
      { label: "Stopped", scene: "placeholder", args: ["stopped"] },
    ],
    notes: [
      "Not installed: the header keeps the title the layout saved. Open Plugins… opens the manager; installing the plugin fills the place.",
      "Turned off: Turn on starts the plugin and its panel fills in.",
      "Stopped, after a crash or the 10-second limit: the panel's last content goes and this shows instead, with the reason. Start again starts the helper and asks the plugin to draw the panel again; Details opens the Log at the entry.",
      "Closing a placeholder removes it from the layout, the same as Remove from the layout in section 3.",
      "A saved layout with a missing plugin's panel loads; today it would be refused (workspace_layout.cpp:497).",
    ],
    recs: [
      "Approve the three states, each with one command (Q3).",
      "Approve that closing a placeholder forgets it.",
    ],
  },
  {
    n: 7, id: "dialogs", title: "A plugin's dialog",
    opens: "hikari.dialogs.show(id, tree) opens a DG1 window titled by the dialog, with dialogId plugin.<plugin id>.<dialog id>, so its place and size are remembered per dialog.",
    rows: [
      { label: "A form", scene: "dialog" },
      { label: "A wait", scene: "dialogBusy" },
    ],
    notes: [
      "Buttons go in the footer in the platform's order. The plugin names the default button and the one that cancels; Esc and the close glyph press the cancel one.",
      "Modeless by default; modal to the main window when the plugin asks for an answer before it goes on (hikari.dialogs.ask returns the button and the values).",
      "The body is the same widget tree as a panel's, so a plugin can use one form in both.",
      "A dialog belongs to its plugin's helper: if the plugin stops, its dialogs close.",
    ],
    recs: [
      "Approve plugin dialogs on the DG1 frame with per-dialog placement.",
      "Approve modeless by default, modal only for hikari.dialogs.ask.",
    ],
  },
];
