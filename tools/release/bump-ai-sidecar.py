#!/usr/bin/env python3
"""Bumps the sidecar's Claude Agent SDK, provider SDKs and Bun to their newest stable versions published at least
7 days ago (ADR 0031). It changes nothing unless the Claude Agent SDK has a newer version: Bun and the provider SDKs
only ride along. Prints `changed=true|false` and, when changed, a `title=` line for the PR.

  bump-ai-sidecar.py <tree>
"""
import datetime
import hashlib
import io
import json
import os
import re
import sys
import urllib.parse
import urllib.request
import zipfile

AGENT_SDK = "@anthropic-ai/claude-agent-sdk"
COOLDOWN = datetime.timedelta(days=7)
STABLE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
BUN_PLATFORMS = {"linux-x64": "bun-linux-x64-baseline/bun", "windows-x64": "bun-windows-x64-baseline/bun.exe"}


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "hikarisub-ai-bump"})
    token = os.environ.get("GH_TOKEN")
    if token and url.startswith("https://api.github.com/"):
        request.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def key(version):
    return tuple(int(part) for part in version.split("."))


def published(stamp):
    return datetime.datetime.fromisoformat(stamp.replace("Z", "+00:00"))


def newest_npm(package, now):
    times = json.loads(fetch(f"https://registry.npmjs.org/{urllib.parse.quote(package, safe='@')}"))["time"]
    ready = [v for v, stamp in times.items() if STABLE.match(v) and now - published(stamp) >= COOLDOWN]
    return max(ready, key=key) if ready else None


def newest_bun(now):
    releases = json.loads(fetch("https://api.github.com/repos/oven-sh/bun/releases?per_page=30"))
    ready = [r["tag_name"].removeprefix("bun-v") for r in releases
             if not r["draft"] and not r["prerelease"] and STABLE.match(r["tag_name"].removeprefix("bun-v"))
             and now - published(r["published_at"]) >= COOLDOWN]
    return max(ready, key=key) if ready else None


def bump_bun(cmake, version):
    text = open(cmake, encoding="utf-8").read()
    text = re.sub(r'set\(HIKARI_BUN_VERSION "[^"]*"\)', f'set(HIKARI_BUN_VERSION "{version}")', text)
    for platform, member in BUN_PLATFORMS.items():
        archive = fetch(f"https://github.com/oven-sh/bun/releases/download/bun-v{version}/{member.split('/')[0]}.zip")
        binary = zipfile.ZipFile(io.BytesIO(archive)).read(member)
        for name, data in ((f"HIKARI_BUN_{platform}_ARCHIVE_SHA256", archive), (f"HIKARI_BUN_{platform}_SHA256", binary)):
            text = re.sub(rf'set\({name} "[0-9a-f]*"\)', f'set({name} "{hashlib.sha256(data).hexdigest()}")', text)
    open(cmake, "w", encoding="utf-8").write(text)


def main():
    tree = sys.argv[1]
    package_file = os.path.join(tree, "src", "ai-sidecar", "package.json")
    package = json.load(open(package_file, encoding="utf-8"))
    dependencies = package["dependencies"]
    if AGENT_SDK not in dependencies:
        print(f"::notice::{AGENT_SDK} isn't a dependency yet (AI15), so nothing is bumped.", file=sys.stderr)
        print("changed=false")
        return
    now = datetime.datetime.now(datetime.timezone.utc)
    agent = newest_npm(AGENT_SDK, now)
    if not agent or key(agent) <= key(dependencies[AGENT_SDK]):
        print("changed=false")
        return
    changes = [f"{AGENT_SDK} {agent}"]
    for name, current in dependencies.items():
        newest = agent if name == AGENT_SDK else newest_npm(name, now)
        if newest and key(newest) > key(current):
            dependencies[name] = newest
            if name != AGENT_SDK:
                changes.append(f"{name} {newest}")
    cmake = os.path.join(tree, "cmake", "HikariBun.cmake")
    current_bun = re.search(r'set\(HIKARI_BUN_VERSION "([^"]*)"\)', open(cmake, encoding="utf-8").read()).group(1)
    bun = newest_bun(now)
    if bun and key(bun) > key(current_bun):
        bump_bun(cmake, bun)
        package["devDependencies"]["@types/bun"] = bun
        changes.append(f"Bun {bun}")
    with open(package_file, "w", encoding="utf-8") as file:
        json.dump(package, file, indent=2)
        file.write("\n")
    print("changed=true")
    print(f"title=AI sidecar: {', '.join(changes)}")


if __name__ == "__main__":
    main()
