#!/usr/bin/env python3
"""Write the codebase-specific half of the HikariSub NSIS installer.

hikarisub.nsi is the same file on the legacy and Qt branches. Everything that
differs between the two codebases, or between two builds, comes from the
payload.nsh this script writes:

  HS_VERSION, HS_CODEBASE        the SemVer release version and "legacy" or "qt"
  HS_VI_VERSION                  MAJOR.MINOR.PATCH.0 for the version resource
  HS_PAYLOAD                     the staged payload folder
  HS_PAYLOAD_KB                  the payload size in KiB (EstimatedSize)
  HS_MANIFEST                    the install-manifest.txt written beside it
  HS_ICON_IDS, HS_ICON_ID_<ext>  the file-type icon resource ids in HikariSub.exe
  HS_INSTALLER_ICON              optional installer icon
  HS_PAYLOAD_FILES               a macro of SetOutPath/File lines, one per file
  HsIsPayloadPath / un.          functions: is $R0 a path of this payload?

install-manifest.txt holds one payload-relative path per line, files only,
with backslashes, sorted. The installer writes it into the install folder last,
and the next installer or the uninstaller reads it (installers spec, 6.5/6.6).

Usage:
  make-payload-nsh.py --payload <dir> --version <V> --codebase legacy|qt
                      --out <dir> [--icon <file.ico>] [--icon-header <FileTypeIcons.h>]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# The file types the installer registers (spec 1.5): never .txt. The ids are
# the icon resource ids both codebases' HikariSub.exe carry; they are part of
# the registry format (DefaultIcon = "<exe>",-<id>) and never move.
FILE_TYPES = [
    ("ass", 109),
    ("ssa", 110),
    ("srt", 111),
    ("sub", 112),
]

SEMVER = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-((?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*)"
    r"(?:\.(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*))*))?$")

# Files the installer itself writes into the install folder. A payload must
# not carry them, or the uninstaller would treat them as payload.
RESERVED = {"package-kind.txt", "install-manifest.txt", "uninstall hikarisub.exe"}


def nsis_escape(text: str) -> str:
    """text for inside an NSIS double-quoted string, $ escaped."""
    if '"' in text or "\n" in text or "\r" in text:
        raise SystemExit(f"cannot quote for NSIS: {text!r}")
    return text.replace("$", "$$")


def nsis_quote(text: str) -> str:
    return '"' + nsis_escape(text) + '"'


def check_icon_header(header: Path) -> None:
    """Fail when FileTypeIcons.h disagrees with FILE_TYPES."""
    text = header.read_text(encoding="utf-8", errors="replace")
    for ext, rid in FILE_TYPES:
        m = re.search(rf"^#define\s+IDI_FILETYPE_{ext.upper()}\s+(\d+)", text, re.M)
        if not m:
            raise SystemExit(f"{header}: IDI_FILETYPE_{ext.upper()} not found")
        if int(m.group(1)) != rid:
            raise SystemExit(f"{header}: IDI_FILETYPE_{ext.upper()} is {m.group(1)}, "
                             f"the installer expects {rid}")


def collect(payload: Path) -> list[Path]:
    files = []
    for path in payload.rglob("*"):
        if path.is_symlink():
            raise SystemExit(f"payload holds a symlink: {path}")
        if not path.is_file():
            continue
        rel = path.relative_to(payload)
        text = rel.as_posix()
        # ASCII without $: makensis takes File sources literally, while the
        # same paths are run-time strings elsewhere, and the manifest is read
        # in the ANSI code page.
        if not text.isascii() or "$" in text:
            raise SystemExit(f"payload path must be ASCII without $: {text!r}")
        if rel.name.lower() in RESERVED and len(rel.parts) == 1:
            raise SystemExit(f"payload must not contain {rel.name}")
        files.append(rel)
    if not files:
        raise SystemExit(f"payload is empty: {payload}")
    # Sorted case-insensitively, then exactly, so the order does not depend on
    # the file system the payload was staged on.
    files.sort(key=lambda p: (p.as_posix().lower(), p.as_posix()))
    lowered = [p.as_posix().lower() for p in files]
    if len(set(lowered)) != len(lowered):
        raise SystemExit("payload has paths that differ only in case")
    return files


def win(rel: Path) -> str:
    return "\\".join(rel.parts)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--payload", required=True, help="the staged payload folder")
    ap.add_argument("--version", required=True, help="the release version, no leading v")
    ap.add_argument("--codebase", required=True, choices=["legacy", "qt"])
    ap.add_argument("--out", required=True, help="folder for payload.nsh and install-manifest.txt")
    ap.add_argument("--icon", help="installer and uninstaller icon (.ico)")
    ap.add_argument("--icon-header", help="FileTypeIcons.h to check the icon ids against")
    args = ap.parse_args()

    m = SEMVER.match(args.version)
    if not m:
        raise SystemExit(f"not a SemVer release version (no build metadata): {args.version}")
    major, minor, patch = (int(m.group(i)) for i in (1, 2, 3))
    is_qt = (major, minor) >= (0, 1)
    if is_qt != (args.codebase == "qt"):
        raise SystemExit(f"version {args.version} does not belong to codebase {args.codebase}")
    if max(major, minor, patch) > 65535:
        raise SystemExit("version part above 65535 does not fit the version resource")

    if args.icon_header:
        check_icon_header(Path(args.icon_header))

    payload = Path(args.payload).resolve()
    if not (payload / "HikariSub.exe").is_file():
        raise SystemExit(f"{payload} has no HikariSub.exe")
    files = collect(payload)
    total = sum((payload / f).stat().st_size for f in files)

    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    manifest = out / "install-manifest.txt"
    manifest.write_bytes(("".join(win(f) + "\r\n" for f in files)).encode("ascii"))

    lines = [
        "; Generated by packaging/windows/make-payload-nsh.py. Do not edit.",
        f"!define HS_VERSION {nsis_quote(args.version)}",
        f"!define HS_CODEBASE {nsis_quote(args.codebase)}",
        f"!define HS_VI_VERSION \"{major}.{minor}.{patch}.0\"",
        f"!define HS_PAYLOAD {nsis_quote(payload.as_posix())}",
        f"!define HS_PAYLOAD_KB {(total + 1023) // 1024}",
        f"!define HS_PAYLOAD_FILE_COUNT {len(files)}",
        f"!define HS_MANIFEST {nsis_quote(manifest.as_posix())}",
        "!define HS_ICON_IDS \"" + ",".join(f".{e}={i}" for e, i in FILE_TYPES) + "\"",
    ]
    lines += [f"!define HS_ICON_ID_{e} {i}" for e, i in FILE_TYPES]
    if args.icon:
        icon = Path(args.icon).resolve()
        if not icon.is_file():
            raise SystemExit(f"no such icon: {icon}")
        lines.append(f"!define HS_INSTALLER_ICON {nsis_quote(icon.as_posix())}")

    lines += ["", "!macro HS_PAYLOAD_FILES"]
    current = None
    for f in files:
        folder = f.parent
        if folder != current:
            sub = "" if folder == Path(".") else "\\" + nsis_escape(win(folder))
            lines.append(f'  SetOutPath "$INSTDIR{sub}"')
            current = folder
        lines.append(f"  File {nsis_quote((payload / f).as_posix())}")
    lines.append("  SetOutPath \"$INSTDIR\"")
    lines.append("!macroend")

    # Membership test for the upgrade's orphan sweep (6.6, step 3). StrCmp
    # ignores case, as Windows paths do.
    lines += ["", "!macro HS_PAYLOAD_PATH_FUNCTION PREFIX",
              "Function ${PREFIX}HsIsPayloadPath",
              "  StrCpy $R1 1"]
    for f in files:
        lines.append(f"  StrCmp $R0 {nsis_quote(win(f))} hs_in_payload")
    lines += ["  StrCpy $R1 0", "  hs_in_payload:", "FunctionEnd", "!macroend", ""]

    (out / "payload.nsh").write_text("\n".join(lines), encoding="utf-8")
    print(f"payload.nsh: {len(files)} files, {total} bytes, version {args.version} ({args.codebase})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
