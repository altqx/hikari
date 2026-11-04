---
status: accepted
amends: 0019
---

# The update manifest is signed with minisign

On 2026-11-02 altqx decided that the update manifest is signed ([MS1 #656](https://github.com/altqx/hikari/issues/656)). [ADR 0019](0019-installers-and-seamless-updates.md) said "Nothing is code-signed; SHA-256 from GitHub over HTTPS is the only integrity check". That still holds for the executables and installers: no code signing. It no longer holds for the update manifest.

`hikarisub-update.json` names every package and its SHA-256. Since [ADR 0026](0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md), it also names the AI sidecar's. Before this ADR, anyone who could write to the GitHub release could send installed copies a different package: a stolen token, or a compromised release job. With a signed manifest, they also need a key that never leaves the owner's hands.

## What is signed

- **Files:** the release job signs `hikarisub-update.json` into `hikarisub-update.json.minisig`, and `SHA256SUMS` into `SHA256SUMS.minisig`. The second signature is for people who check by hand.
- **Trusted comment:** each signature's trusted comment is `hikarisub <tag> <file>`, for example `hikarisub v0.1.0-beta.4 hikarisub-update.json`. The comment is covered by the signature, so a signature can't be moved to another release or another file.
- **The signing job:** it runs only the pinned `minisign` (`packaging/linux/tools.lock`) and the repository's own scripts. It uses no third-party actions and runs nothing from the built packages. It receives the secret key and its password (`MINISIGN_SECRET_KEY`, `MINISIGN_PASSWORD`) in its signing step only, and never prints them.
- **Checks in the job:**
  - It checks the manifest's signature with the app's own check (`tools/release/verify_release_signature.cpp`) against the keys the app ships, so a release can't be signed by a key that copies would refuse.
  - It checks `SHA256SUMS` with `minisign -V`.
  - It publishes the release only after both signatures are uploaded and read back.

## What HikariSub checks

The update check (HikariSub only; HikariSub legacy stays as it is) and the AI sidecar download both check the manifest:

- **Keys:** the app carries the trusted public keys in its source (`kReleaseKeys`), as a list.
- **A release with a manifest must have `hikarisub-update.json.minisig`.** It is downloaded under the same rules as the manifest: a fixed URL, the same hosts, and 64 KiB at most. It must:
  - verify with a trusted key whose key id matches, as a prehashed `ED` (BLAKE2b-512) or legacy `Ed` signature;
  - carry a global signature that covers its trusted comment;
  - have the trusted comment `hikarisub <this release's tag> hikarisub-update.json`.
- **A missing or bad signature skips the release**, as a broken manifest does. The walk moves on to the next candidate, and the `hikari.updates` log says why. No toast is shown. The AI sidecar download fails with the manifest error, and the reason is its detail.
- **A HikariSub release without a manifest is only named.** The app shows that a newer version is available, with its release page, and never installs it from GitHub's asset digests (owner, 2026-11-04). Otherwise anyone who could write to the release could simply leave out the manifest.
- **Older releases:** releases from before this ADR have no manifest signature. They are older than any copy that checks, so the walk never offers them.

The Ed25519 and BLAKE2b code is libsodium from vcpkg, which the plugin signatures already use ([PL1 #413](https://github.com/altqx/hikari/issues/413), `hikari_minisign`). No new dependency is added.

## Key custody

- The owner runs `minisign -G` on their own machine and keeps the secret key and its password offline.
- They reach GitHub only as the repository secrets `MINISIGN_SECRET_KEY` (the whole `.key` file) and `MINISIGN_PASSWORD`. They pass through a thread only by `request_secret`, never in chat, a commit or a log.
- The public key is in the source, in [distribution](../distribution.md#integrity), and on the site's download page.

The first key, from 2026-11-04:

```
untrusted comment: minisign public key A97C7A20EFD98999
RWSZidnvIHp8qW7ynpUV8iDc/FH3riGpLuA+tz6zSeqwE3Q9Y+jmY5TH
```

## Rotation

1. The owner makes the next key pair offline.
2. A release adds the next public key to `kReleaseKeys`, next to the current one. That release is still signed with the current key.
3. Once that release has been out long enough that copies have it, the owner replaces the two secrets with the next key, and later releases are signed with it.
4. A later release removes the old key from the list.

A lost or leaked key works the same way, except that step 3 can't wait. Copies that never received the next key can't verify releases signed with it. They see no update until they install a newer version by hand. The release notes and the site then say so.
