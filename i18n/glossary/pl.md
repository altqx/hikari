# Polish glossary (pl)

Terms for `i18n/hikarisub_pl.ts`. They follow the legacy Polish catalog
(`i18n/hikarisub_gettext_pl.ts`, Marcin Drob's translation) where it had a
term, and the glossary in `CONTEXT.md` for what each term means. Where
legacy used two words for one thing (linia / linijka, karta / zakładka), the
first column below is the one to use.

## Conventions

- **Commands** (menu items, buttons) use the imperative: Otwórz, Zapisz,
  Zamknij, Wczytaj. **Descriptions and tooltips** use the third person:
  Otwiera…, Zapisuje… (as legacy does). Settings check boxes describe the
  behaviour: "Pokazuj…", "Otwieraj…".
- **Sentence case**, as in the English source: only the first word and proper
  names are capitalized.
- **Questions** use the infinitive or "Czy…?": "Wczytać poprzednią sesję?",
  "Czy na pewno chcesz usunąć ten katalog?".
- **Quotes** keep the source's straight quotes (`"%1"`). Ellipsis is `…`
  where the source has `…`, `...` where it has `...`.
- **Placeholders** (`%1`, `%2`, `%n`), `&` accelerators, HTML tags, line
  breaks and file patterns (`*.ass`) are kept. An accelerator goes on a letter
  of the Polish word, preferably the first.
- **Plurals** (numerus) have three forms: 1 (`1 linijka`), 2–4 and 22–24…
  (`%n linijki`), everything else, 0 and 5–21 included (`%n linijek`).
- **Units**: s, ms, px, %, fps stay as written; numbers keep the source's
  separators (they come from the code, not the translation).

## Terms

| English | Polish | Notes |
| --- | --- | --- |
| Line | linijka (pl. linijki, linijek) | A subtitle Line; never "linia". "wiersz" for a text row or a log line; "linia" only for a drawn line (audio markers, vector drawing). |
| Line wrap | łamanie wiersza | Grid column: "Łamania". |
| Line editor | edytor linijki | |
| Line group | grupa linijek | |
| Document | dokument | |
| Document tab | zakładka (dokumentu) | Legacy "zakładka"; "Otwieraj… w nowej karcie" stays as carried. |
| Session | sesja | |
| Workspace | obszar roboczy | Panel arrangement of a tab. |
| Panel | panel | |
| Grid | lista napisów / siatka | Legacy "pole napisów". As a panel name: "Lista napisów". |
| Grid filter | filtr listy napisów | |
| Script properties | właściwości skryptu | |
| Style | styl | |
| Style manager | menedżer stylów | |
| Style catalog | katalog stylów | |
| Font catalog | katalog czcionek | |
| Font collector | kolekcjoner czcionek | |
| ASS override tag | tag (ASS) | Legacy "tag". |
| Visual tool | narzędzie wizualne | |
| Visual tool names | Wskaźnik pozycji, Pozycjonowanie tekstu, Ruch tekstu, Skalowanie tekstu, Obrót tekstu wokół osi Z, Obrót tekstu wokół osi X / Y, Wycinek prostokątny, Wycinek wektorowy, Rysunek wektorowy, Przesuwanie pozycji, Hydra, Perspektywa | Rail, Hotkeys page and tool bar. |
| Shear (`\fax`) | pochylenie | |
| Quad (Perspective) | czworokąt | |
| Secondary colour | kolor zastępczy | |
| Aspect ratio | proporcje obrazu | |
| Dummy video | atrapa wideo | |
| Typesetting | typesetting | Kept, as Polish fansubbing says it. |
| Timing | timing | Legacy. "czasy" for the start/end times themselves. |
| Time shift | przesunięcie czasów | Window: "Przesuwanie czasów". |
| Timing postprocessor | postprocesor timingu | |
| Keyframe | klatka kluczowa | |
| Frame | klatka | |
| Start / End time | czas początkowy / końcowy | Short labels and columns: Początek / Koniec. A "Start" button that starts something: Rozpocznij. |
| Duration | czas trwania | |
| Layer | warstwa | |
| Actor | aktor | |
| Effect | efekt | |
| Comment | komentarz | |
| Clip | wycinek (wektorowy / prostokątny) | |
| Drawing / Shape | rysunek / kształt | |
| Karaoke | karaoke | |
| Syllable | sylaba | |
| Translation mode | tryb tłumaczenia | |
| Original text | tekst oryginalny | |
| Translated text / Translation | tekst tłumaczenia | |
| Unconfirmed | niepewna (linijka) | Legacy "niepewne". |
| Automation | automatyzacja | |
| Automation macro | makro | |
| Automation script | skrypt automatyzacji | |
| Export filter | filtr eksportu | |
| Template | szablon | |
| Template editor | edytor szablonów | |
| Template line / code line | linijka szablonu / linijka kodu | |
| Karaoke line | linijka karaoke | |
| Generated lines | wygenerowane linijki | "older" (stale): nieaktualne. |
| Karaoke templater | templater karaoke | Keywords (`template`, `code`, `pre-line`, `noblank`…) and variables stay as written. |
| Run (Translation mode) | fragment | "Fragment %1", "Wstaw fragment 1", placed: wstawiony. |
| Run (of a macro or script) | uruchomienie | "Powtórz ostatnie uruchomienie". |
| Tap to time | timing stukaniem | tap: stukać / wystukać; latency: opóźnienie. |
| Kanji timer | timer kanji | |
| Getting started | Pierwsze kroki | |
| Preset | preset / ustawienie wstępne | Short labels: "Preset". |
| Hotkey | skrót klawiszowy (short: skrót) | |
| Hotkey command "X alt" | X (drugi skrót) | The alternate binding of X. |
| Tag button N | Przycisk tagu N | |
| Settings | Ustawienia | |
| User folder | folder użytkownika | |
| Spell check | sprawdzanie pisowni | |
| Dictionary | słownik | |
| Grammar check | sprawdzanie gramatyki | |
| Subtitles | napisy | |
| Video / Audio | wideo / audio | |
| Waveform / Spectrum | przebieg fali / spektrum | |
| Inspector bar | pasek inspektora | |
| Notice / toast | powiadomienie | |
| Log | dziennik | Legacy "Okno logów" kept where carried. |
| Autosave | autozapis | |
| Untitled | Bez nazwy | |
| Undo / Redo | Cofnij / Ponów | |
| Fullscreen | pełny ekran | |
| Zoom | powiększenie | |
| Snap | przyklej(anie) | Legacy "Przyklejaj do klatek kluczowych". |
| Update | aktualizacja | |
| Crash / freeze report | raport awarii / zawieszenia | |
