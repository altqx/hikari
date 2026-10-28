#!/usr/bin/env python3
"""Maintainer tool: compute a frozen official-Qt repository lock (cmake/locks/).

Not part of the build. The CMake provisioner only reads the lock this writes.
Reads public repository metadata, resolves the component closure for the
requested package IDs, and records every object the local mirror may serve.

Qt publishes SHA-1 sidecars only. With --hash-dir, each object is streamed,
checked against its publisher SHA-1, and its SHA-256 recorded (trust on first
acquisition over HTTPS). Without it, sha256 fields stay null and the lock is
marked incomplete; the provisioner refuses an incomplete lock.
"""
import argparse, hashlib, io, json, os, re, subprocess, sys, tempfile, urllib.request
import xml.etree.ElementTree as ET

BASE = "https://download.qt.io/online/qtsdkrepository/"
PLATFORMS = {
    "linux": {
        "roots": [
            "linux_x64/desktop/qt6_6112/qt6_6112/",
            "all_os/qt/qt6_6112_unix_line_endings_src/",
            "linux_x64/desktop/tools_generic/",
            "linux_x64/desktop/sdktool/",
            "all_os/license_agreements/licenses/",
            "all_os/qt_patchers/6112/",
            "all_os/unified_patching/",
            "all_os/dependencycheck/",
            "all_os/default_install/",
        ],
        "packages": [
            "qt.qt6.6112.linux_gcc_64",
            # The linux_gcc_64 add-on leaves are virtual: the CLI refuses them
            # ("Component is virtual") yet exits 0. Request the parents; the
            # leaves follow through AutoDependOn (parent + base selected).
            "qt.qt6.6112.addons.qtmultimedia",
            "qt.qt6.6112.addons.qtshadertools",
        ],
        "installer": {
            "url": "https://download.qt.io/archive/online_installers/4.11/qt-online-installer-linux-x64-4.11.0.run",
            "declared_sha256": "40b76bdf74f6a396341efb70ae2e754fcd878474babb6cd9d7f07eff12a85c62",
        },
    },
    "windows": {
        # tools_vcredist is not a locked root: SDKTool adds its nodes only when
        # that repository is offered, so they record as absent-optional.
        "roots": [
            "windows_x86/desktop/qt6_6112/qt6_6112_msvc2022_64/",
            "all_os/qt/qt6_6112_windows_line_endings_src/",
            "windows_x86/desktop/tools_generic/",
            "windows_x86/desktop/sdktool/",
            "all_os/license_agreements/licenses/",
            "all_os/qt_patchers/6112/",
            "all_os/unified_patching/",
            "all_os/dependencycheck/",
            "all_os/default_install/",
        ],
        "packages": [
            "qt.qt6.6112.win64_msvc2022_64",
            "qt.qt6.6112.addons.qtmultimedia",
            "qt.qt6.6112.addons.qtshadertools",
        ],
        "installer": {
            "url": "https://download.qt.io/archive/online_installers/4.11/qt-online-installer-windows-x64-4.11.0.exe",
            "declared_sha256": "ae919bc9b224b8ccdada69ec787a9f69330001f227f3fcbfb4a11a4adb3786f6",
        },
    },
}
UA = {"User-Agent": "hikari-qt-frozen-lock/1"}


def retry(fn, attempts=4):
    for i in range(attempts):
        try:
            return fn()
        except (OSError, TimeoutError):
            if i == attempts - 1:
                raise


def fetch(url):
    def go():
        with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120) as r:
            return r.read()
    return retry(go)


def head_length(url):
    def go():
        req = urllib.request.Request(url, method="HEAD", headers=UA)
        with urllib.request.urlopen(req, timeout=60) as r:
            return int(r.headers["Content-Length"])
    return retry(go)


def stream_hash(url, dest):
    sha1, sha256, n = hashlib.sha1(), hashlib.sha256(), 0
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=300) as r, open(dest, "wb") as f:
        while chunk := r.read(1 << 20):
            sha1.update(chunk); sha256.update(chunk); f.write(chunk); n += len(chunk)
    return sha1.hexdigest(), sha256.hexdigest(), n


def names(field):
    out = []
    for item in (field or "").split(","):
        item = item.strip()
        if item:
            out.append(re.split(r"\s*(?:->|\(|<|>|=)", item, maxsplit=1)[0].strip())
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--platform", default="linux", choices=PLATFORMS)
    ap.add_argument("--out", required=True)
    ap.add_argument("--hash-dir", help="stream objects here to compute SHA-256")
    ap.add_argument("--previous", help="reuse verified digests from this lock for unchanged objects")
    ap.add_argument("--retention-base", help="URL prefix of the retained metadata copy (release assets)")
    a = ap.parse_args()
    plat = PLATFORMS[a.platform]

    roots, index, problems, duplicates = [], {}, [], []
    for suffix in plat["roots"]:
        url = BASE + suffix + "Updates.xml"
        data = fetch(url)
        tree = ET.fromstring(data)
        if tree.findall("RepositoryUpdate"):
            problems.append(f"{suffix}: dynamic RepositoryUpdate present")
        meta = tree.findtext("MetadataName")
        root = {"suffix": suffix, "updates_xml": {"url": url, "length": len(data),
                "sha256": hashlib.sha256(data).hexdigest()}, "metadata": None,
                "checksum": tree.findtext("Checksum"), "package_count": 0}
        if meta:
            root["metadata"] = {"url": BASE + suffix + meta, "name": meta}
        for p in tree.findall("PackageUpdate"):
            n = p.findtext("Name")
            entry = {"root": suffix, "version": p.findtext("Version"),
                     "deps": names(p.findtext("Dependencies")),
                     "auto": names(p.findtext("AutoDependOn")),
                     "virtual": p.findtext("Virtual") == "true",
                     "archives": names(p.findtext("DownloadableArchives")),
                     "script": p.findtext("Script")}
            if n in index:
                old = index[n]
                # The installer keeps the newer version. Accept that only for
                # empty grouping nodes, where nothing else can differ.
                empty = lambda e: not (e["deps"] or e["auto"] or e["archives"] or e["script"])
                if empty(old) and empty(entry):
                    keep = max(old, entry, key=lambda e: e["version"])
                    duplicates.append({"package": n, "roots": [old["root"], suffix], "kept": keep["root"]})
                    index[n] = keep
                else:
                    problems.append(f"duplicate package {n} in {suffix}")
                root["package_count"] += 1
                continue
            index[n] = entry
            root["package_count"] += 1
        roots.append(root)

    # Installer scripts add dependencies at runtime (licenses, patchers). Read
    # them from each root's metadata archive; XML alone under-states closure.
    script_deps, guarded = {}, {}
    with tempfile.TemporaryDirectory() as td:
        for r in roots:
            if not r["metadata"]:
                continue
            data = fetch(r["metadata"]["url"])
            arc = os.path.join(td, "m.7z")
            open(arc, "wb").write(data)
            out = os.path.join(td, "x")
            subprocess.run(["7z", "x", "-y", f"-o{out}", arc], check=True, stdout=subprocess.DEVNULL)
            for dp, _, fs in os.walk(out):
                for f in fs:
                    if not f.endswith(".qs"):
                        continue
                    pkg = os.path.relpath(dp, out).split(os.sep)[0]
                    text = open(os.path.join(dp, f), encoding="utf-8", errors="replace").read()
                    for m in re.finditer(r"add(?:Dependency|AutoDependOn)\(\s*[\"']([^\"']+)", text):
                        script_deps.setdefault(pkg, set()).update(names(m.group(1)))
                    for m in re.finditer(r"componentByName\(\s*[\"']([^\"']+)", text):
                        guarded.setdefault(pkg, set()).add(m.group(1))
            subprocess.run(["rm", "-rf", out], check=True)

    # Closure: hard and script dependencies, then AutoDependOn fixed point.
    selected, stack, absent_optional = set(), [], set()
    def add(n):
        stack.append(n)
        while stack:
            x = stack.pop()
            if x in selected:
                continue
            if x not in index:
                problems.append(f"unresolved dependency {x}")
                continue
            selected.add(x)
            stack.extend(index[x]["deps"])
            for d in sorted(script_deps.get(x, ())):
                if d not in index and d in guarded.get(x, ()):
                    absent_optional.add(f"{x} -> {d}")  # script adds it only if present
                else:
                    stack.append(d)
    for n in plat["packages"]:
        add(n)
    changed = True
    while changed:
        changed = False
        for n, p in index.items():
            if n not in selected and p["auto"] and all(d in selected for d in p["auto"]):
                add(n); changed = True
    reasons = {n: ("requested" if n in plat["packages"] else "dependency/auto") for n in selected}

    # Ancestor nodes the controller may also select; locked, marked conditional.
    conditional = set()
    for n in list(selected):
        parts = n.split(".")
        for i in range(1, len(parts)):
            anc = ".".join(parts[:i])
            if anc in index and anc not in selected:
                conditional.add(anc)
    grow = list(conditional)
    while grow:
        n = grow.pop()
        for d in index[n]["deps"] + sorted(script_deps.get(n, ())):
            if d in index and d not in selected and d not in conditional:
                conditional.add(d); grow.append(d)

    objects = []
    def obj(kind, url, pkg=None, cond=False):
        objects.append({"kind": kind, "url": url, "path": url[len(BASE):], "package": pkg,
                        "conditional": cond, "length": None, "sha1": None, "sha256": None})
    for r in roots:
        if r["metadata"]:
            obj("metadata", r["metadata"]["url"])
    for n in sorted(selected | conditional):
        p = index[n]
        for arc in p["archives"]:
            u = f"{BASE}{p['root']}{n}/{p['version']}{arc}"
            obj("payload", u, n, n in conditional)
            obj("sidecar", u + ".sha1", n, n in conditional)

    installer = dict(plat["installer"], length=None, sha256=None)
    total = 0
    for o in objects:
        o["length"] = head_length(o["url"]); total += o["length"]
    installer["length"] = head_length(installer["url"])

    # Reuse verified digests from a previous lock object by object; hash only
    # what is new or changed (a republished support node, say).
    prev = json.load(open(a.previous)) if a.previous else None
    if prev and not prev.get("complete"):
        sys.exit("previous lock is incomplete")
    known = {o["url"]: o for o in prev["objects"]} if prev else {}
    to_hash = []
    for o in objects:
        k = known.get(o["url"])
        if k and k["length"] == o["length"]:
            o["sha1"], o["sha256"] = k["sha1"], k["sha256"]
        else:
            to_hash.append(o)
    reused = len(objects) - len(to_hash)
    if prev and prev["installer"]["url"] == installer["url"] and prev["installer"]["length"] == installer["length"]:
        installer.update(sha1=prev["installer"]["sha1"], sha256=prev["installer"]["sha256"])
    lic_root = next(r for r in roots if r["suffix"].startswith("all_os/license_agreements"))
    prev_lic = next((r for r in prev["roots"] if r["suffix"] == lic_root["suffix"]), None) if prev else None
    lock_licenses = prev["license_texts"] if prev_lic and prev_lic["updates_xml"]["sha256"] == lic_root["updates_xml"]["sha256"] else None
    needs_hash = to_hash or installer.get("sha256") is None or lock_licenses is None
    if needs_hash and not a.hash_dir:
        sys.exit(f"{len(to_hash)} objects need hashing; rerun with --hash-dir")
    if needs_hash:
        os.makedirs(a.hash_dir, exist_ok=True)
        sidecars = {}
        for o in to_hash:
            if o["kind"] == "sidecar":
                d = fetch(o["url"])
                o["sha1"] = hashlib.sha1(d).hexdigest(); o["sha256"] = hashlib.sha256(d).hexdigest()
        for o in objects:
            if o["kind"] == "sidecar" and o["path"][:-5] in {x["path"] for x in to_hash}:
                sidecars[o["url"][:-5]] = fetch(o["url"]).decode().split()[0].strip().lower()
        lic_meta = next(o for o in objects if o["url"] == lic_root["metadata"]["url"])
        for o in to_hash + ([lic_meta] if lock_licenses is None and lic_meta not in to_hash else []):
            if o["kind"] == "sidecar":
                continue
            dest = os.path.join(a.hash_dir, o["path"].replace("/", "__"))
            s1, s256, n = stream_hash(o["url"], dest)
            if n != o["length"]:
                problems.append(f"length mismatch {o['path']}")
            if o["kind"] == "payload" and sidecars.get(o["url"]) != s1:
                problems.append(f"publisher SHA-1 mismatch {o['path']}")
            if o["sha256"] is not None and o["sha256"] != s256:
                problems.append(f"digest changed {o['path']}")
            o["sha1"], o["sha256"] = s1, s256
            print(f"hashed {o['path']} {n}", file=sys.stderr)
        if installer.get("sha256") is None:
            s1, s256, n = stream_hash(installer["url"], os.path.join(a.hash_dir, "installer.run"))
            installer["sha1"], installer["sha256"] = s1, s256
        if installer["sha256"] != installer["declared_sha256"]:
            problems.append("installer SHA-256 differs from declared value")
        if lock_licenses is None:
            # License texts: changed bytes require fresh owner review.
            with tempfile.TemporaryDirectory() as td:
                subprocess.run(["7z", "x", "-y", f"-o{td}", os.path.join(a.hash_dir, lic_meta["path"].replace("/", "__"))],
                               check=True, stdout=subprocess.DEVNULL)
                licenses = []
                for dp, _, fs in os.walk(td):
                    for f in sorted(fs):
                        if f.lower().endswith((".txt", ".html", ".rtf")) or "licen" in f.lower():
                            full = os.path.join(dp, f)
                            licenses.append({"file": os.path.relpath(full, td),
                                             "sha256": hashlib.sha256(open(full, "rb").read()).hexdigest()})
            lock_licenses = sorted(licenses, key=lambda x: x["file"])

    # Mutable repository metadata is served from a retained copy (GitHub
    # release assets); versioned payload archives still come from Qt.
    if a.retention_base:
        for r in roots:
            path = r["updates_xml"]["url"][len(BASE):]
            r["updates_xml"]["retained_url"] = a.retention_base + path.replace("/", "__")
        for o in objects:
            if o["kind"] == "metadata":
                o["retained_url"] = a.retention_base + o["path"].replace("/", "__")

    lock = {
        "schema": 1,
        "platform": a.platform,
        "base": BASE,
        "requested": plat["packages"],
        "complete": all(o["sha256"] for o in objects) and bool(lock_licenses) and not problems,
        "problems": problems,
        "installer": installer,
        "roots": roots,
        "selected": sorted(selected),
        "selection_reason": dict(sorted(reasons.items())),
        "conditional": sorted(conditional),
        "duplicate_grouping_nodes": duplicates,
        "absent_optional_script_dependencies": sorted(absent_optional),
        "script_dependencies": {k: sorted(v) for k, v in sorted(script_deps.items())},
        "objects": objects,
        "declared_bytes": {"objects": total, "installer": installer["length"],
                           "unconditional_payload": sum(o["length"] for o in objects
                                                        if o["kind"] == "payload" and not o["conditional"])},
        "license_texts": lock_licenses,
        "trust": "Publisher SHA-1 sidecars over HTTPS; SHA-256 computed on first verified acquisition."
                 + (f" {reused} object digests reused from the previous lock by URL and length." if prev else ""),
        "metadata_retention": a.retention_base or None,
    }
    with open(a.out, "w") as f:
        json.dump(lock, f, indent=1)
        f.write("\n")
    print(json.dumps({"selected": len(selected), "conditional": sorted(conditional),
                      "objects": len(objects), "bytes": total, "problems": problems}, indent=1))


if __name__ == "__main__":
    main()
