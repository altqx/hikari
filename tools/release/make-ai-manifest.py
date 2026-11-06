#!/usr/bin/env python3
"""Writes the AI channel's hikari-ai-update.json (ADR 0031): this build first, then the releases the channel already
lists, with the runtimes they use and the versions src/ai-sidecar/channel.json blocks.

  make-ai-manifest.py --build <dir> --channel <channel.json> [--previous <hikari-ai-update.json>] --out <file>
      --uploads <file>

<dir> holds build-ai-sidecar.sh's build.json, bundle and runtimes. --uploads gets the asset names to upload: the bundle,
and each runtime the channel doesn't have yet. A runtime the channel has keeps its published entry.
"""
import argparse
import hashlib
import json
import os
import re
import sys

PLATFORMS = ("linux-x64", "windows-x64")
VERSION = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$")
RUNTIME = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+-[0-9a-f]{8}$")


def fail(message):
    print(f"::error::{message}", file=sys.stderr)
    sys.exit(1)


def semver(version):
    core, _, pre = version.partition("-")
    numbers = tuple(int(part) for part in core.split("."))
    tail = tuple((0, int(p), "") if p.isdigit() else (1, 0, p) for p in pre.split(".")) if pre else None
    return numbers, tail is None, tail or ()


def asset(directory, name):
    path = os.path.join(directory, name)
    if not os.path.isfile(path):
        fail(f"{name} is missing from {directory}")
    with open(path, "rb") as file:
        data = file.read()
    return {"name": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True)
    parser.add_argument("--channel", required=True)
    parser.add_argument("--previous")
    parser.add_argument("--out", required=True)
    parser.add_argument("--uploads", required=True)
    args = parser.parse_args()

    with open(os.path.join(args.build, "build.json"), encoding="utf-8") as file:
        build = json.load(file)
    with open(args.channel, encoding="utf-8") as file:
        channel = json.load(file)
    version, runtime = build["version"], build["runtime"]
    if not VERSION.match(version) or not RUNTIME.match(runtime):
        fail(f"build.json names version {version!r} and runtime {runtime!r}")
    blocked = channel.get("blocked", [])
    if not all(isinstance(v, str) and VERSION.match(v) for v in blocked):
        fail(f"{args.channel}: blocked must list versions")
    keep = channel.get("keep", 10)

    previous = {"runtimes": {}, "releases": []}
    if args.previous:
        with open(args.previous, encoding="utf-8") as file:
            previous = json.load(file)
        if previous.get("schema") != 1:
            fail("the published manifest isn't schema 1")
        newest = previous["releases"][0]["version"]
        if semver(version) <= semver(newest):
            fail(f"ai-v{version} is not newer than the published ai-v{newest}")

    uploads = [f"hikari-ai-{version}.zip"]
    runtimes = {}
    for platform in PLATFORMS:
        name = f"hikari-ai-runtime-{runtime}-{platform}.gz"
        published = previous["runtimes"].get(runtime, {}).get(platform)
        if published:
            runtimes.setdefault(runtime, {})[platform] = published
        else:
            runtimes.setdefault(runtime, {})[platform] = asset(args.build, name)
            uploads.append(name)
    release = {
        "version": version,
        "protocol": build["protocol"],
        "required_api": build["requiredApiVersion"],
        "runtime": runtime,
        "bundle": asset(args.build, f"hikari-ai-{version}.zip"),
        "packages": {"bun": build["bun"], **build["packages"]},
    }
    releases = [release] + previous["releases"][: keep - 1]
    for entry in releases[1:]:
        runtimes.setdefault(entry["runtime"], previous["runtimes"][entry["runtime"]])

    manifest = {"schema": 1, "version": version, "runtimes": runtimes, "releases": releases, "blocked": blocked}
    with open(args.out, "w", encoding="utf-8") as file:
        json.dump(manifest, file, indent=2)
        file.write("\n")
    with open(args.uploads, "w", encoding="utf-8") as file:
        file.write("".join(f"{name}\n" for name in uploads))


if __name__ == "__main__":
    main()
