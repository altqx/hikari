#include "hikari/backends/libass_font_service.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>

extern "C" {
#include <ass/ass.h>
}
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <dwrite.h>
#include <windows.h>
#else
#include <fontconfig/fontconfig.h>
// After fontconfig.h, which it needs.
#include <fontconfig/fcfreetype.h>
#endif

namespace hikari::backends {

using application::FontError;
using application::FontReport;
using application::NameMatch;
using application::ResolvedFace;
using application::SelectionStage;
using application::SystemFace;

namespace {

std::string sha256(const std::vector<unsigned char> &bytes)
{
    return QCryptographicHash::hash(QByteArrayView(bytes.data(), qsizetype(bytes.size())),
                                    QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string decodeName(const FT_SfntName &n)
{
    if (n.platform_id == TT_PLATFORM_MICROSOFT || n.platform_id == TT_PLATFORM_APPLE_UNICODE) {
        std::u16string text; // UTF-16BE
        for (FT_UInt i = 0; i + 1 < n.string_len; i += 2)
            text.push_back(char16_t(n.string[i] << 8 | n.string[i + 1]));
        return QString::fromStdU16String(text).toStdString();
    }
    return std::string(reinterpret_cast<const char *>(n.string), n.string_len);
}

// The face's own names, read from the bytes, and how `requested` matches them.
void readNames(const std::vector<unsigned char> &bytes, long index, const std::string &requested,
               ResolvedFace &face)
{
    FT_Library ft = nullptr;
    if (FT_Init_FreeType(&ft))
        return;
    FT_Face f = nullptr;
    if (!FT_New_Memory_Face(ft, bytes.data(), FT_Long(bytes.size()), index, &f)) {
        std::map<int, std::vector<std::string>> byId;
        for (FT_UInt i = 0, n = FT_Get_Sfnt_Name_Count(f); i < n; ++i) {
            FT_SfntName name;
            if (FT_Get_Sfnt_Name(f, i, &name))
                continue;
            if (name.name_id == TT_NAME_ID_FONT_FAMILY || name.name_id == TT_NAME_ID_TYPOGRAPHIC_FAMILY ||
                name.name_id == TT_NAME_ID_FULL_NAME || name.name_id == TT_NAME_ID_PS_NAME) {
                const std::string text = decodeName(name);
                auto &list = byId[name.name_id];
                if (!text.empty() && std::find(list.begin(), list.end(), text) == list.end())
                    list.push_back(text);
            }
        }
        const std::pair<int, NameMatch> order[] = {{TT_NAME_ID_FONT_FAMILY, NameMatch::Family},
                                                   {TT_NAME_ID_TYPOGRAPHIC_FAMILY, NameMatch::TypographicFamily},
                                                   {TT_NAME_ID_FULL_NAME, NameMatch::FullName},
                                                   {TT_NAME_ID_PS_NAME, NameMatch::PostScriptName}};
        const std::string want = lower(requested);
        for (const auto &[id, match] : order)
            for (const auto &text : byId[id]) {
                if (std::find(face.familyNames.begin(), face.familyNames.end(), text) == face.familyNames.end())
                    face.familyNames.push_back(text);
                if (face.nameMatch == NameMatch::None && lower(text) == want)
                    face.nameMatch = match;
            }
        FT_Done_Face(f);
    }
    FT_Done_FreeType(ft);
}

struct State {
    FontReport report;     // faces live here in both modes
    std::size_t current = 0;
    std::map<int, std::size_t> uidToFace; // the renderer's selector-local ids
    std::vector<std::pair<std::string, std::string>> attachments; // name, sha256
    std::vector<std::pair<std::string, std::string>> externals;   // Y6: external font path, sha256
    application::FontCollection *collection = nullptr; // set while collecting a document
};

std::string codeName(std::uint32_t code)
{
    char text[16];
    std::snprintf(text, sizeof text, "U+%04X", unsigned(code));
    return text;
}

template <typename T> void addUnique(std::vector<T> &list, const T &value)
{
    if (std::find(list.begin(), list.end(), value) == list.end())
        list.push_back(value);
}

// Collection mode: one entry per distinct byte stream, with its faces and roles.
void collectFace(State &st, const ResolvedFace &face, const std::vector<unsigned char> &bytes)
{
    auto &c = *st.collection;
    if (face.stage != SelectionStage::Requested && face.code == 0 && !face.requestedFamily.empty())
        addUnique(c.missingFamilies, face.requestedFamily);
    if (face.stage == SelectionStage::Requested && face.code == 0 && face.nameMatch == NameMatch::None)
        addUnique(c.substitutedFamilies, face.requestedFamily);
    if (face.stage == SelectionStage::Fallback && face.code != 0)
        addUnique(c.fallbackGlyphs, face.code);
    if (face.sha256.empty())
        return;
    auto it = std::find_if(c.fonts.begin(), c.fonts.end(),
                           [&](const application::CollectedFont &f) { return f.sha256 == face.sha256; });
    if (it == c.fonts.end()) {
        application::CollectedFont font;
        font.sha256 = face.sha256;
        auto copy = std::make_shared<std::vector<std::byte>>(bytes.size());
        std::memcpy(copy->data(), bytes.data(), bytes.size());
        font.bytes = std::move(copy);
        font.attachment = face.attachment;
        font.path = face.path;
        font.name = !face.attachment.empty() ? face.attachment
                    : !face.path.empty()      ? QFileInfo(QString::fromStdString(face.path)).fileName().toStdString()
                                              : face.postscriptName + ".font";
        c.fonts.push_back(std::move(font));
        it = std::prev(c.fonts.end());
    }
    switch (face.stage) {
    case SelectionStage::Requested: addUnique(it->roles, "requested " + face.requestedFamily); break;
    case SelectionStage::DefaultFamily: addUnique(it->roles, std::string("default family")); break;
    case SelectionStage::Fallback: addUnique(it->roles, "fallback " + codeName(face.code)); break;
    case SelectionStage::DefaultPath: addUnique(it->roles, std::string("default path")); break;
    }
}

SelectionStage stageOf(ASS_HikariFontStage s)
{
    switch (s) {
    case ASS_HIKARI_FONT_DEFAULT_FAMILY: return SelectionStage::DefaultFamily;
    case ASS_HIKARI_FONT_FALLBACK: return SelectionStage::Fallback;
    case ASS_HIKARI_FONT_DEFAULT_PATH: return SelectionStage::DefaultPath;
    default: return SelectionStage::Requested;
    }
}

void onSelected(void *data, const ASS_HikariFontSelection *s)
{
    auto &st = *static_cast<State *>(data);
    if (s->uid >= 0 && st.uidToFace.contains(s->uid))
        return; // the same face again (libass then skips it too)
    ResolvedFace face;
    face.stage = stageOf(s->stage);
    face.requestedFamily = s->requested_family ? s->requested_family : "";
    face.bold = s->bold;
    face.italic = s->italic;
    face.code = s->code;
    face.embedded = s->embedded != 0;
    face.postscriptName = s->postscript_name ? s->postscript_name : "";
    face.faceIndex = s->index;
    std::vector<unsigned char> bytes;
    if (s->read) {
        const std::size_t size = s->read(s->read_data, nullptr, 0, 0);
        bytes.resize(size);
        if (size)
            bytes.resize(s->read(s->read_data, bytes.data(), 0, size));
    } else if (s->path) {
        face.path = s->path;
        QFile file(QString::fromUtf8(s->path));
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray all = file.readAll();
            bytes.assign(all.begin(), all.end());
        }
    }
    face.size = bytes.size();
    if (!bytes.empty()) {
        face.sha256 = sha256(bytes);
        readNames(bytes, s->index, face.requestedFamily, face);
    }
    for (const auto &[name, hash] : st.attachments)
        if (hash == face.sha256) {
            face.attachment = name;
            break;
        }
    // Y6: an external font (EXTERNAL_FONTS_DIRECTORY) is named by its file.
    if (face.attachment.empty() && face.path.empty())
        for (const auto &[name, hash] : st.externals)
            if (hash == face.sha256) {
                face.path = name;
                break;
            }
    if (st.collection)
        collectFace(st, face, bytes);
    const std::size_t index = st.report.faces.size();
    st.report.faces.push_back(std::move(face));
    if (s->uid >= 0)
        st.uidToFace[s->uid] = index;
    if (st.collection)
        return;
    auto &request = st.report.requests[st.current];
    request.faces.push_back(index);
    const auto &added = st.report.faces[index];
    if (s->stage != ASS_HIKARI_FONT_REQUESTED)
        request.usedFallback = true;
    else if (added.nameMatch == NameMatch::None)
        request.substituted = true; // the provider answered with a face of other names
    else
        request.requestedFamilyFound = true;
}

void onFaceOpened(void *data, int uid, long faceIndex, long numFaces, const char *postscript, int nCoords,
                  const double *coords)
{
    auto &st = *static_cast<State *>(data);
    const auto it = st.uidToFace.find(uid);
    if (it == st.uidToFace.end())
        return;
    auto &face = st.report.faces[it->second];
    face.faceIndex = faceIndex;
    face.faceCount = numFaces;
    if (postscript && face.postscriptName.empty())
        face.postscriptName = postscript;
    face.coords.assign(coords, coords + nCoords);
    if (st.collection && !face.sha256.empty())
        for (auto &font : st.collection->fonts)
            if (font.sha256 == face.sha256)
                addUnique(font.faces, faceIndex);
}

void onGlyphSimulated(void *data, int uid, unsigned, int embolden, int italicize)
{
    auto &st = *static_cast<State *>(data);
    const auto it = st.uidToFace.find(uid);
    if (it == st.uidToFace.end())
        return;
    st.report.faces[it->second].emboldened |= embolden != 0;
    st.report.faces[it->second].italicized |= italicize != 0;
}

void onGlyphMissing(void *data, uint32_t code, const char *, unsigned, unsigned)
{
    auto &st = *static_cast<State *>(data);
    if (st.collection) {
        addUnique(st.collection->missingGlyphs, code);
        return;
    }
    auto &missing = st.report.requests[st.current].missingGlyphs;
    if (std::find(missing.begin(), missing.end(), code) == missing.end())
        missing.push_back(code);
}

void onMessage(int, const char *format, va_list args, void *data)
{
    char text[512];
    std::vsnprintf(text, sizeof text, format, args);
    constexpr std::string_view prefix = "Using font provider ";
    if (std::string_view(text).starts_with(prefix))
        static_cast<State *>(data)->report.provider = std::string(text + prefix.size());
}

std::string escapeText(const std::string &text)
{
    std::string out;
    for (char c : text) {
        if (c == '{' || c == '}' || c == '\\')
            continue; // no override tags or escapes from request text
        if (c == '\n')
            out += "\\N";
        else
            out += c;
    }
    return out;
}

// Y6: EXTERNAL_FONTS_DIRECTORY's fonts, after the attachments. Legacy
// registered them for the process (AddFontResourceExW FR_PRIVATE), so the
// renderer saw them as installed fonts.
void addExternalFonts(ASS_Library *library, const application::FontEnvironment &environment, State *state)
{
    for (const auto &f : environment.externalFonts) {
        if (!f.bytes)
            continue;
        ass_add_font(library, f.name.c_str(), reinterpret_cast<const char *>(f.bytes->data()), int(f.bytes->size()));
        if (state) {
            std::vector<unsigned char> copy(reinterpret_cast<const unsigned char *>(f.bytes->data()),
                                            reinterpret_cast<const unsigned char *>(f.bytes->data()) + f.bytes->size());
            state->externals.emplace_back(f.name, sha256(copy));
        }
    }
}

} // namespace

std::expected<FontReport, FontError> LibassFontService::resolve(const application::FontEnvironment &environment,
                                                                const std::vector<application::FontRequest> &requests)
{
    for (const auto &r : requests)
        if (r.family.find_first_of(",\r\n") != std::string::npos)
            return std::unexpected(FontError::InvalidInput); // not representable in a style line

    ASS_Library *library = ass_library_init();
    if (!library)
        return std::unexpected(FontError::RendererUnavailable);
    State state;
    state.report.generation = environment.generation;
    char version[32];
    std::snprintf(version, sizeof version, "0x%08x", unsigned(ass_library_version()));
    state.report.libassVersion = version;
    state.report.provider = environment.systemFonts ? "" : "none";
    ass_set_message_cb(library, onMessage, &state);
    const ASS_HikariFontDiagnostics callbacks{ASS_HIKARI_FONT_DIAGNOSTICS_VERSION, onSelected, onFaceOpened,
                                              onGlyphSimulated, onGlyphMissing};
    ass_hikari_set_font_diagnostics(library, &callbacks, &state);
    for (const auto &a : environment.attachments) {
        if (!a.bytes)
            continue;
        ass_add_font(library, a.name.c_str(), reinterpret_cast<const char *>(a.bytes->data()), int(a.bytes->size()));
        std::vector<unsigned char> copy(reinterpret_cast<const unsigned char *>(a.bytes->data()),
                                        reinterpret_cast<const unsigned char *>(a.bytes->data()) + a.bytes->size());
        state.attachments.emplace_back(a.name, sha256(copy));
    }
    addExternalFonts(library, environment, &state);

    for (std::size_t i = 0; i < requests.size(); ++i) {
        const auto &request = requests[i];
        state.report.requests.push_back({request, {}, false, false, false, {}});
        state.current = i;
        state.uidToFace.clear();
        // A fresh renderer: its font cache and selector know nothing yet.
        ASS_Renderer *renderer = ass_renderer_init(library);
        if (!renderer) {
            ass_library_done(library);
            return std::unexpected(FontError::RendererUnavailable);
        }
        ass_set_frame_size(renderer, 640, 360);
        ass_set_fonts(renderer, nullptr, environment.defaultFamily.empty() ? nullptr : environment.defaultFamily.c_str(),
                      environment.systemFonts ? ASS_FONTPROVIDER_AUTODETECT : ASS_FONTPROVIDER_NONE, nullptr, 1);
        const std::string script =
            "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n\n[V4+ Styles]\nFormat: Name, Fontname, "
            "Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
            "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, "
            "Encoding\nStyle: R," + request.family + ",40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000," +
            (request.bold ? "-1" : "0") + "," + (request.italic ? "-1" : "0") +
            ",0,0,100,100,0,0,1,0,0,7,10,10,10,1\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, "
            "MarginR, MarginV, Effect, Text\nDialogue: 0,0:00:00.00,0:00:01.00,R,,0,0,0,," +
            escapeText(request.text) + "\n";
        std::vector<char> buffer(script.begin(), script.end());
        ASS_Track *track = ass_read_memory(library, buffer.data(), buffer.size(), nullptr);
        if (track) {
            int changed = 0;
            ass_render_frame(renderer, track, 500, &changed);
            ass_free_track(track);
        }
        ass_renderer_done(renderer);
    }
    ass_library_done(library);
    return std::move(state.report);
}

std::vector<SystemFace> LibassFontService::systemFaces()
{
    std::vector<SystemFace> out;
#ifdef _WIN32
    IDWriteFactory *factory = nullptr;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown **>(&factory))))
        return out;
    IDWriteFontCollection *collection = nullptr;
    // Y6 (F47-refresh): after refresh() the shared factory's collection is
    // checked for installed and removed fonts.
    if (SUCCEEDED(factory->GetSystemFontCollection(&collection, m_checkForUpdates.exchange(false) ? TRUE : FALSE))) {
        auto utf8 = [](const wchar_t *w) { return QString::fromWCharArray(w).toStdString(); };
        auto strings = [&](IDWriteFont *font, DWRITE_INFORMATIONAL_STRING_ID id) {
            std::vector<std::string> values;
            IDWriteLocalizedStrings *list = nullptr;
            BOOL exists = FALSE;
            if (SUCCEEDED(font->GetInformationalStrings(id, &list, &exists)) && exists && list) {
                for (UINT32 i = 0; i < list->GetCount(); ++i) {
                    UINT32 length = 0;
                    list->GetStringLength(i, &length);
                    std::wstring s(length + 1, L'\0');
                    list->GetString(i, s.data(), length + 1);
                    values.push_back(utf8(s.c_str()));
                }
                list->Release();
            }
            return values;
        };
        auto localized = [&](IDWriteFont *font, DWRITE_INFORMATIONAL_STRING_ID id) {
            std::string value;
            IDWriteLocalizedStrings *list = nullptr;
            BOOL exists = FALSE;
            if (SUCCEEDED(font->GetInformationalStrings(id, &list, &exists)) && exists && list) {
                UINT32 index = 0;
                BOOL found = FALSE;
                wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
                if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0)
                    list->FindLocaleName(locale, &index, &found);
                if (!found)
                    list->FindLocaleName(L"en-us", &index, &found);
                if (!found)
                    index = 0;
                UINT32 length = 0;
                if (list->GetCount() > index && SUCCEEDED(list->GetStringLength(index, &length))) {
                    std::wstring s(length + 1, L'\0');
                    if (SUCCEEDED(list->GetString(index, s.data(), length + 1)))
                        value = utf8(s.c_str());
                }
                list->Release();
            }
            return value;
        };
        for (UINT32 f = 0; f < collection->GetFontFamilyCount(); ++f) {
            IDWriteFontFamily *family = nullptr;
            if (FAILED(collection->GetFontFamily(f, &family)))
                continue;
            for (UINT32 i = 0; i < family->GetFontCount(); ++i) {
                IDWriteFont *font = nullptr;
                if (FAILED(family->GetFont(i, &font)))
                    continue;
                SystemFace face;
                // GDI-compatible family names, as libass's DirectWrite provider matches them.
                face.families = strings(font, DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES);
                // Y6: the name GDI's EnumFontFamiliesEx listed (FontEnumerator.cpp:155):
                // the Win32 family name in the user's language, else English, else the first.
                face.listedFamily = localized(font, DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES);
                for (auto &name : strings(font, DWRITE_INFORMATIONAL_STRING_PREFERRED_FAMILY_NAMES))
                    if (std::find(face.families.begin(), face.families.end(), name) == face.families.end())
                        face.families.push_back(name);
                const auto sub = strings(font, DWRITE_INFORMATIONAL_STRING_WIN32_SUBFAMILY_NAMES);
                face.style = sub.empty() ? std::string() : sub.front();
                const auto ps = strings(font, DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME);
                face.postscriptName = ps.empty() ? std::string() : ps.front();
                face.weight = int(font->GetWeight());
                face.italic = font->GetStyle() != DWRITE_FONT_STYLE_NORMAL;
                IDWriteFontFace *fontFace = nullptr;
                if (SUCCEEDED(font->CreateFontFace(&fontFace))) {
                    face.index = int(fontFace->GetIndex());
                    UINT32 files = 1;
                    IDWriteFontFile *file = nullptr;
                    if (SUCCEEDED(fontFace->GetFiles(&files, &file)) && file) {
                        const void *key = nullptr;
                        UINT32 keySize = 0;
                        IDWriteFontFileLoader *loader = nullptr;
                        IDWriteLocalFontFileLoader *local = nullptr;
                        if (SUCCEEDED(file->GetReferenceKey(&key, &keySize)) && SUCCEEDED(file->GetLoader(&loader)) &&
                            SUCCEEDED(loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),
                                                             reinterpret_cast<void **>(&local)))) {
                            UINT32 length = 0;
                            if (SUCCEEDED(local->GetFilePathLengthFromKey(key, keySize, &length))) {
                                std::wstring path(length + 1, L'\0');
                                if (SUCCEEDED(local->GetFilePathFromKey(key, keySize, path.data(), length + 1)))
                                    face.path = utf8(path.c_str());
                            }
                            local->Release();
                        }
                        if (loader)
                            loader->Release();
                        file->Release();
                    }
                    fontFace->Release();
                }
                out.push_back(std::move(face));
                font->Release();
            }
            family->Release();
        }
        collection->Release();
    }
    factory->Release();
#else
    if (!FcInit())
        return out;
    FcPattern *pattern = FcPatternCreate();
    FcObjectSet *objects = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_FILE, FC_INDEX, FC_WEIGHT, FC_SLANT,
                                            FC_POSTSCRIPT_NAME, nullptr);
    if (FcFontSet *set = FcFontList(nullptr, pattern, objects)) {
        for (int i = 0; i < set->nfont; ++i) {
            FcPattern *p = set->fonts[i];
            SystemFace face;
            FcChar8 *s = nullptr;
            for (int n = 0; FcPatternGetString(p, FC_FAMILY, n, &s) == FcResultMatch; ++n)
                face.families.emplace_back(reinterpret_cast<const char *>(s));
            if (FcPatternGetString(p, FC_STYLE, 0, &s) == FcResultMatch)
                face.style = reinterpret_cast<const char *>(s);
            if (FcPatternGetString(p, FC_POSTSCRIPT_NAME, 0, &s) == FcResultMatch)
                face.postscriptName = reinterpret_cast<const char *>(s);
            if (FcPatternGetString(p, FC_FILE, 0, &s) == FcResultMatch)
                face.path = reinterpret_cast<const char *>(s);
            int value = 0;
            if (FcPatternGetInteger(p, FC_INDEX, 0, &value) == FcResultMatch)
                face.index = value;
            double weight = 0;
            if (FcPatternGetDouble(p, FC_WEIGHT, 0, &weight) == FcResultMatch)
                face.weight = int(FcWeightToOpenTypeDouble(weight));
            if (FcPatternGetInteger(p, FC_SLANT, 0, &value) == FcResultMatch)
                face.italic = value != FC_SLANT_ROMAN;
            out.push_back(std::move(face));
        }
        FcFontSetDestroy(set);
    }
    FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);
#endif
    return out;
}

namespace {

constexpr int kFrameWidth = 640, kFrameHeight = 360;

// Composites one libass frame onto transparent black and hashes it.
std::string frameHash(ASS_Image *image)
{
    std::vector<unsigned char> rgba(std::size_t(kFrameWidth) * kFrameHeight * 4, 0);
    for (; image; image = image->next) {
        const unsigned r = image->color >> 24, g = (image->color >> 16) & 0xff, b = (image->color >> 8) & 0xff;
        const unsigned opacity = 255 - (image->color & 0xff);
        for (int y = 0; y < image->h; ++y) {
            const int dy = image->dst_y + y;
            if (dy < 0 || dy >= kFrameHeight)
                continue;
            for (int x = 0; x < image->w; ++x) {
                const int dx = image->dst_x + x;
                if (dx < 0 || dx >= kFrameWidth)
                    continue;
                const unsigned a = image->bitmap[y * image->stride + x] * opacity / 255;
                unsigned char *p = &rgba[(std::size_t(dy) * kFrameWidth + dx) * 4];
                p[0] = static_cast<unsigned char>((r * a + p[0] * (255 - a)) / 255);
                p[1] = static_cast<unsigned char>((g * a + p[1] * (255 - a)) / 255);
                p[2] = static_cast<unsigned char>((b * a + p[2] * (255 - a)) / 255);
                p[3] = static_cast<unsigned char>(a + p[3] * (255 - a) / 255);
            }
        }
    }
    return sha256(rgba);
}

struct Rendering {
    std::vector<std::int64_t> times;
    std::vector<std::string> hashes;
};

// Renders `script` at every event's midpoint in one renderer; false when
// cancelled. Diagnostics, if installed on `library`, see every selection.
std::expected<Rendering, FontError> renderDocument(ASS_Library *library, const std::vector<std::byte> &script,
                                                   const application::FontEnvironment &environment,
                                                   const std::atomic<bool> *cancel)
{
    for (const auto &a : environment.attachments)
        if (a.bytes)
            ass_add_font(library, a.name.c_str(), reinterpret_cast<const char *>(a.bytes->data()),
                         int(a.bytes->size()));
    addExternalFonts(library, environment, nullptr);
    ASS_Renderer *renderer = ass_renderer_init(library);
    if (!renderer)
        return std::unexpected(FontError::RendererUnavailable);
    ass_set_frame_size(renderer, kFrameWidth, kFrameHeight);
    ass_set_fonts(renderer, nullptr, environment.defaultFamily.empty() ? nullptr : environment.defaultFamily.c_str(),
                  environment.systemFonts ? ASS_FONTPROVIDER_AUTODETECT : ASS_FONTPROVIDER_NONE, nullptr, 1);
    std::vector<char> buffer(reinterpret_cast<const char *>(script.data()),
                             reinterpret_cast<const char *>(script.data()) + script.size());
    ASS_Track *track = ass_read_memory(library, buffer.data(), buffer.size(), nullptr);
    if (!track) {
        ass_renderer_done(renderer);
        return std::unexpected(FontError::InvalidInput);
    }
    Rendering out;
    for (int i = 0; i < track->n_events; ++i)
        out.times.push_back(track->events[i].Start + track->events[i].Duration / 2);
    std::sort(out.times.begin(), out.times.end());
    out.times.erase(std::unique(out.times.begin(), out.times.end()), out.times.end());
    for (const std::int64_t t : out.times) {
        if (cancel && cancel->load()) {
            ass_free_track(track);
            ass_renderer_done(renderer);
            return std::unexpected(FontError::Cancelled);
        }
        int changed = 0;
        out.hashes.push_back(frameHash(ass_render_frame(renderer, track, t, &changed)));
    }
    ass_free_track(track);
    ass_renderer_done(renderer);
    return out;
}

} // namespace

std::expected<application::FontCollection, FontError>
LibassFontService::collect(const std::vector<std::byte> &script, const application::FontEnvironment &environment,
                           const std::atomic<bool> *cancel)
{
    ASS_Library *library = ass_library_init();
    if (!library)
        return std::unexpected(FontError::RendererUnavailable);
    application::FontCollection collection;
    collection.generation = environment.generation;
    State state;
    state.collection = &collection;
    state.report.provider = environment.systemFonts ? "" : "none";
    ass_set_message_cb(library, onMessage, &state);
    const ASS_HikariFontDiagnostics callbacks{ASS_HIKARI_FONT_DIAGNOSTICS_VERSION, onSelected, onFaceOpened,
                                              onGlyphSimulated, onGlyphMissing};
    ass_hikari_set_font_diagnostics(library, &callbacks, &state);
    for (const auto &a : environment.attachments) {
        if (!a.bytes)
            continue;
        std::vector<unsigned char> copy(reinterpret_cast<const unsigned char *>(a.bytes->data()),
                                        reinterpret_cast<const unsigned char *>(a.bytes->data()) + a.bytes->size());
        state.attachments.emplace_back(a.name, sha256(copy));
    }
    for (const auto &f : environment.externalFonts)
        if (f.bytes) {
            std::vector<unsigned char> copy(reinterpret_cast<const unsigned char *>(f.bytes->data()),
                                            reinterpret_cast<const unsigned char *>(f.bytes->data()) + f.bytes->size());
            state.externals.emplace_back(f.name, sha256(copy));
        }
    auto rendering = renderDocument(library, script, environment, cancel);
    ass_library_done(library);
    if (!rendering)
        return std::unexpected(rendering.error());
    collection.provider = state.report.provider;
    collection.selections = std::move(state.report.faces);
    collection.frameTimesMs = std::move(rendering->times);
    collection.frameHashes = std::move(rendering->hashes);
    return collection;
}

std::expected<application::ReimportCheck, FontError>
LibassFontService::verifyReimport(const std::vector<std::byte> &script, const application::FontCollection &collection,
                                  const std::string &defaultFamily)
{
    application::FontEnvironment clean;
    clean.systemFonts = false;
    clean.defaultFamily = defaultFamily;
    for (const auto &font : collection.fonts)
        clean.attachments.push_back({font.name, font.bytes});
    ASS_Library *library = ass_library_init();
    if (!library)
        return std::unexpected(FontError::RendererUnavailable);
    auto rendering = renderDocument(library, script, clean, nullptr);
    ass_library_done(library);
    if (!rendering)
        return std::unexpected(rendering.error());
    application::ReimportCheck check;
    for (std::size_t i = 0; i < rendering->hashes.size() && i < collection.frameHashes.size(); ++i)
        if (rendering->hashes[i] != collection.frameHashes[i])
            check.differingFrames.push_back(i);
    check.identical = check.differingFrames.empty() && rendering->hashes.size() == collection.frameHashes.size();
    return check;
}

namespace {

// Y6: the faces of one external font (EXTERNAL_FONTS_DIRECTORY), read from
// its bytes as the platform listed them once legacy had registered the file:
// fontconfig's view of the face on Linux (the shim's FcConfigAppFontAddFile,
// platform.h:1230), the GDI family name (name ID 1 in the user's language,
// else English, else the first) on Windows.
std::vector<SystemFace> externalFaces(const application::FontAttachment &font)
{
    std::vector<SystemFace> out;
    if (!font.bytes || font.bytes->empty())
        return out;
    FT_Library ft = nullptr;
    if (FT_Init_FreeType(&ft))
        return out;
    const auto *data = reinterpret_cast<const FT_Byte *>(font.bytes->data());
    const auto size = FT_Long(font.bytes->size());
    long count = 0;
    if (FT_Face probe = nullptr; !FT_New_Memory_Face(ft, data, size, -1, &probe)) {
        count = probe->num_faces;
        FT_Done_Face(probe);
    }
    for (long i = 0; i < count; ++i) {
        FT_Face f = nullptr;
        if (FT_New_Memory_Face(ft, data, size, i, &f))
            continue;
        SystemFace face;
        face.index = int(i);
        face.path = font.name;
        face.externalFile = font.name;
#ifdef _WIN32
        std::vector<std::pair<FT_UShort, std::string>> names; // language, family
        for (FT_UInt n = 0, total = FT_Get_Sfnt_Name_Count(f); n < total; ++n) {
            FT_SfntName name;
            if (!FT_Get_Sfnt_Name(f, n, &name) && name.name_id == TT_NAME_ID_FONT_FAMILY &&
                name.platform_id == TT_PLATFORM_MICROSOFT) {
                const std::string text = decodeName(name);
                if (!text.empty())
                    names.emplace_back(name.language_id, text);
            }
        }
        const LANGID user = GetUserDefaultLangID();
        for (const LANGID want : {user, LANGID(0x0409)}) {
            for (const auto &[language, text] : names)
                if (language == want && face.listedFamily.empty())
                    face.listedFamily = text;
        }
        if (face.listedFamily.empty() && !names.empty())
            face.listedFamily = names.front().second;
        for (const auto &[language, text] : names)
            if (std::find(face.families.begin(), face.families.end(), text) == face.families.end())
                face.families.push_back(text);
        face.weight = (f->style_flags & FT_STYLE_FLAG_BOLD) ? 700 : 400;
        face.italic = (f->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
#else
        if (FcPattern *p = FcFreeTypeQueryFace(f, reinterpret_cast<const FcChar8 *>(font.name.c_str()), unsigned(i), nullptr)) {
            FcChar8 *s = nullptr;
            for (int n = 0; FcPatternGetString(p, FC_FAMILY, n, &s) == FcResultMatch; ++n)
                face.families.emplace_back(reinterpret_cast<const char *>(s));
            if (FcPatternGetString(p, FC_STYLE, 0, &s) == FcResultMatch)
                face.style = reinterpret_cast<const char *>(s);
            if (FcPatternGetString(p, FC_POSTSCRIPT_NAME, 0, &s) == FcResultMatch)
                face.postscriptName = reinterpret_cast<const char *>(s);
            double weight = 0;
            if (FcPatternGetDouble(p, FC_WEIGHT, 0, &weight) == FcResultMatch)
                face.weight = int(FcWeightToOpenTypeDouble(weight));
            int slant = 0;
            if (FcPatternGetInteger(p, FC_SLANT, 0, &slant) == FcResultMatch)
                face.italic = slant != FC_SLANT_ROMAN;
            FcPatternDestroy(p);
        }
#endif
        if (face.families.empty() && f->family_name)
            face.families.emplace_back(f->family_name);
        if (face.style.empty() && f->style_name)
            face.style = f->style_name;
        if (face.postscriptName.empty())
            if (const char *ps = FT_Get_Postscript_Name(f))
                face.postscriptName = ps;
        out.push_back(std::move(face));
        FT_Done_Face(f);
    }
    FT_Done_FreeType(ft);
    return out;
}

// Whether `f` maps every character (U+0000 never: the shim's GetGlyphIndicesW
// marks it missing, platform.h:1110).
bool coversAll(FT_Face f, const std::u32string &characters)
{
    for (const char32_t c : characters)
        if (c == 0 || FT_Get_Char_Index(f, FT_ULong(c)) == 0)
            return false;
    return true;
}

#ifndef _WIN32
bool charsetCovers(const FcCharSet *set, const std::u32string &characters)
{
    for (const char32_t c : characters)
        if (c == 0 || !set || !FcCharSetHasChar(set, FcChar32(c)))
            return false;
    return true;
}
#endif

} // namespace

std::vector<SystemFace> LibassFontService::pickerFaces(const application::FontEnvironment &environment)
{
    std::vector<SystemFace> out = environment.systemFonts ? systemFaces() : std::vector<SystemFace>();
    for (const auto &font : environment.externalFonts)
        for (auto &face : externalFaces(font))
            out.push_back(std::move(face));
    return out;
}

std::vector<bool> LibassFontService::facesCover(const std::vector<SystemFace> &faces,
                                                const application::FontEnvironment &environment,
                                                const std::u32string &characters)
{
    std::vector<bool> out(faces.size(), false);
    FT_Library ft = nullptr;
    if (FT_Init_FreeType(&ft))
        ft = nullptr;
    // External fonts: their own bytes.
    for (std::size_t i = 0; i < faces.size(); ++i) {
        if (faces[i].externalFile.empty() || !ft)
            continue;
        const auto font = std::find_if(environment.externalFonts.begin(), environment.externalFonts.end(),
                                       [&](const application::FontAttachment &a) { return a.name == faces[i].externalFile; });
        if (font == environment.externalFonts.end() || !font->bytes)
            continue;
        FT_Face f = nullptr;
        if (FT_New_Memory_Face(ft, reinterpret_cast<const FT_Byte *>(font->bytes->data()), FT_Long(font->bytes->size()),
                               faces[i].index, &f))
            continue;
#ifdef _WIN32
        out[i] = coversAll(f, characters);
#else
        FcCharSet *set = FcFreeTypeCharSet(f, nullptr);
        out[i] = charsetCovers(set, characters);
        if (set)
            FcCharSetDestroy(set);
#endif
        FT_Done_Face(f);
    }
#ifdef _WIN32
    // Installed fonts: GetGlyphIndicesW with the face selected
    // (FontEnumerator::CheckGlyphsExists) reads its character map.
    std::map<std::string, std::vector<std::size_t>> byPath;
    for (std::size_t i = 0; i < faces.size(); ++i)
        if (faces[i].externalFile.empty() && !faces[i].path.empty())
            byPath[faces[i].path].push_back(i);
    for (const auto &[path, indices] : byPath) {
        QFile file(QString::fromStdString(path));
        if (!ft || !file.open(QIODevice::ReadOnly))
            continue;
        const qint64 size = file.size();
        uchar *data = size > 0 ? file.map(0, size) : nullptr;
        if (!data)
            continue;
        for (const std::size_t i : indices) {
            FT_Face f = nullptr;
            if (FT_New_Memory_Face(ft, data, FT_Long(size), faces[i].index, &f))
                continue;
            out[i] = coversAll(f, characters);
            FT_Done_Face(f);
        }
        file.unmap(data);
    }
#else
    // Installed fonts: the charset of the face fontconfig lists (the shim's
    // FcCharSetHasChar on the matched face, platform.h:1106).
    if (FcInit()) {
        FcPattern *pattern = FcPatternCreate();
        FcObjectSet *objects = FcObjectSetBuild(FC_FILE, FC_INDEX, FC_CHARSET, nullptr);
        if (FcFontSet *set = FcFontList(nullptr, pattern, objects)) {
            std::map<std::pair<std::string, int>, FcCharSet *> sets;
            for (int n = 0; n < set->nfont; ++n) {
                FcChar8 *file = nullptr;
                int index = 0;
                FcCharSet *charset = nullptr;
                if (FcPatternGetString(set->fonts[n], FC_FILE, 0, &file) != FcResultMatch)
                    continue;
                FcPatternGetInteger(set->fonts[n], FC_INDEX, 0, &index);
                if (FcPatternGetCharSet(set->fonts[n], FC_CHARSET, 0, &charset) == FcResultMatch)
                    sets.emplace(std::make_pair(std::string(reinterpret_cast<const char *>(file)), index), charset);
            }
            for (std::size_t i = 0; i < faces.size(); ++i)
                if (faces[i].externalFile.empty())
                    if (const auto it = sets.find({faces[i].path, faces[i].index}); it != sets.end())
                        out[i] = charsetCovers(it->second, characters);
            FcFontSetDestroy(set);
        }
        FcObjectSetDestroy(objects);
        FcPatternDestroy(pattern);
    }
#endif
    if (ft)
        FT_Done_FreeType(ft);
    return out;
}

void LibassFontService::refresh()
{
#ifdef _WIN32
    m_checkForUpdates = true;
#else
    // The listing's configuration reads the font folders again; libass makes
    // its own configuration for each renderer.
    FcInitReinitialize();
#endif
}

std::vector<std::string> LibassFontService::fontDirectories()
{
    std::vector<std::string> out;
#ifdef _WIN32
    // FontEnumerator::CheckFontsProc's two folders (FontEnumerator.cpp:362-374).
    const QString windows = qEnvironmentVariable("WINDIR");
    if (!windows.isEmpty())
        out.push_back((windows + QStringLiteral("\\Fonts\\")).toStdString());
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    if (!local.isEmpty())
        out.push_back((local + QStringLiteral("\\Microsoft\\Windows\\Fonts\\")).toStdString());
#else
    // hikarisub_linux_font_directories (platform.h:1186): fontconfig's font
    // folders, sorted and once each.
    if (!FcInit())
        return out;
    if (FcStrList *list = FcConfigGetFontDirs(FcConfigGetCurrent())) {
        while (FcChar8 *directory = FcStrListNext(list))
            out.emplace_back(reinterpret_cast<const char *>(directory));
        FcStrListDone(list);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
#endif
    return out;
}

} // namespace hikari::backends
