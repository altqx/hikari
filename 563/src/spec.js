const QUESTIONS = [
  {
    id: "Q1", title: "Where the list lives",
    rec: "A third view in the Automation panel, Recommended, beside Scripts and Runs. It has the same filter and folding sections as Scripts.",
    alt: "A Recommended section at the end of the Scripts list, after Autoload.",
  },
  {
    id: "Q2", title: "What a row shows",
    rec: "The name, then Install or the Installed mark at the trailing edge. Under it, our own one line on what the script does (translated), then the author and licence, muted, with any requirement (Needs Git). Hovering the name shows the feed's own description and, once installed, the version.",
    alt: "Author and licence only in the name's tooltip, with one Credits line at the foot of the view.",
  },
  {
    id: "Q3", title: "How an install runs",
    rec: "It is an Automation run, “Install Git Signs”, through the bundled DependencyControl. The row shows Installing… with the busy mark, a run over about 1 s shows the usual progress card with Cancel, and its log stays in Runs. On success the autoload scripts are refreshed, so the script loads without a restart, the row shows Installed, and a toast says where its macros are, with Show. A failure keeps the row, shows the reason in danger with Try again, and Details opens its run.",
    alt: "Open DependencyControl's own Install Script dialog with the script chosen, with its own messages.",
  },
  {
    id: "Q4", title: "Uninstalling",
    rec: "Uninstall… in the row's ⋯ menu and on Delete. It asks “Uninstall Git Signs?”: DependencyControl deletes its files from the user folder, and the modules it brought stay for other scripts. Cancel is the default. The script unloads at once.",
    alt: "An Uninstall button on the row next to the Installed mark, with the same question.",
  },
  {
    id: "Q5", title: "What counts as installed",
    rec: "DependencyControl's record of the script (from any feed, at any version), or a script with the same file name in an autoload folder. A copy put there by hand shows Installed, with Uninstall… greyed and a hint to remove the file. A script turned off in Scripts still counts.",
    alt: "Only DependencyControl's record counts; a copy put there by hand shows Install, and installing replaces it.",
  },
  {
    id: "Q6", title: "Versions and updates",
    rec: "Install the feed's current release and leave updates to DependencyControl, as DC1 decided. The row names no version. The list is checked again whenever the corpus copy of the scripts is refreshed.",
    alt: "Pin each entry to the version the corpus ran. DependencyControl installs only a feed's current release, so this needs our own download.",
  },
  {
    id: "Q7", title: "Where the list comes from",
    rec: "A file built into the app. Each entry has the namespace, feed, group, our one line, author, licence, project page and corpus file. A test checks that each entry has a recorded run in the corpus, a licence and an author. The list changes with releases.",
    alt: "Download the list from the site, so it can change between releases (the corpus test then runs on the site's copy).",
  },
  {
    id: "Q8", title: "Saying what installing does",
    rec: "One muted line at the top of the view: “Installs from each author's feed through DependencyControl. A script runs its code when it loads.” There is no extra question, because Install is already an explicit click.",
    alt: "Ask once, before the first install, with “Don't ask again”.",
  },
  {
    id: "Q9", title: "Ways in",
    rec: "No new command and no hotkey. The Scripts view's empty state gets Browse recommended scripts. DependencyControl's Toolbox macros stay as they are.",
    alt: "Add Automation › Recommended scripts… to the menu bar, which is a new command (a hotkey id through the pin queue).",
  },
];

const SECTIONS = [
  {
    n: 1, id: "view", title: "The Recommended view",
    opens: "Automation panel › Recommended. Two scripts are installed here: Git Signs, through DependencyControl, and Rewriting Tools. The filter matches the name, the one line and the author.",
    rows: [
      { label: "The list", scene: "panel" },
    ],
    notes: [
      "The groups follow the card: Typesetting, Editing, QC and Karaoke, each a folding section with its count, like Scripts. Inside a group, entries are sorted by name.",
      "Every row has three lines: the name with its action, our one line on the job, and the author and licence. Credits are on every row, so no script is listed without its author and licence.",
      "Up and Down move between entries. Enter installs the chosen entry, the Menu key opens its ⋯ menu, and Delete asks to uninstall. Each row's accessible name is its name and state (“Git Signs, installed”), and its description is the one line.",
      "The Installed mark is an icon and a word, never colour alone.",
      "When the panel is narrow, the segment labels elide before the icon buttons do. The view keeps its own filter text.",
    ],
    recs: [
      "Approve a third view, Recommended (Q1).",
      "Approve the three-line row with credits on each row (Q2).",
      "Approve the groups and the one-line texts: Derive Perspective Track “Makes a power-pin track from the lines’ perspective, for Perspective-Motion.”; Focus Lines “Draws moving focus lines, frame by frame.”; Split Tag Sections “Splits a line at its tags, one line per section.”; Rewriting Tools “Keeps earlier rewrites of a line and switches between them.”; Git Signs “Marks the lines changed since a git commit.” (Needs Git); Fix KFX Boilerplate “Fixes old KFX templates so Aegisub 3.5 runs them too.”",
      "Approve the trust line at the top of the view, with no extra question (Q8).",
    ],
  },
  {
    n: 2, id: "states", title: "A row's states",
    opens: "One row, through an install, a failure and a copy put in place by hand.",
    rows: [
      { label: "Not installed", scene: "rowState", args: ["install"] },
      { label: "Installing", scene: "rowState", args: ["installing"] },
      { label: "Failed", scene: "rowState", args: ["failed"] },
      { label: "Installed, ⋯ open", scene: "rowState", args: ["menu"] },
      { label: "Put there by hand", scene: "rowState", args: ["manual"] },
    ],
    notes: [
      "Install runs DependencyControl's updater for the script's namespace and feed, as its Toolbox's Install Script does, and brings the modules the feed names (for Derive Perspective Track: l0.Functional, l0.ASSFoundation, arch.Math and arch.Perspective).",
      "While a script installs, the other rows' Install stays on, and those installs wait their turn. Rows already installed keep their menus.",
      "A failure says why in one line, from DependencyControl's own error: no network, the feed couldn't be read, or a file was locked. The full log is in Runs.",
      "The ⋯ menu has Show in Scripts, which switches to Scripts at its Autoload entry; Open project page; then Uninstall… last.",
      "A copy put there by hand counts as installed (Q5). Uninstall… is greyed, with the hint below it.",
    ],
    recs: [
      "Approve installs as Automation runs, with the busy mark, the usual progress card and the log in Runs (Q3).",
      "Approve the failure line with Try again and Details.",
      "Approve the ⋯ menu: Show in Scripts, Open project page, Uninstall… (Q4).",
      "Approve hand copies counting as installed, with Uninstall… greyed (Q5).",
    ],
  },
  {
    n: 3, id: "after", title: "After an install, and uninstalling",
    opens: "The toast after Git Signs installs, the Scripts view it opens, and the uninstall question.",
    rows: [
      { label: "Installed", scene: "toast" },
      { label: "Show → Scripts", scene: "scripts" },
      { label: "Uninstall…", scene: "uninstall" },
    ],
    notes: [
      "The toast is an info toast. It fades after 5 s, and Show switches the panel to Scripts with the new script chosen.",
      "The new script is loaded already, because the install refreshes the autoload scripts. Its macros are in the Automation menu at once.",
      "The question is a standard message box: a specific title, verb buttons, and Cancel as the safe default. Uninstalling unloads the script, removes its files through DependencyControl, and the row shows Install again.",
      "Shortcuts bound to its macros stay, and they show under Shortcuts for missing macros until it comes back.",
    ],
    recs: [
      "Approve the toast text “Git Signs is installed. Its macros are in Automation › Git Signs.” with Show.",
      "Approve the uninstall question and its detail text (Q4).",
    ],
  },
  {
    n: 4, id: "ways", title: "Ways in",
    opens: "The Scripts view with nothing loaded. No new command, menu bar item or hotkey (Q9).",
    rows: [
      { label: "Scripts, empty", scene: "emptyScripts" },
    ],
    notes: [
      "Browse recommended scripts switches the panel to Recommended. Load script… stays the first button.",
      "No hotkey commands are added, so FA6 moves no pins.",
    ],
    recs: [
      "Approve the link in the empty state, with no new command (Q9).",
    ],
  },
];
