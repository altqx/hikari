#pragma once

// A FontServicePort that answers like the renderer from a table of faces
// (Y8 tests): the requested family's nearest face, a fallback face for the
// characters it lacks, or a missing glyph. Whole Documents report the
// configured `document` collection.

#include "hikari/application/font_service.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace hikari::testing {

using namespace hikari::application;

inline std::vector<std::byte> bytes(const std::string &s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

// A face the fake renderer can select.
struct Face {
    std::string family;
    int weight = 400;
    bool italic = false;
    std::string path;
    std::u32string glyphs; // empty: every character
};

// Answers like the renderer: the requested family's nearest face, a
// fallback face for characters it lacks, or a missing glyph.
class FakeFonts final : public FontServicePort {
public:
    std::vector<Face> faces;
    std::optional<Face> fallback;
    FontCollection document;          // what a whole Document reports
    bool reimportIdentical = true;
    bool documentUnreadable = false;  // a whole Document fails (ass_read_memory)
    int collects = 0;
    std::vector<std::vector<std::string>> reimported; // the font names of each verifyReimport

    std::expected<FontReport, FontError> resolve(const FontEnvironment &, const std::vector<FontRequest> &) override
    {
        return std::unexpected(FontError::RendererUnavailable);
    }
    std::vector<SystemFace> systemFaces() override
    {
        std::vector<SystemFace> out;
        for (const auto &f : faces)
            out.push_back({{f.family}, "", f.family + "-PS", f.path, 0, f.weight, f.italic});
        if (fallback)
            out.push_back({{fallback->family}, "", "", fallback->path, 0, 400, false});
        return out;
    }
    static std::shared_ptr<const std::vector<std::byte>> fontBytes(const std::string &path)
    {
        return std::make_shared<std::vector<std::byte>>(bytes("font:" + path));
    }
    static void add(FontCollection &c, const Face &f, SelectionStage stage, std::uint32_t code, const std::string &request,
                    NameMatch match)
    {
        ResolvedFace r;
        r.stage = stage;
        r.requestedFamily = request;
        r.code = code;
        r.path = f.path;
        r.sha256 = "sha-" + f.path;
        r.nameMatch = match;
        r.postscriptName = f.family + "-PS";
        c.selections.push_back(r);
        if (std::none_of(c.fonts.begin(), c.fonts.end(), [&](const CollectedFont &x) { return x.sha256 == r.sha256; })) {
            CollectedFont font;
            font.sha256 = r.sha256;
            font.bytes = fontBytes(f.path);
            font.name = f.path.substr(f.path.find_last_of('/') + 1);
            font.path = f.path;
            font.faces = {0};
            font.roles = {stage == SelectionStage::Fallback ? "fallback" : "requested " + request};
            c.fonts.push_back(font);
        }
    }
    std::expected<FontCollection, FontError> collect(const std::vector<std::byte> &s, const FontEnvironment &env,
                                                     const std::atomic<bool> *cancel) override
    {
        ++collects;
        if (cancel && cancel->load())
            return std::unexpected(FontError::Cancelled);
        if (!env.systemFonts)
            return std::unexpected(FontError::InvalidInput);
        const auto doc = core::loadAss(s).document;
        const auto styles = core::decodeStyles(doc);
        if (styles.size() != 1 || styles[0].name != u8"P") {
            if (documentUnreadable)
                return std::unexpected(FontError::InvalidInput);
            FontCollection c = document;
            c.frameHashes = {"frame"};
            return c;
        }
        // A probe: one Style, one Line.
        const std::string family(styles[0].fontname.begin(), styles[0].fontname.end());
        const int weight = styles[0].bold ? 700 : 400;
        const bool italic = styles[0].italic;
        std::string lowerFamily = family;
        std::transform(lowerFamily.begin(), lowerFamily.end(), lowerFamily.begin(), ::tolower);
        const Face *best = nullptr;
        for (const auto &f : faces) {
            std::string lf = f.family;
            std::transform(lf.begin(), lf.end(), lf.begin(), ::tolower);
            if (lf != lowerFamily)
                continue;
            if (!best || (f.weight == weight && f.italic == italic) ||
                (!(best->weight == weight && best->italic == italic) && f.weight == 400 && !f.italic))
                best = &f;
        }
        FontCollection c;
        c.provider = "fake";
        c.frameHashes = {"probe"};
        if (!best) {
            c.missingFamilies.push_back(family);
            return c;
        }
        add(c, *best, SelectionStage::Requested, 0, family, NameMatch::Family);
        const std::u16string text16 = core::toUtf16(doc.lines()[0]->text);
        for (const char16_t ch : text16) {
            if (best->glyphs.empty() || best->glyphs.find(char32_t(ch)) != std::u32string::npos)
                continue;
            if (fallback && fallback->glyphs.find(char32_t(ch)) != std::u32string::npos) {
                add(c, *fallback, SelectionStage::Fallback, ch, family, NameMatch::None);
                c.fallbackGlyphs.push_back(ch);
            } else {
                c.missingGlyphs.push_back(ch);
            }
        }
        return c;
    }
    std::expected<ReimportCheck, FontError> verifyReimport(const std::vector<std::byte> &, const FontCollection &c,
                                                           const std::string &) override
    {
        std::vector<std::string> names;
        for (const auto &f : c.fonts)
            names.push_back(f.name);
        reimported.push_back(names);
        ReimportCheck r;
        r.identical = reimportIdentical;
        if (!reimportIdentical)
            r.differingFrames = {0};
        return r;
    }
};

} // namespace hikari::testing
