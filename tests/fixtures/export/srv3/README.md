# srv3 goldens

`golden/` holds what [YTSubConverter](https://github.com/arcusmaximus/YTSubConverter) 1.6.5 writes for each script, on .NET 8.0.32 with `LANG=C`. `tools/srv3-goldens/regenerate.sh` downloads both pinned releases, checks their hashes and rewrites the folder.

- `inputs/`: YTSubConverter's `sample1.ass`, `sample2.ass` and the ASS files of its own tests, unchanged (MIT, see `LICENSE`).
- `hikari-*.ytt`: the export fixtures one folder up.
- `*.options.ytt`: the same script with `StyleOptions.xml` (from YTSubConverter's tests) next to the program, kept only where those options change the output.
