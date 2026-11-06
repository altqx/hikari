# srv3 import goldens

`golden/` holds the ASS that [YTSubConverter](https://github.com/arcusmaximus/YTSubConverter) 1.6.5 writes for each YouTube timed text file, on .NET 8.0.32 with `LANG=C`. `tools/srv3-goldens/regenerate.sh` writes it after the export goldens.

- `../../export/srv3/golden/*.ytt`: the export goldens, converted back.
- `inputs/ytsc-ytt.ytt`: YTSubConverter's own `ytt.ytt` sample, unchanged (MIT, see `LICENSE`).
- `inputs/edge-*.ytt`: Hikari's cases for what YouTube serves and what the reader must get right: pens, window styles and positions with gaps in their ids, ruby, karaoke offsets, line-break sections, the dark-text pairs, XML whitespace, CDATA, comments and references, line endings, UTF-16, bad UTF-8 and a default namespace.
