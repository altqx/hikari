# Icon round 3 (IC3)

[IC3](https://github.com/altqx/hikari/issues/778) reviews the icons drawn since [IC2](icon-gaps-2.md) without an icon review, fixes the roles that took a second meaning in wave 8, and draws the icons the open wave-8 cards will need. The list started from the pin train `w8-pin-train` a44575ff7. Rules: [icons.md](icons.md). **New** is a role drawn for IC3, **redrawn** a role whose picture changed, **reused** a role already in the set with the same meaning.

Left without an icon, as IC1 and IC2 decided: check items, entries named by their data, plain text verbs in a DialogButtonBox, and Primary, Danger and Footer buttons. Panel tabs and titles stay text. A notice bar's answers ("Not now", "Dismiss", "Keep mine") stay text.

## 1. Review of the icons drawn without one

| Role | Place | Verdict |
| --- | --- | --- |
| open-timecodes, recent-timecodes, save-timecodes, close-timecodes | Video › Timecodes | redrawn: the ruler filled only the top half of the box; it is now a ruler body with uneven ticks, cut at its lower right for the family's badge |
| video-details | Video › Video details… | redrawn: the open-video film frame with an info badge the size of the set's other badges (the old one reached the box's edge) |
| tab-close-others | Document tab menu: Close other tabs | redrawn: the kept tab with a cross on each side |
| tab-close-right | Document tab menu: Close tabs to the right | redrawn: the kept tab with a cross on its right only; the arrow over the cross was too busy at 16 px |
| convert-folds | Grid › Line group: Convert Aegisub folds to line groups | redrawn: the fold's bracket beside make-tree's group; the old one read as the letters "LE" |
| reload-video, reload-audio | Video › Reload video, Audio › Reload audio | redrawn: open-video's film frame and open-audio's note with the reload badge, a circle open at the top right with a solid arrowhead; the old badge's thin corner arrow read as a blot at 16 px |
| open-audio, recent-audio, close-audio, video-streams, audio-from-video | Audio menu, Video › Streams | redrawn at the owner's request: the note's head is a tilted oval the stem rises from, in place of a round dot beside the stem |
| layout-reset, rerun-macro, refresh, check-updates, restart, restore-default, history, tool-rotate-z | as drawn | redrawn at the owner's request: every circular arrow is a circle open at the top with a solid arrowhead, as the reload badge is |
| checkpoint (pending AI2 #430) | Agent panel: a turn's checkpoint | redrawn: a node on a line; the diamond means a keyframe everywhere else in the set |
| hard-space, paste-special, tab-switch, layout-lock, align-to-video, subtitles-snapshot, subtitles-copy | as drawn | kept |
| node-editor (pending NE2 #424) | View › Node effects, the mode's toolbar toggle | kept: the approved NE2 mockup uses it |
| ai-send (pending AI7 #435) | Agent panel: Send | kept |
| review-keep (pending AI2 #430) | Keep AI change, Keep all AI changes | kept |

## 2. One icon, two meanings, and surfaces without icons

| Place | Was | Now |
| --- | --- | --- |
| File › Open user folder; Settings › General: User folder's Open; Getting started: Open folder; Plugin manager: Open data folder | folder-open | show-in-folder (reused): they open a folder in the file manager; folder-open stays "Choose a folder" |
| Document tab menu, Home page: Copy path; the Explorer's Copy path (PJ4 #772) | edit-copy | copy-path (new: Copy's sheets with a path's slashes) |
| Subtitles › Timing postprocessor… | shift-times | timing-postprocessor (new: a line with lead-in and lead-out arrows) |
| Video › Shift by frames to video: Selected lines / Following lines, start at video | time-offset-start (a Settings row) | shift-start-to-video (new: the playhead with a line moved to start on it) |
| Video › Shift by frames to video: Selected lines / Following lines, end at video | time-offset-end | shift-end-to-video (new) |
| Video › Centre times within frames | split-frames | center-times (new: frame edges with arrows to the middle) |
| Plugin manager: Update from file… | plugin-install | plugin-update (new: the plugin piece with an up arrow) |
| A plugin's panel: Turn on | show | plugin-turn-on (new: the plugin piece with run-script's play badge) |
| Settings › AI: Test connection (twice) | none | test-connection (new: a pulse line) |
| Export as…: Browse… | none | folder-open (reused) |
| Export as…: Use video frame rate | none | fps-from-video (reused) |

Checked and left as they are: Lock layout (check item in the View menu; layout-lock on the status bar and dock headers), the timecodes and video details commands (their own roles), the folds' other line group commands, the Add provider menu's hosted providers (ai-assistant, as IC2 placed them), the provider form's fields (form fields, not option rows), "Turn off all AI features" and "Use prompt caching" (check items), the plugin manager's Start again (restart: restarting a process), Uninstall… (DangerButton), the plugin rows and a plugin's panels and commands (named by their data).

## 3. Drawn for the open cards

The manifest marks these `pending` until their card places them.

| Role | Card | For |
| --- | --- | --- |
| open-project | PJ2 #770 | File › Open folder… (5255), Home's Open folder… |
| recent-projects | PJ2 #770 | Recent projects (the projects are entries named by data) |
| close-project | PJ2 #770 | File › Close project (5256) |
| project | PJ2 #770 | the project in the title bar and on Home; also PJ5's **Project** tier chip, Save to project, and PJ3's "Overridden in this project" |
| mine | PJ5 #773 | the **Mine** tier chip, Save to mine |
| copy-to-tier, move-to-tier | PJ5 #773 | Copy to and Move to between tiers |
| trust | PJ3 #771 | Trust this folder (the trust bar, PJ8's locked rows), Settings › Trusted folders |
| untrusted | PJ8 #776 | a script or plugin row locked until the folder is trusted |
| explorer | PJ4 #772 | View › Explorer (panel 18) |
| new-subtitle-file | PJ4 #772 | Explorer: New subtitle file here |
| show-all-files | PJ4 #772 | Explorer: Show all files |
| group-by-name | PJ4 #772 | Explorer: Group by name |
| tab-to-new-window | MW2 #630 | Move tab to new window (5221) |
| tab-to-window | MW2 #630 | Move tab to window ▸ (5222; the windows are entries named by data) |
| qc-notes | FA20 #577 | View › QC notes (panel 12), Subtitles › QC notes… (5241): the approved mockup's flag |
| qc-done-reset | FA20 #577 | QC notes: Mark all as not done |
| node-library | NE2 #424 | the graph menu's From the library… |
| compact | AI21 #725 | Compact conversation (the thread's ⋯ menu, the 97% offer) |
| review-panel | AI2 #430 | View › Review panel, Show Review panel |
| web-search | AI23 #742 | Let AI search the web, the composer's web toggle |
| update | AI24 #744 | Update now for the sidecar and an agent's CLI |
| network | PL5 #417 | Let plugins connect to the internet; MK1 #667's Turn off offline mode |
| plugin-sandbox | PL7 #419 | a plugin whose helper runs in the OS sandbox |

Reused for the open cards, no new role:

| Place | Card | Role |
| --- | --- | --- |
| Reopen last tabs | PJ2 #770 | last-session |
| Explorer: Open in a new tab; Show in folder; Move to trash; Rename; Project settings | PJ4 #772 | tab-new; show-in-folder; delete; edit (as every Rename); settings |
| File › Quit | MW5 #629 | exit |
| QC notes: Next, Previous; Add note; Import notes file…; from the clipboard; Export notes…; Remove all notes… | FA20 #577 | chevron-right, chevron-left; add; import; edit-paste; export; clear |
| Node effects: Add node, New graph; Fit; Disconnect; Save to the library | NE2 #424 | add; zoom-reset; remove; save-as (as Save as preset…) |
| Agent panel; New thread; Threads; Rename; Export as Markdown…; Delete; Stop, Stop all | AI7 #435, AI22 #730 | ai-assistant; add; history; edit; export; delete; media-stop |
| Retry the turn, a failed compaction's Retry | AI7 #435, AI21 #725 | refresh (as the session bar's Retry) |
| Next AI change, Previous AI change | AI2 #430 | match-next, match-previous (as Find next / previous) |
| Undo AI change, Undo all AI changes | AI2 #430 | undo |
| Check now; What's new | AI24 #744 | check-updates; whats-new |
| Install from file…, an install link's confirmation | PL6 #418, MK1 #667 | plugin-install |
| Update available; Unverified | PL6 #418 | plugin-update; plugin-unverified |
| Video › Perspective tracking… | FA13 #570 | tool-perspective (the card has no track forward / backward commands) |

Not drawn:
- **Vendor and brand marks.** Providers, external agents, MCP servers and search providers are entries named by their data.
- **AI8 #436's inline actions.** Its design gate isn't drawn yet, so the verbs are unknown.
- **PL6's later states:** revoked, rollback, permission review. They come in PL6b, which has no design yet.
- **New window, and the window that is playing.** MW2, MW3 and MW5 have no such command or indicator.
- **The node families.** They are text categories.
- **AI4, AI9, AI10–AI16 and AI17.** They name no icon-bearing UI beyond warnings, which take notice-warning.
