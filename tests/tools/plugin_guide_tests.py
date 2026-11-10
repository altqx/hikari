import json
import pathlib
import re
import unittest

DOCS = pathlib.Path(__file__).resolve().parents[2] / "docs"
GUIDE = DOCS / "plugin-guide.md"
EXAMPLE = DOCS / "examples" / "first-plugin"
FENCE = re.compile(r"^( *)```(\S*)(?: (\S+))?\n(.*?)^\1```$", re.M | re.S)
LINK = re.compile(r"\]\(([^)\s]+)\)")


def blocks():
    for match in FENCE.finditer(GUIDE.read_text(encoding="utf-8")):
        indent, language, path, body = match.groups()
        lines = [line[len(indent):] if line.startswith(indent) else line for line in body.splitlines()]
        yield language, path, "\n".join(lines) + "\n"


def slug(heading):
    text = re.sub(r"[^\w\- ]", "", heading.strip().lower())
    return text.replace(" ", "-")


def anchors(markdown):
    return {slug(line.lstrip("#")) for line in markdown.splitlines() if line.startswith("#")}


class PluginGuide(unittest.TestCase):
    def test_lua_and_json_blocks_name_an_example_file_and_are_part_of_it(self):
        checked = 0
        for language, path, body in blocks():
            if language not in ("lua", "json"):
                continue
            self.assertIsNotNone(path, f"a {language} block in the guide names no example file:\n{body}")
            source = EXAMPLE / path
            self.assertTrue(source.is_file(), f"{path} isn't in docs/examples/first-plugin")
            self.assertIn(body, source.read_text(encoding="utf-8"), f"the guide's block isn't part of {path}:\n{body}")
            checked += 1
        self.assertGreaterEqual(checked, 10)

    def test_every_step_is_a_whole_plugin(self):
        steps = sorted(p.name for p in EXAMPLE.iterdir() if p.is_dir())
        self.assertEqual(steps, ["step-1", "step-2", "step-3"])
        for number, step in enumerate(steps, start=1):
            manifest = json.loads((EXAMPLE / step / "hikari-plugin.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["version"], f"0.{number}.0")
            self.assertTrue((EXAMPLE / step / manifest["main"]).is_file())

    def test_links_reach_their_files_and_headings(self):
        text = GUIDE.read_text(encoding="utf-8")
        for target in LINK.findall(text):
            if re.match(r"[a-z]+:", target):
                continue
            path, _, anchor = target.partition("#")
            file = (GUIDE.parent / path) if path else GUIDE
            self.assertTrue(file.exists(), f"{target}: {file} is missing")
            if anchor:
                self.assertIn(anchor, anchors(file.read_text(encoding="utf-8")), f"{target}: no such heading")


if __name__ == "__main__":
    unittest.main()
