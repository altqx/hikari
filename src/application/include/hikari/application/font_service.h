#pragma once

// Subtitle font identity (N4; docs/qt/fonts.md, ADR 0007). Resolution asks
// the renderer itself: the pinned libass, with the document's attachments and
// the platform provider it would use (fontconfig on Linux, DirectWrite on
// Windows), reports which bytes, collection faces, instances, fallbacks and
// simulations it actually used. A family-name lookup is never the answer.
// Authored names stay as written; requested and resolved identities are kept
// apart, and anything unresolved is reported, never counted as complete.

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
    bool requestedFamilyFound = false;       // a Requested-stage base face
    bool usedFallback = false;               // any face from a later stage
    std::vector<std::uint32_t> missingGlyphs;
    bool captured() const { return requestedFamilyFound && !usedFallback && missingGlyphs.empty(); }
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
};

enum class FontError { RendererUnavailable, InvalidInput };

class FontServicePort {
public:
    virtual ~FontServicePort() = default;
    virtual std::expected<FontReport, FontError> resolve(const FontEnvironment &environment,
                                                         const std::vector<FontRequest> &requests) = 0;
    virtual std::vector<SystemFace> systemFaces() = 0;
};

} // namespace hikari::application
