#include "operations.h"

#include <algorithm>

namespace hikari::api {

namespace detail {

void addEvents(Registry &registry)
{
    registry.addEvent(EventType{"document.changed", "A Document's content changed: a commit, Undo or Redo.",
                                schema(R"({"type": "object", "properties": {
                                    "document": {"$ref": "#/$defs/DocumentId"},
                                    "revision": {"$ref": "#/$defs/Revision"},
                                    "step": {"type": "string", "description": "The History step now current."}},
                                    "required": ["document", "revision", "step"], "additionalProperties": false})")});
    registry.addEvent(EventType{"selection.changed", "The selected or the active Line changed.",
                                schema(R"({"type": "object", "properties": {
                                    "document": {"$ref": "#/$defs/DocumentId"},
                                    "revision": {"$ref": "#/$defs/Revision"},
                                    "active": {"anyOf": [{"$ref": "#/$defs/LineId"}, {"type": "null"}]},
                                    "selected": {"type": "array", "items": {"$ref": "#/$defs/LineId"}}},
                                    "required": ["document", "revision", "active", "selected"],
                                    "additionalProperties": false})")});
    registry.addEvent(EventType{"documents.changed", "A Document opened or closed, or another became active.",
                                schema(R"({"type": "object", "properties": {
                                    "documents": {"type": "array", "items": {"$ref": "#/$defs/DocumentId"}},
                                    "editingTarget": {"anyOf": [{"$ref": "#/$defs/DocumentId"}, {"type": "null"}]}},
                                    "required": ["documents", "editingTarget"], "additionalProperties": false})")});
    registry.addEvent(EventType{"panel.changed", "A panel opened, closed, floated, docked or took the focus.",
                                schema(R"({"type": "object", "properties": {
                                    "panel": {"type": "string"}, "title": {"type": "string"}, "open": {"type": "boolean"},
                                    "floating": {"type": "boolean"}, "focused": {"type": "boolean"}},
                                    "required": ["panel", "title", "open", "floating", "focused"],
                                    "additionalProperties": false})")});
    registry.addEvent(EventType{"command.ran", "A command ran, from a menu, a key, the API or anywhere else.",
                                schema(R"({"type": "object", "properties": {"command": {"type": "string"},
                                    "ok": {"type": "boolean", "description": "False when it was refused or failed."}},
                                    "required": ["command", "ok"], "additionalProperties": false})")});
}

}

std::uint64_t Api::subscribe(const Caller &caller, std::vector<std::string> events, Listener listener)
{
    for (const std::string &name : events)
        if (!registry().findEvent(name))
            return 0;
    const std::uint64_t id = m_nextSubscription++;
    m_subscriptions.push_back(Subscription{id, caller, std::move(events), std::move(listener)});
    return id;
}

void Api::unsubscribe(std::uint64_t subscription)
{
    std::erase_if(m_subscriptions, [&](const Subscription &s) { return s.id == subscription; });
}

void Api::publish(const Event &event, std::optional<application::DocumentId> document, const Event *narrowed)
{
    const auto target = m_host.documents ? m_host.documents->workspace().editingTarget() : std::nullopt;
    const std::vector<Subscription> current = m_subscriptions;
    for (const Subscription &s : current) {
        if (!s.events.empty() && std::find(s.events.begin(), s.events.end(), event.name) == s.events.end())
            continue;
        if (document && !s.caller.allDocuments && target != document)
            continue;
        const bool live = std::any_of(m_subscriptions.begin(), m_subscriptions.end(),
                                      [&](const Subscription &l) { return l.id == s.id; });
        if (live)
            s.listener(narrowed && !s.caller.allDocuments ? *narrowed : event);
    }
}

void Api::documentChanged(application::DocumentId document)
{
    const application::EditSession *session = m_host.documents ? m_host.documents->session(document) : nullptr;
    if (!session)
        return;
    const auto history = session->history();
    const std::string step = session->historyCursor() < history.size() ? history[session->historyCursor()].name : "";
    publish(Event{"document.changed", Json::object({{"document", Json(document.value)},
                                                    {"revision", Json(session->revision())},
                                                    {"step", Json(step)}})},
            document);
}

void Api::selectionChanged(application::DocumentId document)
{
    const application::EditSession *session = m_host.documents ? m_host.documents->session(document) : nullptr;
    if (!session)
        return;
    Json data = Json::object({{"document", Json(document.value)}, {"revision", Json(session->revision())}});
    Json selection = detail::selectionJson(*session);
    for (Json::Member &m : selection.asObject())
        data[m.key] = std::move(m.value);
    publish(Event{"selection.changed", std::move(data)}, document);
}

void Api::documentsChanged()
{
    if (!m_host.documents)
        return;
    const application::Workspace &workspace = m_host.documents->workspace();
    const auto target = workspace.editingTarget();
    Json all = Json::array();
    for (const application::DocumentId id : workspace.documents())
        all.push(Json(id.value));
    const Json editing = target ? Json(target->value) : Json(nullptr);
    const Event full{"documents.changed", Json::object({{"documents", all}, {"editingTarget", editing}})};
    const Event narrow{"documents.changed",
                       Json::object({{"documents", target ? Json::array({editing}) : Json::array()}, {"editingTarget", editing}})};
    publish(full, std::nullopt, &narrow);
}

void Api::panelChanged(std::string_view panel)
{
    if (!m_host.panels)
        return;
    for (const PanelState &state : m_host.panels->panels())
        if (state.id == panel)
            publish(Event{"panel.changed", detail::panelJson(state)}, std::nullopt);
}

void Api::commandRan(std::string_view command, bool ok)
{
    publish(Event{"command.ran", Json::object({{"command", Json(command)}, {"ok", Json(ok)}})}, std::nullopt);
}

}
