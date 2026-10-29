#!/usr/bin/env python3
# Serves a GitHub-shaped release feed for test builds that read HIKARISUB_UPDATE_FEED.

import argparse
import hashlib
import http.server
import json
import os
import re
import shutil
import sys
from datetime import datetime, timezone

MANIFEST = "hikarisub-update.json"
MIN_OS = "10.0.17763"
MIN_GLIBC = {"legacy": "2.35", "qt": "2.34"}
VERSION = r"(\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?)"
KINDS = {
    "nsis": re.compile(rf"^HikariSub-{VERSION}-windows-x64-setup\.exe$"),
    "portable-windows": re.compile(rf"^HikariSub-{VERSION}-windows-x64\.zip$"),
    "appimage": re.compile(rf"^HikariSub-{VERSION}-linux-x86_64\.AppImage$"),
    "deb": re.compile(rf"^hikarisub-{VERSION}-linux-amd64\.deb$"),
    "rpm": re.compile(rf"^hikarisub-{VERSION}-linux-x86_64\.rpm$"),
    "portable-linux": re.compile(rf"^hikarisub-{VERSION}-linux-x86_64\.tar\.gz$"),
}


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def codebase_of(version):
    major, minor = (int(part) for part in version.split(".")[:2])
    return "legacy" if (major, minor) == (0, 0) else "qt"


def version_key(version):
    """SemVer precedence, as a sort key."""
    core, _, pre = version.partition("-")
    numbers = tuple(int(part) for part in core.split("."))
    if not pre:
        return numbers, (1,)
    fields = tuple((0, int(f), "") if f.isdigit() else (1, 0, f) for f in pre.split("."))
    return numbers, (0, fields)


def pairs(values, what):
    result = {}
    for value in values:
        key, sep, rest = value.partition("=")
        if not sep:
            sys.exit(f"make-feed: --{what} takes V=value, not {value}")
        result[key] = rest
    return result


def build(source, out, args):
    releases = {}
    for name in sorted(os.listdir(source)):
        for key, pattern in KINDS.items():
            match = pattern.match(name)
            if match:
                releases.setdefault(match.group(1), {})[key] = name
    if not releases:
        sys.exit(f"make-feed: no package with a release asset name is in {source}")

    offer_to = pairs(args.offer_to, "offer-to")
    if os.path.isdir(out):
        for entry in ("releases.json", "download", "releases"):
            path = os.path.join(out, entry)
            if os.path.isdir(path):
                shutil.rmtree(path)
            elif os.path.exists(path):
                os.remove(path)
    os.makedirs(out, exist_ok=True)

    listing = []
    published = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    for version in sorted(releases, key=version_key, reverse=True):
        codebase = codebase_of(version)
        folder = os.path.join(out, "download", f"v{version}")
        os.makedirs(folder)
        assets = []
        packages = {}
        for key, name in sorted(releases[version].items()):
            target = os.path.join(folder, name)
            shutil.copyfile(os.path.join(source, name), target)
            size = os.path.getsize(target)
            digest = sha256_of(target)
            assets.append({"name": name, "size": size, "digest": f"sha256:{digest}"})
            package = {"name": name, "size": size,
                       "sha256": "0" * 64 if name in args.bad_sha256 else digest}
            if key in ("nsis", "portable-windows"):
                package["min_os"] = MIN_OS
            else:
                package["min_glibc"] = MIN_GLIBC[codebase]
            packages[key] = package
        if version not in args.no_manifest:
            manifest = {
                "schema": 1,
                "product": "HikariSub",
                "version": version,
                "codebase": codebase,
                "offer_to": [entry for entry in offer_to.get(version, codebase).split(",") if entry],
                "published": published,
                "packages": packages,
            }
            path = os.path.join(folder, MANIFEST)
            with open(path, "w", encoding="utf-8") as file:
                file.write(json.dumps(manifest, indent=2) + "\n")
            assets.append({"name": MANIFEST, "size": os.path.getsize(path), "digest": f"sha256:{sha256_of(path)}"})
        tag = f"v{version}"
        listing.append({
            "tag_name": tag,
            "name": f"HikariSub {version}",
            "draft": version in args.draft,
            "prerelease": "-" in version,
            "body": f"Test release {version}, served by make-feed.py.",
            "html_url": f"releases/tag/{tag}",
            "published_at": published,
            "assets": assets,
        })
        page = os.path.join(out, "releases", "tag")
        os.makedirs(page, exist_ok=True)
        with open(os.path.join(page, tag), "w", encoding="utf-8") as file:
            file.write(f"<!doctype html><title>HikariSub {version}</title><h1>HikariSub {version}</h1>\n")
    with open(os.path.join(out, "releases.json"), "w", encoding="utf-8") as file:
        file.write(json.dumps(listing, indent=2) + "\n")
    for release in listing:
        print(f"{release['tag_name']}: {', '.join(asset['name'] for asset in release['assets'])}")


def serve(out, port):
    log_path = os.path.join(out, "requests.log")
    log = open(log_path, "a", encoding="utf-8", buffering=1)

    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *arguments, **keywords):
            super().__init__(*arguments, directory=out, **keywords)

        def guess_type(self, path):
            if path.endswith(".json"):
                return "application/json"
            if os.path.basename(os.path.dirname(path)) == "tag":
                return "text/html"
            return "application/octet-stream"

        def log_message(self, format, *arguments):
            line = f"{self.log_date_time_string()} {format % arguments}"
            log.write(line + "\n")
            print(line, file=sys.stderr, flush=True)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"serving {out} at http://127.0.0.1:{server.server_port}/ ; requests go to {log_path}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


def main():
    parser = argparse.ArgumentParser(description="Make and serve a local update feed for test builds.")
    parser.add_argument("source", help="the packages, under their release asset names")
    parser.add_argument("out", help="where to write the feed")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-serve", action="store_true", help="write the feed only")
    parser.add_argument("--no-manifest", action="append", default=[], metavar="V",
                        help="leave out the manifest of version V")
    parser.add_argument("--bad-sha256", action="append", default=[], metavar="NAME",
                        help="give asset NAME a wrong SHA-256 in its manifest")
    parser.add_argument("--offer-to", action="append", default=[], metavar="V=LIST",
                        help="the offer_to of version V, a comma list from legacy and qt")
    parser.add_argument("--draft", action="append", default=[], metavar="V", help="mark version V a draft")
    args = parser.parse_args()
    out = os.path.abspath(args.out)
    build(os.path.abspath(args.source), out, args)
    if not args.no_serve:
        serve(out, args.port)


if __name__ == "__main__":
    main()
