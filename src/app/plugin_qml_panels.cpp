#include "hikari/app/plugin_panel_api.h"
#include "hikari/app/plugin_ui.h"

#include "hikari/plugins/qml_imports.h"
#include "plugin_variant.h"
#include "workspace_layout.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSaveFile>

#include <algorithm>

namespace hikari::app {

namespace {

Q_LOGGING_CATEGORY(lcPluginPanels, "hikari.plugins.panels")

using api::Json;

QString qs(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

QByteArray readAll(const QString &path)
{
    QFile in(path);
    return in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray();
}

QString errorsText(const QList<QQmlError> &errors, const QString &root)
{
    QStringList lines;
    for (const QQmlError &e : errors) {
        const QString file = e.url().isLocalFile() ? QDir(root).relativeFilePath(e.url().toLocalFile()) : e.url().toString();
        lines << (file.isEmpty() ? e.description()
                                 : e.line() > 0 ? QStringLiteral("%1:%2: %3").arg(file).arg(e.line()).arg(e.description())
                                                : QStringLiteral("%1: %2").arg(file, e.description()));
    }
    return lines.join(u'\n');
}

std::optional<QString> importProblem(const QString &root)
{
    QDirIterator files(root, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString path = files.next();
        const QString relative = QDir(root).relativeFilePath(path);
        if (!plugins::isQmlSource(relative.toStdString()))
            continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        if (const auto refused = plugins::qmlImportProblem(relative.toStdString(), file.readAll().toStdString()))
            return QCoreApplication::translate("HikariSub.Plugins", "%1 imports %2, which plugin panels can’t use.")
                .arg(relative, qs(*refused));
    }
    return std::nullopt;
}

struct DockParts {
    std::string plugin;
    std::string panel;
};

DockParts partsOfDock(const QString &dock)
{
    const QString rest = dock.mid(dock.indexOf(u':') + 1);
    const qsizetype cut = rest.indexOf(u'/');
    return {rest.left(cut).toStdString(), rest.mid(cut + 1).toStdString()};
}

}

void PluginPanelState::setQml(bool qml, QString loadError, bool held, QQuickItem *item)
{
    if (qml == m_qml && loadError == m_loadError && held == m_held && item == m_item)
        return;
    m_qml = qml;
    m_loadError = std::move(loadError);
    m_held = held;
    m_item = item;
    emit qmlChanged();
}

void PluginUi::keepLoadedPanelsIn(const QString &file)
{
    m_loadedPanelsFile = file;
    const QJsonObject json = QJsonDocument::fromJson(readAll(file)).object();
    for (const QJsonValue id : json.value(QStringLiteral("held")).toArray())
        m_held.insert(id.toString().toStdString());
    for (const QJsonValue id : json.value(QStringLiteral("loaded")).toArray()) {
        m_held.insert(id.toString().toStdString());
        m_heldUnnoticed.insert(id.toString().toStdString());
    }
    writeLoadedPanels();
    noticeHeld();
}

void PluginUi::writeLoadedPanels()
{
    if (m_loadedPanelsFile.isEmpty())
        return;
    QJsonArray loaded, held;
    for (const auto &[id, known] : m_plugins)
        if (qmlLoaded(id))
            loaded << qs(id);
    for (const std::string &id : m_held)
        held << qs(id);
    if (loaded.isEmpty() && held.isEmpty()) {
        QFile::remove(m_loadedPanelsFile);
        return;
    }
    QDir().mkpath(QFileInfo(m_loadedPanelsFile).absolutePath());
    QSaveFile out(m_loadedPanelsFile);
    if (!out.open(QIODevice::WriteOnly) ||
        out.write(QJsonDocument(QJsonObject{{QStringLiteral("loaded"), loaded}, {QStringLiteral("held"), held}}).toJson()) < 0 ||
        !out.commit())
        qCWarning(lcPluginPanels) << "Couldn't save" << m_loadedPanelsFile << out.errorString();
}

void PluginUi::noticeHeld()
{
    if (!m_qmlHooks.log)
        return;
    for (auto it = m_heldUnnoticed.begin(); it != m_heldUnnoticed.end();) {
        const auto known = m_plugins.find(*it);
        if (known == m_plugins.end()) {
            ++it;
            continue;
        }
        m_qmlHooks.log(qs(known->second.plugin.name),
                       QCoreApplication::translate("HikariSub.Plugins",
                                                   "HikariSub closed while its panel was open, so its panels stay closed "
                                                   "until you choose Load again."),
                       false);
        it = m_heldUnnoticed.erase(it);
    }
}

bool PluginUi::qmlLoaded(const std::string &plugin) const
{
    return std::ranges::any_of(m_qml, [&](const auto &entry) {
        return entry.second.item && partsOfDock(entry.first).plugin == plugin;
    });
}

void PluginUi::setQmlHost(const QString &dock, QQuickItem *host)
{
    if (!ui::WorkspaceLayoutController::isPluginPanel(dock))
        return;
    QmlPanel &qml = m_qml[dock];
    if (qml.host == host)
        return;
    unloadQml(dock);
    qml.host = host;
    loadQml(dock);
}

void PluginUi::loadAgain(const QString &dock)
{
    if (!ui::WorkspaceLayoutController::isPluginPanel(dock))
        return;
    const std::string plugin = partsOfDock(dock).plugin;
    m_held.erase(plugin);
    m_qml[dock].error.clear();
    writeLoadedPanels();
    for (auto &[name, state] : m_panels)
        if (state->pluginId() == qs(plugin))
            refresh(*state);
    loadShownQmlOf(plugin);
}

void PluginUi::loadShownQmlOf(const std::string &plugin)
{
    std::vector<QString> docks;
    for (const auto &[dock, qml] : m_qml)
        if (partsOfDock(dock).plugin == plugin)
            docks.push_back(dock);
    for (const QString &dock : docks)
        loadQml(dock);
}

void PluginUi::loadQml(const QString &dock)
{
    const auto entry = m_qml.find(dock);
    if (entry == m_qml.end() || !entry->second.host || entry->second.item || entry->second.leaving ||
        !entry->second.error.isEmpty() || !m_shown.contains(dock))
        return;
    const DockParts parts = partsOfDock(dock);
    const auto known = m_plugins.find(parts.plugin);
    if (known == m_plugins.end() || !known->second.plugin.enabled || known->second.status != QStringLiteral("running") ||
        m_held.contains(parts.plugin))
        return;
    const Plugin &plugin = known->second.plugin;
    const auto declared = std::ranges::find(plugin.panels, parts.panel, &plugins::Panel::id);
    if (declared == plugin.panels.end() || declared->qml.empty())
        return;
    QQuickItem *host = entry->second.host;
    QQmlEngine *engine = qmlEngine(host);
    if (!engine)
        return;
    const QString root = plugin.root.toLocalFile();
    if (!plugin.unrestricted) {
        failQml(dock, QCoreApplication::translate("HikariSub.Plugins", "Only Full trust plugins can ship QML panels."), {});
        return;
    }
    if (const auto problem = importProblem(root)) {
        failQml(dock, *problem, {});
        return;
    }
    QUrl url = QUrl::fromLocalFile(QDir(root).filePath(qs(declared->qml)));
    url.setQuery(QStringLiteral("v=%1.%2").arg(qs(plugin.version)).arg(++m_loads));
    watchWarnings(engine);
    QQmlComponent component(engine, url, QQmlComponent::PreferSynchronous);
    if (component.status() != QQmlComponent::Ready) {
        const QString errors = errorsText(component.errors(), root);
        failQml(dock, errors.section(u'\n', 0, 0), errors);
        return;
    }
    const std::string id = parts.plugin;
    const std::string panel = parts.panel;
    QPointer<PluginUi> self(this);
    PluginPanelApi::Hooks hooks{
        [self, id](const std::string &operation, const api::Json &params, std::function<void(api::Result)> done) {
            if (self && self->m_qmlHooks.call)
                self->m_qmlHooks.call(id, operation, params, std::move(done));
            else
                done(api::fail(api::errors::kUnavailable, "the Hikari API isn't available here"));
        },
        [self, id, panel](api::Json data) {
            if (self)
                self->deliver(id, "panel:" + panel, api::Json::object({{"kind", "message"}, {"data", std::move(data)}}), {});
        },
        [self, id](const std::string &setting, const api::Json &value) -> std::optional<std::string> {
            if (!self || !self->m_qmlHooks.setSetting)
                return "HikariSub can’t keep settings here";
            return self->m_qmlHooks.setSetting(id, setting, value);
        },
    };
    const QVariantMap settings = m_qmlHooks.settings ? toVariant(m_qmlHooks.settings(id)).toMap() : QVariantMap();
    auto *api = new PluginPanelApi(engine, std::move(hooks), settings, m_qmlHooks.locale ? m_qmlHooks.locale() : QString());
    auto *context = new QQmlContext(engine->rootContext());
    QObject *object = component.createWithInitialProperties({{QStringLiteral("api"), QVariant::fromValue(api)}}, context);
    auto *item = qobject_cast<QQuickItem *>(object);
    if (!item) {
        const QString errors = errorsText(component.errors(), root);
        delete object;
        delete context;
        delete api;
        failQml(dock, errors.isEmpty() ? QCoreApplication::translate("HikariSub.Plugins", "Its root isn’t an Item.")
                                       : errors.section(u'\n', 0, 0),
                errors);
        return;
    }
    QQmlEngine::setObjectOwnership(item, QQmlEngine::CppOwnership);
    context->setParent(item);
    api->setParent(item);
    item->setParent(host);
    item->setParentItem(host);
    item->setSize(host->size());
    connect(host, &QQuickItem::widthChanged, item, [item, host] { item->setWidth(host->width()); });
    connect(host, &QQuickItem::heightChanged, item, [item, host] { item->setHeight(host->height()); });
    connect(item, &QObject::destroyed, this, [this, dock, item, engine = QPointer(engine)] {
        qmlGone(dock, item);
        QMetaObject::invokeMethod(
            this,
            [this, dock, engine] {
                if (engine) {
                    // trimComponentCache keeps every type that unreclaimed JS objects still reference.
                    engine->collectGarbage();
                    engine->trimComponentCache();
                }
                loadQml(dock);
            },
            Qt::QueuedConnection);
    });
    entry->second.item = item;
    entry->second.api = api;
    writeLoadedPanels();
    if (const auto state = m_panels.find(dock); state != m_panels.end())
        refresh(*state->second);
}

void PluginUi::failQml(const QString &dock, const QString &error, const QString &details)
{
    m_qml[dock].error = error;
    const DockParts parts = partsOfDock(dock);
    const auto known = m_plugins.find(parts.plugin);
    if (m_qmlHooks.log && known != m_plugins.end()) {
        const auto &panels = known->second.plugin.panels;
        const auto declared = std::ranges::find(panels, parts.panel, &plugins::Panel::id);
        const QString title = declared == panels.end() ? qs(parts.panel) : qs(declared->title);
        m_qmlHooks.log(qs(known->second.plugin.name),
                       QCoreApplication::translate("HikariSub.Plugins", "Its panel “%1” couldn’t load: %2")
                           .arg(title, details.isEmpty() ? error : details),
                       true);
    }
    if (const auto state = m_panels.find(dock); state != m_panels.end())
        refresh(*state->second);
}

void PluginUi::unloadQml(const QString &dock)
{
    const auto entry = m_qml.find(dock);
    if (entry == m_qml.end() || !entry->second.item)
        return;
    QQuickItem *item = entry->second.item;
    entry->second.item = nullptr;
    entry->second.leaving = item;
    entry->second.api = nullptr;
    item->setVisible(false);
    item->setParentItem(nullptr);
    item->deleteLater();
    writeLoadedPanels();
}

void PluginUi::qmlGone(const QString &dock, QQuickItem *item)
{
    const auto entry = m_qml.find(dock);
    if (entry == m_qml.end() || (entry->second.item && entry->second.item != item))
        return;
    entry->second.item = nullptr;
    entry->second.api = nullptr;
    writeLoadedPanels();
    if (const auto state = m_panels.find(dock); state != m_panels.end())
        refresh(*state->second);
}

void PluginUi::post(const std::string &plugin, const api::Json &params, const Reply &reply)
{
    const Json *panel = params.find("panel");
    const std::string id = panel && panel->isString() ? panel->asString() : std::string();
    const auto &declared = m_plugins.at(plugin).plugin.panels;
    const auto it = std::ranges::find(declared, id, &plugins::Panel::id);
    if (it == declared.end() || it->qml.empty()) {
        reply(Json::object({{"error", Json::object({{"code", "invalid_params"},
                                                    {"message", "\xE2\x80\x9C" + id + "\xE2\x80\x9D isn't a QML panel in hikari-plugin.json"}})}})
                  .dump());
        return;
    }
    const auto entry = m_qml.find(ui::WorkspaceLayoutController::pluginPanelId(qs(plugin), qs(id)));
    const bool loaded = entry != m_qml.end() && entry->second.api;
    if (loaded) {
        const Json *data = params.find("data");
        entry->second.api->receive(data ? *data : Json());
    }
    reply(Json::object({{"result", Json::object({{"delivered", loaded}})}}).dump());
}

void PluginUi::settingsChanged(const std::string &plugin)
{
    if (!m_qmlHooks.settings)
        return;
    const QVariantMap settings = toVariant(m_qmlHooks.settings(plugin)).toMap();
    for (const auto &[dock, qml] : m_qml)
        if (qml.api && partsOfDock(dock).plugin == plugin)
            qml.api->setSettings(settings);
}

void PluginUi::watchWarnings(QQmlEngine *engine)
{
    if (!m_watchedEngines.insert(engine).second)
        return;
    connect(engine, &QQmlEngine::warnings, this, &PluginUi::qmlWarnings);
    connect(engine, &QObject::destroyed, this, [this, engine] { m_watchedEngines.erase(engine); });
}

void PluginUi::qmlWarnings(const QList<QQmlError> &warnings)
{
    if (!m_qmlHooks.log)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const QQmlError &warning : warnings) {
        if (!warning.url().isLocalFile())
            continue;
        const QString file = warning.url().toLocalFile();
        for (const auto &[id, known] : m_plugins) {
            const QString root = known.plugin.root.toLocalFile();
            if (root.isEmpty() || !file.startsWith(QDir(root).absolutePath() + u'/'))
                continue;
            std::deque<qint64> &times = m_warningTimes[id];
            while (!times.empty() && now - times.front() >= 60'000)
                times.pop_front();
            if (times.size() < std::size_t(kQmlWarningsPerMinute)) {
                times.push_back(now);
                m_qmlHooks.log(qs(known.plugin.name), errorsText({warning}, root), true);
            }
            break;
        }
    }
}

}
