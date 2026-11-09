#!/bin/bash
# run.sh (under vx11): the app on ep01.ass with the MCP server on; Claude Code edits through `hikarisub --mcp`;
# the Review panel shows both edits; its buttons Keep the second and Undo the first; a scripted client reads the Lines back.
W=/home/altq/.t3/worktrees/hikari/w8-ai1/out/logs/e2e-ai3b
APP=/home/altq/.t3/worktrees/hikari/w8-ai1/out/build/ai3/src/app/hikarisub
U=$W/user2
export HIKARISUB_USER_DIR=$U
cd "$W" || exit 1
shot() { import -window root "$W/$1.png"; echo "shot $1"; }
keys() { xdotool search --sync --onlyvisible --name "HikariSub" windowactivate --sync 2>/dev/null; xdotool key --clearmodifiers "$@"; sleep 1.5; }

"$APP" "$W/ep01.ass" > "$W/app.log" 2>&1 &
pid=$!
for _ in $(seq 240); do
    python3 -I -c 'import json,sys; sys.exit(0 if json.load(open(sys.argv[1])).get("socket") else 1)' \
        "$U/ai/mcp.json" 2>/dev/null && break
    sleep 0.5
done
sleep 6
shot 0-before

cat > "$W/claude-mcp.json" <<EOF
{"mcpServers": {"hikari": {"command": "$APP", "args": ["--mcp"], "env": {"HIKARISUB_USER_DIR": "$U"}}}}
EOF
claude -p "HikariSub has a subtitle file open. Use only the hikari MCP tools. Fix the punctuation in two separate edits: \
first call document_apply with name \"Add the period\" so the Line \"Hello there\" reads \"Hello there.\", then read the \
Document's revision again and call document_apply with name \"Add the exclamation mark\" so the Line \"General Kenobi\" \
reads \"General Kenobi!\". Change nothing else. Then say which Lines you changed." \
    --mcp-config "$W/claude-mcp.json" --strict-mcp-config --allowedTools mcp__hikari --max-turns 16 \
    --output-format stream-json --verbose > "$W/claude.jsonl" 2> "$W/claude.err"
echo "claude $?"
sleep 2
shot 1-after-claude

keys ctrl+alt+shift+r
shot 2-review-panel
click() { xdotool mousemove "$1" "$2" click 1; sleep 1.5; }
click 1248 733
shot 3-kept-exclamation
click 1217 671
shot 4-undone-period

python3 -I "$W/read.py" "$APP" "$U" > "$W/lines.txt" 2>&1
echo "read $?"
kill "$pid"
wait "$pid" 2>/dev/null
echo "app stopped"
