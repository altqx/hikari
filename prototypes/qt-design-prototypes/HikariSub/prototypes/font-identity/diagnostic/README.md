# Why the captured multilingual font set does not replay with provider NONE

**Diagnosis, 2026-09-27: the replay removes the cross-family fallback resolver.** The captured fonts and collection face are usable for these specimens. Loading their bytes does not make libass search every attached family for a missing Arial glyph. The original [17/18 result and incomplete status](../README.md) are preserved; this addendum does not turn the eighteenth case into a pass.

Review [the rendered comparison report](report.html) and [exact observations](observations.json). No font binaries, copied libass tree or executable are included in the tracked addendum.

## Reproduced and narrowed

The existing hook executable reproduced the original full-frame SHA-256 pair exactly: DirectWrite `b07438461e219bd5c61506a2561c2d2913727e6cfb128d5f16a8297bf628dad5`, provider NONE `b0b1d571712be645e7f49ad1508014d8e745f480d948030a522622f45e6cd790`. A small native harness then compared 19 cases, each in a fresh library/render context, twice. All 19 second-run frames matched their first-run hashes. The two full baseline frames also match the preserved original evidence.

| Controlled comparison | Observed result |
| --- | --- |
| Original Arial text `A中ع́😀`, DirectWrite versus NONE with the three captured files | Different pixels; NONE reports missing CJK and emoji fallback. |
| ASCII `A` or Arabic with combining acute `ع́` | Same pixels for each provider pair. |
| One character `中`, or one emoji `😀` | Each still differs, narrowing the failure to missing fallback. |
| Reverse the three attachments; otherwise retain NONE/Arial | Same failed pixels as the original NONE replay. |
| Retain the captured attachments but restore DirectWrite | Exact original full-frame pixels and selected byte hashes/faces. This is **same-host provider-assisted reproduction**, not portable or self-contained replay. |
| NONE, unchanged Arial request, change only default family to Yu Gothic UI Semibold for `中` | Exact single-character DirectWrite pixels; captured TTC hash and opened face 2. |
| NONE, unchanged Arial request, default family Segoe UI Emoji for `😀` | Exact single-emoji DirectWrite pixels; captured hash and face 0. |
| Explicitly request those families in the corresponding single-character fixtures | Each agrees too; this changes authored selection and is only a reachability discriminator. |
| Full text, NONE, either one default family | Neither restores the whole frame. One default cannot supply both missing families here. |
| Original synthetic text amended with explicit font tags for the two fallback characters | All three expected hashes/faces are selected, but full pixels differ: `0e54190138a3fc8565d84cb5a0e6954d979543347a972ad8f6ba661309d02ff2`. Name rewriting is not demonstrated to be transparent and is not proposed as a fix. |

The exact pixel cause of the amended-text difference was not investigated: it is unnecessary to distinguish missing bytes from resolver loss. Matching selected font identities alone is visibly insufficient for claiming identical full rendering.

## Source explanation and falsified alternatives

The pinned [attachment initialization](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass_fontselect.c#L1091) loads embedded fonts before deciding whether to create a system provider; NONE skips `default_provider` creation. [Name/coverage matching](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass_fontselect.c#L726) considers fonts matching the requested family or full name and checks glyph coverage. It does not search unrelated attached families merely because their cmap contains the character.

The [selection sequence](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass_fontselect.c#L884) is requested family, configured default family, then the provider's `get_fallback` callback. With NONE and default Arial, the first two steps cannot find either missing character, and the third step does not exist. Reversing attachment order cannot create it.

The pinned [DirectWrite callback](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass_directwrite.c#L509) uses a one-character DirectWrite layout with hardcoded Arial/medium/normal, checks character support and returns a Win32 family name (or family name fallback). Its `base` argument is unused in that body. libass then rematches the returned family with the original requested weight/italic and extended-family matching. That provider behavior is additional environment state, absent from the attachment-only replay.

Thus missing/incorrect capture bytes or loss of TTC face 2 are **ruled out as the cause for these specimens**: the same captured files select the expected faces and reproduce each isolated glyph with NONE. This is not a universal capture-integrity proof. Attachment order is ruled out in the tested reversal. The harness intentionally changes provider policy; that limitation explains this mismatch rather than a file-copy defect. DirectWrite-assisted agreement does not prove that a machine lacking the original provider/font environment would replay it.

## Reproduce without replacing the original evidence

From the prototype checkout:

```powershell
& "$env:LOCALAPPDATA/HikariSub/prototype-runtime/Scripts/python.exe" HikariSub/prototypes/font-identity/diagnostic/run.py --expect-original-agreement
& "$env:LOCALAPPDATA/HikariSub/prototype-runtime/Scripts/python.exe" HikariSub/prototypes/font-identity/diagnostic/report.py
```

The first command deliberately returns **1** while the original full provider-NONE mismatch remains; JSON identifies each comparison rather than treating the expected failure as a pass. Omit the flag for a normal diagnostic capture exit. It compiles only this small C++ fixture using the existing MSVC/Debug archives and original ignored hook objects/captured fonts at `_run/20260926T215219440218Z` (override `--original-run` if relocated). It verifies the recorded source/archive/font hashes first. Those original generated objects are reused with explicit hashes, not reproducibly rebuilt or attested. A fresh clone lacks the ignored prerequisites; the parent experiment must be run separately to obtain matching local artifacts. No dependencies are installed.

Final capture: `2026-09-27T01:28:11.797736+00:00`, Windows 11 build 26200, Python 3.12.14, existing MSVC 14.51.36231. Diagnostic executable SHA-256: `77cf39428e46ed4b51c5b6f5f46fc0874218ac3448030294adead2e3a3059368`. Exact compiler, source, reused object, archive and selected-font hashes are in the observations. The preserved parent JSON remains `618a44e2b8f7facf26792e0476e4ece94913af8416d095974ff8a4b3a2e82112`; its report remains `8d1514c29001a9a553d182e10ee27f4b6e9814099f9734bbc7539079f8c402b5`.

## Remaining boundary

Keep the accepted captured/missing/fallback/incomplete distinctions. Strict agreement still requires reproducing the relevant resolution environment, not certifying a copied file list. There is no new policy choice required to label this case incomplete. A production resolver/replay representation, cache and context lifetime contract remain engineering design/qualification work; this diagnostic chooses none of them. A bounded future local experiment could test recorded fallback choices through a diagnostic-only selector seam without altering ASS, but it would require new instrumentation and would still not prove a general portable renderer.

Linux remains unavailable. Full-document coverage, variable instances, concurrency/refresh/cancellation, provider portability, rendering correctness and production build qualification remain outside this result. These are the same rights/provenance restrictions as the parent experiment: system font bytes stay in ignored local run folders; no user subtitle files, font installation, service or server was changed.
