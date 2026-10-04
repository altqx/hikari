#!/bin/bash
# kwin_windows.sh: KWin's window list (caption, frame geometry, output,
# active, fullscreen), printed by a KWin script through a D-Bus call back
# into this bus (dbus-monitor captures it).
js=$(mktemp --suffix=.js)
cat > "$js" <<'EOF'
const out = [];
for (const w of workspace.windowList()) {
    if (w.desktopWindow || w.dock || w.specialWindow && !w.dialog && !w.utility && !w.normalWindow) continue;
    const g = w.frameGeometry;
    out.push(`${w.resourceClass} ${JSON.stringify(w.caption)} ${g.x},${g.y} ${g.width}x${g.height} output=${w.output ? w.output.name : "?"} active=${w.active} fullscreen=${w.fullScreen} minimized=${w.minimized} type=${w.windowType}`);
}
callDBus("org.gate.Sink", "/", "org.gate.Sink", "report", out.join("\n"));
EOF
dbus-monitor --session "interface='org.gate.Sink'" > "$js.mon" 2>/dev/null &
mon=$!
sleep 0.3
id=$(gdbus call --session --dest org.kde.KWin --object-path /Scripting --method org.kde.kwin.Scripting.loadScript "$js" "gate-$$" | tr -dc '0-9')
gdbus call --session --dest org.kde.KWin --object-path /Scripting/Script$id --method org.kde.kwin.Script.run >/dev/null
sleep 0.5
gdbus call --session --dest org.kde.KWin --object-path /Scripting --method org.kde.kwin.Scripting.unloadScript "gate-$$" >/dev/null
kill $mon
sed -n '/string "/,/"$/p' "$js.mon" | sed 's/^ *string "//; s/"$//'  | grep -v -E '^:[0-9]|^signal '
mv "$js" "$js.mon" /tmp/ 2>/dev/null
true
