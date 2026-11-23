#include "hikari/app/plugin_popups.h"

#include "hikari/api/schema.h"
#include "plugin_variant.h"

#include <set>
#include <string_view>

namespace hikari::app {

namespace {

using api::Json;

constexpr std::string_view kDefs = R"({
"QuickPickItem": {"type": "object", "properties": {
  "kind": {"enum": ["item", "separator"]},
  "id": {"type": "string", "minLength": 1, "maxLength": 200},
  "label": {"type": "string", "minLength": 1, "maxLength": 200},
  "description": {"type": "string", "maxLength": 200},
  "detail": {"type": "string", "maxLength": 500},
  "icon": {"type": "string", "maxLength": 68, "pattern": "^(own:)?[a-z0-9]+(-[a-z0-9]+)*$"},
  "picked": {"type": "boolean"},
  "always_show": {"type": "boolean"},
  "disabled": {"type": "string", "minLength": 1, "maxLength": 200},
  "buttons": {"type": "array", "maxItems": 2, "items": {"$ref": "#/$defs/QuickPickButton"}}},
 "required": ["label"], "additionalProperties": false},
"QuickPickButton": {"type": "object", "properties": {
  "id": {"type": "string", "pattern": "^[a-z0-9]+(-[a-z0-9]+)*$", "maxLength": 64},
  "icon": {"type": "string", "maxLength": 68, "pattern": "^(own:)?[a-z0-9]+(-[a-z0-9]+)*$"},
  "tooltip": {"type": "string", "minLength": 1, "maxLength": 100}},
 "required": ["id", "icon", "tooltip"], "additionalProperties": false},
"Step": {"type": "object", "properties": {"current": {"type": "integer", "minimum": 1}, "total": {"type": "integer", "minimum": 1}},
 "required": ["current", "total"], "additionalProperties": false}
})";

constexpr std::string_view kPick = R"({"type": "object", "properties": {
  "title": {"type": "string", "maxLength": 100},
  "placeholder": {"type": "string", "maxLength": 200},
  "value": {"type": "string", "maxLength": 1000},
  "items": {"type": "array", "maxItems": 10000, "items": {"$ref": "#/$defs/QuickPickItem"}},
  "many": {"type": "boolean"},
  "match_on": {"type": "array", "maxItems": 2, "items": {"enum": ["description", "detail"]}},
  "keep_order": {"type": "boolean"},
  "step": {"$ref": "#/$defs/Step"},
  "back": {"type": "boolean"},
  "buttons": {"type": "array", "maxItems": 3, "items": {"$ref": "#/$defs/QuickPickButton"}}},
 "additionalProperties": false})";

constexpr std::string_view kPickLive = R"({
  "filter": {"enum": ["fuzzy", "none"]},
  "busy": {"type": "boolean"},
  "active": {"type": "string"}})";

constexpr std::string_view kBox = R"({"type": "object", "properties": {
  "title": {"type": "string", "maxLength": 100},
  "prompt": {"type": "string", "maxLength": 300},
  "placeholder": {"type": "string", "maxLength": 200},
  "value": {"type": "string", "maxLength": 10000},
  "selection": {"type": "object", "properties": {"from": {"type": "integer", "minimum": 0}, "to": {"type": "integer", "minimum": 0}},
    "required": ["from", "to"], "additionalProperties": false},
  "password": {"type": "boolean"},
  "validation": {"type": "object", "properties": {
    "pattern": {"type": "string", "maxLength": 500},
    "message": {"type": "string", "minLength": 1, "maxLength": 200},
    "min_length": {"type": "integer", "minimum": 0},
    "max_length": {"type": "integer", "minimum": 1, "maximum": 10000}},
   "required": ["message"], "additionalProperties": false},
  "step": {"$ref": "#/$defs/Step"},
  "back": {"type": "boolean"}},
 "additionalProperties": false})";

constexpr std::string_view kBoxLive = R"({
  "message": {"oneOf": [{"type": "null"}, {"type": "object", "properties": {
    "severity": {"enum": ["error", "warning", "info"]},
    "text": {"type": "string", "minLength": 1, "maxLength": 300}},
   "required": ["severity", "text"], "additionalProperties": false}]},
  "busy": {"type": "boolean"}})";

Json parsed(std::string_view text)
{
    return *Json::parse(text);
}

const Json &defs()
{
    static const Json d = parsed(kDefs);
    return d;
}

Json schemaOf(bool pick, bool live, bool update)
{
    Json schema = parsed(pick ? kPick : kBox);
    if (live)
        for (const Json::Member &m : parsed(pick ? kPickLive : kBoxLive).asObject())
            schema["properties"][m.key] = m.value;
    if (pick && !live)
        schema["required"] = Json::array({"items"});
    if (update) {
        schema["properties"][pick ? "pick" : "box"] = Json::object({{"type", "string"}});
        schema["required"] = Json::array({pick ? "pick" : "box"});
    }
    return schema;
}

std::string result(Json value = Json::object())
{
    return Json::object({{"result", std::move(value)}}).dump();
}

std::string refusal(std::string_view code, std::string message, std::string_view reason = {})
{
    Json error = Json::object({{"code", code}, {"message", std::move(message)}});
    if (!reason.empty())
        error["data"] = Json::object({{"reason", reason}});
    return Json::object({{"error", std::move(error)}}).dump();
}

QString qs(const Json *value)
{
    return value && value->isString() ? QString::fromStdString(value->asString()) : QString();
}

std::optional<std::string> sense(const Json &params)
{
    if (const Json *items = params.find("items")) {
        std::set<std::string> ids;
        for (std::size_t i = 0; i < items->asArray().size(); ++i) {
            const Json &item = items->asArray()[i];
            const Json *kind = item.find("kind");
            const bool separator = kind && kind->asString() == "separator";
            const Json *id = item.find("id");
            const std::string at = "/items/" + std::to_string(i);
            if (separator && (id || item.find("buttons")))
                return at + ": a separator has only a label";
            if (!separator && !id)
                return at + ": an item needs an id";
            if (id && !ids.insert(id->asString()).second)
                return at + "/id: \"" + id->asString() + "\" is already an item's id";
        }
    }
    if (const Json *validation = params.find("validation"))
        if (const Json *pattern = validation->find("pattern"))
            if (!QRegularExpression(qs(pattern)).isValid())
                return "/validation/pattern: not a valid expression";
    if (const Json *selection = params.find("selection"))
        if (selection->find("from")->asInt() > selection->find("to")->asInt())
            return "/selection: from is after to";
    return std::nullopt;
}

// Lua sends an empty table as {}.
Json withEmptyLists(Json params)
{
    if (!params.isObject())
        return params;
    for (Json::Member &m : params.asObject())
        if ((m.key == "items" || m.key == "buttons" || m.key == "match_on") && m.value.isObject() && m.value.asObject().empty())
            m.value = Json::array();
    return params;
}

QVariantMap modelItem(const Json &item)
{
    QVariantMap out = toVariant(item).toMap();
    if (out.contains(QStringLiteral("always_show")))
        out.insert(QStringLiteral("alwaysShow"), out.take(QStringLiteral("always_show")));
    return out;
}

}

PluginPopup::PluginPopup(PluginPopups &owner, QString kind, QString pluginName, bool live, QObject *parent)
    : QObject(parent), m_owner(owner), m_kind(std::move(kind)), m_pluginName(std::move(pluginName)), m_live(live)
{
}

void PluginPopup::apply(const Json &spec)
{
    bool valueSet = false;
    bool activeSet = false;
    bool messageSet = false;
    for (const Json::Member &m : spec.asObject()) {
        const std::string &key = m.key;
        if (key == "title") {
            m_title = qs(&m.value);
        } else if (key == "placeholder") {
            m_placeholder = qs(&m.value);
        } else if (key == "value") {
            m_value = qs(&m.value);
            valueSet = true;
        } else if (key == "step") {
            m_step = QStringLiteral("%1 / %2").arg(m.value.find("current")->asInt()).arg(m.value.find("total")->asInt());
        } else if (key == "back") {
            m_back = m.value.asBool();
        } else if (key == "busy") {
            m_busy = m.value.asBool();
        } else if (key == "items") {
            m_items.clear();
            for (const Json &item : m.value.asArray())
                m_items.append(modelItem(item));
        } else if (key == "many") {
            m_many = m.value.asBool();
        } else if (key == "match_on") {
            m_matchOn.clear();
            for (const Json &field : m.value.asArray())
                m_matchOn.append(qs(&field));
        } else if (key == "keep_order") {
            m_keepOrder = m.value.asBool();
        } else if (key == "filter") {
            m_filtering = m.value.asString() == "fuzzy";
        } else if (key == "active") {
            m_active = qs(&m.value);
            activeSet = true;
        } else if (key == "buttons") {
            m_buttons = toVariant(m.value).toList();
        } else if (key == "prompt") {
            m_prompt = qs(&m.value);
        } else if (key == "password") {
            m_password = m.value.asBool();
        } else if (key == "selection") {
            m_selectionFrom = int(m.value.find("from")->asInt());
            m_selectionTo = int(m.value.find("to")->asInt());
            valueSet = true;
        } else if (key == "validation") {
            Validation v;
            if (const Json *pattern = m.value.find("pattern"))
                v.pattern = QRegularExpression(QRegularExpression::anchoredPattern(qs(pattern)));
            v.message = qs(m.value.find("message"));
            if (const Json *min = m.value.find("min_length"))
                v.minLength = int(min->asInt());
            if (const Json *max = m.value.find("max_length"))
                v.maxLength = int(max->asInt());
            m_validation = std::move(v);
            messageSet = true;
        } else if (key == "message") {
            m_noticeSeverity = m.value.isNull() ? QString() : qs(m.value.find("severity"));
            m_notice = m.value.isNull() ? QString() : qs(m.value.find("text"));
            messageSet = true;
        }
    }
    if (spec.find("value") && !spec.find("selection")) {
        m_selectionFrom = -1;
        m_selectionTo = -1;
    }
    emit changed();
    if (valueSet) {
        emit this->valueSet();
        messageSet = true;
    }
    if (activeSet)
        emit this->activeSet();
    if (messageSet)
        emit messageChanged();
}

std::optional<QString> PluginPopup::failedCheck() const
{
    if (!m_validation)
        return std::nullopt;
    const Validation &v = *m_validation;
    const bool fails = m_value.size() < v.minLength || (v.maxLength >= 0 && m_value.size() > v.maxLength) ||
                       (v.pattern && !v.pattern->match(m_value).hasMatch());
    return fails ? std::optional(v.message) : std::nullopt;
}

QString PluginPopup::severity() const
{
    return failedCheck() ? QStringLiteral("error") : m_noticeSeverity;
}

QString PluginPopup::message() const
{
    const auto failed = failedCheck();
    return failed ? *failed : m_notice;
}

void PluginPopup::edit(const QString &value)
{
    if (value == m_value)
        return;
    m_value = value;
    emit messageChanged();
    if (m_live)
        m_owner.event(Json::object({{"kind", "value"}, {"value", value.toStdString()}}), true);
}

void PluginPopup::activate(const QString &item)
{
    if (m_live && m_kind == QLatin1String("pick") && !item.isEmpty())
        m_owner.event(Json::object({{"kind", "active"}, {"item", item.toStdString()}}));
}

void PluginPopup::accept(const QStringList &items)
{
    if (m_kind == QLatin1String("box")) {
        if (severity() == QLatin1String("error"))
            return;
        if (m_busy) {
            m_acceptWaits = true;
            return;
        }
        m_acceptWaits = false;
        Json data = Json::object({{"value", m_value.toStdString()}});
        if (m_live) {
            data["kind"] = "accept";
            m_owner.event(std::move(data));
        } else {
            data["cancelled"] = false;
            m_owner.answer(std::move(data));
        }
        return;
    }
    Json ids = Json::array();
    for (const QString &id : items)
        ids.push(id.toStdString());
    if (m_live)
        m_owner.event(Json::object({{"kind", "accept"}, {"items", std::move(ids)}}));
    else
        m_owner.answer(Json::object({{"cancelled", false}, {"items", std::move(ids)}}));
}

void PluginPopup::press(const QString &button, const QString &item)
{
    if (!m_live) {
        m_owner.answer(Json::object({{"cancelled", true}, {"button", button.toStdString()}}));
        return;
    }
    Json data = Json::object({{"kind", "button"}, {"button", button.toStdString()}});
    if (!item.isEmpty())
        data["item"] = item.toStdString();
    m_owner.event(std::move(data));
}

void PluginPopup::goBack()
{
    if (!m_back)
        return;
    if (m_live)
        m_owner.event(Json::object({{"kind", "back"}}));
    else
        m_owner.answer(Json::object({{"cancelled", true}, {"back", true}}));
}

void PluginPopup::dismiss()
{
    m_owner.end(true);
}

PluginPopups::PluginPopups(Deliver deliver, QObject *parent) : QObject(parent), m_deliver(std::move(deliver)) {}

PluginPopups::~PluginPopups() = default;

bool PluginPopups::handles(const std::string &operation)
{
    return operation.starts_with("quick_pick.") || operation.starts_with("input_box.");
}

void PluginPopups::serve(const std::string &plugin, const QString &pluginName, const std::string &operation,
                         const Json &params, Reply reply)
{
    const bool pick = operation.starts_with("quick_pick.");
    const std::string verb = operation.substr(operation.find('.') + 1);
    const Json lists = withEmptyLists(params);
    if (verb == "ask" || verb == "open")
        open(plugin, pluginName, pick, verb == "open", lists, reply);
    else if (verb == "update")
        update(plugin, pick, lists, reply);
    else if (verb == "close")
        close(plugin, pick, lists, reply);
    else
        reply(refusal("unknown_operation", "there is no operation \xE2\x80\x9C" + operation + "\xE2\x80\x9D"));
}

void PluginPopups::open(const std::string &plugin, const QString &pluginName, bool pick, bool live, const Json &params,
                        const Reply &reply)
{
    if (const auto failed = api::validate(schemaOf(pick, live, false), params, defs())) {
        reply(refusal("invalid_params", failed->path + ": " + failed->message));
        return;
    }
    if (const auto problem = sense(params)) {
        reply(refusal("invalid_params", *problem));
        return;
    }
    if (m_userAsked && !m_userAsked(plugin)) {
        reply(refusal("refused", "a plugin opens a quick pick or input box only while it answers the user",
                      "not_user_initiated"));
        return;
    }
    end(true);
    auto popup = std::make_unique<Popup>();
    popup->plugin = plugin;
    popup->id = (pick ? "q" : "b") + std::to_string(++m_next);
    popup->state = new PluginPopup(*this, pick ? QStringLiteral("pick") : QStringLiteral("box"), pluginName, live, this);
    popup->state->apply(params);
    if (!live)
        popup->asked = reply;
    const std::string id = popup->id;
    m_shown = std::move(popup);
    emit shownChanged();
    if (live)
        reply(result(Json::object({{pick ? "pick" : "box", id}})));
}

PluginPopups::Popup *PluginPopups::find(const std::string &plugin, bool pick, const Json &params)
{
    const Json *id = params.find(pick ? "pick" : "box");
    if (!m_shown || m_shown->plugin != plugin || m_shown->asked || !id || !id->isString() || m_shown->id != id->asString())
        return nullptr;
    return m_shown.get();
}

void PluginPopups::update(const std::string &plugin, bool pick, const Json &params, const Reply &reply)
{
    if (const auto failed = api::validate(schemaOf(pick, true, true), params, defs())) {
        reply(refusal("invalid_params", failed->path + ": " + failed->message));
        return;
    }
    if (const auto problem = sense(params)) {
        reply(refusal("invalid_params", *problem));
        return;
    }
    Popup *popup = find(plugin, pick, params);
    if (!popup) {
        reply(refusal("not_found", pick ? "that quick pick has closed" : "that input box has closed"));
        return;
    }
    Json fields = params;
    std::erase_if(fields.asObject(), [&](const Json::Member &m) { return m.key == (pick ? "pick" : "box"); });
    PluginPopup *state = popup->state;
    const std::string id = popup->id;
    state->apply(fields);
    reply(result(Json::object({{pick ? "pick" : "box", id}})));
    if (!pick && state->m_acceptWaits && !state->m_busy)
        state->accept();
}

void PluginPopups::close(const std::string &plugin, bool pick, const Json &params, const Reply &reply)
{
    Popup *popup = find(plugin, pick, params);
    if (!popup) {
        reply(refusal("not_found", pick ? "that quick pick has closed" : "that input box has closed"));
        return;
    }
    const std::string id = popup->id;
    end(true);
    reply(result(Json::object({{pick ? "pick" : "box", id}})));
}

void PluginPopups::event(Json data, bool coalesce)
{
    if (!m_shown)
        return;
    const bool pick = m_shown->state->kind() == QLatin1String("pick");
    data[pick ? "pick" : "box"] = m_shown->id;
    const std::string name = (pick ? "quick_pick:" : "input_box:") + m_shown->id;
    m_deliver(m_shown->plugin, name, data.dump(), coalesce ? name + ":value" : std::string());
}

void PluginPopups::answer(Json data)
{
    if (!m_shown || !m_shown->asked)
        return;
    const Reply reply = std::move(*m_shown->asked);
    m_shown->asked.reset();
    end(false);
    reply(result(std::move(data)));
}

void PluginPopups::end(bool deliverHide)
{
    if (!m_shown)
        return;
    std::unique_ptr<Popup> gone = std::move(m_shown);
    emit shownChanged();
    if (gone->asked) {
        (*gone->asked)(result(Json::object({{"cancelled", true}})));
    } else if (deliverHide && gone->state->live()) {
        const bool pick = gone->state->kind() == QLatin1String("pick");
        const std::string name = (pick ? "quick_pick:" : "input_box:") + gone->id;
        m_deliver(gone->plugin, name, Json::object({{"kind", "hide"}, {pick ? "pick" : "box", gone->id}}).dump(), {});
    }
    gone->state->deleteLater();
}

void PluginPopups::closeOf(const std::string &plugin)
{
    if (m_shown && m_shown->plugin == plugin)
        end(false);
}

void PluginPopups::dismissShown()
{
    end(true);
}

}
