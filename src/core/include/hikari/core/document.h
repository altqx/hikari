#pragma once

// Source-preserving subtitle document (docs/qt/document-model.md). Sections and
// records keep authored order; every record links to its exact source bytes.
// Records are semantic views over that source, never a competing copy of it.

#include "hikari/core/time.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace hikari::core {

// Byte range of a record in the source, excluding its final line terminator.
// Usually one physical line; an SRT cue spans its number, timing and text lines.
struct SourceSpan {
    std::size_t offset = 0;
    std::size_t length = 0;
    std::size_t terminatorLength = 0; // 0 (last line), 1 (LF or CR) or 2 (CRLF)
    bool operator==(const SourceSpan &) const = default;
};

enum class TextEncoding {
    Utf8,
    Utf8WithBom,
};

struct SourceText {
    std::vector<std::byte> bytes; // immutable original input, byte for byte
    TextEncoding encoding = TextEncoding::Utf8;
};

// Stable within a Document's lifetime; not a row number, never serialized.
struct LineId {
    std::uint64_t value = 0;
    auto operator<=>(const LineId &) const = default;
};

// An authored time field: the original lexeme, and the value the legacy
// positional parser gives it.
struct TimeField {
    std::u8string lexeme;
    DocumentTime value;
};

// An integer field read like the legacy loader (C atoi): lexeme kept.
struct IntField {
    std::u8string lexeme;
    std::int64_t value = 0;
};

// Actor-field markers as the legacy Dialogue::SetRaw reads them. Only a field
// starting with '[' is examined, and only the first matching kind in the
// order bookmark, hidden, visible, tree_closed, tree_opened,
// tree_description is removed (every occurrence of it). Kinds never combine
// on load; on save the group or visibility marker precedes [bookmark].
enum class LineVisibility { Visible, Hidden, VisibleBlock };
enum class GroupMarker { None, Description, Opened, Closed };

struct LineRecord {
    LineId id;
    bool comment = false;   // ASS Comment: rather than Dialogue:
    // An event line the legacy parser could not read as ASS ("Dial"/"Comm"
    // prefix without "Dialogue"/"Comment", or fewer than 9 fields): the whole
    // line is the text, with zero times and the Default style.
    bool unparsed = false;
    IntField layer;
    TimeField start;
    TimeField end;
    std::u8string style;
    std::u8string actor;    // trimmed, without the marker kind the loader removed
    bool bookmark = false;
    LineVisibility visibility = LineVisibility::Visible;
    GroupMarker group = GroupMarker::None;
    IntField marginLeft;
    IntField marginRight;
    IntField marginVertical;
    std::u8string effect;
    std::u8string text;     // legacy trims surrounding whitespace; raw bytes stay in the span
    SourceSpan span;        // where the record came from; its terminator is reused on save
    bool edited = false;    // the span no longer describes the record; regenerate on save
    // Added in the editor: no source bytes (an empty span at its position).
    // Saved in the legacy form with the newline style of the record before it.
    bool inserted = false;
    std::optional<std::u8string> cueNumber; // SRT: the authored cue number line, if any
    // MicroDVD: authored frame numbers. start/end times stay unresolved (zero)
    // until the Document's own frame rate is set (C01-fps-isolation).
    std::optional<std::int64_t> startFrame;
    std::optional<std::int64_t> endFrame;
    // TLMode pair (Script Info "TLMode: Yes"): an original line in the
    // "TLMode Style" followed by its translation line, read as one Line. The
    // other fields come from the translation line; text is the original's.
    // span covers both lines and anything between them; originalSpan is the
    // original line alone.
    std::optional<SourceSpan> originalSpan;
    std::u8string translation; // legacy TextTl; empty means untranslated
    bool unconfirmed = false;  // legacy State 4, written as the effect "\fD"
    // E6: legacy State 1 and 2, the Grid's changed-Line mark. Each legacy
    // Dialogue::Copy() without keepstate made a changed object
    // (ChangeDialogueState(1), SubsDialogue.cpp:1071-1072), shared by the
    // history steps after it until copied again; a save turned changed
    // objects into saved ones (SubsGridBase.cpp:391). A changed Line carries
    // a version unique to that copy (0: never changed since loading), so the
    // session can mark exactly the saved copies, wherever history keeps them.
    std::uint64_t changeVersion = 0;
};

// E6: how a Document mutation treats a Line's changed-Line mark: Changed
// gives it a new version (legacy Copy()), Kept leaves it as it is (legacy
// Copy(keepstate) or an in-place change, such as filtering's).
enum class ChangeMark { Changed, Kept };
// A version no other changed Line has (process-wide).
std::uint64_t newChangeVersion();

struct StyleRecord {
    std::u8string name;
    std::vector<std::u8string> fields; // positional fields after "Style:", untrimmed
    SourceSpan span;
    bool inserted = false; // added in the editor: written as "Style: " + fields
    bool edited = false;   // changed in the editor: written as "Style: " + fields in place
};

struct PropertyRecord {
    std::u8string key;   // trimmed text before the first ':'
    std::u8string value; // trimmed text after it
    SourceSpan span;
    bool edited = false;   // written as "key: value" (legacy GetSInfos)
    bool inserted = false; // added in the editor
};

struct FormatRecord {
    std::vector<std::u8string> fields; // declaration retained; parsing stays positional
    SourceSpan span;
};

// Anything kept verbatim: blank lines, ';' comments, embedded font/graphic
// payload, unknown-section content, and lines that failed to parse.
struct OpaqueRecord {
    SourceSpan span;
};

using Record = std::variant<LineRecord, StyleRecord, PropertyRecord, FormatRecord, OpaqueRecord>;

enum class SectionKind {
    Preamble,   // content before the first header
    ScriptInfo,
    Styles,     // [V4+ Styles]
    SsaStyles,  // [V4 Styles]: SSA, converted by a separate explicit operation
    Events,
    Fonts,
    Graphics,
    Unknown,    // retained untouched (C03-preservation)
    Cues,       // the single body of a sectionless format (SRT)
};

struct Section {
    SectionKind kind = SectionKind::Preamble;
    std::u8string header;            // as authored, e.g. "[Events]"; empty for the preamble
    std::optional<SourceSpan> headerSpan;
    std::vector<Record> records;
};

struct Diagnostic {
    enum class Severity { Info, Warning, Error };
    enum class Kind {
        InvalidUtf8,         // line kept as opaque bytes
        UnsupportedEncoding, // e.g. a UTF-16 byte-order mark; content kept as bytes
        MalformedTime,       // non-canonical time lexeme; legacy value used
        MalformedEvent,      // not readable as an ASS event; kept as a plain-text Line
        MalformedPair,       // TLMode original without a readable translation line
        RepeatedSection,     // a section kind seen again; both kept in order
        UnknownSection,      // header not recognized; kept untouched
        SsaStyles,           // [V4 Styles] present; conversion is a separate operation
    };
    Severity severity = Severity::Info;
    Kind kind = Kind::InvalidUtf8;
    std::size_t byteOffset = 0;
    std::u8string excerpt;
};

// The legacy format codes (styles.h): which serialization a Document uses.
enum class SubtitleFormat { Ass = 1, Srt, TMPlayer, MicroDvd, Mpl2, PlainText = 0 };

class Document {
public:
    const SourceText &source() const { return m_source; }
    const std::vector<Section> &sections() const { return m_sections; }

    // Lines in document order across all Events sections.
    std::vector<const LineRecord *> lines() const;

    // Replaces one Line's text. The record's source span becomes stale and the
    // Line is regenerated on save; every other record keeps its exact bytes.
    // Returns false when no Line has this id.
    bool setLineText(LineId id, std::u8string text);
    // Sets a Line's Unconfirmed state; like setLineText, the Line is regenerated.
    bool setLineUnconfirmed(LineId id, bool unconfirmed);
    // E6: marks a Line changed without touching its fields or source bytes,
    // as legacy's plain copy of a Dialogue that only moves (SwapRowsF, the
    // sorts). False when no Line has this id.
    bool markLineChanged(LineId id);
    // Changes any fields of one Line; the Line is then regenerated on save.
    // Every mutation marks the Line changed unless `mark` is Kept (E6).
    bool editLine(LineId id, const std::function<void(LineRecord &)> &change, ChangeMark mark = ChangeMark::Changed);
    // Inserts a new Line right after `after`, in the same section. Returns its
    // id, or nullopt when `after` is unknown. Kept: the record's own
    // changeVersion stays (0 for a new legacy Dialogue).
    std::optional<LineId> insertLineAfter(LineId after, LineRecord line, ChangeMark mark = ChangeMark::Changed);
    // Inserts a new Line right before `before`, in the same section.
    std::optional<LineId> insertLineBefore(LineId before, LineRecord line, ChangeMark mark = ChangeMark::Changed);
    // Appends a new Line at the end of the last Events section; nullopt when
    // the Document has none.
    std::optional<LineId> appendLine(LineRecord line, ChangeMark mark = ChangeMark::Changed);
    // Removes a Line (its source bytes are no longer written). False when no
    // Line has this id.
    bool removeLine(LineId id);
    // Removes every Line `remove` picks, in one pass; how many were removed.
    std::size_t removeLinesIf(const std::function<bool(const LineRecord &)> &remove);
    // Moves a Line, keeping its id and source bytes, before `before`, or to
    // the end of the last Events section when `before` is nullopt. False when
    // either Line is unknown or they are the same Line.
    bool moveLine(LineId id, std::optional<LineId> before);

    // The last Script Info value for key, as the legacy SubsFile::GetSInfo sees it.
    std::optional<std::u8string> scriptInfo(std::u8string_view key) const;
    // Legacy AddSInfo: the key's (last) value is replaced, or the key is added
    // after the last Script Info property. False without a Script Info section.
    bool setScriptInfo(std::u8string_view key, std::u8string value);
    // Legacy DeleteSInfo: the key is no longer written. False when absent.
    bool removeScriptInfo(std::u8string_view key);
    // Legacy AddStyle: a Style line after the last Style; `fields` are its
    // positional fields, name first. False without a Styles section.
    bool appendStyle(std::vector<std::u8string> fields);
    // Legacy DeleteStyle(FindStyle(name)): the first Style of that name.
    bool removeStyle(std::u8string_view name);
    // Legacy ChangeStyle: the index-th Style (document order, as decodeStyles
    // lists them) gets these positional fields, name first, and is written
    // as "Style: " + fields in its place. False when there is no such Style.
    bool editStyle(std::size_t index, std::vector<std::u8string> fields);
    // The Style manager's lists (Y1): the Styles become `slots` in this order.
    // A slot naming an existing Style (`from`, document order) moves it with
    // its bytes unless `fields` are given; a slot without `from` is a new
    // Style. Slots fill the existing Style positions in order, extra ones go
    // after the last Style, and Styles beyond the slots are removed. False
    // without a Styles section or for an unknown `from`.
    struct StyleSlot {
        std::optional<std::size_t> from;
        std::optional<std::vector<std::u8string>> fields;
    };
    bool rearrangeStyles(const std::vector<StyleSlot> &slots);
    // A macro's Script Info list (S4; the legacy SInfo vector a macro edits
    // through its subtitles object): the properties become `slots` in this
    // order, as rearrangeStyles does for Styles. A slot naming an existing
    // property (`from`, document order across Script Info sections) keeps its
    // bytes unless `property` gives a new key and value; extra slots go after
    // the last property of the last Script Info section; properties beyond
    // the slots are removed. Comments and blank lines stay where they are.
    // False without a Script Info section or for an unknown `from`.
    struct PropertySlot {
        std::optional<std::size_t> from;
        std::optional<std::pair<std::u8string, std::u8string>> property;
    };
    bool rearrangeScriptInfo(const std::vector<PropertySlot> &slots);

    SubtitleFormat format() const { return m_format; }
    // MicroDVD frame rate for this Document only; nullopt while unknown.
    const std::optional<FrameRate> &frameRate() const { return m_frameRate; }
    // Sets the rate and resolves every frame-timed Line's start/end from its
    // authored frames, exactly (nearest microsecond). Other Documents are untouched.
    void setFrameRate(FrameRate rate);

private:
    friend struct DocumentBuilder;
    SourceText m_source;
    std::vector<Section> m_sections;
    std::uint64_t m_nextLineId = 1;
    SubtitleFormat m_format = SubtitleFormat::Ass;
    std::optional<FrameRate> m_frameRate;
};

struct LoadResult {
    Document document;
    std::vector<Diagnostic> diagnostics;
};

} // namespace hikari::core
