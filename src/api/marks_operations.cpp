#include "operations.h"

#include <array>

namespace hikari::api::detail {

namespace {

using application::MarkColour;

constexpr std::array<std::pair<std::string_view, MarkColour>, 5> kColours{{{"accent", MarkColour::Accent},
                                                                            {"warning", MarkColour::Warning},
                                                                            {"danger", MarkColour::Danger},
                                                                            {"success", MarkColour::Success},
                                                                            {"muted", MarkColour::Muted}}};

std::string_view colourName(MarkColour colour)
{
    for (const auto &[name, c] : kColours)
        if (c == colour)
            return name;
    return "accent";
}

MarkColour colourOf(std::string_view name)
{
    for (const auto &[n, c] : kColours)
        if (n == name)
            return c;
    return MarkColour::Accent;
}

std::expected<Target, Error> markTarget(const Context &context, const Json &params)
{
    if (!context.caller.id.starts_with("plugin:"))
        return fail(errors::kNotGranted, "Only plugins mark Grid rows.", Json::object());
    if (!context.host.marks)
        return fail(errors::kUnavailable, "Row marks are not available.");
    return target(context, params);
}

Result marksSet(const Context &context, const Json &params)
{
    const auto t = markTarget(context, params);
    if (!t)
        return std::unexpected(t.error());
    const core::Document &document = t->session->document();
    std::vector<application::RowMark> marks;
    const Json::Array &given = params.find("marks")->asArray();
    for (std::size_t i = 0; i < given.size(); ++i) {
        const core::LineId line{std::uint64_t(given[i].find("line")->asInt())};
        if (!findLine(document, line))
            return fail(errors::kNotFound, "No Line " + std::to_string(line.value) + " in the Document.",
                        Json::object({{"line", Json(std::int64_t(line.value))}, {"pointer", Json("/marks/" + std::to_string(i))}}));
        marks.push_back(application::RowMark{line, colourOf(given[i].find("colour")->asString()),
                                             given[i].find("tooltip")->asString()});
    }
    if (!context.host.marks->set(context.caller.id, context.caller.label, t->id, marks))
        return fail(errors::kRefused, "A plugin marks at most 20000 Lines of a Document.",
                    Json::object({{"reason", Json("too_many")}}));
    return Json::object({{"document", Json(t->id.value)}, {"marked", Json(std::int64_t(marks.size()))}});
}

Result marksClear(const Context &context, const Json &params)
{
    const auto t = markTarget(context, params);
    if (!t)
        return std::unexpected(t.error());
    std::vector<core::LineId> lines;
    if (const Json *given = params.find("lines"))
        for (const Json &line : given->asArray())
            lines.push_back(core::LineId{std::uint64_t(line.asInt())});
    context.host.marks->clear(context.caller.id, t->id, lines);
    return Json::object({{"document", Json(t->id.value)}});
}

Result marksList(const Context &context, const Json &params)
{
    const auto t = markTarget(context, params);
    if (!t)
        return std::unexpected(t.error());
    const core::Document &document = t->session->document();
    Json list = Json::array();
    for (const application::RowMark &mark : context.host.marks->of(context.caller.id, t->id))
        if (findLine(document, mark.line))
            list.push(Json::object({{"line", Json(std::int64_t(mark.line.value))},
                                    {"colour", Json(colourName(mark.colour))},
                                    {"tooltip", Json(mark.tooltip)}}));
    return Json::object({{"document", Json(t->id.value)}, {"marks", list}});
}

constexpr std::string_view kMark = R"({"type": "object", "properties": {
    "line": {"$ref": "#/$defs/LineId"}, "colour": {"$ref": "#/$defs/MarkColour"},
    "tooltip": {"type": "string", "minLength": 1, "maxLength": 200,
        "description": "What the mark means; shown on hover after the plugin's name and read by screen readers."}},
    "required": ["line", "colour", "tooltip"], "additionalProperties": false})";

std::string withMark(std::string_view text)
{
    std::string out(text);
    if (const std::size_t at = out.find("@mark"); at != std::string::npos)
        out.replace(at, 5, kMark);
    return out;
}

}

void addMarkOperations(Registry &registry)
{
    addOperation(registry, Access::Write, "marks.set",
                 "Marks Grid rows with a coloured bar and a tooltip, replacing the caller's marks on those Lines. Marks "
                 "belong to the plugin, are never saved and go when it stops. Not an undo step. Plugins only.",
                 withMark(R"({"type": "object", "properties": {
                     "document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."},
                     "marks": {"type": "array", "minItems": 1, "maxItems": 20000, "items": @mark}},
                     "required": ["marks"], "additionalProperties": false})"),
                 R"({"type": "object", "properties": {"document": {"$ref": "#/$defs/DocumentId"},
                     "marked": {"type": "integer", "minimum": 0}},
                     "required": ["document", "marked"], "additionalProperties": false})",
                 now(marksSet));
    addOperation(registry, Access::Write, "marks.clear", "Removes the caller's Grid row marks. Plugins only.",
                 R"({"type": "object", "properties": {
                     "document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."},
                     "lines": {"type": "array", "items": {"$ref": "#/$defs/LineId"},
                         "description": "These Lines only; every mark of the caller when omitted."}},
                     "additionalProperties": false})",
                 R"({"type": "object", "properties": {"document": {"$ref": "#/$defs/DocumentId"}},
                     "required": ["document"], "additionalProperties": false})",
                 now(marksClear));
    addOperation(registry, Access::Read, "marks.list", "The caller's Grid row marks on a Document. Plugins only.",
                 R"({"type": "object", "properties": {
                     "document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."}},
                     "additionalProperties": false})",
                 withMark(R"({"type": "object", "properties": {"document": {"$ref": "#/$defs/DocumentId"},
                     "marks": {"type": "array", "items": @mark}},
                     "required": ["document", "marks"], "additionalProperties": false})"),
                 now(marksList));
}

}
