#pragma once

#include "hikari/application/font_service.h"
#include "hikari/core/document.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace hikari::application {

// FONT_COLLECTOR_ACTION's values.
enum class CollectorAction { Check = 0, CopyToFolder = 1, Zip = 2 };

// A copy, so a run never races later edits.
struct CollectorDocument {
    int tab = 0; // 0-based
    std::shared_ptr<const core::Document> document;
    bool ass = true;               // only ASS Documents are rendered
    std::vector<std::byte> script; // encodeAss of the Document
};

namespace collector {

struct Note {
    enum class Kind {
        FoundFile,      // a = shown file
        RendererFile,   // not collected: a = path, b = role
        Copied,         // a = font
        AddedToArchive, // a = font
        UnusedStyle,    // flag: will not be copied
        MissingNormal,
        MissingBoldItalic,
        MissingBold,
        MissingItalic,
        CannotCheckCharacters,
        MissingCharacters,  // a = the characters
        FallbackCharacters, // a = the characters, b = the fallback file
        CannotGetContents,
        CannotOpenFile, // a = file
        CannotFindInFolder,
        CannotCopy, // a = file name
        CannotZip,  // a = file name
    };
    Kind kind = Kind::FoundFile;
    std::u16string family;
    std::u16string a, b;
    bool flag = false;
};

struct Block {
    // Found ends the header with a newline, FoundDot with "."
    enum class Header { Found, FoundDot, NotFound };
    Header header = Header::Found;
    std::u16string family;
    bool notFound = false;
    std::map<std::u16string, std::vector<int>> styles; // Style -> tabs (0-based)
    std::map<int, std::vector<int>> lines;             // Line index (0-based) -> tabs
    std::vector<Note> infos;
    std::vector<Note> warnings;
};

struct Variant {
    std::u16string name; // as first authored
    int bold = 400;      // 1 -> 700, 0 -> 400, other values as written
    bool italic = false;
};
// Lower-cased family, then bold and italic as written.
std::u16string variantKey(const std::u16string &family, int bold, int italic);

struct Scan {
    std::map<std::u16string, Block> found;
    std::map<std::u16string, Block> notFound;
    std::map<std::u16string, Variant> variants; // keyed by variantKey
    std::map<std::u16string, std::set<char32_t>> chars;
    std::vector<std::u16string> families; // in first-use order
};

// `found`: the renderer found the family.
Scan scan(const std::vector<CollectorDocument> &documents, const std::function<bool(const std::u16string &)> &found);

// The asked-for tags (fn, b, i, p by default) and the plain-text runs, in order.
struct Tag {
    std::u32string name; // fn, b, i, p, "plain" or "pvector"
    std::u32string value;
};
std::vector<Tag> parseTags(const std::u32string &text);
std::vector<Tag> parseTags(const std::u32string &text, const std::vector<std::u32string> &names);

} // namespace collector

struct CollectedFile {
    std::u16string family; // the block it is logged in
    std::u16string shown;  // path or name, as logged
    std::u16string name;
    std::shared_ptr<const std::vector<std::byte>> bytes; // null: nothing to read
    std::string sha256;
    std::vector<long> faces; // collection faces the renderer opened
};

// A file the renderer used that no variant asked for; reported with its role, never collected.
struct RendererFile {
    std::u16string family;
    std::u16string shown;
    std::string sha256;
    std::vector<long> faces;
    std::vector<std::string> roles; // "fallback U+XXXX", "substitute for <family>", "default family"
};

struct CollectorReview {
    CollectorAction action = CollectorAction::Check;
    std::optional<std::size_t> retrievedFonts;
    std::int64_t retrieveMs = 0;
    // nothing else is done
    bool retrieveFailed = false;
    std::map<std::u16string, collector::Block> found;
    std::map<std::u16string, collector::Block> notFound;
    int foundCount = 0;    // in check mode, families
    int notFoundCount = 0; // after the unused-style adjustment
    bool allGlyphs = true;
    std::vector<CollectedFile> files;
    std::vector<RendererFile> rendererFiles;

    std::string provider;
    std::vector<std::string> missingFamilies;
    std::vector<std::string> substitutedFamilies;
    std::vector<std::uint32_t> missingGlyphs;
    std::vector<std::uint32_t> fallbackGlyphs;
    struct Reimport {
        int tab = 0;
        bool identical = false;
        std::size_t frames = 0;
        std::vector<std::size_t> differingFrames;
    };
    std::vector<Reimport> reimports;
    // Tabs whose Document the renderer could not read: nothing about them is verified.
    std::vector<int> unrendered;
    // File names two collected fonts share ignoring case, as Windows compares them; the output
    // then differs from the set the renderer verified.
    std::vector<std::u16string> nameClashes;

    // Every Document read and drawn with every family captured, no glyph missing or from a
    // fallback, and (copy modes) every frame reproduced from the collected set alone.
    bool rendererComplete() const;
    bool complete() const { return notFoundCount == 0 && allGlyphs && rendererComplete() && nameClashes.empty(); }
};

class CollectorOutput {
public:
    virtual ~CollectorOutput() = default;
    // False: nothing was written.
    virtual bool open() = 0;
    virtual bool folderFailed() const = 0;
    virtual bool put(const std::u16string &name, const std::vector<std::byte> &bytes) = 0;
    // A text file in a folder, an entry in an archive.
    virtual bool label(const std::string &utf8Text) = 0;
    virtual bool unlabel() = 0;
    // Publishes the output; a no-op for a folder.
    virtual bool commit() = 0;
    // Before commit: removes what was not yet published.
    virtual void discard() = 0;
};

struct CollectorResult {
    std::map<std::u16string, collector::Block> found;
    std::map<std::u16string, collector::Block> notFound;
    int foundCount = 0;
    int notFoundCount = 0;
    int notCopiedCount = 0;
    bool allGlyphs = true;
    bool pathNotAvailable = false;
    bool cannotCreateFolder = false;
    bool cancelled = false;
    bool refused = false; // incomplete and not acknowledged: nothing was written
    bool written = false; // anything was published
    bool labelled = false;
    bool complete = false;
};

class FontCollector {
public:
    // Reads the other half of a Type 1 pair (.pfb/.pfm); null when it cannot be read.
    using FileReader = std::function<std::shared_ptr<const std::vector<std::byte>>(const std::u16string &path)>;
    explicit FontCollector(FontServicePort &fonts, FileReader read = {}) : m_fonts(fonts), m_read(std::move(read)) {}

    // The copy modes list the provider's font files on the first run only. `cancel` ends it
    // with Cancelled.
    std::expected<CollectorReview, FontError> prepare(const std::vector<CollectorDocument> &documents,
                                                      CollectorAction action, const std::atomic<bool> *cancel = nullptr);

    // For the copy modes. An incomplete review writes only with `acknowledgedIncomplete`, and
    // then labels the output; otherwise nothing is written and the result is `refused`.
    CollectorResult apply(const CollectorReview &review, CollectorOutput &output, bool acknowledgedIncomplete,
                          const std::atomic<bool> *cancel = nullptr);
    CollectorResult result(const CollectorReview &review) const; // for Check

    static std::string labelText(const CollectorReview &review, const CollectorResult *result);

private:
    FontServicePort &m_fonts;
    FileReader m_read;
    bool m_retrieved = false;
};

std::u16string toU16(const std::u8string &text);
std::string toUtf8(const std::u16string &text);

} // namespace hikari::application
