#include "hikari/backends/libass_font_service.h"

#include <QCryptographicHash>
#include <QFile>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
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
    FontReport report;
    std::size_t current = 0;
    std::map<int, std::size_t> uidToFace; // per request: the renderer's selector-local ids
    std::vector<std::pair<std::string, std::string>> attachments; // name, sha256
};

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
    auto &request = st.report.requests[st.current];
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
    const std::size_t index = st.report.faces.size();
    st.report.faces.push_back(std::move(face));
    if (s->uid >= 0)
        st.uidToFace[s->uid] = index;
    request.faces.push_back(index);
    if (s->stage == ASS_HIKARI_FONT_REQUESTED)
        request.requestedFamilyFound = true;
    else
        request.usedFallback = true;
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

    for (std::size_t i = 0; i < requests.size(); ++i) {
        const auto &request = requests[i];
        state.report.requests.push_back({request, {}, false, false, {}});
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
    if (SUCCEEDED(factory->GetSystemFontCollection(&collection, FALSE))) {
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

} // namespace hikari::backends
