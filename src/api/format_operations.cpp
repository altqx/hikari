#include "hikari/api/formats.h"

#include "operations.h"

namespace hikari::api {

namespace {

FormatInfo builtIn(std::string id, std::string title, std::vector<std::string> extensions, bool imports, bool exports)
{
    return {std::move(id), std::move(title), std::move(extensions), imports, exports, {}};
}

Json formatJson(const FormatInfo &format)
{
    Json extensions = Json::array();
    for (const auto &extension : format.extensions)
        extensions.push(Json(extension));
    Json out = Json::object({{"id", Json(format.id)},
                             {"title", Json(format.title)},
                             {"extensions", extensions},
                             {"import", Json(format.imports)},
                             {"export", Json(format.exports)}});
    if (!format.plugin.empty())
        out["plugin"] = Json(format.plugin);
    return out;
}

Result formatsList(const Context &context, const Json &)
{
    Json list = Json::array();
    for (const FormatInfo &format : allFormats(context.host))
        list.push(formatJson(format));
    return Json::object({{"formats", list}});
}

}

std::span<const FormatInfo> builtInFormats()
{
    static const std::vector<FormatInfo> formats{
        builtIn("ass", "Advanced SubStation Alpha", {"ass", "ssa"}, true, true),
        builtIn("srt", "SubRip", {"srt"}, true, true),
        builtIn("vtt", "WebVTT", {"vtt"}, false, true),
        builtIn("sbv", "YouTube SBV", {"sbv"}, false, true),
        builtIn("text", "Plain text", {"txt"}, true, true),
        builtIn("microdvd", "MicroDVD", {"sub"}, true, true),
        builtIn("mpl2", "MPL2", {"txt"}, true, true),
        builtIn("tmplayer", "TMPlayer", {"txt"}, true, true),
        builtIn("ttml", "TTML (IMSC1)", {"ttml"}, true, true),
        builtIn("dfxp", "DFXP", {"dfxp"}, false, true),
        builtIn("srv3", "YouTube timed text (srv3)", {"ytt", "srv3"}, true, true),
        builtIn("lrc", "LRC lyrics", {"lrc"}, true, false),
        builtIn("krc", "KRC lyrics", {"krc"}, true, false),
        builtIn("qrc", "QRC lyrics", {"qrc"}, true, false),
    };
    return formats;
}

std::vector<FormatInfo> allFormats(const Host &host)
{
    const auto own = builtInFormats();
    std::vector<FormatInfo> out(own.begin(), own.end());
    if (host.plugins)
        for (FormatInfo &format : host.plugins->formats())
            out.push_back(std::move(format));
    return out;
}

namespace detail {

void addFormatOperations(Registry &registry)
{
    addOperation(registry, Access::Read, "formats.list",
                 "The subtitle formats HikariSub can open and export, built-in and from plugins.",
                 R"({"type": "object", "additionalProperties": false})",
                 R"({"type": "object", "properties": {
  "formats": {"type": "array", "items": {"type": "object", "properties": {
    "id": {"type": "string", "description": "ass, srt, … or plugin:<plugin id>/<format id>."},
    "title": {"type": "string"},
    "extensions": {"type": "array", "items": {"type": "string"}},
    "import": {"type": "boolean"},
    "export": {"type": "boolean"},
    "plugin": {"type": "string", "description": "The plugin's id, for a plugin format."}},
   "required": ["id", "title", "extensions", "import", "export"], "additionalProperties": false}}},
 "required": ["formats"], "additionalProperties": false})",
                 now(formatsList));
}

}

}
