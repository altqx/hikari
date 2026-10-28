#pragma once

// Subtitle font identity (N4; docs/qt/fonts.md, ADR 0007). Resolution asks
// the renderer itself: the pinned libass, with the document's attachments and
// the platform provider it would use (fontconfig on Linux, DirectWrite on
// Windows), reports which bytes, collection faces, instances, fallbacks and
// simulations it actually used. A family-name lookup is never the answer.
// Authored names stay as written; requested and resolved identities are kept
// apart, and anything unresolved is reported, never counted as complete.

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace hikari::application {

struct FontAttachment {
    std::string name;
    std::shared_ptr<const std::vector<std::byte>> bytes;
};

// A document's font environment. Each document has its own, so equal
// attachment names never leak between documents.
struct FontEnvironment {
    std::uint64_t generation = 0;
    std::vector<FontAttachment> attachments;
    bool systemFonts = true;   // use the platform provider
    std::string defaultFamily; // libass's default family; empty for none
    // Y6: EXTERNAL_FONTS_DIRECTORY's files (name: the file's full path),
    // given to the renderer after the attachments. Legacy loaded them into
    // the process (AddFontResourceExW FR_PRIVATE, or the application fonts
    // of its fontconfig shim), so they render and list as installed fonts.
    std::vector<FontAttachment> externalFonts;
};

struct FontRequest {
    std::string family; // as authored
    bool bold = false;
    bool italic = false;
    std::string text;   // UTF-8; the characters that need glyphs
};

enum class SelectionStage {
    Requested,     // the requested family, or a provider substitution for it
    DefaultFamily, // the environment's default family
    Fallback,      // the provider's glyph fallback
    DefaultPath,   // a default font file
};

// How the requested name relates to the selected face's own names.
enum class NameMatch { Family, TypographicFamily, FullName, PostScriptName, None };

struct ResolvedFace {
    SelectionStage stage = SelectionStage::Requested;
    std::string requestedFamily;
    unsigned bold = 0, italic = 0; // libass's requested weight and slant
    std::uint32_t code = 0;        // the character that needed this face (0: the base face)
    std::string path;              // a file the provider named; empty for streams
    std::string attachment;        // the attachment whose bytes these are, if any
    bool embedded = false;         // loaded through the attachment provider
    std::string sha256;            // of the bytes the renderer read
    std::uint64_t size = 0;
    long faceIndex = 0;            // as FreeType opened it (named-instance bits included)
    long faceCount = 0;
    std::string postscriptName;
    std::vector<double> coords;    // design coordinates of a variable face
    std::vector<std::string> familyNames; // from the bytes: family, typographic, full, PostScript
    NameMatch nameMatch = NameMatch::None;
    bool emboldened = false;       // synthetic styles libass applied to glyphs of this face
    bool italicized = false;
};

struct RequestReport {
    FontRequest request;
    std::vector<std::size_t> faces;          // indices into FontReport::faces
    bool requestedFamilyFound = false;       // a face answering to the requested name
    bool substituted = false;                // the provider substituted a face with other names
    bool usedFallback = false;               // any face from a later stage
    std::vector<std::uint32_t> missingGlyphs;
    bool captured() const { return requestedFamilyFound && !substituted && !usedFallback && missingGlyphs.empty(); }
};

struct FontReport {
    std::uint64_t generation = 0;
    std::string provider;       // as libass reported it ("fontconfig", "directwrite", none)
    std::string libassVersion;
    std::vector<ResolvedFace> faces;
    std::vector<RequestReport> requests;
    // Complete only when every request was captured from its requested family.
    bool complete() const
    {
        for (const auto &r : requests)
            if (!r.captured())
                return false;
        return !requests.empty();
    }
};

// One face the platform provider can offer (picker metadata, not identity).
struct SystemFace {
    std::vector<std::string> families; // every family name it answers to
    std::string style;
    std::string postscriptName;
    std::string path;
    int index = 0;
    int weight = 400;
    bool italic = false;
    // Y6: the name legacy's picker listed for this face: fontconfig's first
    // family name (the Linux build's EnumFontFamiliesEx, platform.h:1133);
    // on Windows the GDI family name (DirectWrite's Win32 family name in the
    // user's language, else English, else the first).
    std::string listedFamily;
    std::string externalFile; // Y6: set for a face of an external font (its full path)
};

// The fonts a whole document actually used (I5, F47-corpus), gathered from the
// renderer's own selections across every style and inline change. Fallback-
// dependent characters are reported, not promised: a clean reimport cannot
// recreate the host's fallback resolver.
struct CollectedFont {
    std::string sha256;
    std::shared_ptr<const std::vector<std::byte>> bytes;
    std::string name;              // an attachment name, or the file name it came from
    std::string attachment;        // set when it came from the document's attachments
    std::string path;              // set when the provider named a file
    std::vector<long> faces;       // collection faces used (named-instance bits included)
    std::vector<std::string> roles; // "requested <family>", "default family", "fallback U+XXXX"
};

struct FontCollection {
    std::uint64_t generation = 0;
    std::string provider;
    std::vector<CollectedFont> fonts;
    std::vector<ResolvedFace> selections;     // every selection, in rendering order
    std::vector<std::string> missingFamilies;  // requested families the renderer never found
    // Requested families the provider answered with a face of other names (an
    // alias such as sans-serif, or its best match for an absent family).
    std::vector<std::string> substitutedFamilies;
    std::vector<std::uint32_t> missingGlyphs;  // characters no face had
    std::vector<std::uint32_t> fallbackGlyphs; // characters drawn by a fallback face
    std::vector<std::string> frameHashes;      // the rendered frames, one per sampled time
    std::vector<std::int64_t> frameTimesMs;
    bool complete() const
    {
        return missingFamilies.empty() && substitutedFamilies.empty() && missingGlyphs.empty() &&
               fallbackGlyphs.empty();
    }
};

struct ReimportCheck {
    bool identical = false;
    std::vector<std::size_t> differingFrames; // indices into FontCollection::frameTimesMs
};

enum class FontError { RendererUnavailable, InvalidInput, Cancelled };

class FontServicePort {
public:
    virtual ~FontServicePort() = default;
    virtual std::expected<FontReport, FontError> resolve(const FontEnvironment &environment,
                                                         const std::vector<FontRequest> &requests) = 0;
    virtual std::vector<SystemFace> systemFaces() = 0;
    // Renders `script` at each event's midpoint and gathers what was used.
    // `cancel`, when set during the work, ends it with Cancelled.
    virtual std::expected<FontCollection, FontError> collect(const std::vector<std::byte> &script,
                                                             const FontEnvironment &environment,
                                                             const std::atomic<bool> *cancel = nullptr) = 0;
    // Renders the same frames in a clean environment that holds only the
    // collected fonts (no system provider) and compares them.
    virtual std::expected<ReimportCheck, FontError> verifyReimport(const std::vector<std::byte> &script,
                                                                   const FontCollection &collection,
                                                                   const std::string &defaultFamily = {}) = 0;

    // Y6: the faces the font picker lists: the platform provider's (when
    // the environment uses it), then each external font's faces read from
    // its bytes, in the files' order.
    virtual std::vector<SystemFace> pickerFaces(const FontEnvironment &environment)
    {
        return environment.systemFonts ? systemFaces() : std::vector<SystemFace>();
    }
    // Y6: for each face of `faces` (from pickerFaces with `environment`),
    // whether it has a glyph for every one of `characters` (legacy
    // FontEnumerator::CheckGlyphsExists with the face selected).
    virtual std::vector<bool> facesCover(const std::vector<SystemFace> &faces, const FontEnvironment &environment,
                                         const std::u32string &characters)
    {
        (void)environment;
        (void)characters;
        return std::vector<bool>(faces.size(), true);
    }
    // Y6 (F47-refresh): forget what the provider listed, so the next
    // listing reads the installed fonts again. Renderer contexts made after
    // it see them too (each resolve, collection and render context makes
    // its own provider).
    virtual void refresh() {}
    // Y6: the folders whose changes re-list the fonts (legacy watched the
    // Windows font folders and fontconfig's directories).
    virtual std::vector<std::string> fontDirectories() { return {}; }
};

} // namespace hikari::application
