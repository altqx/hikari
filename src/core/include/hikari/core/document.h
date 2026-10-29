#pragma once

// Records keep authored order and link to their exact source bytes.

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
    std::vector<std::byte> bytes; // immutable original input
    TextEncoding encoding = TextEncoding::Utf8;
};

// Stable within a Document's lifetime; not a row number, never serialized.
struct LineId {
    std::uint64_t value = 0;
    auto operator<=>(const LineId &) const = default;
};

// The value is read positionally; the lexeme is kept.
struct TimeField {
    std::u8string lexeme;
    DocumentTime value;
};

// The value is read like C atoi.
struct IntField {
    std::u8string lexeme;
    std::int64_t value = 0;
};

// Actor-field markers: on load only the first marker kind found is removed.
enum class LineVisibility { Visible, Hidden, VisibleBlock };
enum class GroupMarker { None, Description, Opened, Closed };

struct LineRecord {
    LineId id;
    bool comment = false;   // ASS Comment: rather than Dialogue:
    // Not readable as an ASS event: the whole line is the text, zero times, Default style.
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
    std::u8string text;     // trimmed; raw bytes stay in the span
    SourceSpan span;        // where the record came from; its terminator is reused on save
    bool edited = false;    // the span no longer describes the record; regenerate on save
    // No source bytes (an empty span at its position); saved with the previous record's newline.
    bool inserted = false;
    std::optional<std::u8string> cueNumber; // SRT: the authored cue number line, if any
    // MicroDVD: start/end stay zero until the Document's frame rate is set.
    std::optional<std::int64_t> startFrame;
    std::optional<std::int64_t> endFrame;
    // TLMode pair: the original line ("TLMode Style") and its translation read as one Line; text is
    // the original's, other fields the translation line's. span covers both, originalSpan the original.
    std::optional<SourceSpan> originalSpan;
    std::u8string translation; // empty: untranslated
    bool unconfirmed = false;  // written as the effect "\fD"
    // Unique per changed copy (0: unchanged since loading), so a save marks exactly the saved copies.
    std::uint64_t changeVersion = 0;
    // The karaoke Line a templater run generated this one from; 0: none. Never saved.
    LineId generatedFrom;
};

// Changed gives the Line a new changeVersion; Kept leaves it.
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
    std::u8string key;   // trimmed text before the first ':' (in [Hikari], the first '=')
    std::u8string value; // trimmed text after it
    SourceSpan span;
    bool edited = false;   // written as "key: value"
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
    Unknown,    // retained untouched
    Hikari,     // [Hikari]: HikariSub's own "key=value" lines
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
        MalformedTime,       // non-canonical time lexeme; positional value used
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

// Values match HikariSub legacy's format codes.
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
    // Without touching its fields or source bytes. False when no Line has this id.
    bool markLineChanged(LineId id);
    // Changes any fields of one Line; the Line is then regenerated on save.
    // Every mutation marks the Line changed unless `mark` is Kept.
    bool editLine(LineId id, const std::function<void(LineRecord &)> &change, ChangeMark mark = ChangeMark::Changed);
    // Inserts a new Line right after `after`, in the same section. Returns its
    // id, or nullopt when `after` is unknown. Kept: the record's own changeVersion stays.
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

    // The last value for key.
    std::optional<std::u8string> scriptInfo(std::u8string_view key) const;
    // Replaces the key's (last) value, or adds it after the last property. An ASS Document without
    // Script Info gets one at the top; false for other formats.
    bool setScriptInfo(std::u8string_view key, std::u8string value);
    // False when absent.
    bool removeScriptInfo(std::u8string_view key);
    // After the last Style; `fields` positional, name first. False without a Styles section.
    bool appendStyle(std::vector<std::u8string> fields);
    // The first Style of that name.
    bool removeStyle(std::u8string_view name);
    // `index` in document order (as decodeStyles lists them); `fields` positional, name first.
    // False when there is no such Style.
    bool editStyle(std::size_t index, std::vector<std::u8string> fields);
    // The Styles become `slots` in order. A slot with `from` (document order) keeps that Style's bytes
    // unless `fields` are given; without `from` it is new. Slots fill existing positions, extras go
    // after the last Style, Styles beyond the slots are removed. False without a Styles section or
    // for an unknown `from`.
    struct StyleSlot {
        std::optional<std::size_t> from;
        std::optional<std::vector<std::u8string>> fields;
    };
    bool rearrangeStyles(const std::vector<StyleSlot> &slots);
    // As rearrangeStyles, for Script Info properties (`from` counts across sections; extras go after
    // the last section's last property). Comments and blank lines stay. False without a Script Info
    // section or for an unknown `from`.
    struct PropertySlot {
        std::optional<std::size_t> from;
        std::optional<std::pair<std::u8string, std::u8string>> property;
    };
    bool rearrangeScriptInfo(const std::vector<PropertySlot> &slots);

    // [Hikari] lines are "key=value": without ':', loaders that read any line with one as Script
    // Info pass over them. The last value for key, valid until the Document changes.
    std::optional<std::u8string_view> hikariProperty(std::u8string_view key) const;
    // Replaces the key's (last) value or adds it to the last [Hikari] section. A new section goes
    // before trailing [Fonts]/[Graphics] (libass reads any line after [Fonts] as font data). False
    // for formats other than ASS.
    bool setHikariProperty(std::u8string_view key, std::u8string value);

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

}
