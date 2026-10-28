# Thai (th_TH) glossary

The terms `i18n/hikarisub_th_TH.ts` uses for HikariSub's domain words (CONTEXT.md).
Where the legacy catalog (`i18n/hikarisub_gettext_th_TH.ts`) already names a thing, the rewrite keeps that name,
so a user moving from HikariSub legacy sees the same words.

## Conventions

- **Register:** short, polite, neutral UI Thai, as Windows and macOS write it in Thai. No ครับ/ค่ะ, and no particles
  such as นะ or เลย in labels. Address the user as คุณ only where the English says "you".
- **Spaces:** Thai has no spaces between words. A space separates clauses or sentences, and goes around Latin words,
  numbers and placeholders (`บรรทัด %1 จาก %2`). The English full stop between sentences becomes a space; a single
  sentence ends without a full stop.
- **Ellipsis:** keep `…` where the source has it (an item that opens a window).
- **Accelerators:** put `&` before a Thai consonant or a leading vowel (เ แ โ ใ ไ), never before a vowel written
  above, below or after, or a tone mark (`&ไฟล์`, `ตั&ด`).
- **Placeholders:** keep every `%1`, `%L1`, `%n` and HTML tag. Thai has one plural form, so a numerus entry has one
  form, written with `%n`.
- **Latin names stay Latin:** HikariSub, ASS, SRT, Lua, MoonScript, DependencyControl, FFmpeg, MeCab, Harper,
  GitHub, VSFilter, libass, Hunspell, FPS, CPS, key names (Ctrl, Shift, Alt).
- **Colons:** a field label ends with `:` only when the English one does.

## Terms

| English | Thai | Note |
|---|---|---|
| Line | บรรทัด | a subtitle Line; "1 line" = 1 บรรทัด |
| Document | เอกสาร | the subtitle content being edited |
| Document tab | แท็บเอกสาร | |
| Tab | แท็บ | |
| Session | เซสชัน | |
| Workspace | พื้นที่ทำงาน | |
| Panel | แผง | |
| Grid | ตาราง | legacy: ตารางคำบรรยาย (subtitle grid) |
| Line editor | ตัวแก้ไขบรรทัด | legacy "Edit box": ตัวแก้ไข |
| Subtitles, subtitle | คำบรรยาย | |
| File | ไฟล์ | as Windows and macOS Thai write it; replaces legacy แฟ้ม in the whole catalog (`&ไฟล์` menu) |
| Folder | โฟลเดอร์ | |
| Style | สไตล์ | |
| Style catalog | แคตตาล็อกสไตล์ | legacy: แคตตาล็อก |
| Font | แบบอักษร | |
| Font catalog | แคตตาล็อกแบบอักษร | |
| Script properties | คุณสมบัติสคริปต์ | legacy: คุณสมบัติไฟล์ ASS |
| Actor | ผู้พูด | legacy |
| Effect | เอฟเฟกต์ | legacy |
| Layer | ชั้น | legacy |
| Margin | ระยะขอบ | legacy |
| Comment (a commented Line) | ความคิดเห็น | legacy |
| Dialogue | บทสนทนา | legacy |
| Original (text) | ต้นฉบับ | legacy |
| Translation | การแปล, คำแปล | the mode / the text |
| ASS override tag, tag | แท็ก | |
| Karaoke | คาราโอเกะ | legacy |
| Syllable | พยางค์ | legacy |
| Kanji, Furigana | คันจิ, ฟุริงานะ | |
| Timing (to time lines) | จัดเวลา, การจัดเวลา | legacy: จัดเวลาคาราโอเกะ |
| Typesetting | ไทป์เซ็ต | fansub practice; "the Typesetting preset" = พรีเซ็ตไทป์เซ็ต |
| Preset | พรีเซ็ต | |
| Template | เทมเพลต | legacy editor colours said แม่แบบ; the templater is เทมเพลต |
| Automation | สคริปต์อัตโนมัติ | legacy `สคริปต์&อัตโนมัติ` |
| Script (automation) | สคริปต์ | |
| Macro | แมโคร | |
| Visual tool | เครื่องมือภาพ | legacy |
| Clip (vector, rectangular) | คลิป | legacy คลิปเวกเตอร์ |
| Drawing | ภาพวาด | legacy ภาพวาดเวกเตอร์ |
| Keyframe | คีย์เฟรม | legacy |
| Frame | เฟรม | |
| Waveform | รูปคลื่น | |
| Spectrum | สเปกตรัม | legacy |
| Video | วิดีโอ | |
| Audio | เสียง | |
| Hotkey, shortcut | ปุ่มลัด | legacy |
| Settings | การตั้งค่า | menu `&การตั้งค่า` |
| Spell check | ตรวจการสะกด | legacy |
| Spellchecker | ตัวตรวจการสะกด | legacy |
| Dictionary | พจนานุกรม | |
| Grammar check | ตรวจไวยากรณ์ | |
| Undo, Redo | เลิกทำ, ทำซ้ำ | legacy |
| Preview | ตัวอย่าง | legacy |
| Log | ล็อก | |
| Notice, notification | การแจ้งเตือน | |
| Plugin | ปลั๊กอิน | |
| Theme (application) | ธีม | never for a subtitle Style |
| Resolution | ความละเอียด | legacy |
| Encoding | การเข้ารหัส | legacy |
| Selection, selected | ส่วนที่เลือก, ที่เลือก | |
| Import, Export | นำเข้า, ส่งออก | |
| Report (an issue) | รายงาน | |
| Update | อัปเดต | |
| Error, Warning | ข้อผิดพลาด, คำเตือน | legacy |
