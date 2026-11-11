# Import lyrics…

**File › Import lyrics…** turns a lyrics file into Lines. Choose the file, then the dialog shows what it found and a preview of the first Lines. Card: AU23 #857; owner decisions Q191 and Q263–Q270.

**File › Open** also opens these files. It goes straight to a new untitled file, with the encoding detected and the default end times.

## Formats

| Format | Read |
|---|---|
| LRC (`.lrc`) | Line times `[mm:ss.xx]`, several on one line for repeated lyrics, ID tags, `[offset:]`, and enhanced (A2) word times `<mm:ss.xx>`. |
| TTML (`.ttml`) | Apple Music and AMLL word and line timing (KA6). |
| Kugou KRC (`.krc`) | Only text a decoder has already written: `[start,length]` Lines with `<offset,length,0>` words, offsets counted from the Line's start. |
| QQ Music QRC (`.qrc`) | Only decoded text, either inside its `<Lyric_1 LyricContent="…">` XML or bare: `[start,length]` Lines with words followed by `(start,length)`, counted from the start of the song. |

Word times become one `\k` syllable per word, plus an empty one for each pause. `[ti:]` and `[ar:]` are shown in the dialog. `[offset:]` is applied: a positive value shows the lyrics sooner. KRC's embedded translation (`[language:]`) is ignored.

HikariSub never decrypts lyrics. A `.krc` or `.qrc` file as Kugou or QQ Music ship it is recognised by its `krc1` or binary header, or by QQ Music's hex download, and is refused: decode it to text with your own tool first. The code is built from the format descriptions only; no Lyric Importer or LyricDecoder code is used.

## Encoding

A byte order mark decides first (UTF-8, UTF-16 LE or UTF-16 BE). Then bytes that are valid UTF-8 are read as UTF-8, and otherwise uchardet guesses among Big5, Shift_JIS and EUC-KR, with GB18030 as the fallback. GB18030 also reads GBK and GB2312. The dialog's **Encoding** list overrides the guess and redraws the preview at once.

## Where the Lines go

- **After the last line of the open file:** the default when an ASS file is open. Every Line takes the chosen **Style**, which defaults to the current Line's Style. Script Info is left alone. The whole import is one undo step, "Import lyrics", and the new Lines are selected afterwards.
- **A new file:** opens untitled with unsaved work, with the lyrics' title as its Title.

## End times

These apply only where the file leaves an end open: a plain LRC line, or the last word of a word-timed line without an end stamp. KRC, QRC and TTML Lines carry their own ends.

- **Each line ends when the next one starts** (the default).
- **Leave a gap before the next line** (default 0.20 s). When the gap would leave a Line no time, it ends when the next one starts instead.
- **Last line lasts** (default 5.00 s).

The choices are kept in `lyrics.importOptions`.

## Tests

- `hikari_core_lyrics_import_tests`: each format from fixtures in `tests/fixtures/lyrics`, encrypted files, problems with their file line, and the end-time options.
- `hikari_backends_lyrics_text_tests`: GB18030, Big5, UTF-16 LE/BE and UTF-8 fixtures decode to the same text, and detection.
- `hikari_application_lyrics_insert_tests`: the open-file import is one undo step.
- `hikari_application_document_files_tests`: File › Open with decoded KRC/QRC, encrypted files and the decoder.

The fixtures are written by hand with made-up lyrics. The encrypted ones are the headers followed by random bytes.
