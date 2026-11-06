#include "hikari/app/plugin_ui.h"

#include "hikari/plugins/surface.h"
#include "workspace_layout.h"

#include <QCollator>
#include <QDir>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace hikari::app {

namespace {

using api::Json;

QString qs(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

QVariant toVariant(const Json &json)
{
    const QByteArray text = QByteArray::fromStdString(Json::array({json}).dump());
    return QJsonDocument::fromJson(text).array().at(0).toVariant();
}

Json fromVariant(const QVariant &value)
{
    const QByteArray text = QJsonDocument(QJsonArray{QJsonValue::fromVariant(value)}).toJson(QJsonDocument::Compact);
    auto parsed = Json::parse(text.toStdString());
    return parsed && parsed->isArray() && !parsed->asArray().empty() ? parsed->asArray().front() : Json();
}

std::string result(Json value = Json::object())
{
    return Json::object({{"result", std::move(value)}}).dump();
}

std::string refusal(std::string_view code, std::string message)
{
    return Json::object({{"error", Json::object({{"code", code}, {"message", std::move(message)}})}}).dump();
}

std::string inQuotes(std::string_view s)
{
    return "\xE2\x80\x9C" + std::string(s) + "\xE2\x80\x9D";
}

const char *areaName(plugins::PanelArea area)
{
    switch (area) {
    case plugins::PanelArea::Left: return "left";
    case plugins::PanelArea::Bottom: return "bottom";
    case plugins::PanelArea::Right: break;
    }
    return "right";
}

struct DockParts {
    std::string plugin;
    std::string panel;
};

std::optional<DockParts> partsOf(const QString &dock)
{
    if (!ui::WorkspaceLayoutController::isPluginPanel(dock))
        return std::nullopt;
    const QString rest = dock.mid(dock.indexOf(u':') + 1);
    const qsizetype cut = rest.indexOf(u'/');
    return DockParts{rest.left(cut).toStdString(), rest.mid(cut + 1).toStdString()};
}

bool isDialogId(const std::string &id)
{
    return !id.empty() && id.size() <= 64 &&
           std::ranges::all_of(id, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

QString dialogKey(const std::string &plugin, const std::string &dialog)
{
    return qs("plugin." + plugin + "." + dialog);
}

bool keepsValue(const Json &event)
{
    const Json *kind = event.find("kind");
    const Json *widget = event.find("widget");
    return kind && kind->isString() && (kind->asString() == "change" || kind->asString() == "select") && widget &&
           widget->isString() && event.find("value");
}

std::string coalesceKey(const std::string &surface, const Json &event)
{
    const Json *kind = event.find("kind");
    const Json *widget = event.find("widget");
    const Json *type = event.find("type");
    const std::string k = kind && kind->isString() ? kind->asString() : std::string();
    const std::string w = widget && widget->isString() ? widget->asString() : std::string();
    if (k == "change")
        return surface + "#change:" + w;
    if (k == "show")
        return surface + "#show";
    if (k == "pointer" && type && type->isString() && type->asString() == "move")
        return surface + "#move:" + w;
    return {};
}

}

PluginPanelState::PluginPanelState(QString dock, QString pluginId, QString panelId, QObject *parent)
    : QObject(parent), m_dock(std::move(dock)), m_pluginId(std::move(pluginId)), m_panelId(std::move(panelId))
{
}

void PluginPanelState::describe(QString status, QString reason, QString title, QString pluginName, QUrl packageRoot)
{
    if (status == m_status && reason == m_reason && title == m_title && pluginName == m_pluginName &&
        packageRoot == m_packageRoot)
        return;
    m_status = std::move(status);
    m_reason = std::move(reason);
    m_title = std::move(title);
    m_pluginName = std::move(pluginName);
    m_packageRoot = std::move(packageRoot);
    emit changed();
}

void PluginPanelState::setTree(QVariant tree, QVariantList menu)
{
    m_tree = std::move(tree);
    m_menu = std::move(menu);
    emit treeChanged();
}

PluginDialogState::PluginDialogState(QString key, QString pluginName, QUrl packageRoot, QObject *parent)
    : QObject(parent), m_key(std::move(key)), m_pluginName(std::move(pluginName)), m_packageRoot(std::move(packageRoot))
{
}

void PluginDialogState::present(const Json &spec, bool modal)
{
    m_title = qs(spec.find("title")->asString());
    m_body = toVariant(*spec.find("body"));
    m_buttons = toVariant(*spec.find("buttons")).toList();
    const Json *width = spec.find("width");
    const Json *height = spec.find("height");
    m_width = width ? int(std::min<std::int64_t>(width->asInt(), 4000)) : 0;
    m_height = height ? int(std::min<std::int64_t>(height->asInt(), 4000)) : 0;
    m_modal = modal;
    emit changed();
}

PluginFileRequest::PluginFileRequest(QString kind, QString pluginName, QString title, QString reason, bool writable,
                                     QStringList nameFilters, QString name, Answer answer, QObject *parent)
    : QObject(parent), m_kind(std::move(kind)), m_pluginName(std::move(pluginName)), m_title(std::move(title)),
      m_reason(std::move(reason)), m_writable(writable), m_nameFilters(std::move(nameFilters)), m_answer(std::move(answer))
{
    if (!name.isEmpty())
        m_file = QUrl::fromLocalFile(QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(name));
}

void PluginFileRequest::accept(const QUrl &chosen, bool remember)
{
    const QString path = chosen.toLocalFile();
    if (path.isEmpty()) {
        reject();
        return;
    }
    if (const Answer answer = std::exchange(m_answer, nullptr)) {
        emit settled();
        answer(path, remember);
    }
}

void PluginFileRequest::reject()
{
    if (const Answer answer = std::exchange(m_answer, nullptr)) {
        emit settled();
        answer(std::nullopt, false);
    }
}

PluginUi::PluginUi(Deliver deliver, QObject *parent) : QObject(parent), m_deliver(std::move(deliver)) {}

PluginUi::~PluginUi() = default;

void PluginUi::setPlugins(std::vector<Plugin> plugins)
{
    std::map<std::string, Known> next;
    for (Plugin &plugin : plugins) {
        Known known{std::move(plugin), QStringLiteral("running"), {}};
        if (const auto old = m_plugins.find(known.plugin.id); old != m_plugins.end() && old->second.plugin.enabled) {
            known.status = old->second.status;
            known.reason = old->second.reason;
        }
        next.emplace(known.plugin.id, std::move(known));
    }
    for (const auto &[id, old] : m_plugins)
        if (const auto now = next.find(id); now == next.end() || !now->second.plugin.enabled) {
            closeDialogsOf(id);
            dropFileRequestsOf(id);
        }
    m_plugins = std::move(next);
    for (auto &[dock, state] : m_panels)
        refresh(*state);
    relist();
    emit pluginStatesChanged();
}

QVariantMap PluginUi::pluginState(const QString &plugin) const
{
    const auto it = m_plugins.find(plugin.toStdString());
    if (it == m_plugins.end())
        return {{QStringLiteral("status"), QStringLiteral("missing")}, {QStringLiteral("reason"), QString()}};
    if (!it->second.plugin.enabled)
        return {{QStringLiteral("status"), QStringLiteral("off")}, {QStringLiteral("reason"), QString()}};
    return {{QStringLiteral("status"), it->second.status}, {QStringLiteral("reason"), it->second.reason}};
}

void PluginUi::running(const std::string &plugin)
{
    const auto it = m_plugins.find(plugin);
    if (it == m_plugins.end())
        return;
    it->second.status = QStringLiteral("running");
    it->second.reason.clear();
    emit pluginStatesChanged();
    for (auto &[dock, state] : m_panels)
        if (state->pluginId() == qs(plugin)) {
            refresh(*state);
            if (m_shown.contains(dock))
                deliver(plugin, "panel:" + state->panelId().toStdString(), Json::object({{"kind", "show"}}), {});
        }
}

void PluginUi::stopped(const std::string &plugin, const QString &status, const QString &reason)
{
    closeDialogsOf(plugin);
    dropFileRequestsOf(plugin);
    const auto it = m_plugins.find(plugin);
    if (it == m_plugins.end())
        return;
    it->second.status = status;
    it->second.reason = reason;
    emit pluginStatesChanged();
    for (auto &[dock, state] : m_panels)
        if (state->pluginId() == qs(plugin))
            refresh(*state);
}

PluginPanelState *PluginUi::panel(const QString &dock)
{
    if (const auto it = m_panels.find(dock); it != m_panels.end())
        return it->second;
    const auto parts = partsOf(dock);
    if (!parts)
        return nullptr;
    auto *state = new PluginPanelState(dock, qs(parts->plugin), qs(parts->panel), this);
    m_panels.emplace(dock, state);
    refresh(*state);
    return state;
}

void PluginUi::refresh(PluginPanelState &state)
{
    const auto it = m_plugins.find(state.pluginId().toStdString());
    const plugins::Panel *declared = nullptr;
    if (it != m_plugins.end())
        for (const auto &p : it->second.plugin.panels)
            if (qs(p.id) == state.panelId())
                declared = &p;
    if (!declared) {
        state.describe(QStringLiteral("missing"), {}, {}, {}, {});
    } else {
        const Plugin &plugin = it->second.plugin;
        const QString status = plugin.enabled ? it->second.status : QStringLiteral("off");
        state.describe(status, plugin.enabled ? it->second.reason : QString(), qs(declared->title), qs(plugin.name),
                       plugin.root);
    }
    if (state.status() != QStringLiteral("running") && !state.tree().isNull()) {
        m_values.erase(state.dock());
        state.setTree({}, {});
    }
}

void PluginUi::relist()
{
    QVariantList rows;
    for (const auto &[id, known] : m_plugins) {
        if (!known.plugin.enabled)
            continue;
        for (const auto &panel : known.plugin.panels)
            rows << QVariantMap{{QStringLiteral("dock"), ui::WorkspaceLayoutController::pluginPanelId(qs(id), qs(panel.id))},
                                {QStringLiteral("plugin"), qs(id)},
                                {QStringLiteral("pluginName"), qs(known.plugin.name)},
                                {QStringLiteral("panel"), qs(panel.id)},
                                {QStringLiteral("title"), qs(panel.title)},
                                {QStringLiteral("area"), QString::fromLatin1(areaName(panel.area))}};
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    auto text = [](const QVariant &row, const char *key) { return row.toMap().value(QLatin1String(key)).toString(); };
    std::ranges::stable_sort(rows, [&](const QVariant &a, const QVariant &b) {
        const int byTitle = collator.compare(text(a, "title"), text(b, "title"));
        return byTitle != 0 ? byTitle < 0 : collator.compare(text(a, "pluginName"), text(b, "pluginName")) < 0;
    });
    if (rows == m_rows)
        return;
    m_rows = std::move(rows);
    emit panelsChanged();
}

void PluginUi::serve(const std::string &plugin, const std::string &operation, const Json &params, Reply reply)
{
    const auto known = m_plugins.find(plugin);
    if (known == m_plugins.end() || !known->second.plugin.enabled) {
        reply(refusal("unavailable", "the plugin isn't turned on"));
        return;
    }
    if (operation == "panels.render" || operation == "panels.show") {
        const Json *panel = params.find("panel");
        const std::string id = panel && panel->isString() ? panel->asString() : std::string();
        const auto &declared = known->second.plugin.panels;
        if (std::ranges::find(declared, id, &plugins::Panel::id) == declared.end()) {
            reply(refusal("invalid_params", inQuotes(id) + " isn't a panel in hikari-plugin.json"));
            return;
        }
        const QString dock = ui::WorkspaceLayoutController::pluginPanelId(qs(plugin), qs(id));
        if (operation == "panels.show") {
            emit showRequested(dock);
            reply(result());
            return;
        }
        const Json *tree = params.find("tree");
        if (!tree) {
            reply(refusal("invalid_params", "hikari.panels.render needs a tree"));
            return;
        }
        if (const auto problem = plugins::panelTreeProblem(*tree)) {
            reply(refusal("invalid_params", *problem));
            return;
        }
        Json body = *tree;
        Json menu = Json::array();
        auto &members = body.asObject();
        if (const auto it = std::ranges::find(members, std::string("menu"), &Json::Member::key); it != members.end()) {
            menu = it->value;
            members.erase(it);
        }
        m_values[dock] = plugins::inputValues(body);
        this->panel(dock)->setTree(toVariant(body), toVariant(menu).toList());
        reply(result());
        return;
    }
    if (operation == "dialogs.show" || operation == "dialogs.ask") {
        present(plugin, params, operation == "dialogs.ask" ? std::optional<Reply>(reply) : std::nullopt, reply);
        return;
    }
    if (operation == "dialogs.close") {
        const Json *dialog = params.find("dialog");
        if (dialog && dialog->isString())
            close(dialogKey(plugin, dialog->asString()));
        reply(result());
        return;
    }
    reply(refusal("unknown_operation", "there is no operation " + inQuotes(operation)));
}

void PluginUi::present(const std::string &plugin, const Json &params, std::optional<Reply> asked, const Reply &reply)
{
    const Json *dialog = params.find("dialog");
    const std::string id = dialog && dialog->isString() ? dialog->asString() : std::string();
    if (!isDialogId(id)) {
        reply(refusal("invalid_params", "a dialog id is 1 to 64 lowercase letters, digits and -"));
        return;
    }
    const Json *spec = params.find("spec");
    if (const auto problem = plugins::dialogProblem(spec ? *spec : Json())) {
        reply(refusal("invalid_params", *problem));
        return;
    }
    const QString key = dialogKey(plugin, id);
    auto it = m_dialogs.find(key);
    const bool fresh = it == m_dialogs.end();
    if (fresh) {
        const Known &known = m_plugins.at(plugin);
        auto *state = new PluginDialogState(key, qs(known.plugin.name), known.plugin.root, this);
        it = m_dialogs.emplace(key, Dialog{plugin, id, state, {}, {}, std::nullopt}).first;
    }
    Dialog &d = it->second;
    d.spec = *spec;
    d.values = plugins::inputValues(*spec->find("body"));
    d.asked = std::move(asked);
    d.state->present(*spec, d.asked.has_value());
    if (fresh)
        emit dialogOpened(d.state);
    if (!d.asked)
        reply(result());
}

void PluginUi::setShown(const QString &dock, bool shown)
{
    if (!shown) {
        m_shown.erase(dock);
        return;
    }
    if (!m_shown.insert(dock).second)
        return;
    const auto parts = partsOf(dock);
    PluginPanelState *state = panel(dock);
    if (parts && state && state->status() == QStringLiteral("running"))
        deliver(parts->plugin, "panel:" + parts->panel, Json::object({{"kind", "show"}}), {});
}

void PluginUi::send(const QString &dock, const QVariantMap &event)
{
    const auto parts = partsOf(dock);
    Json data = fromVariant(event);
    if (!parts || !data.isObject())
        return;
    if (keepsValue(data))
        m_values[dock][data.find("widget")->asString()] = *data.find("value");
    deliver(parts->plugin, "panel:" + parts->panel, std::move(data), {});
}

void PluginUi::dialogEvent(const QString &key, const QVariantMap &event)
{
    const auto it = m_dialogs.find(key);
    Json data = fromVariant(event);
    if (it == m_dialogs.end() || !data.isObject())
        return;
    Dialog &d = it->second;
    if (keepsValue(data))
        d.values[data.find("widget")->asString()] = *data.find("value");
    if (!d.asked)
        deliver(d.plugin, "dialog:" + d.id, std::move(data), {});
}

void PluginUi::deliver(const std::string &plugin, const std::string &event, Json data, const std::string &key)
{
    const std::string surface = event.substr(event.find(':') + 1);
    const bool isPanel = event.starts_with("panel:");
    data[isPanel ? "panel" : "dialog"] = surface;
    const api::Json *values = nullptr;
    if (isPanel) {
        if (const auto it = m_values.find(ui::WorkspaceLayoutController::pluginPanelId(qs(plugin), qs(surface)));
            it != m_values.end())
            values = &it->second;
    } else if (const auto it = m_dialogs.find(dialogKey(plugin, surface)); it != m_dialogs.end()) {
        values = &it->second.values;
    }
    data["values"] = values ? *values : Json::object();
    m_deliver(plugin, event, data.dump(), key.empty() ? coalesceKey(event, data) : key);
}

void PluginUi::press(const QString &key, const QString &button)
{
    const auto it = m_dialogs.find(key);
    if (it == m_dialogs.end())
        return;
    Dialog &d = it->second;
    const Json *pressed = nullptr;
    for (const Json &b : d.spec.find("buttons")->asArray())
        if (qs(b.find("id")->asString()) == button)
            pressed = &b;
    if (!pressed)
        return;
    const std::string id = pressed->find("id")->asString();
    if (d.asked) {
        const Reply answer = std::move(*d.asked);
        const Json values = d.values;
        close(key);
        answer(result(Json::object({{"button", id}, {"values", values}})));
        return;
    }
    const Json *stays = pressed->find("stays_open");
    const bool closes = !(stays && stays->asBool());
    deliver(d.plugin, "dialog:" + d.id, Json::object({{"kind", "button"}, {"button", id}}), {});
    if (closes)
        close(key);
}

void PluginUi::dismiss(const QString &key)
{
    const auto it = m_dialogs.find(key);
    if (it == m_dialogs.end())
        return;
    Dialog &d = it->second;
    for (const Json &b : d.spec.find("buttons")->asArray())
        if (const Json *cancel = b.find("cancel"); cancel && cancel->asBool()) {
            const std::string id = b.find("id")->asString();
            if (d.asked) {
                press(key, qs(id));
            } else {
                deliver(d.plugin, "dialog:" + d.id, Json::object({{"kind", "button"}, {"button", id}}), {});
                close(key);
            }
            return;
        }
    if (d.asked) {
        const Reply answer = std::move(*d.asked);
        const Json values = d.values;
        close(key);
        answer(result(Json::object({{"button", nullptr}, {"values", values}})));
        return;
    }
    deliver(d.plugin, "dialog:" + d.id, Json::object({{"kind", "closed"}}), {});
    close(key);
}

QVariant PluginUi::value(const QString &surface, const QString &widget) const
{
    const api::Json *values = nullptr;
    if (const auto it = m_values.find(surface); it != m_values.end())
        values = &it->second;
    else if (const auto d = m_dialogs.find(surface); d != m_dialogs.end())
        values = &d->second.values;
    const Json *found = values ? values->find(widget.toStdString()) : nullptr;
    return found ? toVariant(*found) : QVariant();
}

void PluginUi::close(const QString &key)
{
    const auto it = m_dialogs.find(key);
    if (it == m_dialogs.end())
        return;
    PluginDialogState *state = it->second.state;
    m_dialogs.erase(it);
    emit dialogClosed(state);
    state->deleteLater();
}

void PluginUi::closeDialogsOf(const std::string &plugin)
{
    std::vector<QString> keys;
    for (const auto &[key, dialog] : m_dialogs)
        if (dialog.plugin == plugin)
            keys.push_back(key);
    for (const QString &key : keys)
        close(key);
}

void PluginUi::askForFile(const std::string &plugin, FileAsk ask, PluginFileRequest::Answer answer)
{
    const auto known = m_plugins.find(plugin);
    const QString name = known == m_plugins.end() ? qs(plugin) : qs(known->second.plugin.name);
    auto *request = new PluginFileRequest(std::move(ask.kind), name, std::move(ask.title), std::move(ask.reason), ask.writable,
                                          std::move(ask.nameFilters), std::move(ask.name), std::move(answer), this);
    connect(request, &PluginFileRequest::settled, this, [this, request] { answered(request); });
    m_fileRequests.emplace_back(plugin, request);
    if (m_fileRequests.size() == 1)
        emit fileRequestChanged();
}

void PluginUi::answered(PluginFileRequest *request)
{
    const auto it = std::ranges::find(m_fileRequests, request, &std::pair<std::string, PluginFileRequest *>::second);
    if (it == m_fileRequests.end())
        return;
    const bool front = it == m_fileRequests.begin();
    m_fileRequests.erase(it);
    request->deleteLater();
    if (front)
        emit fileRequestChanged();
}

void PluginUi::dropFileRequestsOf(const std::string &plugin)
{
    const bool front = !m_fileRequests.empty() && m_fileRequests.front().first == plugin;
    std::erase_if(m_fileRequests, [&plugin](const auto &pending) {
        if (pending.first != plugin)
            return false;
        pending.second->deleteLater();
        return true;
    });
    if (front)
        emit fileRequestChanged();
}

bool PluginUi::drop(const QString &dock, const QList<QUrl> &files)
{
    const auto parts = partsOf(dock);
    const auto state = m_panels.find(dock);
    if (!parts || state == m_panels.end() || state->second->status() != QStringLiteral("running") || !m_grantDrop)
        return false;
    const QVariantMap tree = state->second->tree().toMap();
    if (!tree.value(QStringLiteral("drop")).toBool())
        return false;
    Json granted = Json::array();
    for (const QUrl &url : files)
        if (url.isLocalFile())
            if (Json file = m_grantDrop(parts->plugin, url.toLocalFile()); file.isObject())
                granted.push(std::move(file));
    if (granted.asArray().empty())
        return false;
    deliver(parts->plugin, "panel:" + parts->panel, Json::object({{"kind", "drop"}, {"files", std::move(granted)}}), {});
    return true;
}

}
