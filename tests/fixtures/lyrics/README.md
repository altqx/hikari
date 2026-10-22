# Word-timed lyrics fixtures (KA6)

Each file copies the structure of real files: tag order, time formats, namespaces, voice markers, background vocals, companion spans, spacing and overlaps. The words are public-domain lyrics instead: *Twinkle, Twinkle, Little Star* (Jane Taylor, 1806) and the traditional Japanese *Sakura Sakura*.

| File | Structure taken from | What it covers |
|---|---|---|
| `enhanced.lrc` | [steno-hero-songs `Echo.lrc`](https://github.com/aerickt/steno-hero-songs/blob/main/Echo.lrc), [ytmdesktop2 `enhanced-sample.lrc`](https://github.com/Venipa/ytmdesktop2/blob/main/apps/ytmdesktop2/src/renderer-plugins/youtube/lyrics/fixtures/enhanced-sample.lrc), [NewSync `love. - wave to earth.lrc`](https://github.com/ban-heesoo/NewSync) | UTF-8 BOM; ID tags, including one with a space after the colon; a first word with no stamp of its own; a first stamp later than the line time; a trailing end stamp; `v1:` voice markers; back-to-back stamps (a pause); a time with no lyrics; a plain line; 3-digit milliseconds |
| `line-only.lrc` | [SweetLine `example.lrc`](https://github.com/FinalScave/SweetLine/blob/main/tests/files/example.lrc) | Line times only; `[offset:+500]`; one line with two line times |
| `amll.ttml` | [AMLL TTML DB](https://github.com/amll-dev/amll-ttml-db) `raw-lyrics/1723483324539-50987405-e57b4219.ttml` and `1689087424000-39523898-47876155.ttml` (CC0), as AMLL TTML Tool writes them | Single-line XML; `mm:ss.fff` times; agents; `amll:meta` song name; `x-roman` and `x-translation` companions; an `x-bg` background vocal with its own spans; a space between spans; a word that starts before the previous word ends; a `<p>` with no spans |
| `apple.ttml` | Apple Music lyrics TTML (`itunes:timing="Word"`), as in the AMLL TTML DB `am-lyrics` sources | XML declaration; indented markup; seconds-only times; `ttm:title`; an agent with a `ttm:name`; a comment |
| `line-only.ttml` | Apple Music lyrics TTML (`itunes:timing="Line"`) | Clock times `hh:mm:ss.fff`; offset times with `dur`; `<br/>`; an entity |
| `malformed.lrc` | `enhanced.lrc` | Line 3: a word stamp earlier than the one before it |
