#!/usr/bin/env python3
# Writes a release's update manifest, hikarisub-update.json, checks it against the files and GitHub, and
# uploads it. Same bytes on the main and legacy branches.
#
#   make-update-manifest.py --tag v0.0.2 --dir release [--offer-to legacy] [--upload]
#   make-update-manifest.py --tag v0.1.0-beta.1 --dir release \
#       --offer-channel prerelease --offer-pitch pitch.json [--upload]
#   make-update-manifest.py --tag v0.1.0-beta.1 --check-offer --offer-channel ... --offer-pitch ...
#   make-update-manifest.py --tag v0.1.0-beta.1 --verify hikarisub-update.json
#
# A HikariSub (0.1.0 and up) manifest names exactly ["qt"] in offer_to: HikariSub legacy 0.0.2-rc.1 and later
# install a release that names legacy without asking, so legacy users get the `offer` instead (ADR-0021).

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
# The offer's limits, in characters, as legacy reads them (ADR-0021).
OFFER_CHANNELS = ("prerelease", "stable")
LEGACY_SUPPORT = ("continues", "ended")
MAX_TITLE, MAX_HIGHLIGHTS, MAX_HIGHLIGHT, MAX_NOTE = 80, 5, 120, 200
LANGUAGE_TAG = re.compile(r"^[A-Za-z]{2,3}(?:[-_][A-Za-z0-9]{1,8})*$")


def asset_names(version):
    """The package kinds and their asset names."""
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


def check_offer_to(codebase, offer_to):
    if codebase == "qt" and offer_to != ["qt"]:
        fail(f"offer_to {offer_to} must be exactly ['qt']: HikariSub legacy builds install a HikariSub release that names legacy")
    if any(entry not in ("legacy", "qt") for entry in offer_to) or codebase not in offer_to:
        fail(f"offer_to {offer_to} must be drawn from legacy and qt and name {codebase}")


def check_text(where, value, limit):
    if not isinstance(value, str) or not 0 < len(value) <= limit:
        fail(f"{where} must be text of 1 to {limit} characters")


def check_pitch(pitch):
    if not isinstance(pitch, dict) or "en" not in pitch:
        fail("the pitch is an object keyed by language tag, with en")
    for language, entry in pitch.items():
        if not LANGUAGE_TAG.match(language):
            fail(f"{language!r} is not a language tag")
        if not isinstance(entry, dict):
            fail(f"pitch.{language} is not an object")
        unknown = set(entry) - {"title", "highlights", "note"}
        if unknown:
            fail(f"pitch.{language} has unknown members {sorted(unknown)}")
        check_text(f"pitch.{language}.title", entry.get("title"), MAX_TITLE)
        highlights = entry.get("highlights")
        if not isinstance(highlights, list) or not 0 < len(highlights) <= MAX_HIGHLIGHTS:
            fail(f"pitch.{language}.highlights must hold 1 to {MAX_HIGHLIGHTS} entries")
        for index, highlight in enumerate(highlights):
            check_text(f"pitch.{language}.highlights[{index}]", highlight, MAX_HIGHLIGHT)
        if "note" in entry:
            check_text(f"pitch.{language}.note", entry["note"], MAX_NOTE)


def check_offer(codebase, offer):
    if codebase != "qt":
        fail("only a HikariSub release (0.1.0 and up) carries an offer")
    if not isinstance(offer, dict) or set(offer) != {"to", "channel", "legacy_support", "pitch"}:
        fail("the offer has exactly to, channel, legacy_support and pitch")
    if offer["to"] != "legacy":
        fail(f"offer.to {offer['to']!r} must be legacy")
    if offer["channel"] not in OFFER_CHANNELS:
        fail(f"offer.channel {offer['channel']!r} must be one of {', '.join(OFFER_CHANNELS)}")
    if offer["legacy_support"] not in LEGACY_SUPPORT:
        fail(f"offer.legacy_support {offer['legacy_support']!r} must be one of {', '.join(LEGACY_SUPPORT)}")
    check_pitch(offer["pitch"])


def read_offer(codebase, channel, legacy_support, pitch_path):
    if not channel:
        if pitch_path:
            fail("--offer-pitch needs --offer-channel")
        return None
    if not pitch_path:
        fail("--offer-channel needs --offer-pitch")
    try:
        with open(pitch_path, encoding="utf-8") as file:
            pitch = json.load(file)
    except (OSError, ValueError) as error:
        fail(f"cannot read the pitch {pitch_path}: {error}")
    offer = {"to": "legacy", "channel": channel, "legacy_support": legacy_support, "pitch": pitch}
    check_offer(codebase, offer)
    return offer


def verify(path, version):
    try:
        with open(path, encoding="utf-8") as file:
            manifest = json.load(file)
    except (OSError, ValueError) as error:
        fail(f"cannot read {path}: {error}")
    if not isinstance(manifest, dict) or manifest.get("version") != version:
        fail(f"{path} is not the manifest of {version}")
    codebase = codebase_of(version)
    if manifest.get("codebase") != codebase:
        fail(f"{path} names codebase {manifest.get('codebase')!r}, not {codebase}")
    offer_to = manifest.get("offer_to")
    check_offer_to(codebase, offer_to if isinstance(offer_to, list) else [])
    if "offer" in manifest:
        check_offer(codebase, manifest["offer"])


def build_manifest(version, offer_to, offer, directory, assets, published):
    codebase = codebase_of(version)
    if not offer_to:
        offer_to = [codebase]
    check_offer_to(codebase, offer_to)
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
    manifest = {
        "schema": 1,
        "product": "HikariSub",
        "version": version,
        "codebase": codebase,
        "offer_to": offer_to,
        "published": published,
        "packages": packages,
    }
    if offer:
        manifest["offer"] = offer
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--dir", help="the release's files, under their asset names")
    parser.add_argument("--offer-to", default="", help="comma list from legacy and qt; default the codebase")
    parser.add_argument("--offer-channel", default="", help="the offer to legacy users: prerelease or stable")
    parser.add_argument("--offer-legacy-support", default="continues", help="continues or ended")
    parser.add_argument("--offer-pitch", default="", help="the offer's pitch, a JSON file")
    parser.add_argument("--check-offer", action="store_true", help="check the offer and print it; write nothing")
    parser.add_argument("--verify", metavar="MANIFEST", help="check a written manifest of the tag; write nothing")
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
    if args.verify:
        verify(args.verify, version)
        return
    offer = read_offer(codebase_of(version), args.offer_channel, args.offer_legacy_support, args.offer_pitch)
    if args.check_offer:
        print(json.dumps(offer, indent=2, ensure_ascii=False))
        return
    if not args.dir:
        fail("--dir is needed to write a manifest")

    if args.assets_json:
        with open(args.assets_json, encoding="utf-8") as file:
            release = json.load(file)
    else:
        release = json.loads(gh("release", "view", args.tag, "--repo", args.repo, "--json", "assets,publishedAt"))
    published = release.get("publishedAt") or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    manifest = build_manifest(version, offer_to, offer, args.dir, release.get("assets", []), published)

    text = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"
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
