# Git line marks: end-to-end run under vx11

HikariSub from w8-1114 (d7c903226, with `document.saved`), plugin op3-git-line-marks 937d474, a clean user folder.
The file is `ep01.ass` in a git repository, tag `v1`, with uncommitted edits.

1. Automation › Git line marks has Refresh and Clear (01, 02). Refresh opens the right-docked panel with branches, tags and recent commits (03).
2. The first Compare set the marks, but the panel stayed on "Comparing with v1…" (04): the legend canvas had no `id`, so the host refused the tree. Fixed in 937d474, with a test that checks every panel state against the host's widget rules.
3. After a restart the panel remembers v1, and Refresh compares straight away (05). The result: 1 added, 4 changed, 1 removed, 5 Lines marked (06). Line 5 is retimed and has the removed Line above it, so it gets one danger mark.
4. The Grid tooltip names the change (07). Clicking a row in the list selects that Line (08).
5. An edit to Line 9 leaves the marks alone (09). Ctrl+S writes the file, and `document.saved` refreshes: Line 9 is marked and the legend reads 5 changed (10).
6. Clear removes every mark (11). The Plugins page shows the icon, the vcs.read reason, "Marks 6 lines" and both settings (12). Turning the plugin off removes its marks (13).
7. Not ours: the plugin switch still reads "On" next to a switch that is off (14).
