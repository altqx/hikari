#include "operations.h"

#include "hikari/core/legacy_regex.h"
#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <array>

namespace hikari::api::detail {

using application::DocumentId;
using application::EditSession;

std::string narrow(std::u8string_view s)
{
    return std::string(s.begin(), s.end());
}

bool sees(const Caller &caller, std::optional<DocumentId> editingTarget, DocumentId document)
{
    return caller.allDocuments || editingTarget == document || std::ranges::contains(caller.attached, document);
}

bool visible(const Context &context, DocumentId document)
{
    return sees(context.caller, context.host.documents->workspace().editingTarget(), document);
}

std::expected<Target, Error> target(const Context &context, const Json &params)
{
    if (!context.host.documents)
        return fail(errors::kUnavailable, "No documents are available.");
    const DocumentHost &documents = *context.host.documents;
    DocumentId id;
    if (const Json *given = params.find("document")) {
        id = DocumentId{std::uint64_t(given->asInt())};
        if (!visible(context, id))
            return fail(errors::kNotGranted,
                        "Only the active Document and the attached ones are available without the documents.all grant.",
                        Json::object({{"document", *given}}));
    } else if (const auto editing = documents.workspace().editingTarget()) {
        id = *editing;
    } else {
        return fail(errors::kNotFound, "No Document is open.");
    }
    EditSession *session = documents.session(id);
    if (!session)
        return fail(errors::kNotFound, "No open Document has id " + std::to_string(id.value) + ".",
                    Json::object({{"document", Json(id.value)}}));
    return Target{id, session};
}

Json selectionJson(const EditSession &session)
{
    const application::Selection &selection = session.selection();
    Json selected = Json::array();
    for (const core::LineRecord *line : session.document().lines())
        if (selection.selected.contains(line->id))
            selected.push(Json(line->id.value));
    return Json::object({{"active", selection.active ? Json(selection.active->value) : Json(nullptr)}, {"selected", selected}});
}

namespace {

std::int64_t ms(const core::DocumentTime &t)
{
    return t.microseconds() / 1000;
}

std::string_view formatName(core::SubtitleFormat format)
{
    switch (format) {
    case core::SubtitleFormat::Ass: return "ass";
    case core::SubtitleFormat::Srt: return "srt";
    case core::SubtitleFormat::TMPlayer: return "tmplayer";
    case core::SubtitleFormat::MicroDvd: return "microdvd";
    case core::SubtitleFormat::Mpl2: return "mpl2";
    case core::SubtitleFormat::PlainText: return "text";
    }
    return "text";
}

constexpr std::array kLineFields{"comment", "layer",  "start", "end",         "style",      "actor",        "marginL",
                                 "marginR", "marginV", "effect", "text", "translation", "unconfirmed", "generatedFrom"};

Json fieldValue(const core::LineRecord &line, std::string_view field)
{
    if (field == "comment")
        return line.comment;
    if (field == "layer")
        return line.layer.value;
    if (field == "start")
        return ms(line.start.value);
    if (field == "end")
        return ms(line.end.value);
    if (field == "style")
        return narrow(line.style);
    if (field == "actor")
        return narrow(line.actor);
    if (field == "marginL")
        return line.marginLeft.value;
    if (field == "marginR")
        return line.marginRight.value;
    if (field == "marginV")
        return line.marginVertical.value;
    if (field == "effect")
        return narrow(line.effect);
    if (field == "text")
        return narrow(line.text);
    if (field == "translation")
        return narrow(line.translation);
    if (field == "unconfirmed")
        return line.unconfirmed;
    if (field == "generatedFrom")
        return line.generatedFrom.value ? Json(line.generatedFrom.value) : Json(nullptr);
    return {};
}

// `fields`: the LineField names to include, or null for all of them.
Json lineJson(const core::LineRecord &line, std::size_t row, const Json *fields)
{
    Json out = Json::object({{"id", Json(line.id.value)}, {"row", Json(std::uint64_t(row))}});
    for (const char *field : kLineFields) {
        if (fields) {
            const auto &wanted = fields->asArray();
            if (std::find(wanted.begin(), wanted.end(), Json(field)) == wanted.end())
                continue;
        }
        out[field] = fieldValue(line, field);
    }
    return out;
}

Json documentSummary(const DocumentHost &documents, DocumentId id, const EditSession &session)
{
    const application::Workspace &workspace = documents.workspace();
    const std::string *title = workspace.title(id);
    const auto path = documents.path(id);
    return Json::object({
        {"document", Json(id.value)},
        {"title", title ? Json(*title) : Json("")},
        {"path", path ? Json(*path) : Json(nullptr)},
        {"format", Json(formatName(session.document().format()))},
        {"revision", Json(session.revision())},
        {"editingTarget", Json(workspace.editingTarget() == id)},
        {"reference", Json(workspace.reference() == id)},
        {"dirty", Json(session.isDirty())},
        {"readOnly", Json(session.isReadOnly() || session.isProtected())},
    });
}

Result documentsList(const Context &context, const Json &)
{
    if (!context.host.documents)
        return fail(errors::kUnavailable, "No documents are available.");
    const DocumentHost &documents = *context.host.documents;
    const application::Workspace &workspace = documents.workspace();
    std::vector<DocumentId> ids = workspace.tabs();
    if (const auto reference = workspace.reference(); reference && std::find(ids.begin(), ids.end(), *reference) == ids.end())
        ids.push_back(*reference);
    Json list = Json::array();
    for (const DocumentId id : ids) {
        if (!visible(context, id))
            continue;
        if (const EditSession *session = documents.session(id))
            list.push(documentSummary(documents, id, *session));
    }
    return Json::object({{"documents", list}});
}

Result documentsGet(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const EditSession &session = *t->session;
    Json out = documentSummary(*context.host.documents, t->id, session);
    out["lineCount"] = Json(std::uint64_t(session.document().lines().size()));
    out["styleCount"] = Json(std::uint64_t(core::styleCount(session.document())));
    const auto draft = session.draftLine();
    out["draftLine"] = draft ? Json(draft->value) : Json(nullptr);
    return out;
}

Result linesList(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const auto lines = t->session->document().lines();
    const Json *offsetParam = params.find("offset");
    const Json *limitParam = params.find("limit");
    const std::size_t offset = offsetParam ? std::size_t(offsetParam->asInt()) : 0;
    const std::size_t limit = limitParam ? std::size_t(limitParam->asInt()) : 1000;
    Json out = Json::object({{"revision", Json(t->session->revision())}, {"total", Json(std::uint64_t(lines.size()))}});
    Json list = Json::array();
    for (std::size_t i = offset; i < lines.size() && i < offset + limit; ++i)
        list.push(lineJson(*lines[i], i + 1, params.find("fields")));
    out["lines"] = std::move(list);
    return out;
}

Result linesGet(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const auto lines = t->session->document().lines();
    Json list = Json::array();
    for (const Json &id : params.find("ids")->asArray()) {
        const auto at = std::find_if(lines.begin(), lines.end(),
                                     [&](const core::LineRecord *l) { return l->id.value == std::uint64_t(id.asInt()); });
        if (at == lines.end())
            return fail(errors::kNotFound, "No Line has id " + id.dump() + ".", Json::object({{"line", id}}));
        list.push(lineJson(**at, std::size_t(at - lines.begin()) + 1, params.find("fields")));
    }
    return Json::object({{"revision", Json(t->session->revision())}, {"lines", list}});
}

Result linesFind(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const std::string field = params.find("in") ? params.find("in")->asString() : "text";
    std::optional<core::LegacyRegex> regex;
    if (const Json *pattern = params.find("regex")) {
        const bool ignoreCase = params.find("ignoreCase") && params.find("ignoreCase")->asBool();
        regex.emplace();
        if (!regex->compile(core::toUtf16(std::u8string(pattern->asString().begin(), pattern->asString().end())),
                            core::LegacyRegex::Advanced | (ignoreCase ? core::LegacyRegex::IgnoreCase : 0)))
            return fail(errors::kInvalidParams, "The regular expression is not valid.",
                        Json::object({{"pointer", Json("/regex")}}));
    }
    const auto exact = [&](const char *name, std::u8string_view value) {
        const Json *wanted = params.find(name);
        return !wanted || wanted->asString() == narrow(value);
    };
    const Json *text = params.find("text");
    const Json *comment = params.find("comment");
    const Json *from = params.find("from");
    const Json *to = params.find("to");
    const std::size_t limit = params.find("limit") ? std::size_t(params.find("limit")->asInt()) : 500;
    const auto lines = t->session->document().lines();
    Json list = Json::array();
    std::size_t total = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const core::LineRecord &line = *lines[i];
        if (!exact("style", line.style) || !exact("actor", line.actor) || !exact("effect", line.effect))
            continue;
        if (comment && comment->asBool() != line.comment)
            continue;
        if (from && ms(line.end.value) <= from->asInt())
            continue;
        if (to && ms(line.start.value) >= to->asInt())
            continue;
        const std::string subject = fieldValue(line, field).asString();
        if (text && subject.find(text->asString()) == std::string::npos)
            continue;
        if (regex && !regex->matches(core::toUtf16(std::u8string(subject.begin(), subject.end()))))
            continue;
        if (++total <= limit)
            list.push(lineJson(line, i + 1, params.find("fields")));
    }
    return Json::object(
        {{"revision", Json(t->session->revision())}, {"total", Json(std::uint64_t(total))}, {"lines", list}});
}

constexpr std::array kStyleFields{"name",    "fontname", "fontsize",    "primaryColour", "secondaryColour", "outlineColour",
                                  "backColour", "bold",  "italic",      "underline",     "strikeOut",       "scaleX",
                                  "scaleY",  "spacing",  "angle",       "borderStyle",   "outline",         "shadow",
                                  "alignment", "marginL", "marginR",    "marginV",       "encoding"};

Json styleJson(const core::StyleValues &style)
{
    const auto raw = core::legacy::styleRawFields(style);
    Json out = Json::object();
    for (std::size_t i = 0; i < kStyleFields.size() && i < raw.size(); ++i) {
        const std::string_view name = kStyleFields[i];
        const std::string value = narrow(raw[i]);
        if (name == "bold" || name == "italic" || name == "underline" || name == "strikeOut")
            out[name] = Json(value != "0");
        else if (name == "borderStyle")
            out[name] = Json(style.borderStyle);
        else
            out[name] = Json(value);
    }
    return out;
}

Result stylesList(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    Json list = Json::array();
    for (const core::StyleValues &style : core::decodeStyles(t->session->document()))
        list.push(styleJson(style));
    return Json::object({{"revision", Json(t->session->revision())}, {"styles", list}});
}

Result infoList(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    Json list = Json::array();
    for (const core::Section &section : t->session->document().sections()) {
        if (section.kind != core::SectionKind::ScriptInfo)
            continue;
        for (const core::Record &record : section.records)
            if (const auto *property = std::get_if<core::PropertyRecord>(&record))
                list.push(Json::object({{"key", Json(narrow(property->key))}, {"value", Json(narrow(property->value))}}));
    }
    return Json::object({{"revision", Json(t->session->revision())}, {"info", list}});
}

Result selectionGet(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    Json out = Json::object({{"document", Json(t->id.value)}, {"revision", Json(t->session->revision())}});
    Json selection = selectionJson(*t->session);
    for (Json::Member &m : selection.asObject())
        out[m.key] = std::move(m.value);
    return out;
}

constexpr std::string_view kDocumentParam = R"("document": {"$ref": "#/$defs/DocumentId",
    "description": "Defaults to the active Document."})";

std::string documentParams(std::string_view more = {}, std::string_view required = {})
{
    std::string text = R"({"type": "object", "properties": {)";
    text += kDocumentParam;
    if (!more.empty()) {
        text += ", ";
        text += more;
    }
    text += "}, ";
    if (!required.empty()) {
        text += R"("required": )";
        text += required;
        text += ", ";
    }
    text += R"("additionalProperties": false})";
    return text;
}

constexpr std::string_view kSummary = R"(
    "document": {"$ref": "#/$defs/DocumentId"},
    "title": {"type": "string"},
    "path": {"type": ["string", "null"]},
    "format": {"enum": ["ass", "srt", "tmplayer", "microdvd", "mpl2", "text"]},
    "revision": {"$ref": "#/$defs/Revision"},
    "editingTarget": {"type": "boolean"},
    "reference": {"type": "boolean", "description": "The protected comparison reference."},
    "dirty": {"type": "boolean"},
    "readOnly": {"type": "boolean", "description": "A macro holds it, or it is the protected reference."})";

std::string summarySchema(std::string_view more = {})
{
    std::string text = R"({"type": "object", "properties": {)";
    text += kSummary;
    if (!more.empty()) {
        text += ", ";
        text += more;
    }
    text += R"(}, "additionalProperties": false})";
    return text;
}

constexpr std::string_view kFieldsParam =
    R"("fields": {"type": "array", "items": {"$ref": "#/$defs/LineField"},
        "description": "Only these fields besides id and row; all of them when omitted."})";

constexpr std::string_view kLinesResult = R"({"type": "object", "properties": {
    "revision": {"$ref": "#/$defs/Revision"},
    "total": {"type": "integer", "minimum": 0},
    "lines": {"type": "array", "items": {"$ref": "#/$defs/Line"}}},
    "required": ["revision", "lines"], "additionalProperties": false})";

void read(Registry &registry, std::string name, std::string summary, const std::string &params, std::string_view result,
          SyncHandler handler)
{
    registry.add(Operation{std::move(name), Access::Read, std::move(summary), schema(params), schema(result), 1,
                           std::nullopt, now(std::move(handler))});
}

}

void addDocumentOperations(Registry &registry)
{
    read(registry, "documents.list", "The open Documents, in tab order, with the protected reference last.",
         R"({"type": "object", "additionalProperties": false})",
         R"({"type": "object", "properties": {"documents": {"type": "array", "items": )" + summarySchema() +
             R"(}}, "required": ["documents"], "additionalProperties": false})",
         documentsList);
    read(registry, "documents.get", "One Document's summary, Line and Style counts and its pending Line editor draft.",
         documentParams(),
         summarySchema(R"("lineCount": {"type": "integer", "minimum": 0},
             "styleCount": {"type": "integer", "minimum": 0},
             "draftLine": {"anyOf": [{"$ref": "#/$defs/LineId"}, {"type": "null"}],
                 "description": "The Line with uncommitted Line editor changes; a write commits them first."})"),
         documentsGet);
    read(registry, "lines.list", "Lines in Document order, a page at a time.",
         documentParams(std::string(R"("offset": {"type": "integer", "minimum": 0},
             "limit": {"type": "integer", "minimum": 1, "maximum": 5000, "description": "1000 when omitted."}, )") +
                        std::string(kFieldsParam)),
         kLinesResult, linesList);
    read(registry, "lines.get", "Lines by id, in the order asked.",
         documentParams(std::string(R"("ids": {"type": "array", "items": {"$ref": "#/$defs/LineId"}, "minItems": 1,
             "maxItems": 5000}, )") + std::string(kFieldsParam),
                        R"(["ids"])"),
         kLinesResult, linesGet);
    read(registry, "lines.find",
         "Lines matching every criterion given: text or regex in one field, exact Style, Actor or Effect, Comment, and a time window.",
         documentParams(std::string(R"(
             "text": {"type": "string", "minLength": 1, "description": "A case-sensitive substring."},
             "regex": {"type": "string", "minLength": 1, "description": "A regular expression, as Find and replace takes it."},
             "ignoreCase": {"type": "boolean", "description": "For regex."},
             "in": {"enum": ["text", "translation", "actor", "effect", "style"], "description": "Where text and regex look; text when omitted."},
             "style": {"type": "string"},
             "actor": {"type": "string"},
             "effect": {"type": "string"},
             "comment": {"type": "boolean"},
             "from": {"type": "integer", "description": "Lines ending after this time, in milliseconds."},
             "to": {"type": "integer", "description": "Lines starting before this time, in milliseconds."},
             "limit": {"type": "integer", "minimum": 1, "maximum": 5000, "description": "500 when omitted; total still counts every match."}, )") +
                        std::string(kFieldsParam)),
         kLinesResult, linesFind);
    read(registry, "styles.list", "The Document's Styles, in order.", documentParams(),
         R"({"type": "object", "properties": {"revision": {"$ref": "#/$defs/Revision"},
             "styles": {"type": "array", "items": {"$ref": "#/$defs/Style"}}},
             "required": ["revision", "styles"], "additionalProperties": false})",
         stylesList);
    read(registry, "info.list", "The Document's Script Info properties, in order.", documentParams(),
         R"({"type": "object", "properties": {"revision": {"$ref": "#/$defs/Revision"},
             "info": {"type": "array", "items": {"type": "object", "properties": {"key": {"type": "string"},
                 "value": {"type": "string"}}, "required": ["key", "value"], "additionalProperties": false}}},
             "required": ["revision", "info"], "additionalProperties": false})",
         infoList);
    read(registry, "selection.get", "The selected Lines, in Document order, and the active Line.", documentParams(),
         R"({"type": "object", "properties": {"document": {"$ref": "#/$defs/DocumentId"},
             "revision": {"$ref": "#/$defs/Revision"},
             "active": {"anyOf": [{"$ref": "#/$defs/LineId"}, {"type": "null"}]},
             "selected": {"type": "array", "items": {"$ref": "#/$defs/LineId"}}},
             "required": ["document", "revision", "active", "selected"], "additionalProperties": false})",
         selectionGet);
}

}
