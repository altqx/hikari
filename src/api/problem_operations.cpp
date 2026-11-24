#include "operations.h"

#include "hikari/core/text_projection.h"

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <limits>
#include <map>

namespace hikari::api::detail {

namespace {

using application::HeldProblem;
using application::Problem;
using application::ProblemField;
using application::ProblemSeverity;
using LineIndex = absl::flat_hash_map<std::uint64_t, const core::LineRecord *>;

constexpr std::array<std::pair<std::string_view, ProblemSeverity>, 4> kSeverities{{{"error", ProblemSeverity::Error},
                                                                                  {"warning", ProblemSeverity::Warning},
                                                                                  {"info", ProblemSeverity::Info},
                                                                                  {"hint", ProblemSeverity::Hint}}};
constexpr std::size_t kMostMessageCharacters = 500;
constexpr std::string_view kQcName = "QC notes";

std::string_view severityName(ProblemSeverity severity)
{
    for (const auto &[name, s] : kSeverities)
        if (s == severity)
            return name;
    return "error";
}

ProblemSeverity severityOf(std::string_view name)
{
    for (const auto &[n, s] : kSeverities)
        if (n == name)
            return s;
    return ProblemSeverity::Error;
}

std::string_view fieldName(ProblemField field)
{
    return field == ProblemField::Translation ? "translation" : "text";
}

std::u8string u8(std::string_view text)
{
    return {reinterpret_cast<const char8_t *>(text.data()), text.size()};
}

LineIndex indexLines(const core::Document &document)
{
    LineIndex index;
    for (const core::LineRecord *line : document.lines())
        index.emplace(line->id.value, line);
    return index;
}

const core::LineRecord *lineIn(const LineIndex &index, core::LineId line)
{
    const auto it = index.find(line.value);
    return it == index.end() ? nullptr : it->second;
}

std::string sourceOf(const Caller &caller)
{
    constexpr std::string_view kPlugin = "plugin:";
    return caller.id.starts_with(kPlugin) ? caller.id.substr(kPlugin.size()) : caller.id;
}

std::unexpected<Error> invalid(std::string message, std::string pointer)
{
    return fail(errors::kInvalidParams, std::move(message), Json::object({{"pointer", Json(std::move(pointer))}}));
}

std::unexpected<Error> stale(const application::EditSession &session, std::string message)
{
    return fail(errors::kStaleRevision, std::move(message), Json::object({{"current", Json(session.revision())}}));
}

std::optional<std::size_t> offset(const Json &object, std::string_view key)
{
    const Json *value = object.find(key);
    return value ? std::optional(std::size_t(value->asInt())) : std::nullopt;
}

std::expected<Problem, Error> problemOf(const Json &given, const std::string &at, const LineIndex &lines)
{
    const Json *lineId = given.find("line");
    if (!lineId)
        return invalid("Each problem names its Line.", at + "/line");
    Problem problem;
    problem.line = core::LineId{std::uint64_t(lineId->asInt())};
    const core::LineRecord *line = lineIn(lines, problem.line);
    if (!line)
        return fail(errors::kNotFound, "No Line " + std::to_string(problem.line.value) + " in the Document.",
                    Json::object({{"line", Json(std::int64_t(problem.line.value))}, {"pointer", Json(at + "/line")}}));
    if (const Json *field = given.find("field"))
        problem.field = field->asString() == "translation" ? ProblemField::Translation : ProblemField::Text;
    problem.against = application::problemFieldText(*line, problem.field);
    const std::size_t length = core::toUtf16(problem.against).size();
    problem.from = offset(given, "from");
    problem.to = offset(given, "to");
    if (problem.from.has_value() != problem.to.has_value())
        return invalid("from and to come together.", at + (problem.from ? "/to" : "/from"));
    if (problem.to && (*problem.from > *problem.to || *problem.to > length))
        return invalid("from <= to <= " + std::to_string(length) + ", the field's length in UTF-16, is needed.", at + "/to");
    problem.severity = severityOf(given.find("severity")->asString());
    problem.message = given.find("message")->asString();
    if (const Json *code = given.find("code"))
        problem.code = code->asString();
    if (const Json *fixes = given.find("fixes"))
        for (std::size_t f = 0; f < fixes->asArray().size(); ++f) {
            const Json &fix = fixes->asArray()[f];
            application::ProblemFix out{fix.find("title")->asString(), {}};
            const Json::Array &edits = fix.find("edits")->asArray();
            for (std::size_t e = 0; e < edits.size(); ++e) {
                const std::size_t from = *offset(edits[e], "from");
                const std::size_t to = *offset(edits[e], "to");
                if (from > to || to > length)
                    return invalid("An edit needs from <= to <= " + std::to_string(length) + ".",
                                   at + "/fixes/" + std::to_string(f) + "/edits/" + std::to_string(e) + "/to");
                out.edits.push_back({from, to, edits[e].find("insert")->asString()});
            }
            problem.fixes.push_back(std::move(out));
        }
    return problem;
}

std::expected<Target, Error> setterTarget(const Context &context, const Json &params)
{
    if (!context.caller.id.starts_with("plugin:"))
        return fail(errors::kNotGranted, "Only plugins set problems.", Json::object());
    if (!context.host.problems)
        return fail(errors::kUnavailable, "Problems are not available.");
    return target(context, params);
}

Result problemsSet(const Context &context, const Json &params)
{
    const auto t = setterTarget(context, params);
    if (!t)
        return std::unexpected(t.error());
    const application::EditSession &session = *t->session;
    if (std::uint64_t(params.find("revision")->asInt()) != session.revision())
        return stale(session, "The Document changed since that revision; read it again.");
    const LineIndex lines = indexLines(session.document());
    std::vector<Problem> problems;
    const Json::Array &given = params.find("problems")->asArray();
    for (std::size_t i = 0; i < given.size(); ++i) {
        auto problem = problemOf(given[i], "/problems/" + std::to_string(i), lines);
        if (!problem)
            return std::unexpected(problem.error());
        problems.push_back(std::move(*problem));
    }
    const Json *set = params.find("set");
    const std::size_t count = problems.size();
    if (!context.host.problems->set(context.caller.id, sourceOf(context.caller), context.caller.label, t->id,
                                    set ? set->asString() : "default", std::move(problems)))
        return fail(errors::kRefused, "A plugin holds at most 20000 problems per Document.",
                    Json::object({{"reason", Json("too_many")}}));
    return Json::object({{"document", Json(t->id.value)}, {"count", Json(std::int64_t(count))}});
}

Result problemsClear(const Context &context, const Json &params)
{
    std::optional<application::DocumentId> document;
    if (params.find("document")) {
        const auto t = setterTarget(context, params);
        if (!t)
            return std::unexpected(t.error());
        document = t->id;
    } else if (!context.caller.id.starts_with("plugin:")) {
        return fail(errors::kNotGranted, "Only plugins set problems.", Json::object());
    } else if (!context.host.problems) {
        return fail(errors::kUnavailable, "Problems are not available.");
    }
    const Json *set = params.find("set");
    const std::size_t cleared = context.host.problems->clear(sourceOf(context.caller), document,
                                                             set ? std::optional(set->asString()) : std::nullopt);
    return Json::object({{"cleared", Json(std::int64_t(cleared))}});
}

struct QcProblem {
    std::string id;
    application::QcNote note;
    std::optional<core::LineId> line;
};

std::string hex(std::uint64_t value)
{
    std::array<char, 17> out{};
    std::snprintf(out.data(), out.size(), "%016llx", static_cast<unsigned long long>(value));
    return out.data();
}

std::uint64_t noteHash(const application::QcNote &note)
{
    std::uint64_t hash = 0xcbf29ce484222325ull;
    const auto mix = [&](std::string_view bytes) {
        for (const char c : bytes) {
            hash ^= std::uint8_t(c);
            hash *= 0x100000001b3ull;
        }
        hash ^= 0xff;
        hash *= 0x100000001b3ull;
    };
    mix(std::to_string(note.ms));
    mix(narrow(note.section));
    mix(narrow(note.text));
    return hash;
}

std::vector<QcProblem> qcProblems(const core::Document &document)
{
    const auto lines = document.lines();
    std::vector<QcProblem> out;
    std::map<std::string, int> seen;
    for (application::QcNote &note : application::documentQcNotes(document)) {
        if (note.done)
            continue;
        std::string id = "q" + hex(noteHash(note));
        if (const int again = seen[id]++)
            id += "-" + std::to_string(again);
        out.push_back(QcProblem{std::move(id), std::move(note), std::nullopt});
        out.back().line = application::qcNoteLine(lines, out.back().note.ms);
    }
    return out;
}

bool isQcId(std::string_view id)
{
    constexpr std::size_t kDigits = 16;
    if (id.size() < 1 + kDigits || id[0] != 'q')
        return false;
    const std::string_view digits = id.substr(1, kDigits);
    const std::string_view again = id.substr(1 + kDigits);
    return std::ranges::all_of(digits, [](char c) { return std::isxdigit(std::uint8_t(c)) != 0; }) &&
           (again.empty() || (again.size() > 1 && again[0] == '-' &&
                              std::ranges::all_of(again.substr(1), [](char c) { return std::isdigit(std::uint8_t(c)) != 0; })));
}

std::string qcMessage(const application::QcNote &note)
{
    std::string text = narrow(note.text);
    for (std::size_t at = 0; (at = text.find("\\N", at)) != std::string::npos;)
        text.replace(at, 2, "\n");
    if (text.empty())
        text = narrow(application::qcNoteTimeText(note.ms));
    std::size_t characters = 0;
    for (std::size_t i = 0; i < text.size(); ++i)
        if ((std::uint8_t(text[i]) & 0xC0) != 0x80 && ++characters > kMostMessageCharacters)
            return text.substr(0, i);
    return text;
}

Json qcJson(const QcProblem &qc)
{
    Json out = Json::object({{"id", Json(qc.id)}});
    if (qc.line)
        out["line"] = Json(qc.line->value);
    out["time"] = Json(qc.note.ms);
    out["severity"] = Json("info");
    out["message"] = Json(qcMessage(qc.note));
    out["source"] = Json::object({{"kind", Json("qc")},
                                  {"id", Json(application::Problems::kQcSource)},
                                  {"set", Json(narrow(qc.note.section))},
                                  {"name", Json(kQcName)}});
    return out;
}

Json problemJson(const HeldProblem &held, const std::string &name)
{
    const Problem &p = held.problem;
    Json out = Json::object({{"id", Json(held.id)},
                             {"line", Json(p.line.value)},
                             {"field", Json(fieldName(p.field))}});
    if (p.from) {
        out["from"] = Json(std::uint64_t(*p.from));
        out["to"] = Json(std::uint64_t(*p.to));
    }
    out["severity"] = Json(severityName(p.severity));
    out["message"] = Json(p.message);
    if (!p.code.empty())
        out["code"] = Json(p.code);
    out["source"] = Json::object(
        {{"kind", Json("plugin")}, {"id", Json(held.source)}, {"set", Json(held.set)}, {"name", Json(name)}});
    if (!p.fixes.empty()) {
        Json fixes = Json::array();
        for (const application::ProblemFix &fix : p.fixes) {
            Json edits = Json::array();
            for (const application::ProblemEdit &edit : fix.edits)
                edits.push(Json::object({{"from", Json(std::uint64_t(edit.from))},
                                         {"to", Json(std::uint64_t(edit.to))},
                                         {"insert", Json(edit.insert)}}));
            fixes.push(Json::object({{"title", Json(fix.title)}, {"edits", std::move(edits)}}));
        }
        out["fixes"] = std::move(fixes);
    }
    return out;
}

bool current(const HeldProblem &held, const LineIndex &lines)
{
    const core::LineRecord *line = lineIn(lines, held.problem.line);
    return line && application::problemFieldText(*line, held.problem.field) == held.problem.against;
}

Result problemsList(const Context &context, const Json &params)
{
    const auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const core::Document &document = t->session->document();
    const LineIndex lines = indexLines(document);
    absl::flat_hash_map<std::uint64_t, std::size_t> rows;
    for (const core::LineRecord *line : document.lines())
        rows.emplace(line->id.value, rows.size());
    std::optional<std::vector<std::uint64_t>> only;
    if (const Json *given = params.find("lines")) {
        only.emplace();
        for (const Json &line : given->asArray())
            only->push_back(std::uint64_t(line.asInt()));
    }
    const Json *severity = params.find("severity");
    const ProblemSeverity worst = severity ? severityOf(severity->asString()) : ProblemSeverity::Hint;
    const Json *source = params.find("source");
    const auto wanted = [&](std::optional<core::LineId> line, ProblemSeverity s, std::string_view from) {
        if (source && source->asString() != from)
            return false;
        if (s > worst)
            return false;
        return !only || (line && std::ranges::contains(*only, line->value));
    };
    struct Row {
        std::size_t row;
        std::size_t from;
        Json problem;
    };
    constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();
    std::vector<Row> out;
    for (const QcProblem &qc : qcProblems(document))
        if (wanted(qc.line, ProblemSeverity::Info, application::Problems::kQcSource))
            out.push_back(Row{qc.line ? rows.at(qc.line->value) : kNone, 0, qcJson(qc)});
    if (context.host.problems)
        for (const HeldProblem &held : context.host.problems->of(t->id))
            if (current(held, lines) && wanted(held.problem.line, held.problem.severity, held.source))
                out.push_back(Row{rows.at(held.problem.line.value), held.problem.from.value_or(0),
                                  problemJson(held, context.host.problems->name(held.source))});
    std::ranges::stable_sort(out, [](const Row &a, const Row &b) { return std::pair(a.row, a.from) < std::pair(b.row, b.from); });
    Json problems = Json::array();
    for (Row &row : out)
        problems.push(std::move(row.problem));
    return Json::object({{"document", Json(t->id.value)},
                         {"revision", Json(t->session->revision())},
                         {"problems", std::move(problems)}});
}

Result problemsFix(const Context &context, const Json &params)
{
    const auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const application::EditSession &session = *t->session;
    const std::string &id = params.find("problem")->asString();
    const std::size_t index = std::size_t(params.find("fix")->asInt());
    if (isQcId(id)) {
        for (const QcProblem &qc : qcProblems(session.document()))
            if (qc.id == id)
                return invalid("A QC note has no quick fixes.", "/fix");
        return stale(session, "That QC note changed or is done; list the problems again.");
    }
    const HeldProblem *held = context.host.problems ? context.host.problems->find(t->id, id) : nullptr;
    if (!held) {
        if (context.host.problems && context.host.problems->gave(id))
            return stale(session, "That problem is gone; list the problems again.");
        return fail(errors::kNotFound, "No problem has id " + id + ".", Json::object({{"problem", Json(id)}}));
    }
    if (index >= held->problem.fixes.size())
        return invalid("The problem has " + std::to_string(held->problem.fixes.size()) + " fixes.", "/fix");
    if (!current(*held, indexLines(session.document())))
        return stale(session, "The Line changed since the problem was set; list the problems again.");
    const application::ProblemFix &fix = held->problem.fixes[index];
    std::vector<application::ProblemEdit> edits = fix.edits;
    std::ranges::stable_sort(edits, {}, &application::ProblemEdit::from);
    for (std::size_t i = 1; i < edits.size(); ++i)
        if (edits[i].from < edits[i - 1].to)
            return invalid("The fix's edits overlap.", "/fix");
    const std::u16string before = core::toUtf16(held->problem.against);
    std::u16string after;
    std::size_t at = 0;
    for (const application::ProblemEdit &edit : edits) {
        after.append(before, at, edit.from - at);
        after += core::toUtf16(u8(edit.insert));
        at = edit.to;
    }
    after.append(before, at);
    const std::string field(fieldName(held->problem.field));
    const Json apply = Json::object(
        {{"document", Json(t->id.value)},
         {"revision", Json(session.revision())},
         {"name", Json(fix.title)},
         {"edits", Json::array({Json::object({{"op", Json("set_line")},
                                              {"line", Json(held->problem.line.value)},
                                              {"fields", Json::object({{field, Json(narrow(core::toUtf8(after)))}})}})})}});
    Caller named = context.caller;
    named.label = context.host.problems->name(held->source);
    const Context renamed{context.host, named, context.files, context.folders, context.grantFile, context.groups};
    return documentApply(renamed, apply);
}

constexpr std::string_view kProblemsListResult = R"({"type": "object", "properties": {
    "document": {"$ref": "#/$defs/DocumentId"},
    "revision": {"$ref": "#/$defs/Revision"},
    "problems": {"type": "array", "items": {"$ref": "#/$defs/Problem"}}},
    "required": ["document", "revision", "problems"], "additionalProperties": false})";

}

void addProblemOperations(Registry &registry)
{
    addOperation(registry, Access::Read, "problems.list",
                 "The problems on a Document: the open QC notes and the problems plugins set, in Document order, "
                 "then by from, those with no Line last.",
                 R"({"type": "object", "properties": {
                     "document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."},
                     "lines": {"type": "array", "maxItems": 20000, "items": {"$ref": "#/$defs/LineId"}, "description": "These Lines only."},
                     "severity": {"enum": ["error", "warning", "info", "hint"], "description": "This severity and worse only."},
                     "source": {"type": "string", "description": "One source id only."}},
                     "additionalProperties": false})",
                 kProblemsListResult, now(problemsList));
    addOperation(registry, Access::Write, "problems.set",
                 "Replaces the caller's problems under one set on a Document. Never saved and not an undo step; they "
                 "go when the plugin stops, the Document closes, or their Line or field changes. Plugins only.",
                 R"({"type": "object", "properties": {
                     "document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."},
                     "revision": {"$ref": "#/$defs/Revision", "description": "The revision the offsets were measured on; stale_revision when it moved."},
                     "set": {"type": "string", "pattern": "^[a-z0-9]+(-[a-z0-9]+)*$", "maxLength": 64,
                         "description": "The plugin's own name for this group of checks; default when omitted."},
                     "problems": {"type": "array", "maxItems": 20000, "items": {"$ref": "#/$defs/Problem"}}},
                     "required": ["revision", "problems"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {
                     "document": {"$ref": "#/$defs/DocumentId"},
                     "count": {"type": "integer", "minimum": 0}},
                     "required": ["document", "count"], "additionalProperties": false})",
                 now(problemsSet));
    addOperation(registry, Access::Write, "problems.clear", "Removes the caller's problems. Plugins only.",
                 R"({"type": "object", "properties": {
                     "document": {"$ref": "#/$defs/DocumentId", "description": "Every Document when omitted."},
                     "set": {"type": "string", "description": "Every set of the caller's when omitted."}},
                     "additionalProperties": false})",
                 R"({"type": "object", "properties": {"cleared": {"type": "integer", "minimum": 0}},
                     "required": ["cleared"], "additionalProperties": false})",
                 now(problemsClear));
    addOperation(registry, Access::Write, "problems.fix",
                 "Applies one of a problem's quick fixes through document.apply, as one undo step named after the "
                 "problem's source and the fix; stale_revision when its Line changed since the problem was set.",
                 R"({"type": "object", "properties": {
                     "document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."},
                     "problem": {"type": "string", "description": "An id from problems.list."},
                     "fix": {"type": "integer", "minimum": 0, "description": "The fix's index."}},
                     "required": ["problem", "fix"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {
                     "revision": {"$ref": "#/$defs/Revision"},
                     "inserted": {"type": "array", "items": {"$ref": "#/$defs/LineId"}, "description": "New Lines, in edit order."},
                     "changed": {"type": "boolean", "description": "False when the edits changed nothing: no undo step was added."}},
                     "required": ["revision", "inserted", "changed"], "additionalProperties": false})",
                 now(problemsFix));
}

}
