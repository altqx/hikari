#include "hikari/application/script_properties.h"

#include <cstdlib>
#include <set>

namespace hikari::application {

namespace {

using u8 = std::u8string;

int toInt(const std::optional<u8> &v)
{
    if (!v)
        return 0;
    return std::atoi(std::string(v->begin(), v->end()).c_str());
}

u8 fromInt(int v)
{
    const auto s = std::to_string(v);
    return u8(s.begin(), s.end());
}

// SubsGrid::GetASSRes: PlayRes with its fallbacks (1280x720, or 16:9 / 9:16).
std::pair<int, int> assRes(const core::Document &d)
{
    int x = toInt(d.scriptInfo(u8"PlayResX")), y = toInt(d.scriptInfo(u8"PlayResY"));
    if (x < 1 && y < 1)
        return {1280, 720};
    if (x < 1)
        return {static_cast<int>(static_cast<float>(y) * (16.0 / 9.0)), y};
    if (y < 1)
        return {x, static_cast<int>(static_cast<float>(x) * (9.0 / 16.0))};
    return {x, y};
}

} // namespace

const std::vector<u8> &matrixNames()
{
    static const std::vector<u8> names{u8"None",  u8"TV.601", u8"PC.601", u8"TV.709",  u8"PC.709",
                                       u8"TV.FCC", u8"PC.FCC", u8"TV.240M", u8"PC.240M"};
    return names;
}

ScriptProperties scriptProperties(const core::Document &d)
{
    ScriptProperties p;
    auto text = [&](const char8_t *key) { return d.scriptInfo(key).value_or(u8()); };
    p.title = text(u8"Title");
    p.originalScript = text(u8"Original Script");
    p.originalTranslation = text(u8"Original Translation");
    p.originalEditing = text(u8"Original Editing");
    p.originalTiming = text(u8"Original Timing");
    p.updatedBy = text(u8"Script Updated By");
    std::tie(p.playResX, p.playResY) = assRes(d);
    p.layoutResX = toInt(d.scriptInfo(u8"LayoutResX"));
    p.layoutResY = toInt(d.scriptInfo(u8"LayoutResY"));
    const u8 matrix = text(u8"YCbCr Matrix");
    p.matrix = 0;
    for (std::size_t i = 0; i < matrixNames().size(); ++i)
        if (matrixNames()[i] == matrix)
            p.matrix = static_cast<int>(i);
    p.wrapStyle = toInt(d.scriptInfo(u8"WrapStyle"));
    p.reverseCollisions = text(u8"Collisions") == u8"Reverse";
    p.scaledBorderAndShadow = text(u8"ScaledBorderAndShadow") != u8"no";
    return p;
}

std::expected<void, CommandRefusal> applyScriptProperties(EditSession &session, const ScriptProperties &edited,
                                                          const ScriptPropertiesEdits &edits, bool link)
{
    const auto &doc = session.document();
    std::vector<std::pair<u8, u8>> writes;
    auto put = [&](const char8_t *key, u8 value) { writes.emplace_back(key, std::move(value)); };
    // GetASSRes ran when the dialog opened: missing values are written.
    {
        const int x = toInt(doc.scriptInfo(u8"PlayResX")), y = toInt(doc.scriptInfo(u8"PlayResY"));
        const auto [fx, fy] = assRes(doc);
        if (x < 1 && y < 1) {
            put(u8"PlayResX", u8"1280");
            put(u8"PlayResY", u8"720");
        } else if (x < 1) {
            put(u8"PlayResX", fromInt(fx));
        } else if (y < 1) {
            put(u8"PlayResY", fromInt(fy));
        }
    }
    int newx = edited.playResX, newy = edited.playResY, newlx = edited.layoutResX, newly = edited.layoutResY;
    if (newx < 1 && newy < 1) {
        newx = 1280;
        newy = 720;
    } else if (newx < 1) {
        newx = static_cast<int>(static_cast<float>(newy) * (16.0 / 9.0));
    } else if (newy < 1) {
        newy = static_cast<int>(static_cast<float>(newx) * (9.0 / 16.0));
    }
    if (newlx < 1 && newly < 1) {
        newlx = 1280;
        newly = 720;
    } else if (newlx < 1) {
        newlx = static_cast<int>(static_cast<float>(newlx) * (16.0 / 9.0)); // legacy: from itself
    } else if (newly < 1) {
        newly = static_cast<int>(static_cast<float>(newlx) * (9.0 / 16.0));
    }
    if (!edited.title.empty()) {
        if (edits.title)
            put(u8"Title", edited.title);
    } else {
        put(u8"Title", u8"HikariSub Ass File");
    }
    if (edits.originalScript)
        put(u8"Original Script", edited.originalScript);
    if (edits.originalTranslation)
        put(u8"Original Translation", edited.originalTranslation);
    if (edits.originalEditing)
        put(u8"Original Editing", edited.originalEditing);
    if (edits.originalTiming)
        put(u8"Original Timing", edited.originalTiming);
    if (edits.updatedBy)
        put(u8"Script Updated By", edited.updatedBy);
    if (edits.playResX)
        put(u8"PlayResX", fromInt(newx));
    if (edits.playResY)
        put(u8"PlayResY", fromInt(newy));
    const bool linkAndLayoutExists = link && !doc.scriptInfo(u8"LayoutResX").value_or(u8()).empty();
    if (edits.layoutResX || linkAndLayoutExists)
        put(u8"LayoutResX", fromInt(link ? newx : newlx));
    if (edits.layoutResY || linkAndLayoutExists)
        put(u8"LayoutResY", fromInt(link ? newy : newly));
    const auto opened = scriptProperties(doc);
    if (edited.matrix != opened.matrix && edited.matrix >= 0 && edited.matrix < static_cast<int>(matrixNames().size()))
        put(u8"YCbCr Matrix", matrixNames()[static_cast<std::size_t>(edited.matrix)]);
    if (edited.wrapStyle != opened.wrapStyle)
        put(u8"WrapStyle", fromInt(edited.wrapStyle));
    const u8 collisions = edited.reverseCollisions ? u8"Reverse" : u8"Normal";
    if (doc.scriptInfo(u8"Collisions").value_or(u8()) != collisions)
        put(u8"Collisions", collisions);
    const u8 border = edited.scaledBorderAndShadow ? u8"yes" : u8"no";
    if (doc.scriptInfo(u8"ScaledBorderAndShadow").value_or(u8()) != border)
        put(u8"ScaledBorderAndShadow", border);
    // AddSInfo with an unchanged value records nothing.
    std::erase_if(writes, [&](const auto &w) { return doc.scriptInfo(w.first) == w.second; });
    if (writes.empty())
        return {};
    const auto ran = session.run(Command{"Changing the subtitle header", session.revision(), {}, [&](core::Document &d) {
                                             for (const auto &[key, value] : writes)
                                                 if (!d.setScriptInfo(key, value))
                                                     return false;
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    return {};
}

} // namespace hikari::application
