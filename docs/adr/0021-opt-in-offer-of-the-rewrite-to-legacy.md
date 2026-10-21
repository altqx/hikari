---
status: accepted
amends: 0018, 0019
---

# Offer the rewrite to legacy users opt-in, from the release feed

On 2026-10-21 altqx decided how legacy users move to the rewrite. Moving is always the user's choice. Legacy (0.0.x) keeps its own releases until the owner ends its support. This replaces the `offer_to` gate in [ADR-0019](0019-installers-and-seamless-updates.md) and the [distribution policy](../qt/distribution.md).

## Why the gate changes

Every legacy build since 0.0.2-rc.1 treats a release whose `offer_to` names `legacy` as an ordinary update. With auto-install on, which is the default, it downloads that release silently and installs it at the next quit. Those builds can't be changed, so no rewrite release ever names `legacy` in `offer_to`. A rewrite manifest is always `"offer_to": ["qt"]`, and `HIKARISUB_OFFER_QT_TO_LEGACY` no longer changes it.

## The `offer` field

The offer is an optional member of a rewrite release's `hikarisub-update.json`. The manifest stays at schema 1, and builds that don't know the member ignore it.

```json
"offer": {
  "to": "legacy",
  "channel": "prerelease",
  "legacy_support": "continues",
  "pitch": {
    "en": {
      "title": "Try the new HikariSub",
      "highlights": ["…", "…"],
      "note": "This is a beta. You can go back to 0.0.x at any time."
    }
  }
}
```

- `to`: `legacy`, the only value.
- `channel`:
  - `prerelease`: offered to legacy users who see prereleases, that is, "Stable versions only" is off or they run an rc build.
  - `stable`: offered to every legacy user.
- `legacy_support`:
  - `continues`: a normal offer.
  - `ended`: the offer also says legacy gets no further updates. Legacy keeps working, and moving stays optional.
- `pitch`: plain text keyed by language tag. `en` is required, and a legacy build uses its interface language when the pitch has it.
  - `title`: up to 80 characters.
  - `highlights`: 1 to 5 entries, each up to 120 characters.
  - `note`: optional, up to 200 characters.
- A member that breaks these rules makes legacy ignore the offer. The rest of the manifest still counts.

Legacy 0.0.2-rc.5 and later read `offer` from rewrite releases before dropping them as not offered to legacy. Of the releases whose offer applies to the user, they take the highest one. The packages, sizes and SHA-256 come from the same manifest, as for any update.

The manifest is uploaded after `SHA256SUMS` is computed. To change a published offer's pitch or `legacy_support`, upload a new `hikarisub-update.json` to the same release. The owner approves every pitch before it is published.

## In legacy

- The offer has its own window, owned by the main window:
  - the pitch;
  - **Try it**;
  - **Remind me later**, which shows it again at the first start at least 3 days later;
  - **Don't offer again**, which holds until a higher rewrite version carries an offer.
- Help has "Try the new HikariSub…", which shows the current offer even after Don't offer again.
- A legacy update always comes first. The offer waits for a later start.
- Nothing is downloaded before Try it. After that, the download shows its progress and is checked against its SHA-256, then "Update ready" offers Restart now and Later, as for a legacy update.
- Every updater window is owned by the main window, centred on it and brought to the front. When the app is minimised or in the background, the taskbar button flashes instead. At startup, update windows wait until the last-session and crash prompts are closed.

## Moving and going back

The rewrite installs over legacy under the same identity, as ADR-0019 decided. It reads legacy's settings and never writes them, so going back is reinstalling a legacy 0.0.x release with the settings intact. A user who moves keeps the channel they had in legacy, and the rewrite's own updater takes over.

## AUR

Each AUR package moves to the rewrite on its own:
- `hikarisub-beta-bin` follows the rewrite from 0.1.0-beta.1, so its users move without an offer;
- `hikarisub-stable-bin` keeps legacy stable releases until the rewrite has a stable release.

## Legacy 0.0.1

0.0.1 reads no manifest. It reports the highest stable release, so it will show the first stable rewrite release as an ordinary "0.1.0 is available" notice that leads to the download page. That release's notes begin with "Coming from 0.0.x? Moving is optional; legacy keeps getting updates", linking the latest legacy release.

## Branches

When wave 6 ends, `main` becomes `legacy` and `qt` becomes `main`. This amends [ADR-0018](0018-bounded-beta-and-owner-approved-cutover.md), which kept `main` for legacy until the stable cutover. The stable gates there still apply to the first stable rewrite release.
