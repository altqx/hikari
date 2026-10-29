#!/usr/bin/env python3
"""Usage: check_declared_skips.py SOURCE_DIR"""
import ast
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
gate = root / "tools/native-gate/windows/gate_windows.py"
tests_ps1 = root / "tools/native-gate/windows/tests.ps1"

known = None
for node in ast.walk(ast.parse(gate.read_text(encoding="utf-8"))):
    if isinstance(node, ast.FunctionDef) and node.name == "step_test_executables":
        for stmt in ast.walk(node):
            if isinstance(stmt, ast.Assign) and any(getattr(t, "id", None) == "known" for t in stmt.targets):
                known = ast.literal_eval(stmt.value)
if not known:
    sys.exit(f"no `known` tuple in step_test_executables ({gate})")

runs = re.findall(r"exe\s*=\s*'hikari_ui_(\w+)';\s*fns\s*=\s*@\(([^)]*)\)", tests_ps1.read_text(encoding="utf-8"))
if len(runs) != 6:
    sys.exit(f"expected the 6 test executables of {tests_ps1}, found {[r[0] for r in runs]}")

failures = []
all_reasons = []
for suite, fns in runs:
    source = root / "tests/ui" / f"{suite}.cpp"
    text = source.read_text(encoding="utf-8")
    reasons = re.findall(r'QSKIP\("((?:[^"\\]|\\.)*)"\)', text)
    all_reasons += reasons
    if fns.strip():
        continue
    unread = len(re.findall(r"\bQSKIP\(", text)) - len(reasons)
    if unread:
        failures.append(f"{source.relative_to(root)}: {unread} QSKIP whose reason is not one string literal; "
                        f"the declared skips cannot be checked against it")
    for reason in reasons:
        if not any(k in reason for k in known):
            failures.append(f"{source.relative_to(root)}: QSKIP(\"{reason}\") is not declared in "
                            f"step_test_executables' known skips, so the Windows gate fails on it")
for k in known:
    if not any(k in reason for reason in all_reasons):
        failures.append(f"declared skip {k!r} is no reason any suite tests.ps1 runs skips with")

for f in failures:
    print(f)
print(f"{len(known)} declared skips, {len(all_reasons)} QSKIP reasons in {len(runs)} suites, "
      f"{len(failures)} problems")
sys.exit(1 if failures else 0)
