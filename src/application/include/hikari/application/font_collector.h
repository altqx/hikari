#pragma once

// Y8: the font collector (GLOBAL_OPEN_FONT_COLLECTOR; legacy FontCollector
// and FontCollectorDialog, FontCollector.cpp at 20d647c4). The Document scan,
// the found / not found blocks, the warnings and the counts follow legacy
// GetAssFonts, CheckPathAndGlyphs and CheckOrCopyFonts. What legacy decided
// through GDI (EnumFontFamiliesEx names, GetFontData bytes matched by size
// against the Fonts folders, CheckGlyphsExists) is answered by the renderer
// instead (docs/qt/fonts.md, ADR 0007): the pinned libass resolves each
// family and variant, renders the Documents and reports the bytes, faces,
// fallbacks and missing glyphs it actually used, and the collected set is
// re-rendered in a clean environment. A collection the renderer cannot
// reproduce is incomplete, never reported as success; partial output is
// written only after acknowledgment and is then labelled incomplete
// (surface-decision-routing #54, #60: staged options, review, apply).

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

// FONT_COLLECTOR_ACTION: the "Options" radio box (FontCollectorDialog,
// FontCollector.cpp:166-172).
enum class CollectorAction { Check = 0, CopyToFolder = 1, Zip = 2 };

// One tab's Document, as the collection reads it. The collector works on a
// copy, so the run never races later edits (legacy disabled every window
// while its thread ran, FontCollectorDialog::EnableControls).
struct CollectorDocument {
    int tab = 0;                          // 0-based tab index (legacy Notebook order)
    std::shared_ptr<const core::Document> document;
    bool ass = true;                      // only ASS Documents are rendered
    std::vector<std::byte> script;        // the Document as the renderer reads it (encodeAss)
};

// The scan of legacy GetAssFonts (FontCollector.cpp:572-695).
namespace collector {

// A FontLogContent: one family's block in the log.
struct Note {
    enum class Kind {
        // AppendInfo
        FoundFile,          // "Found \"%s\" font file." (a)
        RendererFile,       // a file the renderer used beyond the variants (not collected): a = path, b = role
        Copied,             // "Copied font \"%s\"." (a)
        AddedToArchive,     // "Added font \"%s\" to the archive." (a)
        // AppendWarnings
        UnusedStyle,        // "Font \"%s\" belongs to a style\nthat is not used." (+ "\nWill not be copied." when flag)
        MissingNormal,
        MissingBoldItalic,
        MissingBold,
        MissingItalic,
        CannotCheckCharacters,
        MissingCharacters,  // a = the characters
        FallbackCharacters, // a = the characters, b = the fallback file (renderer report)
        CannotGetContents,
        CannotOpenFile,     // a = file
        CannotFindInFolder,
        CannotCopy,         // a = file name
        CannotZip,          // a = file name
    };
    Kind kind = Kind::FoundFile;
    std::u16string family;
    std::u16string a, b;
    bool flag = false;
};

struct Block {
    // "Found font \"%s\"\n" (GetAssFonts), "Found font \"%s\"." (CheckPathAndGlyphs)
    // or "Font not found \"%s\".\n".
    enum class Header { Found, FoundDot, NotFound };
    Header header = Header::Found;
    std::u16string family;
    bool notFound = false;
    std::map<std::u16string, std::vector<int>> styles; // Style -> tabs (0-based)
    std::map<int, std::vector<int>> lines;             // Line index (0-based) -> tabs
    std::vector<Note> infos;
    std::vector<Note> warnings;
};

// SubsFont: a family with the weight and slant one use asked for.
struct Variant {
    std::u16string name; // as authored (the first use of this lower-cased key)
    int bold = 400;      // 1 -> 700, 0 -> 400, other values as written
    bool italic = false;
};
// The foundFonts key: fn.Lower() << bold << italic, with the values as the
// Style or the \b and \i tags gave them (FontCollector.cpp:584, 641).
std::u16string variantKey(const std::u16string &family, int bold, int italic);

struct Scan {
    std::map<std::u16string, Block> found;    // findFontsLog
    std::map<std::u16string, Block> notFound; // notFindFontsLog
    std::map<std::u16string, Variant> variants; // foundFonts, keyed fn.Lower() << bold << italic
    std::map<std::u16string, std::set<char32_t>> chars; // FontMap
    std::vector<std::u16string> families;     // every family named, in first-use order
};

// GetAssFonts for every Document in order. `found` answers legacy's
// facenames.Index(fn, false) (here: the renderer found the family).
Scan scan(const std::vector<CollectorDocument> &documents, const std::function<bool(const std::u16string &)> &found);

// Legacy ParseTags(tags {"fn","b","i","p"}, 4, plainText = true)
// (SubsDialogue.cpp:1086-1158): the tags and the plain-text runs in order.
struct Tag {
    std::u32string name; // fn, b, i, p, "plain" or "pvector"
    std::u32string value;
};
std::vector<Tag> parseTags(const std::u32string &text);

} // namespace collector

// One file the collection writes (copy or zip), in legacy order.
struct CollectedFile {
    std::u16string family;   // the block it is logged in
    std::u16string shown;    // what "Found \"%s\" font file." names (path, or name)
    std::u16string name;     // file name written (HikariPathName)
    std::shared_ptr<const std::vector<std::byte>> bytes; // null: nothing to read (legacy's failing copy)
    std::string sha256;
    std::vector<long> faces; // collection faces the renderer opened
};

// A file the renderer used for the Documents that no variant asked for: a
// fallback, a substitute for a family not found, the default family. It is
// reported with its role, never collected as the requested font (fonts.md).
struct RendererFile {
    std::u16string family;
    std::u16string shown;
    std::string sha256;
    std::vector<long> faces;
    std::vector<std::string> roles; // "fallback U+XXXX", "substitute for <family>", "default family"
};

// The staged review: what Start found, before anything is written.
struct CollectorReview {
    CollectorAction action = CollectorAction::Check;
    std::optional<std::size_t> retrievedFonts; // "Retrieved sizes and names of %i fonts"
    std::int64_t retrieveMs = 0;               // "..., elapsed time %sms."
    // "Cannot retrieve the font file sizes and names;\ncopying will be
    // canceled." (FontCollector.cpp:726-728): nothing else is done.
    bool retrieveFailed = false;
    std::map<std::u16string, collector::Block> found;
    std::map<std::u16string, collector::Block> notFound;
    int foundCount = 0;     // CheckPathAndGlyphs' found (check mode: families)
    int notFoundCount = 0;  // after the unused-style adjustment of CheckOrCopyFonts
    bool allGlyphs = true;
    std::vector<CollectedFile> files;
    std::vector<RendererFile> rendererFiles;

    // The renderer's verification.
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

    // The renderer drew every Document with every family captured and no
    // glyph missing or from a fallback, and (copy modes) reproduced every
    // frame from the collected set alone.
    bool rendererComplete() const;
    // Legacy's "Completed Successfully" condition (no font missing, every
    // glyph present) and the renderer's agreement.
    bool complete() const { return notFoundCount == 0 && allGlyphs && rendererComplete(); }
};

// The output of Apply (CopyToFolder / Zip). Implemented over the file system
// in the backends.
class CollectorOutput {
public:
    virtual ~CollectorOutput() = default;
    // MakeDirectory (and CreateZip). False: nothing was written.
    virtual bool open() = 0;
    // False when the folder could not be created (legacy "Cannot create folder.").
    virtual bool folderFailed() const = 0;
    // SaveFont: one font under its file name.
    virtual bool put(const std::u16string &name, const std::vector<std::byte> &bytes) = 0;
    // The incomplete label (a text file in the folder, an entry in the archive).
    virtual bool label(const std::string &utf8Text) = 0;
    virtual bool unlabel() = 0;
    // CloseZip: the output is published. For a folder, nothing to do.
    virtual bool commit() = 0;
    // Cancelled before commit: what was not yet published is removed.
    virtual void discard() = 0;
};

struct CollectorResult {
    std::map<std::u16string, collector::Block> found;
    std::map<std::u16string, collector::Block> notFound;
    int foundCount = 0;
    int notFoundCount = 0;
    int notCopiedCount = 0;
    bool allGlyphs = true;
    bool pathNotAvailable = false; // "Path is not available"
    bool cannotCreateFolder = false;
    bool cancelled = false;
    bool refused = false;          // incomplete and not acknowledged: nothing was written
    bool written = false;          // anything was published
    bool labelled = false;         // the output carries the incomplete label
    bool complete = false;         // legacy success and the renderer's agreement
};

class FontCollector {
public:
    explicit FontCollector(FontServicePort &fonts) : m_fonts(fonts) {}

    // Start: the scan, the renderer's verification and, for the copy modes,
    // the files to write. The copy modes list the provider's font files at
    // the first run (legacy fontSizes, kept for the collector's lifetime).
    // `cancel` ends it with Cancelled.
    std::expected<CollectorReview, FontError> prepare(const std::vector<CollectorDocument> &documents,
                                                      CollectorAction action, const std::atomic<bool> *cancel = nullptr);

    // Apply for the copy modes; for Check the review is the result. An
    // incomplete review writes only with `acknowledgedIncomplete`, and its
    // output then carries the incomplete label (surface-decision-routing
    // #54); otherwise nothing is written and the result is `refused`.
    CollectorResult apply(const CollectorReview &review, CollectorOutput &output, bool acknowledgedIncomplete,
                          const std::atomic<bool> *cancel = nullptr);
    CollectorResult result(const CollectorReview &review) const; // Check

    // The text of the incomplete label for a review (and its write result).
    static std::string labelText(const CollectorReview &review, const CollectorResult *result);

private:
    FontServicePort &m_fonts;
    bool m_retrieved = false; // legacy fontSizes, kept for the collector's lifetime
};

std::u16string toU16(const std::u8string &text);
std::string toUtf8(const std::u16string &text);

} // namespace hikari::application
