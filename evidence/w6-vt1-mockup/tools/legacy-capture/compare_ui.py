#!/usr/bin/env python3
"""F1/F3: compare the legacy ui captures with the rewrite's replay.

    compare_ui.py <legacy observations.json> <find-replace-capture.json> [--markdown]

The legacy side is ui_capture.py's (or drive.py's) observations; the rewrite
side is the artifact FindReplaceCapture.ReplaysTheLegacyCaptures writes
(<build>/tests/application/artifacts/find-replace-capture.json). For each F1
case it lists every state dump (texts, selected rows, active row, editor
selection) and every answered message box beside the rewrite's, marking the
differences and the cases the rewrite departs from by an approved
departure. The F3 cases list the legacy Spellchecker walk (row, selection,
word); LegacySpellingCapture.WindowWalkMatchesTheLinuxCapture compares it.
A listed difference is a candidate for review, not a verdict.
"""
import json
import sys


def legacy_dump(state):
    if state is None:
        return None
    return {"texts": [l["text"] for l in state["lines"]], "selected": state["selected"],
            "active": state["active"], "editor_selection": state["editor_selection"]}


def short(d):
    if d is None:
        return "(no response)"
    return f"rows {d['selected']} active {d['active']} editor {d['editor_selection']} texts {d['texts']}"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        sys.exit(__doc__)
    legacy = json.load(open(args[0], encoding="utf-8"))
    rewrite = {c["id"]: c for c in json.load(open(args[1], encoding="utf-8"))["cases"]}
    cases = [o for c in legacy["cases"] if c.get("route") == "ui" for o in c["observations"]]
    differ, same, departures = 0, 0, []
    out = [f"# F1/F3 legacy captures against the rewrite", "",
           f"Legacy {legacy.get('legacy_commit', '')[:8]} on {legacy.get('host', '')}, captured {legacy.get('captured_utc', '')}.", ""]
    for case in cases:
        cid = case["id"]
        out.append(f"## {cid} ({case.get('status')})")
        if case.get("card") == "F3":
            for s in case["steps"]:
                if s["do"] == "spell_walk":
                    texts = s.get("last_state", {}).get("lines", [])
                    for w in s["words"]:
                        a, b = w["editor_selection"]
                        word = texts[w["active"]]["text"][a - 1:b - 1] if w["active"] < len(texts) else "?"
                        out.append(f"- row {w['active']} [{a - 1}, {b - 2}] {word!r}")
                    out.append(f"- end: {s.get('end')}")
            out.append("")
            continue
        mine = rewrite.get(cid)
        if mine is None:
            out.append("- not replayed by the rewrite")
            out.append("")
            continue
        if mine.get("departure"):
            departures.append(cid)
            out.append(f"- departure: {mine['departure']}")
        for i, s in enumerate(case["steps"]):
            r = mine["steps"][i] if i < len(mine["steps"]) else {}
            if s["do"] == "dump":
                theirs = legacy_dump(s.get("state"))
                ours = r.get("state")
                ok = theirs == ours
                same += ok
                differ += not ok
                mark = "same" if ok else "DIFFERS"
                out.append(f"- step {i} dump {mark}: legacy {short(theirs)}")
                if not ok:
                    out.append(f"  rewrite {short(ours)}")
            elif s["do"] == "answer":
                out.append(f"- step {i} answer {s.get('answer')}: legacy box {s.get('title')!r}, "
                           f"rewrite {r.get('title')!r} {r.get('text', '')!r}")
            elif r.get("results"):
                out.append(f"- step {i} {s['do']}: rewrite results {r['results']}")
        out.append("")
    out.insert(4, f"Dumps: {same} the same, {differ} different; departures: {', '.join(departures) or 'none'}.")
    out.insert(5, "")
    print("\n".join(out))


if __name__ == "__main__":
    main()
