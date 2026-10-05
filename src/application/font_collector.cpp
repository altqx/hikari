#include "hikari/application/font_collector.h"

#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <functional>

namespace hikari::application {

using collector::Block;
using collector::Note;
using collector::Scan;
using collector::Tag;
using collector::Variant;

std::u16string toU16(const std::u8string &text)
{
    return core::toUtf16(text);
}

std::string toUtf8(const std::u16string &text)
{
    const std::u8string u = core::toUtf8(text);
    return std::string(u.begin(), u.end());
}

namespace {

using u16 = std::u16string;
using u32 = std::u32string;

u32 toU32(const u16 &text)
{
    u32 out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char32_t c = text[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            ++i;
        }
        out += c;
    }
    return out;
}

u16 fromU32(const u32 &text)
{
    u16 out;
    for (char32_t c : text) {
        if (c >= 0x10000) {
            c -= 0x10000;
            out += char16_t(0xD800 + (c >> 10));
            out += char16_t(0xDC00 + (c & 0x3FF));
        } else {
            out += char16_t(c);
        }
    }
    return out;
}

std::string utf8Of(const u32 &text)
{
    return toUtf8(fromU32(text));
}

u16 number(long long v)
{
    const std::string s = std::to_string(v);
    return u16(s.begin(), s.end());
}

// wxString::Lower (towlower per character).
u16 lower(const u16 &text)
{
    u16 out = text;
    for (auto &c : out)
        c = char16_t(std::towlower(wint_t(c)));
    return out;
}

bool isDigit(char32_t c)
{
    return c >= U'0' && c <= U'9';
}

// wxString::ToCDouble: the whole value as a C-locale number.
bool toCDouble(const u32 &text)
{
    std::string ascii;
    for (const char32_t c : text) {
        if (c > 0x7F)
            return false;
        ascii += char(c);
    }
    if (ascii.empty())
        return false;
    char *end = nullptr;
    std::strtod(ascii.c_str(), &end);
    return end != ascii.c_str() && *end == '\0';
}

// wxAtoi.
int atoi32(const u32 &text)
{
    std::string ascii;
    for (const char32_t c : text)
        ascii += c < 0x80 ? char(c) : '?';
    return std::atoi(ascii.c_str());
}

u32 trimmed(const u32 &value)
{
    const auto isSpace = [](char32_t c) { return c == U' ' || c == U'\t' || c == U'\r' || c == U'\n'; };
    std::size_t b = 0, e = value.size();
    while (b < e && isSpace(value[b]))
        ++b;
    while (e > b && isSpace(value[e - 1]))
        --e;
    return value.substr(b, e - b);
}

void replaceAll(u32 &text, const u32 &from, const u32 &to)
{
    for (std::size_t pos = text.find(from); pos != u32::npos; pos = text.find(from, pos + to.size()))
        text.replace(pos, from.size(), to);
}

Block &blockFor(std::map<u16, Block> &map, const u16 &family, Block::Header header)
{
    auto it = map.find(family);
    if (it == map.end()) {
        Block b;
        b.header = header;
        b.family = family;
        b.notFound = header == Block::Header::NotFound;
        it = map.emplace(family, std::move(b)).first;
    }
    return it->second;
}

} // namespace

namespace collector {

std::u16string variantKey(const std::u16string &family, int bold, int italic)
{
    return lower(family) + number(bold) + number(italic);
}

std::vector<Tag> parseTags(const std::u32string &txt)
{
    // SubsDialogue.cpp:1086-1158, with tags {"fn", "b", "i", "p"} and plainText.
    static const std::vector<u32> names{U"fn", U"b", U"i", U"p"};
    return parseTags(txt, names);
}

std::vector<Tag> parseTags(const std::u32string &txt, const std::vector<std::u32string> &names)
{
    std::vector<Tag> out;
    std::size_t pos = 0, plainStart = 0;
    bool hasDrawing = false, tagsBlock = false;
    const std::size_t len = txt.size();
    if (len < 1)
        return out;
    while (pos < len) {
        const char32_t ch = txt[pos];
        if (ch == U'}') {
            tagsBlock = false;
            plainStart = pos + 1;
        } else if (ch == U'{' || pos >= len - 1) {
            tagsBlock = true;
            if (pos >= len - 1)
                ++pos;
            // SubString(plainStart, pos - 1)
            if (plainStart + 1 <= pos)
                out.push_back({hasDrawing ? U"pvector" : U"plain", txt.substr(plainStart, pos - plainStart)});
        } else if (tagsBlock && ch == U'\\') {
            ++pos;
            const std::size_t slash = txt.find(U'\\', pos), bracket = txt.find(U'}', pos);
            const std::size_t tagEnd = slash == u32::npos && bracket == u32::npos ? len
                                       : slash == u32::npos                     ? bracket
                                       : bracket == u32::npos                   ? slash
                                       : bracket < slash                        ? bracket
                                                                                : slash;
            u32 tag = tagEnd > pos ? txt.substr(pos, tagEnd - pos) : u32();
            if (!tag.empty() && tag.back() == U')')
                tag.pop_back();
            for (const u32 &name : names) {
                const std::size_t tagLen = name.size();
                if (tag.size() <= tagLen || tag.compare(0, tagLen, name) != 0)
                    continue;
                const char32_t first = tag[tagLen];
                if (!(first == U'(' || isDigit(first) || name == U"fn" || first == U'.' || first == U'-' ||
                      first == U'+'))
                    continue;
                u32 value = tag.substr(tagLen);
                if (name == U"p") {
                    value = trimmed(value); // Trim().Trim(false) changes the value too
                    hasDrawing = value != U"0";
                } else if (first == U'(') {
                    value = value.substr(value.find(U'(') + 1);
                    value = value.substr(0, value.find(U')'));
                } else if (name != U"fn" && !toCDouble(value)) {
                    u32 digits;
                    for (const char32_t c : value) {
                        if (!isDigit(c) && c != U'.' && c != U'-' && c != U'+')
                            break;
                        digits += c;
                    }
                    value = digits;
                }
                out.push_back({name, value});
                pos = tagEnd - 1;
                break;
            }
        }
        ++pos;
    }
    return out;
}

Scan scan(const std::vector<CollectorDocument> &documents, const std::function<bool(const std::u16string &)> &found)
{
    Scan s;
    auto named = [&](const u16 &family) {
        if (std::find(s.families.begin(), s.families.end(), family) == s.families.end())
            s.families.push_back(family);
    };
    // SubsFont (FontCollector.cpp:45-54): 1 -> 700, 0 -> 400; the first use
    // of a key names it.
    auto variant = [&](const u16 &fn, int bold, int italic) {
        const u16 key = variantKey(fn, bold, italic);
        if (!s.variants.contains(key))
            s.variants[key] = Variant{fn, bold == 1 ? 700 : bold == 0 ? 400 : bold, italic != 0};
    };
    for (const auto &doc : documents) {
        if (!doc.document)
            continue;
        const int tab = doc.tab;
        // The Style table (FontCollector.cpp:578-609).
        std::map<u16, core::StyleValues> styles;
        for (const auto &style : core::decodeStyles(*doc.document)) {
            const u16 fn = toU16(style.fontname);
            named(fn);
            const u16 name = toU16(style.name);
            if (!found(fn)) {
                blockFor(s.notFound, fn, Block::Header::NotFound).styles[name].push_back(tab);
            } else {
                variant(fn, style.bold ? 1 : 0, style.italic ? 1 : 0);
                blockFor(s.found, fn, Block::Header::Found).styles[name].push_back(tab);
            }
            styles[name] = style;
        }
        // The Lines (FontCollector.cpp:613-693).
        const auto lines = doc.document->lines();
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto &line = *lines[i];
            if (line.comment)
                continue;
            // GetTextNoCopy: the translation when there is one.
            const u32 text = toU32(toU16(line.translation.empty() ? line.text : line.translation));
            const auto tags = parseTags(text);
            const auto st = styles.find(toU16(line.style));
            const core::StyleValues *lstyle = st == styles.end() ? nullptr : &st->second;
            u16 ifont = lstyle ? toU16(lstyle->fontname) : u16();
            int bold = lstyle ? int(lstyle->bold) : 0;
            int italic = lstyle ? int(lstyle->italic) : 0;
            bool newFont = false, lastPlain = false;
            u32 textHavingFont;
            const std::size_t tagsSize = tags.size();
            for (std::size_t j = 0; j < tagsSize; ++j) {
                const Tag &tag = tags[j];
                if (tag.name == U"p" || tag.name == U"pvector")
                    continue;
                if (tag.name == U"plain") {
                    textHavingFont += tag.value;
                    if ((lastPlain && j < tagsSize - 1) || ifont.empty())
                        continue;
                    lastPlain = true;
                    if (!found(ifont)) {
                        Block &nflc = blockFor(s.notFound, ifont, Block::Header::NotFound);
                        if (newFont)
                            nflc.lines[int(i)].push_back(tab);
                    } else {
                        variant(ifont, bold, italic);
                        if (newFont) {
                            blockFor(s.found, ifont, Block::Header::Found).lines[int(i)].push_back(tab);
                            newFont = false;
                        }
                    }
                    // Every text is kept, so a font that is not found is
                    // known to be needed.
                    replaceAll(textHavingFont, U"\\N", U"");
                    replaceAll(textHavingFont, U"\\n", U"");
                    replaceAll(textHavingFont, U"\\h", U" ");
                    auto &chars = s.chars[ifont];
                    for (const char32_t c : textHavingFont)
                        chars.insert(c);
                    textHavingFont.clear();
                } else {
                    if (tag.name == U"fn") {
                        ifont = fromU32(tag.value);
                        named(ifont);
                        newFont = true;
                    } else if (tag.name == U"b") {
                        bold = atoi32(tag.value);
                    } else if (tag.name == U"i") {
                        italic = atoi32(tag.value);
                    }
                    lastPlain = false;
                }
            }
        }
    }
    return s;
}

} // namespace collector

namespace {

constexpr char32_t kByteOrderMark = 65279; // not checked (CheckPathAndGlyphs)

// A one-Line script that renders `text` in the family, as the probe of one
// legacy SubsFont.
std::vector<std::byte> probeScript(const u16 &family, bool bold, bool italic, const std::set<char32_t> &chars)
{
    u32 text;
    for (const char32_t c : chars)
        if (c != kByteOrderMark && c != U'{' && c != U'}' && c != U'\\' && c != U'\n' && c != U'\r')
            text += c;
    if (text.empty())
        text = U"a";
    // A family the Style line cannot carry (a \fn value with a comma) is
    // asked for as the Document asks for it, by an override tag before the
    // text; the Style's own family is then never drawn.
    const bool inStyle = family.find_first_of(u",\r\n") == u16::npos;
    std::string fn = toUtf8(family);
    std::erase_if(fn, [](char c) { return c == '\r' || c == '\n'; });
    const std::string s =
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n\n[V4+ Styles]\nFormat: Name, Fontname, "
        "Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
        "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, "
        "Encoding\nStyle: P," + (inStyle ? fn : std::string()) + ",40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000," +
        (bold ? "-1" : "0") + "," + (italic ? "-1" : "0") +
        ",0,0,100,100,0,0,1,0,0,7,10,10,10,1\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, "
        "MarginV, Effect, Text\nDialogue: 0,0:00:00.00,0:00:01.00,P,,0,0,0,," + (inStyle ? std::string() : "{\\fn" + fn + "}") +
        utf8Of(text) + "\n";
    std::vector<std::byte> out(s.size());
    std::transform(s.begin(), s.end(), out.begin(), [](char c) { return std::byte(c); });
    return out;
}

// The face the renderer opened for the request itself (not a fallback, not a
// face of other names).
const ResolvedFace *baseFace(const FontCollection &c)
{
    for (const auto &f : c.selections)
        if (f.stage == SelectionStage::Requested && f.code == 0 && f.nameMatch != NameMatch::None)
            return &f;
    return nullptr;
}

bool familyFound(const FontCollection &c)
{
    return baseFace(c) != nullptr;
}

std::string codeName(std::uint32_t code)
{
    char text[16];
    std::snprintf(text, sizeof text, "U+%04X", unsigned(code));
    return text;
}

template <typename T> void addUnique(std::vector<T> &list, const T &v)
{
    if (std::find(list.begin(), list.end(), v) == list.end())
        list.push_back(v);
}

u16 fileName(const u16 &path)
{
    const auto slash = path.find_last_of(u"/\\");
    return slash == u16::npos ? path : path.substr(slash + 1);
}

struct Probe {
    std::expected<FontCollection, FontError> result = std::unexpected(FontError::RendererUnavailable);
};

// The legacy GetLogFont flags (FontCollector.cpp:56-93): the family's faces,
// as the provider lists them, against the requested weight and slant.
struct Fake {
    bool normal = false, boldItalic = false, bold = false, italic = false;
};
Fake fakeFlags(const Variant &v, const std::vector<SystemFace> &faces)
{
    bool BoldItalic = false, Bold = false, Italic = false, Normal = false;
    const std::string want = toUtf8(lower(v.name));
    for (const auto &f : faces) {
        bool match = false;
        for (const auto &name : f.families)
            if (toUtf8(lower(toU16(std::u8string(name.begin(), name.end())))) == want)
                match = true;
        if (!match)
            continue;
        if (f.weight >= 700 && f.italic)
            BoldItalic = true;
        else if (f.weight >= 700)
            Bold = true;
        else if (f.italic)
            Italic = true;
        else
            Normal = true;
    }
    Fake out;
    const bool italic = v.italic;
    out.boldItalic = (v.bold >= 700 && italic) ? !BoldItalic : false;
    out.bold = (v.bold >= 700 && italic) ? !BoldItalic : (v.bold >= 700) ? !Bold : false;
    out.italic = italic ? !Italic : false;
    out.normal = (!italic && v.bold < 700) ? !Normal : false;
    return out;
}

FontEnvironment systemEnvironment()
{
    FontEnvironment env; // the system provider, as the video's subtitles render
    env.systemFonts = true;
    return env;
}

// A file name for a collected font: the provider's file, or for stream bytes
// (DirectWrite) the installed file of the same face.
void nameFile(CollectedFile &file, const CollectedFont &font, const FontCollection &from,
              const std::vector<SystemFace> &faces)
{
    if (!font.path.empty()) {
        file.shown = toU16(std::u8string(font.path.begin(), font.path.end()));
        file.name = fileName(file.shown);
        return;
    }
    file.shown = file.name = toU16(std::u8string(font.name.begin(), font.name.end()));
    for (const auto &sel : from.selections) {
        if (sel.sha256 != font.sha256 || sel.postscriptName.empty())
            continue;
        for (const auto &f : faces)
            if (f.postscriptName == sel.postscriptName && !f.path.empty()) {
                file.shown = toU16(std::u8string(f.path.begin(), f.path.end()));
                file.name = fileName(file.shown);
                return;
            }
    }
}

} // namespace

bool CollectorReview::rendererComplete() const
{
    if (!unrendered.empty() || !missingFamilies.empty() || !substitutedFamilies.empty() || !missingGlyphs.empty() || !fallbackGlyphs.empty())
        return false;
    for (const auto &r : reimports)
        if (!r.identical)
            return false;
    return true;
}

std::expected<CollectorReview, FontError> FontCollector::prepare(const std::vector<CollectorDocument> &documents,
                                                                 CollectorAction action, const std::atomic<bool> *cancel)
{
    const auto cancelled = [&] { return cancel && cancel->load(); };
    const auto started = std::chrono::steady_clock::now();
    const bool copyFonts = action != CollectorAction::Check;
    CollectorReview review;
    review.action = action;
    const FontEnvironment env = systemEnvironment();
    const std::vector<SystemFace> faces = m_fonts.systemFaces();

    // CheckOrCopyFonts (FontCollector.cpp:708-790): the copy modes first
    // list the installed font files, once for the collector's lifetime.
    if (copyFonts && !m_retrieved) {
        std::set<std::string> files;
        for (const auto &f : faces)
            if (!f.path.empty())
                files.insert(f.path);
        if (files.empty()) {
            review.retrieveFailed = true; // and nothing else is done
            return review;
        }
        review.retrievedFonts = files.size();
        review.retrieveMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        m_retrieved = true;
    }

    // Which families the renderer finds (legacy facenames.Index): one probe
    // per family, regular, over the characters it is used for.
    const Scan names = collector::scan(documents, [](const u16 &) { return true; });
    std::map<u16, Probe> familyProbe; // by lower-case name
    for (const auto &family : names.families) {
        if (cancelled())
            return std::unexpected(FontError::Cancelled);
        const u16 key = lower(family);
        if (familyProbe.contains(key))
            continue;
        const auto chars = names.chars.find(family);
        familyProbe[key].result =
            m_fonts.collect(probeScript(family, false, false, chars == names.chars.end() ? std::set<char32_t>{} : chars->second),
                            env, cancel);
        if (!familyProbe[key].result && (familyProbe[key].result.error() == FontError::Cancelled ||
                                         familyProbe[key].result.error() == FontError::RendererUnavailable))
            return std::unexpected(familyProbe[key].result.error());
    }
    const auto found = [&](const u16 &family) {
        const auto it = familyProbe.find(lower(family));
        return it != familyProbe.end() && it->second.result && familyFound(*it->second.result);
    };
    const Scan s = collector::scan(documents, found);
    review.found = s.found;
    review.notFound = s.notFound;

    // The whole Documents, as the renderer draws them, and the collected set
    // re-rendered without the system provider (F47-corpus).
    std::vector<std::pair<int, FontCollection>> rendered;
    std::vector<const CollectorDocument *> renderedDocs;
    for (const auto &doc : documents) {
        if (!doc.ass || doc.script.empty())
            continue;
        if (cancelled())
            return std::unexpected(FontError::Cancelled);
        auto c = m_fonts.collect(doc.script, env, cancel);
        if (!c) {
            if (c.error() == FontError::Cancelled || c.error() == FontError::RendererUnavailable)
                return std::unexpected(c.error());
            // A Document the renderer cannot read is not verified, so the
            // collection is incomplete (fonts.md), never passed by default.
            review.unrendered.push_back(doc.tab);
            continue;
        }
        if (review.provider.empty())
            review.provider = c->provider;
        for (const auto &f : c->missingFamilies)
            addUnique(review.missingFamilies, f);
        for (const auto &f : c->substitutedFamilies)
            addUnique(review.substitutedFamilies, f);
        for (const auto g : c->missingGlyphs)
            addUnique(review.missingGlyphs, g);
        for (const auto g : c->fallbackGlyphs)
            addUnique(review.fallbackGlyphs, g);
        rendered.emplace_back(doc.tab, std::move(*c));
        renderedDocs.push_back(&doc);
    }
    std::vector<const CollectedFont *> union_;
    std::vector<const FontCollection *> unionFrom;
    for (const auto &[tab, c] : rendered)
        for (const auto &f : c.fonts)
            if (std::none_of(union_.begin(), union_.end(), [&](const CollectedFont *u) { return u->sha256 == f.sha256; })) {
                union_.push_back(&f);
                unionFrom.push_back(&c);
            }

    // CheckPathAndGlyphs (FontCollector.cpp:1068-1244) over foundFonts.
    std::map<u16, Probe> variantProbe;
    std::set<std::string> fontnames; // files already found (by their bytes)
    u16 lastfn;
    for (const auto &[key, v] : s.variants) {
        if (cancelled())
            return std::unexpected(FontError::Cancelled);
        const u16 &fn = v.name;
        const bool isNewFont = lastfn != fn;
        lastfn = fn;
        Block &flc = blockFor(review.found, fn, Block::Header::FoundDot);
        const auto charsIt = s.chars.find(fn);
        if (charsIt == s.chars.end() || charsIt->second.empty()) {
            if (isNewFont)
                flc.warnings.push_back({Note::Kind::UnusedStyle, fn, {}, {}, copyFonts});
            continue;
        }
        const auto &chars = charsIt->second;
        const Fake fake = fakeFlags(v, faces);
        if (fake.normal)
            flc.warnings.push_back({Note::Kind::MissingNormal, fn});
        else if (fake.boldItalic)
            flc.warnings.push_back({Note::Kind::MissingBoldItalic, fn});
        else if (fake.bold)
            flc.warnings.push_back({Note::Kind::MissingBold, fn});
        else if (fake.italic)
            flc.warnings.push_back({Note::Kind::MissingItalic, fn});
        auto &probe = variantProbe[key];
        probe.result = m_fonts.collect(probeScript(fn, v.bold >= 700, v.italic, chars), env, cancel);
        if (!probe.result && probe.result.error() == FontError::Cancelled)
            return std::unexpected(FontError::Cancelled);
        if (isNewFont) {
            // CheckGlyphsExists, answered by the renderer: a character the
            // requested face does not draw is missing from the font, whether
            // a fallback drew it or nothing did.
            if (!probe.result) {
                flc.warnings.push_back({Note::Kind::CannotCheckCharacters, fn});
            } else {
                std::set<char32_t> missing;
                for (const auto g : probe.result->missingGlyphs)
                    missing.insert(g);
                std::map<std::string, std::set<char32_t>> fallbackFiles;
                for (const auto &sel : probe.result->selections)
                    if (sel.code != 0 && (sel.stage != SelectionStage::Requested || sel.nameMatch == NameMatch::None)) {
                        missing.insert(sel.code);
                        const std::string file = !sel.path.empty() ? sel.path : sel.postscriptName;
                        fallbackFiles[file].insert(sel.code);
                    }
                u32 text;
                for (const char32_t c : chars)
                    if (c != kByteOrderMark && missing.contains(c))
                        text += c;
                if (!text.empty()) {
                    review.allGlyphs = false;
                    flc.warnings.push_back({Note::Kind::MissingCharacters, fn, fromU32(text)});
                }
                for (const auto &[file, codes] : fallbackFiles) {
                    u32 drawn;
                    for (const char32_t c : codes)
                        drawn += c;
                    flc.warnings.push_back({Note::Kind::FallbackCharacters, fn, fromU32(drawn),
                                            toU16(std::u8string(file.begin(), file.end()))});
                }
            }
        }
        if (!copyFonts) {
            if (isNewFont)
                ++review.foundCount;
            continue;
        }
        const ResolvedFace *base = probe.result ? baseFace(*probe.result) : nullptr;
        if (!probe.result) {
            flc.warnings.push_back({Note::Kind::CannotGetContents, fn});
            if (isNewFont)
                ++review.notFoundCount;
            continue;
        }
        if (!base) {
            flc.warnings.push_back({Note::Kind::CannotFindInFolder, fn});
            // Legacy also decremented `found`, which nothing had counted for
            // this font, so the count could go below the fonts written
            // (FontCollector.cpp:1228-1232; FC-found-negative).
            ++review.notFoundCount;
            continue;
        }
        const CollectedFont *font = nullptr;
        for (const auto &f : probe.result->fonts)
            if (f.sha256 == base->sha256)
                font = &f;
        if (!font || !font->bytes || font->bytes->empty()) {
            flc.warnings.push_back({Note::Kind::CannotGetContents, fn});
            if (isNewFont)
                ++review.notFoundCount;
            continue;
        }
        if (fontnames.insert(font->sha256).second) {
            CollectedFile file;
            file.family = fn;
            file.bytes = font->bytes;
            file.sha256 = font->sha256;
            file.faces = font->faces;
            nameFile(file, *font, *probe.result, faces);
            flc.infos.push_back({Note::Kind::FoundFile, fn, file.shown});
            const u16 ext = lower(file.shown.size() > 3 ? file.shown.substr(file.shown.size() - 3) : u16());
            review.files.push_back(file);
            if (ext == u"pfm" || ext == u"pfb") {
                // The Type 1 pair (FontCollector.cpp:1206-1219): the other
                // file is named and copied too. Legacy's RemoveLast(3) cut
                // the path in place, so it copied the cut path, which failed
                // (FC-type1-pair).
                u16 repl = ext == u"pfm" ? u"pfb" : u"pfm";
                if (file.shown.back() < u'Z')
                    for (auto &c : repl)
                        c = char16_t(std::towupper(wint_t(c)));
                const u16 partner = file.shown.substr(0, file.shown.size() - 3) + repl;
                flc.infos.push_back({Note::Kind::FoundFile, fn, partner});
                CollectedFile pair;
                pair.family = fn;
                pair.shown = partner;
                pair.name = fileName(partner);
                pair.bytes = m_read ? m_read(partner) : nullptr; // null: "Cannot copy"
                pair.sha256 = "type1:" + toUtf8(partner);
                review.files.push_back(pair);
            }
        }
    }

    // Files the renderer used for the Documents beyond the variants' own
    // faces: fallbacks, substitutes, the default family. They are reported
    // with their role, never collected as the requested font (fonts.md).
    if (copyFonts) {
        for (std::size_t i = 0; i < union_.size(); ++i) {
            const CollectedFont &font = *union_[i];
            if (fontnames.contains(font.sha256))
                continue;
            fontnames.insert(font.sha256);
            RendererFile file;
            file.sha256 = font.sha256;
            file.faces = font.faces;
            for (const auto &sel : unionFrom[i]->selections) {
                if (sel.sha256 != font.sha256)
                    continue;
                if (file.family.empty())
                    file.family = toU16(std::u8string(sel.requestedFamily.begin(), sel.requestedFamily.end()));
                if (sel.stage == SelectionStage::Requested && sel.code == 0 && sel.nameMatch == NameMatch::None)
                    addUnique(file.roles, "substitute for " + sel.requestedFamily);
            }
            for (const auto &r : font.roles)
                if (!(r.starts_with("requested ") &&
                      std::find(file.roles.begin(), file.roles.end(), "substitute for " + r.substr(10)) != file.roles.end()))
                    addUnique(file.roles, r);
            CollectedFile named;
            nameFile(named, font, *unionFrom[i], faces);
            file.shown = named.shown;
            if (file.family.empty())
                file.family = named.name;
            std::string role;
            for (const auto &r : file.roles)
                role += (role.empty() ? "" : ", ") + r;
            Block &b = review.notFound.contains(file.family) ? review.notFound[file.family]
                                                             : blockFor(review.found, file.family, Block::Header::FoundDot);
            b.infos.push_back({Note::Kind::RendererFile, file.family, file.shown,
                               toU16(std::u8string(role.begin(), role.end()))});
            review.rendererFiles.push_back(std::move(file));
        }
    }
    // Two different fonts under one file name: the output does not hold the
    // set verified below (a folder keeps the last, as wxCopyFile overwrote
    // it; an archive holds both entries, as PutNextEntry added them).
    {
        std::map<u16, std::string> seen; // lower-case name -> bytes
        for (const auto &file : review.files) {
            if (!file.bytes || file.bytes->empty())
                continue;
            const auto [it, added] = seen.emplace(lower(file.name), file.sha256);
            if (!added && it->second != file.sha256 &&
                std::find(review.nameClashes.begin(), review.nameClashes.end(), file.name) == review.nameClashes.end())
                review.nameClashes.push_back(file.name);
        }
    }

    // The faces each file was opened with, from the Documents' rendering.
    for (auto &file : review.files)
        for (const auto *f : union_)
            if (f->sha256 == file.sha256)
                for (const long face : f->faces)
                    addUnique(file.faces, face);

    // The collected set alone, in a clean environment, against the frames
    // the Documents rendered with the system provider (F47-corpus): what
    // the output would reproduce.
    if (copyFonts) {
        for (std::size_t i = 0; i < rendered.size(); ++i) {
            if (cancelled())
                return std::unexpected(FontError::Cancelled);
            FontCollection collected = rendered[i].second;
            collected.fonts.clear();
            for (const auto &file : review.files) {
                if (!file.bytes || file.bytes->empty())
                    continue;
                CollectedFont f;
                f.sha256 = file.sha256;
                f.bytes = file.bytes;
                f.name = toUtf8(file.name);
                f.faces = file.faces;
                collected.fonts.push_back(std::move(f));
            }
            const auto check = m_fonts.verifyReimport(renderedDocs[i]->script, collected);
            CollectorReview::Reimport r;
            r.tab = rendered[i].first;
            r.frames = collected.frameHashes.size();
            if (check) {
                r.identical = check->identical;
                r.differingFrames = check->differingFrames;
            }
            review.reimports.push_back(std::move(r));
        }
    }

    // CheckOrCopyFonts (FontCollector.cpp:826-840): a family that is not
    // found counts unless no text uses it.
    review.notFoundCount += int(review.notFound.size());
    for (auto &[family, block] : review.notFound) {
        const auto it = s.chars.find(family);
        if (it == s.chars.end() || it->second.empty()) {
            block.warnings.push_back({Note::Kind::UnusedStyle, family, {}, {}, false});
            --review.notFoundCount;
        }
    }
    return review;
}

CollectorResult FontCollector::result(const CollectorReview &review) const
{
    CollectorResult r;
    r.found = review.found;
    r.notFound = review.notFound;
    r.foundCount = review.foundCount;
    r.notFoundCount = review.notFoundCount;
    r.allGlyphs = review.allGlyphs;
    r.complete = review.complete();
    return r;
}

CollectorResult FontCollector::apply(const CollectorReview &review, CollectorOutput &output,
                                     bool acknowledgedIncomplete, const std::atomic<bool> *cancel)
{
    CollectorResult r = result(review);
    if (review.action == CollectorAction::Check || review.retrieveFailed)
        return r;
    // Partial output only after explicit acknowledgment (routing #54).
    if (!review.complete() && !acknowledgedIncomplete) {
        r.refused = true;
        r.complete = false;
        return r;
    }
    if (review.files.empty()) // MakeDirectory waits for the first file found (FontCollector.cpp:1190)
        return r;
    if (!output.open()) {
        // MakeDirectory failed: "Path is not available" and nothing more
        // (FontCollector.cpp:821-824).
        r.pathNotAvailable = true;
        r.cannotCreateFolder = output.folderFailed();
        r.complete = false;
        return r;
    }
    const bool zip = review.action == CollectorAction::Zip;
    // Until the last file is written, a folder carries the label: a run that
    // ends early (cancelled, or the process gone) leaves nothing unlabelled.
    if (!zip)
        r.labelled = output.label(labelText(review, nullptr) + "Writing was not finished.\n");
    for (const auto &file : review.files) {
        if (cancel && cancel->load()) {
            r.cancelled = true;
            break;
        }
        // SaveFont (FontCollector.cpp:877-911).
        const bool ok = file.bytes && output.put(file.name, *file.bytes);
        auto it = r.found.find(file.family);
        Block &b = it != r.found.end() ? it->second : r.notFound[file.family];
        if (ok) {
            b.infos.push_back({zip ? Note::Kind::AddedToArchive : Note::Kind::Copied, file.family, file.name});
            ++r.foundCount;
        } else {
            b.warnings.push_back({zip ? Note::Kind::CannotZip : Note::Kind::CannotCopy, file.family, file.name});
            ++r.notCopiedCount;
        }
    }
    r.complete = !r.cancelled && review.complete() && r.notCopiedCount == 0;
    if (zip) {
        if (r.cancelled) {
            // The archive is published only when it is closed: nothing is left.
            output.discard();
            return r;
        }
        if (!r.complete)
            r.labelled = output.label(labelText(review, &r));
        r.written = output.commit();
        if (!r.written)
            r.labelled = false;
    } else {
        r.written = true;
        if (r.complete)
            r.labelled = !output.unlabel();
        else
            r.labelled = output.label(labelText(review, &r));
    }
    return r;
}

std::string FontCollector::labelText(const CollectorReview &review, const CollectorResult *result)
{
    std::string s = "HikariSub font collection: INCOMPLETE\n\n";
    if (!review.complete())
        s += "These fonts do not reproduce the subtitles on their own. Do not treat this collection as "
             "complete.\n\n";
    auto list = [&](const char *title, const std::vector<std::string> &items) {
        if (items.empty())
            return;
        s += title;
        for (const auto &i : items)
            s += "  " + i + "\n";
    };
    std::vector<std::string> notFound;
    for (const auto &[family, block] : review.notFound)
        notFound.push_back(toUtf8(family));
    list("Fonts not found:\n", notFound);
    std::vector<std::string> tabs;
    for (const int t : review.unrendered)
        tabs.push_back("tab " + std::to_string(t + 1));
    list("Documents the renderer could not read:\n", tabs);
    list("Families the renderer did not find:\n", review.missingFamilies);
    list("Families answered by a font of another name:\n", review.substitutedFamilies);
    std::vector<std::string> codes;
    for (const auto g : review.missingGlyphs)
        codes.push_back(codeName(g));
    list("Characters no font has:\n", codes);
    codes.clear();
    for (const auto g : review.fallbackGlyphs)
        codes.push_back(codeName(g));
    list("Characters drawn by a fallback font of this computer:\n", codes);
    std::vector<std::string> roles;
    for (const auto &f : review.rendererFiles) {
        std::string role;
        for (const auto &r : f.roles)
            role += (role.empty() ? "" : ", ") + r;
        roles.push_back(toUtf8(f.shown) + " (" + role + "), not collected");
    }
    list("Files the renderer used in another role:\n", roles);
    std::vector<std::string> frames;
    for (const auto &r : review.reimports)
        if (!r.identical)
            frames.push_back("tab " + std::to_string(r.tab + 1) + ": " + std::to_string(r.differingFrames.size()) +
                             " of " + std::to_string(r.frames) + " frames differ");
    list("Rendering with the collected fonts alone:\n", frames);
    std::vector<std::string> clashes;
    for (const auto &n : review.nameClashes)
        clashes.push_back(toUtf8(n));
    list("Different fonts with the same file name (only one is kept in a folder):\n", clashes);
    if (!review.allGlyphs)
        s += "Some fonts do not contain all glyphs used in the text.\n";
    if (result) {
        std::vector<std::string> failed;
        for (const auto *map : {&result->found, &result->notFound})
            for (const auto &[family, block] : *map)
                for (const auto &w : block.warnings)
                    if (w.kind == Note::Kind::CannotCopy || w.kind == Note::Kind::CannotZip)
                        failed.push_back(toUtf8(w.a));
        list("Fonts that could not be written:\n", failed);
        if (result->cancelled)
            s += "Writing was cancelled.\n";
    }
    return s;
}

} // namespace hikari::application
