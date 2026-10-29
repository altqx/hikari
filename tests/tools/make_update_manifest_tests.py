#!/usr/bin/env python3
"""tools/release/make-update-manifest.py without GitHub: stand-in packages
and an asset list given with --assets-json.

  python3 -B tests/tools/make_update_manifest_tests.py

Checks the rewrite's offer_to (exactly ["qt"]), the offer member and its
pitch rules as legacy reads them (ADR-0021), --check-offer, --verify, and
every pitch under packaging/offer."""
import glob
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest

TREE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(TREE, "tools", "release", "make-update-manifest.py")
PITCH = {"en": {"title": "Try the new HikariSub", "highlights": ["One", "Two"], "note": "A beta."}}


def run(*arguments):
    return subprocess.run([sys.executable, "-I", SCRIPT, *arguments], capture_output=True, text=True)


class ManifestTest(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.dir = self.work.name

    def tearDown(self):
        self.work.cleanup()

    def path(self, name):
        return os.path.join(self.dir, name)

    def write_json(self, name, value):
        with open(self.path(name), "w", encoding="utf-8") as file:
            json.dump(value, file, ensure_ascii=False)
        return self.path(name)

    def release(self, version):
        """The AppImage of version in <dir>/release and the asset list naming it."""
        os.makedirs(self.path("release"), exist_ok=True)
        name = f"HikariSub-{version}-linux-x86_64.AppImage"
        data = f"stand-in {version}".encode()
        with open(os.path.join(self.path("release"), name), "wb") as file:
            file.write(data)
        asset = {"name": name, "size": len(data), "digest": "sha256:" + hashlib.sha256(data).hexdigest()}
        return self.write_json("assets.json", {"assets": [asset], "publishedAt": "2026-10-29T00:00:00Z"})

    def make(self, version, *arguments):
        assets = self.release(version)
        return run("--tag", f"v{version}", "--dir", self.path("release"), "--assets-json", assets,
                   "--output", self.path("out.json"), *arguments)

    def made(self, version, *arguments):
        result = self.make(version, *arguments)
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(self.path("out.json"), encoding="utf-8") as file:
            return json.load(file)

    def refused(self, result, text):
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn(text, result.stderr)

    def offer(self, pitch, *extra):
        return self.make("0.1.0-beta.1", "--offer-channel", "prerelease",
                         "--offer-pitch", self.write_json("pitch.json", pitch), *extra)

    def test_rewrite_manifest_names_qt_only_and_carries_the_offer(self):
        manifest = self.made("0.1.0-beta.1", "--offer-channel", "prerelease",
                             "--offer-pitch", self.write_json("pitch.json", PITCH))
        self.assertEqual(manifest["codebase"], "qt")
        self.assertEqual(manifest["offer_to"], ["qt"])
        self.assertEqual(manifest["offer"], {"to": "legacy", "channel": "prerelease",
                                             "legacy_support": "continues", "pitch": PITCH})
        self.assertNotIn("offer", self.made("0.1.0-beta.1"))

    def test_rewrite_offer_to_must_be_exactly_qt(self):
        for offer_to in ("qt,legacy", "legacy,qt", "legacy", "qt,qt"):
            self.refused(self.make("0.1.0-beta.1", "--offer-to", offer_to), "must be exactly ['qt']")
        self.assertEqual(self.made("0.1.0", "--offer-to", "qt")["offer_to"], ["qt"])

    def test_legacy_manifest_is_unchanged_and_carries_no_offer(self):
        self.assertEqual(self.made("0.0.3", "--offer-to", "legacy,qt")["offer_to"], ["legacy", "qt"])
        self.refused(self.make("0.0.3", "--offer-to", "qt"), "name legacy")
        self.refused(self.make("0.0.3", "--offer-channel", "stable",
                               "--offer-pitch", self.write_json("pitch.json", PITCH)), "only a rewrite release")

    def test_channel_and_legacy_support(self):
        manifest = self.made("0.1.0", "--offer-channel", "stable", "--offer-legacy-support", "ended",
                             "--offer-pitch", self.write_json("pitch.json", PITCH))
        self.assertEqual((manifest["offer"]["channel"], manifest["offer"]["legacy_support"]), ("stable", "ended"))
        self.refused(self.make("0.1.0", "--offer-channel", "beta", "--offer-pitch", self.path("pitch.json")),
                     "offer.channel")
        self.refused(self.make("0.1.0", "--offer-channel", "stable", "--offer-legacy-support", "over",
                               "--offer-pitch", self.path("pitch.json")), "offer.legacy_support")
        self.refused(self.make("0.1.0", "--offer-channel", "stable"), "needs --offer-pitch")
        self.refused(self.make("0.1.0", "--offer-pitch", self.path("pitch.json")), "needs --offer-channel")
        self.refused(self.make("0.1.0", "--offer-channel", "stable", "--offer-pitch", self.path("none.json")),
                     "cannot read the pitch")

    def test_pitch_limits_count_characters(self):
        def pitch(**entry):
            return {"en": {"title": "Title", "highlights": ["One"], **entry}}
        accepted = [
            pitch(title="ł" * 80, highlights=["ż" * 120] * 5, note="é" * 200),
            {**pitch(), "pt-BR": {"title": "Título", "highlights": ["Um"]}, "zh_Hant": pitch()["en"]},
        ]
        for value in accepted:
            result = self.offer(value)
            self.assertEqual(result.returncode, 0, f"{value}: {result.stderr}")
        refused = [
            ({"pl": pitch()["en"]}, "with en"),
            ([], "with en"),
            (pitch(title="ł" * 81), "title must be text of 1 to 80"),
            (pitch(title=""), "title must be text"),
            (pitch(title=None), "title must be text"),
            (pitch(highlights=[]), "1 to 5 entries"),
            (pitch(highlights=["x"] * 6), "1 to 5 entries"),
            (pitch(highlights="One"), "1 to 5 entries"),
            (pitch(highlights=["ż" * 121]), "highlights[0] must be text of 1 to 120"),
            (pitch(highlights=["One", 2]), "highlights[1] must be text"),
            (pitch(note="é" * 201), "note must be text of 1 to 200"),
            (pitch(note=""), "note must be text"),
            (pitch(highlight=["typo"]), "unknown members ['highlight']"),
            ({**pitch(), "english": pitch()["en"]}, "is not a language tag"),
            ({**pitch(), "de": "Hallo"}, "pitch.de is not an object"),
        ]
        for value, text in refused:
            with self.subTest(pitch=value):
                self.refused(self.offer(value), text)

    def test_check_offer_needs_no_packages(self):
        pitch = self.write_json("pitch.json", PITCH)
        result = run("--tag", "v0.1.0-beta.1", "--check-offer", "--offer-channel", "prerelease", "--offer-pitch", pitch)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["pitch"], PITCH)
        self.refused(run("--tag", "v0.1.0-beta.1", "--check-offer", "--offer-channel", "prerelease",
                         "--offer-pitch", self.write_json("bad.json", {"en": {"title": "x"}})), "1 to 5 entries")
        self.refused(run("--tag", "v0.1.0-beta.1"), "--dir is needed")

    def test_verify_refuses_a_manifest_not_offered_to_qt_only(self):
        manifest = self.made("0.1.0-beta.1", "--offer-channel", "prerelease",
                             "--offer-pitch", self.write_json("pitch.json", PITCH))
        good = self.write_json("good.json", manifest)
        self.assertEqual(run("--tag", "v0.1.0-beta.1", "--verify", good).returncode, 0)
        cases = [
            ({"offer_to": ["qt", "legacy"]}, "must be exactly ['qt']"),
            ({"offer_to": ["legacy"]}, "must be exactly ['qt']"),
            ({"offer_to": "qt"}, "must be exactly ['qt']"),
            ({"codebase": "legacy"}, "names codebase 'legacy'"),
            ({"version": "0.1.0-beta.2"}, "is not the manifest of 0.1.0-beta.1"),
            ({"offer": {**manifest["offer"], "to": "qt"}}, "offer.to"),
        ]
        for change, text in cases:
            with self.subTest(change=change):
                self.refused(run("--tag", "v0.1.0-beta.1", "--verify", self.write_json("bad.json", {**manifest, **change})),
                             text)
        self.refused(run("--tag", "v0.1.0-beta.1", "--verify", self.path("none.json")), "cannot read")

    def test_every_pitch_in_the_tree_is_valid(self):
        for path in glob.glob(os.path.join(TREE, "packaging", "offer", "*.json")):
            version = os.path.basename(path)[:-len(".json")]
            with self.subTest(pitch=path):
                result = run("--tag", f"v{version}", "--check-offer", "--offer-channel", "prerelease",
                             "--offer-pitch", path)
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
