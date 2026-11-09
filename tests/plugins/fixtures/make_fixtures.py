#!/usr/bin/env python3
import hashlib
import importlib.util
import io
import json
import pathlib
import struct
import zipfile
import zlib

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("minisign_fixtures", HERE.parent.parent / "minisign/fixtures/make_fixtures.py")
minisign = importlib.util.module_from_spec(spec)
spec.loader.exec_module(minisign)


def public_key(name):
    return minisign.public_file(name).splitlines()[1]


def manifest(version, key=None, **extra):
    m = {"id": "com.example.hello", "name": "Hello", "version": version, "api": 1, "main": "main.lua",
         "authors": ["Ana"], "description": "Says hello"}
    if key:
        m["publicKey"] = public_key(key)
    m.update(extra)
    return json.dumps(m, indent=2) + "\n"


def package(files):
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in files.items():
            info = zipfile.ZipInfo(name, date_time=(2026, 11, 2, 0, 0, 0))
            info.external_attr = 0o644 << 16
            z.writestr(info, data, zipfile.ZIP_DEFLATED)
    return out.getvalue()


def hello(version, key=None, **extra):
    return package({"hikari-plugin.json": manifest(version, key, **extra), "main.lua": f"return '{version}'\n"})


def board(edit_manifest=None, files=None):
    folder = HERE / "qml-board"
    contents = {p.relative_to(folder).as_posix(): p.read_text() for p in sorted(folder.rglob("*")) if p.is_file()}
    if edit_manifest:
        m = json.loads(contents["hikari-plugin.json"])
        edit_manifest(m)
        contents["hikari-plugin.json"] = json.dumps(m, indent=2) + "\n"
    if files:
        files(contents)
    return package(contents)


def png(width, height):
    return b"\x89PNG\r\n\x1a\n" + struct.pack(">I4sII5B", 13, b"IHDR", width, height, 8, 6, 0, 0, 0)


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def picture(width, height, background, rects):
    rows = []
    for y in range(height):
        row = bytearray(bytes(background) * width)
        for x0, y0, x1, y1, colour in rects:
            if y0 <= y < y1:
                row[x0 * 3:x1 * 3] = bytes(colour) * (x1 - x0)
        rows.append(b"\0" + bytes(row))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) +
            chunk(b"IEND", b""))


def icon(colour, glyph):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">'
            f'<rect width="64" height="64" rx="14" fill="{colour}"/>'
            f'<g fill="none" stroke="#fff" stroke-width="5" stroke-linecap="round" stroke-linejoin="round">{glyph}</g></svg>\n')


def media():
    folder = HERE / "marketplace/m"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "hello.svg").write_text(icon("#2f6fde", '<path d="M14 18h36v22H30l-10 8v-8h-6z"/>'))
    (folder / "native.svg").write_text(icon("#7a4fd6", '<circle cx="32" cy="32" r="14"/><path d="M32 10v10M32 44v10M10 32h10M44 32h10"/>'))
    (folder / "glossary.svg").write_text(icon("#1f9d74", '<path d="M16 14h22a8 8 0 0 1 8 8v28H24a8 8 0 0 1-8-8z"/><path d="M24 26h14M24 34h10"/>'))
    panel, line, accent, text = (36, 38, 44), (58, 61, 70), (47, 111, 222), (150, 154, 166)
    rects = [(0, 0, 1280, 40, (28, 30, 34)), (24, 64, 1256, 104, panel), (40, 78, 360, 90, text),
             (24, 120, 1256, 696, panel), (48, 144, 300, 160, accent)]
    for i in range(10):
        y = 184 + i * 48
        rects += [(48, y, 1232, y + 36, line), (64, y + 12, 240, y + 24, text), (272, y + 12, 272 + 60 * (i % 5 + 4), y + 24, text)]
    (folder / "glossary-1.png").write_bytes(picture(1280, 720, (24, 25, 28), rects))


def write(name, data, signer=None, signed=None):
    (HERE / name).write_bytes(data)
    if signer:
        signature = minisign.signature_file(signed or data, signer, comment=f"timestamp:0 file:{name}")
        (HERE / (name + ".minisig")).write_text(signature)


def main():
    v1 = hello("1.0.0", "a")
    write("hello-1.0.0.hikariplugin", v1, "a")
    write("hello-1.1.0.hikariplugin", hello("1.1.0", "a"), "a")
    write("hello-2.0.0-other-key.hikariplugin", hello("2.0.0", "b"), "b")
    write("hello-1.2.0-unsigned.hikariplugin", hello("1.2.0"))
    write("hello-tampered.hikariplugin", hello("1.0.0", "a", description="Changed after signing"), "a", signed=v1)
    write("needs-api-99.hikariplugin", hello("1.0.0", api=99))
    write("bad-schema.hikariplugin", hello("1.0.0", id="Not An Id"))
    write("bad-icon.hikariplugin", package({"hikari-plugin.json": manifest("1.0.0", icon="icon.png"),
                                            "main.lua": "return '1.0.0'\n", "icon.png": png(128, 128)}))
    write("not-a-zip.hikariplugin", b"hikari-plugin.json is not inside a zip\n")
    write("hello-1.3.0-network.hikariplugin",
          hello("1.3.0", "a", permissions={
              "network": {"hosts": ["api.example.com", "cdn.example.com"], "reason": "Looks words up in an online dictionary"},
              "files": [{"id": "glossaries", "title": "Glossary folder", "writable": False, "reason": "Reads your glossaries"}]}),
          "a")
    native = hello("1.0.0", "a", id="com.example.native", name="Native Tools", tier="unrestricted",
                   description="Runs a native tracker")
    write("native-1.0.0-unrestricted.hikariplugin", native)
    write("board-1.0.0-qml.hikariplugin", board())
    write("board-restricted-qml.hikariplugin", board(lambda m: m.pop("tier")))
    write("board-ui-import.hikariplugin",
          board(files=lambda f: f.update({"qml/parts/Badge.qml": f["qml/parts/Badge.qml"].replace("import QtQuick\n", "import QtQuick\nimport Hikari.Ui\n", 1)})))
    marketplace(v1, hello("1.1.0", "a"), native)


MARKET = "https://marketplace.test/"


def listed(version, package, **extra):
    v = {"version": version, "api": 1, "tier": "restricted", "name": "Hello", "summary": "Says hello",
         "license": "MIT", "authors": ["Ana"], "permissions": {},
         "url": f"{MARKET}p/{extra.pop('id', 'com.example.hello')}/{version}.hikariplugin",
         "sha256": hashlib.sha256(package).hexdigest(), "size": len(package), "published": "2026-11-01T10:00:00Z",
         "readme": f"{MARKET}readme.md"}
    v.update(extra)
    return v


def plugin(id, categories, versions):
    return {"id": id, "author": "ana", "authorName": "Ana Fansubs", "repo": f"https://github.com/ana/{id.split('.')[-1]}",
            "publicKey": public_key("a"), "categories": categories, "versions": versions}


def marketplace(hello_1_0, hello_1_1, native):
    media()
    shot = {"url": f"{MARKET}m/glossary-1.png", "caption": "Definitions beside the grid", "width": 1280, "height": 720}
    index = {"format": 1, "serial": 1, "generated": "2026-11-02T10:00:00Z", "apiVersions": {"1": "0.1.0"},
             "featured": ["com.example.hello"], "plugins": [
                 plugin("com.example.hello", ["Timing"], [
                     listed("1.1.0", hello_1_1, description="Says hello in the log, now in more languages.",
                            changelog="Greets in Polish and Korean.", icon=f"{MARKET}m/hello.svg"),
                     listed("1.0.0", hello_1_0, icon=f"{MARKET}m/hello.svg")]),
                 plugin("com.example.native", ["Video"], [
                     listed("1.0.0", native, id="com.example.native", tier="unrestricted", name="Native Tools",
                            summary="Tracks signs with a native library", icon=f"{MARKET}m/native.svg")]),
                 plugin("com.example.glossary", ["Translation"], [
                     listed("2.0.0", hello_1_0, id="com.example.glossary", name="Glossary",
                            summary="Looks words up in an online dictionary", icon=f"{MARKET}m/glossary.svg",
                            screenshots=[shot],
                            permissions={"network": {"hosts": ["api.example.com"], "reason": "Looks words up"}})])]}
    data = (json.dumps(index, indent=2, ensure_ascii=False) + "\n").encode()
    (HERE / "marketplace").mkdir(exist_ok=True)
    (HERE / "marketplace/index.json").write_bytes(data)
    (HERE / "marketplace/index.json.minisig").write_text(
        minisign.signature_file(data, "b", comment="file:index.json serial:1 generated:2026-11-02T10:00:00Z"))


if __name__ == "__main__":
    main()
