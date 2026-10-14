#!/usr/bin/env python3
"""Checks Thirdparty/Build/Libass/Libass.vcxproj against libass's own source lists.

The Windows build compiles libass from a hand-written project, so a libass
update that adds a source file links with unresolved symbols, or quietly
leaves an assembly kernel out, until the project lists it. This compares
the C and x86 assembly sources an x64 DirectWrite build compiles, as
libass/meson.build and libass/Makefile_library.am give them, with the
ClCompile and Nasm items of the project and its .filters file. It also
checks that every file the project names exists.

    python .github/scripts/check-libass-vcxproj.py [repository root]
"""
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
libass = root / "Thirdparty" / "libass" / "libass"
project = root / "Thirdparty" / "Build" / "Libass" / "Libass.vcxproj"
filters = project.with_name(project.name + ".filters")
PREFIX = "..\\..\\libass\\libass\\"

errors = []


def compiled(name):
    return name.endswith((".c", ".asm"))


def meson_sources():
    text = (libass / "meson.build").read_text(encoding="utf-8")
    lists = {}
    for m in re.finditer(r"^(\w+)\s*=\s*files\((.*?)\)", text, re.M | re.S):
        lists[m.group(1)] = re.findall(r"'([^']+)'", m.group(2))
    wanted = ["libass_src", "src_directwrite", "src_x86"]
    missing = [name for name in wanted if name not in lists]
    if missing:
        sys.exit(f"meson.build has no files() list named {', '.join(missing)}")
    return {f for name in wanted for f in lists[name] if compiled(f)}


def automake_sources():
    text = (libass / "Makefile_library.am").read_text(encoding="utf-8")
    text = text.replace("\\\n", " ")
    # the conditional blocks this build takes; FONTCONFIG, CORETEXT and
    # AARCH64 are the ones it does not
    taken = {"ASM", "X86", "DIRECTWRITE"}
    stack, found = [], set()
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("if "):
            stack.append(line[3:].strip())
            continue
        if line == "endif":
            stack.pop()
            continue
        if line.startswith("else"):
            sys.exit("Makefile_library.am uses else; teach this script about it")
        if not all(cond in taken for cond in stack):
            continue
        m = re.match(r"libass_libass_internal_la_SOURCES\s*\+?=(.*)", line)
        if m:
            found |= {f.removeprefix("libass/") for f in m.group(1).split() if compiled(f)}
    if not found:
        sys.exit("found no libass_libass_internal_la_SOURCES in Makefile_library.am")
    return found


def project_items(path, kinds):
    ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
    tree = ET.parse(path)
    items = {}
    for kind in kinds:
        for el in tree.getroot().iterfind(f".//m:ItemGroup/m:{kind}", ns):
            items.setdefault(kind, []).append(el.get("Include"))
    return items


def libass_relative(includes):
    result = set()
    for inc in includes:
        if inc.startswith(PREFIX):
            result.add(inc[len(PREFIX):].replace("\\", "/"))
    return result


def compare(label, have, want, source):
    for name in sorted(want - have):
        errors.append(f"{label} is missing libass/{name}")
    for name in sorted(have - want):
        errors.append(f"{label} lists libass/{name}, which {source} does not build")


meson = meson_sources()
automake = automake_sources()
if meson != automake:
    errors.append("meson.build and Makefile_library.am disagree: "
                  f"only meson {sorted(meson - automake)}, only automake {sorted(automake - meson)}")

items = project_items(project, ["ClCompile", "Nasm", "ClInclude", "None"])
built = libass_relative(items.get("ClCompile", []) + items.get("Nasm", []))
compare(project.name, built, meson | automake, "libass")

filter_items = project_items(filters, ["ClCompile", "Nasm"])
compare(filters.name, libass_relative(filter_items.get("ClCompile", []) + filter_items.get("Nasm", [])), built, project.name)

for includes in items.values():
    for inc in includes:
        if not (project.parent / inc.replace("\\", "/")).is_file():
            errors.append(f"{project.name} names {inc}, which does not exist")

if errors:
    for e in errors:
        print(f"error: {e}")
    sys.exit(1)
print(f"{project.name} builds the {len(built)} C and assembly sources libass lists")
