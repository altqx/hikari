---
status: accepted
---

# Author translations in TS and load QM in the Qt rewrite

The Qt rewrite will author translations exclusively in TS and load compiled QM catalogs, following altqx's [“migrate fully to TS” decision](https://github.com/altqx/hikari/issues/29#issuecomment-5849403539) and [research into Qt extraction, context and plural handling](https://github.com/altqx/hikari/blob/78ea3804dd9a732ff1f26183353d79535582c3f7/docs/research/qt-localisation.md). Existing PO catalogs remain pinned migration input while the wx `main` branch stays on its maintenance workflow; the rewrite has no ongoing PO/TS dual authoring, and preserves `aegisub.gettext` through TS/QM compatibility entries. The [localisation specification](../qt/localisation.md) requires reviewed key/placeholder migration and future runtime checks; Git contributions need no new hosted service, and no provider choice or completed conversion is implied.
