#!/usr/bin/env python3
"""Tests tools/release/make-ai-manifest.py with stand-in assets.

  python3 -B tests/tools/make_ai_manifest_tests.py
"""
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest

TREE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(TREE, "tools", "release", "make-ai-manifest.py")
RUNTIME = "1.4.4-0123abcd"


class AiManifestTest(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.dir = self.work.name
        self.channel = self.write("channel.json", {"keep": 3, "blocked": []})

    def tearDown(self):
        self.work.cleanup()

    def path(self, name):
        return os.path.join(self.dir, name)

    def write(self, name, data):
        with open(self.path(name), "w", encoding="utf-8") as file:
            json.dump(data, file)
        return self.path(name)

    def build(self, version, runtime=RUNTIME):
        folder = self.path(f"build-{version}")
        os.makedirs(folder, exist_ok=True)
        names = [f"hikari-ai-{version}.zip"] + [f"hikari-ai-runtime-{runtime}-{p}.gz" for p in ("linux-x64", "windows-x64")]
        for name in names:
            with open(os.path.join(folder, name), "wb") as file:
                file.write(f"stand-in {name} {version}".encode())
        with open(os.path.join(folder, "build.json"), "w", encoding="utf-8") as file:
            json.dump({"version": version, "protocol": 1, "requiredApiVersion": 1, "hikariApiVersion": 1,
                       "runtime": runtime, "bun": runtime.split("-")[0], "packages": {"openai": "7.45.0"}}, file)
        return folder

    def make(self, version, previous=None, runtime=RUNTIME):
        arguments = ["--build", self.build(version, runtime), "--channel", self.channel, "--out", self.path("out.json"),
                     "--uploads", self.path("uploads.txt")]
        if previous:
            arguments += ["--previous", self.write("previous.json", previous)]
        return subprocess.run([sys.executable, "-I", SCRIPT, *arguments], capture_output=True, text=True)

    def made(self, version, previous=None, runtime=RUNTIME):
        result = self.make(version, previous, runtime)
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(self.path("out.json"), encoding="utf-8") as file, open(self.path("uploads.txt")) as uploads:
            return json.load(file), uploads.read().split()

    def test_the_first_release_lists_its_bundle_and_both_runtimes(self):
        manifest, uploads = self.made("1.0.0")
        self.assertEqual(manifest["schema"], 1)
        self.assertEqual(manifest["version"], "1.0.0")
        release = manifest["releases"][0]
        self.assertEqual(release["required_api"], 1)
        self.assertEqual(release["packages"], {"bun": "1.4.4", "openai": "7.45.0"})
        data = b"stand-in hikari-ai-1.0.0.zip 1.0.0"
        self.assertEqual(release["bundle"], {"name": "hikari-ai-1.0.0.zip", "size": len(data),
                                             "sha256": hashlib.sha256(data).hexdigest()})
        self.assertEqual(sorted(manifest["runtimes"][RUNTIME]), ["linux-x64", "windows-x64"])
        self.assertEqual(uploads, ["hikari-ai-1.0.0.zip", f"hikari-ai-runtime-{RUNTIME}-linux-x64.gz",
                                   f"hikari-ai-runtime-{RUNTIME}-windows-x64.gz"])

    def test_a_published_runtime_keeps_its_entry_and_is_not_uploaded_again(self):
        first, _ = self.made("1.0.0")
        second, uploads = self.made("1.1.0", first)
        self.assertEqual([r["version"] for r in second["releases"]], ["1.1.0", "1.0.0"])
        self.assertEqual(second["runtimes"][RUNTIME], first["runtimes"][RUNTIME])
        self.assertEqual(uploads, ["hikari-ai-1.1.0.zip"])

    def test_keeps_the_newest_releases_and_only_the_runtimes_they_use(self):
        manifest, _ = self.made("1.0.0", runtime="1.4.3-00000000")
        for version in ("1.1.0", "1.2.0", "1.3.0"):
            manifest, _ = self.made(version, manifest)
        self.assertEqual([r["version"] for r in manifest["releases"]], ["1.3.0", "1.2.0", "1.1.0"])
        self.assertEqual(list(manifest["runtimes"]), [RUNTIME])

    def test_an_older_or_equal_version_is_refused(self):
        first, _ = self.made("1.2.0")
        for version in ("1.2.0", "1.1.0"):
            result = self.make(version, first)
            self.assertEqual(result.returncode, 1)
            self.assertIn("not newer", result.stderr)

    def test_the_channel_file_blocks_versions(self):
        self.write("channel.json", {"keep": 3, "blocked": ["1.0.0"]})
        first, _ = self.made("1.0.0")
        self.assertEqual(first["blocked"], ["1.0.0"])
        self.write("channel.json", {"blocked": ["not a version"]})
        self.assertEqual(self.make("1.1.0", first).returncode, 1)

    def test_the_channel_file_names_agent_cli_limits(self):
        limits = {"claude": {"minimum": "2.0.0", "broken": ["2.0.9"]}, "codex": {"minimum": "0.40.0"}}
        self.write("channel.json", {"agents": limits})
        first, _ = self.made("1.0.0")
        self.assertEqual(first["agents"], limits)
        for bad in ({"gemini": {}}, {"claude": {"minimum": "soon"}}, {"codex": {"broken": "0.41.0"}}, []):
            self.write("channel.json", {"agents": bad})
            self.assertEqual(self.make("1.1.0", first).returncode, 1, bad)


if __name__ == "__main__":
    unittest.main()
