#include "operations.h"

#include "hikari/core/ass_save.h"
#include "hikari/core/style.h"

#include <algorithm>

namespace hikari::api::detail {

namespace {

using application::CommandRefusal;
using application::DocumentId;
using application::EditSession;

std::u8string wide(const std::string &s)
{
    return std::u8string(s.begin(), s.end());
}

core::TimeField timeOf(std::int64_t ms)
{
    return core::TimeField{core::legacy::assTimeText(ms), core::DocumentTime(ms * 1000)};
}

core::IntField intOf(std::int64_t v)
{
    return core::IntField{wide(std::to_string(v)), v};
}

// True when a field changed.
bool applyLineFields(core::LineRecord &line, const Json &fields)
{
    bool changed = false;
    const auto set = [&](auto &target, auto value) {
        if (!(target == value)) {
            target = std::move(value);
            changed = true;
        }
    };
    const auto setInt = [&](core::IntField &target, std::int64_t value) {
        if (target.value != value || target.lexeme.empty()) {
            target = intOf(value);
            changed = true;
        }
    };
    const auto setTime = [&](core::TimeField &target, std::int64_t ms) {
        if (target.value != core::DocumentTime(ms * 1000) || target.lexeme.empty()) {
            target = timeOf(ms);
            changed = true;
        }
    };
    for (const Json::Member &m : fields.asObject()) {
        const Json &v = m.value;
        if (m.key == "comment")
            set(line.comment, v.asBool());
        else if (m.key == "unconfirmed")
            set(line.unconfirmed, v.asBool());
        else if (m.key == "layer")
            setInt(line.layer, v.asInt());
        else if (m.key == "marginL")
            setInt(line.marginLeft, v.asInt());
        else if (m.key == "marginR")
            setInt(line.marginRight, v.asInt());
        else if (m.key == "marginV")
            setInt(line.marginVertical, v.asInt());
        else if (m.key == "start")
            setTime(line.start, v.asInt());
        else if (m.key == "end")
            setTime(line.end, v.asInt());
        else if (m.key == "style")
            set(line.style, wide(v.asString()));
        else if (m.key == "actor")
            set(line.actor, wide(v.asString()));
        else if (m.key == "effect")
            set(line.effect, wide(v.asString()));
        else if (m.key == "text")
            set(line.text, wide(v.asString()));
        else if (m.key == "translation")
            set(line.translation, wide(v.asString()));
    }
    if (changed)
        line.unparsed = false;
    return changed;
}

core::LineRecord freshLine()
{
    core::LineRecord line;
    line.layer = intOf(0);
    line.start = timeOf(0);
    line.end = timeOf(0);
    line.style = u8"Default";
    line.marginLeft = intOf(0);
    line.marginRight = intOf(0);
    line.marginVertical = intOf(0);
    return line;
}

constexpr std::u8string_view kNewStyle =
    u8"Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1";

void applyStyleFields(core::StyleValues &style, const Json &fields)
{
    for (const Json::Member &m : fields.asObject()) {
        const Json &v = m.value;
        const auto text = [&] { return wide(v.isString() ? v.asString() : std::string()); };
        if (m.key == "name")
            style.name = text();
        else if (m.key == "fontname")
            style.fontname = text();
        else if (m.key == "fontsize")
            style.fontsize = text();
        else if (m.key == "primaryColour")
            style.primary = core::legacy::colour(text());
        else if (m.key == "secondaryColour")
            style.secondary = core::legacy::colour(text());
        else if (m.key == "outlineColour")
            style.outline = core::legacy::colour(text());
        else if (m.key == "backColour")
            style.back = core::legacy::colour(text());
        else if (m.key == "bold")
            style.bold = v.asBool();
        else if (m.key == "italic")
            style.italic = v.asBool();
        else if (m.key == "underline")
            style.underline = v.asBool();
        else if (m.key == "strikeOut")
            style.strikeOut = v.asBool();
        else if (m.key == "scaleX")
            style.scaleX = text();
        else if (m.key == "scaleY")
            style.scaleY = text();
        else if (m.key == "spacing")
            style.spacing = text();
        else if (m.key == "angle")
            style.angle = text();
        else if (m.key == "borderStyle")
            style.borderStyle = static_cast<int>(v.asInt());
        else if (m.key == "outline")
            style.outlineWidth = text();
        else if (m.key == "shadow")
            style.shadow = text();
        else if (m.key == "alignment")
            style.alignment = text();
        else if (m.key == "marginL")
            style.marginLeft = text();
        else if (m.key == "marginR")
            style.marginRight = text();
        else if (m.key == "marginV")
            style.marginVertical = text();
        else if (m.key == "encoding")
            style.encoding = text();
    }
}

class ChangeSet {
public:
    explicit ChangeSet(const Json &edits) : m_edits(edits) {}

    // The Lines the edits name, for the Command's declared touches.
    std::set<core::LineId> touches() const
    {
        std::set<core::LineId> lines;
        for (const Json &edit : m_edits.asArray())
            for (const char *key : {"line", "after", "before"})
                if (const Json *id = edit.find(key); id && id->isInt())
                    lines.insert(core::LineId{std::uint64_t(id->asInt())});
        return lines;
    }

    // False stops the run: problem() is set, or nothing changed.
    bool apply(core::Document &doc)
    {
        for (std::size_t i = 0; i < m_edits.asArray().size(); ++i) {
            m_at = i;
            if (!applyOne(doc, m_edits.asArray()[i]))
                return false;
        }
        return m_changed;
    }

    bool changed() const { return m_changed; }
    const std::optional<Error> &problem() const { return m_problem; }
    const std::vector<core::LineId> &inserted() const { return m_inserted; }

private:
    bool refuse(std::string_view code, std::string message, Json data = Json::object())
    {
        data["edit"] = Json(std::uint64_t(m_at));
        m_problem = Error{std::string(code), "Edit " + std::to_string(m_at) + ": " + std::move(message), std::move(data)};
        return false;
    }

    bool lineGone(const Json &id)
    {
        return refuse(errors::kNotFound, "no Line has id " + id.dump() + ".", Json::object({{"line", id}}));
    }

    static const core::LineRecord *findLine(const core::Document &doc, const Json &id)
    {
        for (const core::LineRecord *line : doc.lines())
            if (line->id.value == std::uint64_t(id.asInt()))
                return line;
        return nullptr;
    }

    bool applyOne(core::Document &doc, const Json &edit)
    {
        const std::string &op = edit.find("op")->asString();
        if (op == "set_line") {
            const Json &id = *edit.find("line");
            const core::LineRecord *line = findLine(doc, id);
            if (!line)
                return lineGone(id);
            core::LineRecord copy = *line;
            if (!applyLineFields(copy, *edit.find("fields")))
                return true;
            m_changed = true;
            return doc.editLine(line->id, [&](core::LineRecord &l) { applyLineFields(l, *edit.find("fields")); });
        }
        if (op == "insert_line") {
            core::LineRecord line = freshLine();
            if (const Json *fields = edit.find("fields"))
                applyLineFields(line, *fields);
            std::optional<core::LineId> id;
            if (const Json *after = edit.find("after")) {
                if (!findLine(doc, *after))
                    return lineGone(*after);
                id = doc.insertLineAfter(core::LineId{std::uint64_t(after->asInt())}, std::move(line));
            } else if (const Json *before = edit.find("before")) {
                if (!findLine(doc, *before))
                    return lineGone(*before);
                id = doc.insertLineBefore(core::LineId{std::uint64_t(before->asInt())}, std::move(line));
            } else {
                id = doc.appendLine(std::move(line));
            }
            if (!id)
                return refuse(errors::kRefused, "the Document cannot take a Line here.");
            m_inserted.push_back(*id);
            m_changed = true;
            return true;
        }
        if (op == "delete_line") {
            const Json &id = *edit.find("line");
            if (!findLine(doc, id))
                return lineGone(id);
            m_changed = true;
            return doc.removeLine(core::LineId{std::uint64_t(id.asInt())});
        }
        if (op == "move_line") {
            const Json &id = *edit.find("line");
            if (!findLine(doc, id))
                return lineGone(id);
            std::optional<core::LineId> before;
            if (const Json *b = edit.find("before"); b && !b->isNull()) {
                if (!findLine(doc, *b))
                    return lineGone(*b);
                before = core::LineId{std::uint64_t(b->asInt())};
            }
            const auto lines = doc.lines();
            const auto at = std::find_if(lines.begin(), lines.end(), [&](auto *l) { return l->id.value == std::uint64_t(id.asInt()); });
            const auto next = at + 1;
            const bool inPlace = before ? next != lines.end() && (*next)->id == *before : next == lines.end();
            if (inPlace || (before && before->value == std::uint64_t(id.asInt())))
                return true;
            m_changed = true;
            return doc.moveLine(core::LineId{std::uint64_t(id.asInt())}, before);
        }
        if (op == "set_style")
            return setStyle(doc, edit);
        if (op == "delete_style") {
            const std::string &name = edit.find("name")->asString();
            if (!doc.removeStyle(wide(name)))
                return refuse(errors::kNotFound, "no Style is named \"" + name + "\".", Json::object({{"style", Json(name)}}));
            m_changed = true;
            return true;
        }
        if (op == "set_info") {
            const std::u8string key = wide(edit.find("key")->asString());
            const Json &value = *edit.find("value");
            const auto current = doc.scriptInfo(key);
            if (value.isNull()) {
                if (!current)
                    return true;
                m_changed = true;
                return doc.removeScriptInfo(key);
            }
            if (current && *current == wide(value.asString()))
                return true;
            if (!doc.setScriptInfo(key, wide(value.asString())))
                return refuse(errors::kRefused, "only ASS Documents have Script Info.");
            m_changed = true;
            return true;
        }
        return refuse(errors::kInvalidParams, "unknown op.");
    }

    bool setStyle(core::Document &doc, const Json &edit)
    {
        const std::string &name = edit.find("name")->asString();
        const auto styles = core::decodeStyles(doc);
        const auto named = [&](std::u8string_view n) {
            return std::find_if(styles.begin(), styles.end(), [&](const core::StyleValues &s) { return s.name == n; });
        };
        const auto found = named(wide(name));
        core::StyleValues values = found != styles.end() ? *found : core::legacy::decodeStyle(kNewStyle, false);
        values.name = wide(name);
        if (const Json *fields = edit.find("fields"))
            applyStyleFields(values, *fields);
        if (values.name.empty())
            return refuse(errors::kInvalidParams, "a Style needs a name.");
        if (values.name != wide(name) && named(values.name) != styles.end())
            return refuse(errors::kRefused, "a Style is already named \"" + narrow(values.name) + "\".",
                          Json::object({{"style", Json(narrow(values.name))}}));
        auto fields = core::legacy::styleRawFields(values);
        if (found != styles.end()) {
            if (fields == core::legacy::styleRawFields(*found))
                return true;
            m_changed = true;
            return doc.editStyle(std::size_t(found - styles.begin()), std::move(fields)) ||
                   refuse(errors::kRefused, "the Style cannot be changed.");
        }
        m_changed = true;
        return doc.appendStyle(std::move(fields)) || refuse(errors::kRefused, "the Document has no Styles section.");
    }

    const Json &m_edits;
    std::size_t m_at = 0;
    bool m_changed = false;
    std::optional<Error> m_problem;
    std::vector<core::LineId> m_inserted;
};

std::unexpected<Error> refusal(CommandRefusal refusal, const EditSession &session)
{
    switch (refusal) {
    case CommandRefusal::StaleRevision:
        return fail(errors::kStaleRevision, "The Document changed since that revision; read it again.",
                    Json::object({{"current", Json(session.revision())}}));
    case CommandRefusal::ReadOnly: return fail(errors::kReadOnly, "A macro is running on the Document.");
    case CommandRefusal::Protected: return fail(errors::kProtected, "The comparison reference cannot be changed.");
    case CommandRefusal::InvalidDraft:
        return fail(errors::kRefused, "The Line editor holds a change that cannot be committed.",
                    Json::object({{"reason", Json("invalid_draft")}}));
    case CommandRefusal::GroupBreak: {
        Json lines = Json::array();
        for (const core::LineId id : session.lastGroupBreak())
            lines.push(Json(id.value));
        return fail(errors::kRefused, "The edits would break a Line group.",
                    Json::object({{"reason", Json("group_break")}, {"lines", lines}}));
    }
    case CommandRefusal::UnknownLine:
        return fail(errors::kNotFound, "An edit names a Line that no longer exists.");
    case CommandRefusal::Invalid: break;
    }
    return fail(errors::kRefused, "The edits were refused.", Json::object({{"reason", Json("invalid")}}));
}

std::unexpected<Error> unwritable(const Context &context, const Target &t)
{
    if (t.session->isProtected() || context.host.documents->workspace().reference() == t.id)
        return fail(errors::kProtected, "The comparison reference cannot be changed.");
    return fail(errors::kReadOnly, "A macro is running on the Document.");
}

bool writable(const Context &context, const Target &t)
{
    return !t.session->isProtected() && !t.session->isReadOnly() &&
           context.host.documents->workspace().reference() != t.id;
}

Result documentApply(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    if (!writable(context, *t))
        return unwritable(context, *t);
    EditSession &session = *t->session;
    const Json &edits = *params.find("edits");
    ChangeSet changes(edits);
    const std::string &name = params.find("name")->asString();
    application::Command command;
    command.name = context.caller.label.empty() ? name : context.caller.label + ": " + name;
    command.expectedRevision = std::uint64_t(params.find("revision")->asInt());
    command.touches = changes.touches();
    command.apply = [&](core::Document &doc) { return changes.apply(doc); };
    if (auto ran = session.run(command); !ran) {
        if (changes.problem())
            return std::unexpected(*changes.problem());
        if (ran.error() == CommandRefusal::Invalid && !changes.changed())
            return Json::object({{"revision", Json(session.revision())}, {"inserted", Json::array()}, {"changed", Json(false)}});
        const auto lines = session.document().lines();
        if (ran.error() == CommandRefusal::UnknownLine)
            for (const core::LineId id : command.touches)
                if (std::none_of(lines.begin(), lines.end(), [&](auto *l) { return l->id == id; }))
                    return fail(errors::kNotFound, "No Line has id " + std::to_string(id.value) + ".",
                                Json::object({{"line", Json(id.value)}}));
        return refusal(ran.error(), session);
    }
    Json inserted = Json::array();
    for (const core::LineId id : changes.inserted())
        inserted.push(Json(id.value));
    context.host.documents->committed(t->id);
    return Json::object({{"revision", Json(session.revision())}, {"inserted", inserted}, {"changed", Json(true)}});
}

Result selectionSet(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const auto lines = t->session->document().lines();
    const auto exists = [&](const Json &id) {
        return std::any_of(lines.begin(), lines.end(), [&](auto *l) { return l->id.value == std::uint64_t(id.asInt()); });
    };
    application::Selection selection;
    for (const Json &id : params.find("selected")->asArray()) {
        if (!exists(id))
            return fail(errors::kNotFound, "No Line has id " + id.dump() + ".", Json::object({{"line", id}}));
        selection.selected.insert(core::LineId{std::uint64_t(id.asInt())});
    }
    if (const Json *active = params.find("active"); active && !active->isNull()) {
        if (!exists(*active))
            return fail(errors::kNotFound, "No Line has id " + active->dump() + ".", Json::object({{"line", *active}}));
        selection.active = core::LineId{std::uint64_t(active->asInt())};
    } else if (!selection.selected.empty()) {
        for (const core::LineRecord *line : lines)
            if (selection.selected.contains(line->id) && !selection.active)
                selection.active = line->id;
    }
    selection.anchor = selection.active;
    if (!context.host.documents->select(t->id, selection))
        return fail(errors::kRefused, "The Document cannot take that selection now.",
                    Json::object({{"reason", Json("selection")}}));
    Json out = Json::object({{"document", Json(t->id.value)}, {"revision", Json(t->session->revision())}});
    Json current = selectionJson(*t->session);
    for (Json::Member &m : current.asObject())
        out[m.key] = std::move(m.value);
    return out;
}

void write(Registry &registry, std::string name, std::string summary, std::string_view params, std::string_view result,
           SyncHandler handler)
{
    registry.add(Operation{std::move(name), Access::Write, std::move(summary), schema(params), schema(result), 1,
                           std::nullopt, now(std::move(handler))});
}

}

void addWriteOperations(Registry &registry)
{
    write(registry, "document.apply",
          "Applies edits prepared against a revision as one undo step; a stale revision changes nothing.",
          R"({"type": "object", "properties": {
              "document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."},
              "revision": {"$ref": "#/$defs/Revision", "description": "The revision the edits were prepared against."},
              "name": {"type": "string", "minLength": 1, "maxLength": 200,
                  "description": "Names the undo step, after the caller's name."},
              "edits": {"type": "array", "minItems": 1, "maxItems": 100000, "items": {"$ref": "#/$defs/Edit"},
                  "description": "Applied in order; any refusal leaves the Document as it was."}},
              "required": ["revision", "name", "edits"], "additionalProperties": false})",
          R"({"type": "object", "properties": {
              "revision": {"$ref": "#/$defs/Revision"},
              "inserted": {"type": "array", "items": {"$ref": "#/$defs/LineId"}, "description": "New Lines, in edit order."},
              "changed": {"type": "boolean", "description": "False when the edits changed nothing: no undo step was added."}},
              "required": ["revision", "inserted", "changed"], "additionalProperties": false})",
          documentApply);
    write(registry, "selection.set", "Selects Lines and makes one active, as a click in the Grid would; never an undo step.",
          R"({"type": "object", "properties": {
              "document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."},
              "selected": {"type": "array", "items": {"$ref": "#/$defs/LineId"}},
              "active": {"anyOf": [{"$ref": "#/$defs/LineId"}, {"type": "null"}],
                  "description": "The first selected Line when omitted."}},
              "required": ["selected"], "additionalProperties": false})",
          R"({"type": "object", "properties": {"document": {"$ref": "#/$defs/DocumentId"},
              "revision": {"$ref": "#/$defs/Revision"},
              "active": {"anyOf": [{"$ref": "#/$defs/LineId"}, {"type": "null"}]},
              "selected": {"type": "array", "items": {"$ref": "#/$defs/LineId"}}},
              "required": ["document", "revision", "active", "selected"], "additionalProperties": false})",
          selectionSet);
}

}
