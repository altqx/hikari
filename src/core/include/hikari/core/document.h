#pragma once

// Source-preserving subtitle document (docs/qt/document-model.md). Sections and
// records keep authored order; every record links to its exact source bytes.
// Records are semantic views over that source, never a competing copy of it.

#include "hikari/core/time.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
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

struct LineRecord {
    LineId id;
    bool comment = false;   // ASS Comment: rather than Dialogue:
    IntField layer;
    TimeField start;
    TimeField end;
    std::u8string style;
    std::u8string actor;    // as authored after trimming; marker syntax not interpreted here
    IntField marginLeft;
    IntField marginRight;
    IntField marginVertical;
    std::u8string effect;
    std::u8string text;     // legacy trims surrounding whitespace; raw bytes stay in the span
    SourceSpan span;        // where the record came from; its terminator is reused on save
    bool edited = false;    // the span no longer describes the record; regenerate on save
    std::optional<std::u8string> cueNumber; // SRT: the authored cue number line, if any
};

struct StyleRecord {
    std::u8string name;
    std::vector<std::u8string> fields; // positional fields after "Style:", untrimmed
    SourceSpan span;
};

struct PropertyRecord {
    std::u8string key;   // trimmed text before the first ':'
    std::u8string value; // trimmed text after it
    SourceSpan span;
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
        MalformedEvent,      // Dialogue/Comment with fewer than 9 comma fields; kept opaque
        RepeatedSection,     // a section kind seen again; both kept in order
        UnknownSection,      // header not recognized; kept untouched
        SsaStyles,           // [V4 Styles] present; conversion is a separate operation
    };
    Severity severity = Severity::Info;
    Kind kind = Kind::InvalidUtf8;
    std::size_t byteOffset = 0;
    std::u8string excerpt;
};

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

private:
    friend struct DocumentBuilder;
    SourceText m_source;
    std::vector<Section> m_sections;
    std::uint64_t m_nextLineId = 1;
};

struct LoadResult {
    Document document;
    std::vector<Diagnostic> diagnostics;
};

} // namespace hikari::core
