#!/usr/bin/env python3
# Writes a release's update manifest, hikarisub-update.json, checks it
# against the files and against GitHub, and uploads it (installers spec,
# 2.3). The same file on the legacy and Qt branches.
#
#   make-update-manifest.py --tag v0.0.2 --dir release [--offer-to legacy] [--upload]
#
# --dir holds the release's files under their asset names (2.1). Each
# package's size and SHA-256 come from the file there, and the SHA-256 must
# equal the digest GitHub gives the uploaded asset. --assets-json reads the
# asset list from a file (the JSON of `gh release view --json assets`)
# rather than from gh, for tests.

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from datetime import datetime, timezone

REPO_DEFAULT = "altqx/hikari"
MANIFEST = "hikarisub-update.json"
MAX_MANIFEST = 64 * 1024
MAX_PACKAGE = 1 << 30
ASSET_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
SEMVER = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-((?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*))*))?$")
MIN_OS = "10.0.17763"
MIN_GLIBC = {"legacy": "2.35", "qt": "2.34"}


def asset_names(version):
    """The package kinds and their asset names (2.1)."""
    return {
        "nsis": f"HikariSub-{version}-windows-x64-setup.exe",
        "portable-windows": f"HikariSub-{version}-windows-x64.zip",
        "appimage": f"HikariSub-{version}-linux-x86_64.AppImage",
        "deb": f"hikarisub-{version}-linux-amd64.deb",
        "rpm": f"hikarisub-{version}-linux-x86_64.rpm",
        "portable-linux": f"hikarisub-{version}-linux-x86_64.tar.gz",
    }


def codebase_of(version):
    major, minor = (int(part) for part in version.split(".")[:2])
    return "legacy" if (major, minor) == (0, 0) else "qt"


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def fail(message):
    print(f"make-update-manifest: {message}", file=sys.stderr)
    sys.exit(1)


def gh(*arguments):
    return subprocess.run(["gh", *arguments], check=True, capture_output=True, text=True).stdout


def build_manifest(version, offer_to, directory, assets, published):
    codebase = codebase_of(version)
    if not offer_to:
        offer_to = [codebase]
    if any(entry not in ("legacy", "qt") for entry in offer_to) or codebase not in offer_to:
        fail(f"offer_to {offer_to} must be drawn from legacy and qt and name {codebase}")
    by_name = {asset["name"]: asset for asset in assets}
    packages = {}
    for key, name in asset_names(version).items():
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            continue
        if not ASSET_NAME.match(name):
            fail(f"{name} is not a valid asset name")
        size = os.path.getsize(path)
        if not 0 < size <= MAX_PACKAGE:
            fail(f"{name} is {size} bytes; a package is 1 byte to 1 GiB")
        digest = sha256_of(path)
        asset = by_name.get(name)
        if asset is None:
            fail(f"{name} is not on the release")
        if asset.get("size") != size:
            fail(f"{name} is {asset.get('size')} bytes on GitHub and {size} here")
        github = (asset.get("digest") or "").lower()
        if github != f"sha256:{digest}":
            fail(f"{name}: GitHub's digest {github or '(none)'} differs from sha256:{digest}")
        package = {"name": name, "size": size, "sha256": digest}
        if key in ("nsis", "portable-windows"):
            package["min_os"] = MIN_OS
        else:
            package["min_glibc"] = MIN_GLIBC[codebase]
        packages[key] = package
    if not packages:
        fail(f"no package of {version} is in {directory}")
    return {
        "schema": 1,
        "product": "HikariSub",
        "version": version,
        "codebase": codebase,
        "offer_to": offer_to,
        "published": published,
        "packages": packages,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--dir", required=True, help="the release's files, under their asset names")
    parser.add_argument("--offer-to", default="", help="comma list from legacy and qt; default the codebase")
    parser.add_argument("--repo", default=os.environ.get("GH_REPO", REPO_DEFAULT))
    parser.add_argument("--assets-json", help="the release's assets as `gh release view --json assets` gives them")
    parser.add_argument("--output", help="where to write the manifest; default <dir>/" + MANIFEST)
    parser.add_argument("--upload", action="store_true", help="upload it to the release, and read it back")
    args = parser.parse_args()

    if not args.tag.startswith("v"):
        fail("the tag starts with v")
    version = args.tag[1:]
    if "+" in version or not SEMVER.match(version):
        fail(f"{version} is not a release version (SemVer without build metadata)")
    offer_to = [entry for entry in args.offer_to.split(",") if entry]

    if args.assets_json:
        with open(args.assets_json, encoding="utf-8") as file:
            release = json.load(file)
    else:
        release = json.loads(gh("release", "view", args.tag, "--repo", args.repo, "--json", "assets,publishedAt"))
    published = release.get("publishedAt") or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    manifest = build_manifest(version, offer_to, args.dir, release.get("assets", []), published)

    text = json.dumps(manifest, indent=2) + "\n"
    if len(text.encode()) > MAX_MANIFEST:
        fail("the manifest is over 64 KiB")
    output = args.output or os.path.join(args.dir, MANIFEST)
    if os.path.basename(output) != MANIFEST:
        output_dir = tempfile.mkdtemp()
        upload_path = os.path.join(output_dir, MANIFEST)
    else:
        upload_path = output
    with open(output, "w", encoding="utf-8") as file:
        file.write(text)
    if upload_path != output:
        with open(upload_path, "w", encoding="utf-8") as file:
            file.write(text)
    print(text, end="")

    if args.upload:
        gh("release", "upload", args.tag, upload_path, "--repo", args.repo, "--clobber")
        uploaded = json.loads(gh("release", "view", args.tag, "--repo", args.repo, "--json", "assets"))
        found = [asset for asset in uploaded["assets"] if asset["name"] == MANIFEST]
        if not found or (found[0].get("digest") or "").lower() != f"sha256:{sha256_of(upload_path)}":
            fail("the uploaded manifest does not read back as written")


if __name__ == "__main__":
    main()
