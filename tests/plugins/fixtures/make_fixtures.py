#!/usr/bin/env python3
import importlib.util
import io
import json
import pathlib
import struct
import zipfile

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


def png(width, height):
    return b"\x89PNG\r\n\x1a\n" + struct.pack(">I4sII5B", 13, b"IHDR", width, height, 8, 6, 0, 0, 0)


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


if __name__ == "__main__":
    main()
