#!/usr/bin/env python3
import base64
import hashlib
import pathlib

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

HERE = pathlib.Path(__file__).resolve().parent

KEYS = {
    "a": (bytes(range(32)), bytes.fromhex("0123456789abcdef")),
    "b": (bytes(range(32, 64)), bytes.fromhex("fedcba9876543210")),
}
MESSAGE = b"hikarisub test manifest\n"
COMMENT = "hikarisub v1.0.0 hikarisub-update.json"


def private(name):
    return Ed25519PrivateKey.from_private_bytes(KEYS[name][0])


def key_id(name):
    return KEYS[name][1]


def shown_id(name):
    return key_id(name)[::-1].hex().upper()


def public_file(name):
    pk = private(name).public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
    line = base64.b64encode(b"Ed" + key_id(name) + pk).decode()
    return f"untrusted comment: minisign public key {shown_id(name)}\n{line}\n"


def signature_file(message, signer, *, prehashed=True, claimed=None, comment=COMMENT, signed_comment=None):
    algorithm = b"ED" if prehashed else b"Ed"
    payload = hashlib.blake2b(message, digest_size=64).digest() if prehashed else message
    signature = private(signer).sign(payload)
    global_signature = private(signer).sign(signature + (signed_comment or comment).encode())
    line = base64.b64encode(algorithm + key_id(claimed or signer) + signature).decode()
    return (
        "untrusted comment: signature from minisign secret key\n"
        f"{line}\n"
        f"trusted comment: {comment}\n"
        f"{base64.b64encode(global_signature).decode()}\n"
    )


def main():
    (HERE / "a.pub").write_text(public_file("a"))
    (HERE / "b.pub").write_text(public_file("b"))
    (HERE / "message.txt").write_bytes(MESSAGE)
    (HERE / "prehashed.minisig").write_text(signature_file(MESSAGE, "a"))
    (HERE / "legacy.minisig").write_text(signature_file(MESSAGE, "a", prehashed=False))
    (HERE / "by-b.minisig").write_text(signature_file(MESSAGE, "b"))
    (HERE / "b-claims-a.minisig").write_text(signature_file(MESSAGE, "b", claimed="a"))
    (HERE / "changed-comment.minisig").write_text(
        signature_file(MESSAGE, "a", comment=COMMENT.replace("v1.0.0", "v9.9.9"), signed_comment=COMMENT))


if __name__ == "__main__":
    main()
