# Associated-file fixtures (C05, P9)

Inputs for associated-file discovery on open (legacy `HikariSubFrame::FindFile`
and `Notebook::LoadVideo` with its "Associated files" question, at 20d647c4),
under approved C05-audio-association: a Document's associations come only from
its own Script Info.

| File | Role |
| --- | --- |
| `ep01.ass` | Script Info `Video File: ep01.mkv`, `Audio File: audio/ep01.flac`, `Keyframes File: ep01_keyframes.txt`, all relative to the subtitles and present |
| `ep01.mkv`, `audio/ep01.flac` | placeholders standing for the media (not decodable; the application tests copy real media fixtures where a file must open) |
| `ep01_keyframes.txt` | a keyframes file |
| `ep02.ass` | `Video File: gone.mkv`, `Audio File: gone.wav`, both missing: nothing associated is offered |
| `ep02.mp4` | the same-named video beside `ep02.ass`, offered as "Video from directory" |

The files are read only; no test writes here.
