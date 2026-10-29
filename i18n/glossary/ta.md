# Tamil (ta) glossary

The terms `i18n/hikarisub_ta.ts` uses for HikariSub's domain words, so every
surface says the same thing. Legacy's catalog (`i18n/hikarisub_gettext_ta.ts`)
is the seed: its term is kept where it is the usual Tamil computing word.
Where legacy used a wrong sense or a rare spelling, the column "Not" names
it, and the interface catalog uses this term instead (the gettext catalog,
which Automation scripts read, keeps legacy's text).

## Conventions

- **Script.** Tamil script only; English loan words are written in Tamil
  letters the way Tamil interfaces commonly write them (வீடியோ, ஆடியோ,
  கரோக்கி). Legacy writes a loan's "s" as ச் (ச்கிரிப்ட், ச்பெக்ட்ரம்);
  keep that spelling for the same loans.
- **Names stay Latin:** HikariSub, ASS, SRT, MKV, FFMS2, Lua, MoonScript,
  Hunspell, Harper, DependencyControl, Aegisub, file extensions, tag names
  (`\k`, `\pos`), keys (Ctrl, Shift, Alt) and units (px, ms, fps, Hz).
- **Commands** (menu items, buttons, toolbar names) use the short
  imperative: திற, சேமி, மூடு, நீக்கு, இயக்கு. **Descriptions, tooltips and
  questions** use full sentences, with the polite imperative (சேமிக்கவும்)
  where they ask the user to do something.
- **Hotkey command names** (`HikariSub.Hotkeys`) use the short imperative
  and the same words as the matching menu item or tool name.
- **Accelerators:** `&` stays on a letter of the source: the translation
  ends with ` (&x)`, the source's accelerated letter in lower case, as
  legacy does (`&Save` → `சேமி (&s)`).
- **Placeholders:** every `%1`, `%2`, `%n`, `%L1` stays, once each; Tamil
  word order may move them. HTML tags and entities stay as they are.
  Numerus entries have two forms: one (n = 1) and other.
- **Punctuation:** keep the source's `…`, colons and line breaks.
- **Sentence case:** Tamil has no case; keep Latin names as written.

## Terms

| English | Tamil | Not |
|---|---|---|
| Line | வரி (வரிகள்) | |
| Line editor | வரி திருத்தி | |
| Line group | வரிக் குழு | Grid: "Make line group" வரிக் குழுவை உருவாக்கு |
| Timing postprocessor | நேரப் பின்செயலாக்கி | |
| Grid | கட்டம் | |
| Document | ஆவணம் | |
| Document tab | ஆவணத் தாவல் | |
| Tab | தாவல் | |
| Session | அமர்வு | |
| Workspace | பணியிடம் | |
| Panel | பலகம் | |
| Style | பாணி | ச்டைல் |
| Style manager | பாணி மேலாளர் | |
| Catalog (style catalog) | பட்டியல் | |
| Subtitles | வசன வரிகள் | |
| Subtitle file | வசனக் கோப்பு | |
| Script (subtitle or Automation) | ச்கிரிப்ட் | |
| Automation | தானியங்கு | ஆட்டோமேசன் |
| Macro | குறுநிரல் | |
| Template | வார்ப்புரு | |
| Preset | முன்னமைவு | |
| Karaoke | கரோக்கி | |
| Syllable | அசை | |
| Kanji / Furigana | காஞ்சி / ஃபுரிகானா | |
| Typesetting | டைப்செட்டிங் | |
| Timing | நேரமிடல் | |
| Video | வீடியோ | ஒளிதோற்றம் |
| Audio | ஆடியோ | |
| Frame | சட்டகம் | பிரேம் |
| Keyframe | கீஃப்ரேம் | |
| Waveform | அலைவடிவம் | |
| Spectrum | ச்பெக்ட்ரம் | |
| Play / Pause / Stop | இயக்கு / இடைநிறுத்து / நிறுத்து | விளையாடு |
| Zoom (in / out) | பெரிதாக்கு / சிறிதாக்கு; zoom level பெரிதாக்கம் | |
| Snap to | ஒட்டிப் பொருத்து | ஒடி |
| Resolution | தெளிவுத்திறன் | தீர்மானம், பகுத்தல் |
| Clip (vector, rectangle) | கிளிப் | |
| Drawing / Shape | வரைபடம் / வடிவம் | |
| Tag (override tag) | குறிச்சொல் | |
| Actor / Effect / Layer | நடிகர் / விளைவு / அடுக்கு | |
| Comment | கருத்து | |
| Margin | விளிம்பு | |
| Outline / Shadow / Border | வெளிக்கோடு / நிழல் / எல்லை | |
| Font | எழுத்துரு | |
| Bold / Italic / Underline / Strikeout | தடிமன் / சாய்வு / அடிக்கோடு / அடித்தல் | |
| Colour | வண்ணம் | |
| Original text / Translation | அசல் உரை / மொழிபெயர்ப்பு | |
| Translation mode | மொழிபெயர்ப்பு பயன்முறை | |
| Mode | பயன்முறை | |
| Spell check / Spellchecker | எழுத்துப்பிழை சரிபார்ப்பு / சரிபார்ப்பி | |
| Dictionary | அகராதி | |
| Grammar | இலக்கணம் | |
| Settings | அமைப்புகள் | |
| Options (a setting's choices) | விருப்பங்கள் | |
| Hotkey / Shortcut | குறுக்குவிசை | ஆட்ச்கி |
| Menu / Toolbar | மெனு / கருவிப்பட்டி | |
| Notice | அறிவிப்பு | |
| Log | பதிவு | |
| Theme | கருப்பொருள் | |
| File / Folder | கோப்பு / கோப்புறை | |
| Open / Save / Save as / Close | திற / சேமி / எனச் சேமி / மூடு | |
| Undo / Redo | செயல்தவிர் / மீண்டும்செய் | |
| Cut / Copy / Paste / Delete | வெட்டு / நகலெடு / ஒட்டு / நீக்கு | |
| Import / Export | இறக்குமதி / ஏற்றுமதி | |
| Update | புதுப்பிப்பு | |
| Apply / Cancel / OK | பயன்படுத்து / ரத்துசெய் / சரி | இடு (Apply) |
| Click / Drag | சொடுக்கு / இழு | |
| Clipboard | இடைநிலைப்பலகை | |
| Plugin | செருகுநிரல் | |
| Commit (the edit) | கமிட் | |
| Encoding | குறியாக்கம் | |
| Crash report | செயலிழப்பு அறிக்கை | |
| Layout (panel layout, script layout) | தளவமைப்பு | மனையமைவு |
| Dock / Float | பொருத்து / மிதக்கவிடு | |
| Window / Full screen | சாளரம் / முழுத் திரை | |
| Reference (the protected compare document) | மேற்கோள் | குறிப்பு (that is "note") |
| Aspect ratio | தோற்ற விகிதம் | |
| Frame rate | சட்டக வீதம் | |
| Autosave | தானியங்கு சேமிப்பு | |
| Autoload | தானியங்கு ஏற்றம் | ஆட்டோலோட் |
| Reset / Restore | மீட்டமை | |
| Default | இயல்புநிலை | |
| Replace | மாற்று | மாற்றிடு |
| Rename / Merge / Duplicate | மறுபெயரிடு / ஒன்றிணை / நகலாக்கு | |
| Dismiss / Discard / Revert | நிராகரி / கைவிடு / திரும்பப்பெறு | |
| Legacy (HikariSub legacy, its files) | பழைய | |
| Lead-in / Lead-out | லீட்-இன் / லீட்-அவுட் | |
| Wrap (line wrapping) | மடக்கு | |
| Index / Indexing | அட்டவணைப்படுத்தல் | குறியீட்டு |
| Shift times | நேர நகர்த்தல் (command நகர்த்து) | |
| Scale / Size | அளவீடு / அளவு | |
| Resample | மறுமாதிரியாக்கு | |
| Visual tools | காட்சிக் கருவிகள் | |
| Inspector bar | ஆய்வுப் பட்டி | |
| Mute / Unmute | ஒலியடக்கு / ஒலியை இயக்கு | |
| Regular expression | வழக்கமான வெளிப்பாடு | |
| Archive (zip) | காப்பகம் | |
| Conflict | முரண்பாடு | |
| Review | மதிப்பாய்வு | |
| Read-only | படிக்க மட்டும் | |
| Light / Dark / High contrast (themes) | வெளிர் / இருண்ட / உயர் மாறுபாடு | |
| Hue / Saturation / Lightness / Alpha | சாயல் / செறிவு / ஒளிர்வு / ஆல்ஃபா | லேசான தன்மை |
| Release notes | வெளியீட்டுக் குறிப்புகள் | |
| Log levels: fatal, error, warning, hint, debug, trace | கடும் பிழை, பிழை, எச்சரிக்கை, குறிப்பு, பிழைத்திருத்தம், தடம் | |
| Auto (a setting's value) | தானாக | |
| On / Off (a setting's state) | இயக்கத்தில் / முடக்கம் | |
| Start (a time, a column) / Start (command) | தொடக்கம் / தொடங்கு | |
| Editor (Line editor, Template editor) | திருத்தி | எடிட்டர், ஆசிரியர் |
| Template editor / Karaoke templater | வார்ப்புரு திருத்தி / கரோக்கி வார்ப்புருவாக்கி | |
| Template line / Code line / Karaoke line | வார்ப்புரு வரி / குறியீட்டு வரி / கரோக்கி வரி | |
| Generated line | உருவாக்கப்பட்ட வரி | |
| Pre-line template | வரிக்கு முந்தைய வார்ப்புரு | முன்-வரி |
| Variable / Modifier (templates) | மாறி / மாற்றி | |
| Override tags | மேலெழுதும் குறிச்சொற்கள் | |
| Problem | சிக்கல் | |
| Run (of a macro) / Repeat last run | இயக்கம் / கடைசி இயக்கத்தை மீண்டும் செய் | |
| Run (a tag run in translation mode) / Place run | தொகுதி / தொகுதியை வை | பகுதி |
| Getting started / Setup | தொடங்குதல் / அமைவு | |
| Dialog | உரையாடல் | |
| Title bar | தலைப்புப் பட்டி | |
| User folder | பயனர் கோப்புறை | |
| Tap to time / Tapping | தட்டி நேரமிடு / தட்டல் | |
| Latency / Calibrate | தாமதம் / அளவுதிருத்து | |
| Kanji timer | காஞ்சி நேரமிடுபவர் | காஞ்சி நேரமிடல் |
| Marker (audio) | குறிப்பான் | மார்க்கர் |
| Post processor | பின்செயலாக்கி | போச்ட் செயலி |
| Filter (noun / command) | வடிப்பி / வடிகட்டு | |
| Scroll | உருட்டு | ச்க்ரோல் |
| Column | நெடுவரிசை | |
| Occurrence (a search match) | நிகழ்வு | |
| Cache | தற்காலிகச் சேமிப்பு | |
| Colour picker | வண்ணத் தேர்வாளர் | |
| Alt (a hotkey's second command, "Commit alt") | (மாற்று வழி) | |
| Perspective (visual tool) | பார்வைக்கோணம் | |
| Position pointer / Position shifter | நிலைச் சுட்டி / நிலை நகர்த்தி | |
| Vector / Quad | திசையன் / நாற்கரம் | |
| Rotation axes | X / Y / Z (Latin letters) | ஃச் / ஒய் / சட் |
| Shear / Blur / Glow | சரிவு / மங்கல் / ஒளிவீச்சு | |
| Fade in / Fade out | உள்மங்கல் / வெளிமங்கல் | |
| Gradient | சாய்வு | |
| Animation | அசைவூட்டம் | அனிமேசன் |
| Offset | இடப்பெயர்ச்சி | ஆஃப்செட் |

Units (ms, cs, s, min, px, fps) stay in Latin letters. `QT_LAYOUT_DIRECTION`
is `LTR`.
