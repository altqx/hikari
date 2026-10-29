# GitHub release list fixture

`releases.json` is GitHub's answer to `GET /repos/altqx/hikari/releases?per_page=50`, recorded on
2026-10-21, for tests that must not use the network (SH3 #223, What's new).

- **Recorded part:** every release published then (legacy 0.0.x releases and the two `qt-metadata-*`
  tags). Each keeps only its first two assets, and `author` and `uploader` are cut down to `login` and `type`.
- **Added part:** HikariSub (0.1.0 and up) had published no release yet. The first seven entries (`v0.1.0` to
  `v0.1.4`) are made up in the same shape. `v0.1.4` is a draft, `v0.1.3-beta.2` and `v0.1.2-rc.1` are
  pre-releases, and `v0.1.1` has an empty name. `v0.1.3`'s notes hold what the renderer must refuse:
  an image, raw HTML, a `javascript:` link and a `file:` link.
