# A11Y1 (#298) evidence: native gate step `gtkbridge`

Build of qt 9c1eef089 (the application is unchanged by A11Y1), in the native gate's container: mutter (Wayland) with GNOME's desktop names, KWin (Wayland) with Plasma's. GTK's ATK bridge is on (env.sh's `NO_AT_BRIDGE=0`); no session sets `NO_AT_BRIDGE=1`.

- `before/`: the gate's lookup as on qt (the first application named after HikariSub).
- `after/`: the branch's lookup (`gatelib.hikarisub_application`: Qt's). `after/mutter/titlebar-*` is SH1's title bar step on mutter without its old `NO_AT_BRIDGE=1`.
- `bridge-off/mutter/`: Orca with HikariSub started with `NO_AT_BRIDGE=1`. The chooser is silent (`bridgeoff-orca-speech.txt`), against the same keys with the bridge on (`bridge-on-orca-speech.txt`).

Per session:
- `gtkbridge-apps.json`: the platform theme and every application the process registered on the bus.
- `gtkbridge-tree.txt`: the tree the lookup finds.
- `gtkbridge-orca-speech.txt`: what Orca spoke for File > Open and back.
- `results.json`: the verdicts.
