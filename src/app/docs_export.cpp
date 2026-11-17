#include "hikari/app/docs_export.h"
#include "docking.h"
#include "theme.h"

#include "hikari/api/reference.h"
#include "hikari/app/application.h"
#include "hikari/app/font_catalogs_controller.h"
#include "hikari/app/local_only_network.h"
#include "hikari/app/plugin_marketplace.h"
#include "hikari/app/style_manager_controller.h"
#include "hikari/application/guide_links.h"
#include "hikari/application/hotkeys.h"
#include "hikari/application/options_dialog.h"
#include "hikari/application/settings.h"
#include "settings_catalogue.h"
#include "settings_store.h"

#include <QDir>
#include <QFile>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>

#include <map>

namespace hikari::app {

namespace {

QString qs(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

bool has(const QObject *o, const char *property)
{
    return o->metaObject()->indexOfProperty(property) >= 0;
}

QString text(const QObject *o, const char *property)
{
    return o->property(property).toString();
}

// A QML `var` property or function result can arrive as a QJSValue.
QVariant plain(const QVariant &value)
{
    return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}

bool write(const QString &dir, const QString &name, const QByteArray &bytes, QString *error)
{
    QSaveFile file(QDir(dir).filePath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) < 0 || !file.commit()) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(file.fileName(), file.errorString());
        return false;
    }
    return true;
}

bool write(const QString &dir, const QString &name, const QJsonObject &object, QString *error)
{
    return write(dir, name, QJsonDocument(object).toJson(QJsonDocument::Indented), error);
}

QJsonObject commands(const QVariantMap &menus)
{
    std::map<int, QJsonArray> defaults;
    for (const auto &[key, hotkey] : application::defaultHotkeys())
        if (!hotkey.accel.empty())
            defaults[key.id].append(QJsonObject{{QStringLiteral("context"), qs(application::hotkeyWindowName(key.type))},
                                                {QStringLiteral("keys"), qs(application::qtKeysOfAccel(hotkey.accel))}});
    std::map<int, QJsonObject> byId;
    for (const application::HotkeyAction &action : application::hotkeyActions()) {
        const std::string name = application::hotkeyName(action.id);
        if (name.empty() || application::isRetiredHotkey(action.id))
            continue;
        const QString symbol = qs(action.symbol);
        byId[action.id] = QJsonObject{
            {QStringLiteral("id"), action.id},
            {QStringLiteral("symbol"), symbol},
            {QStringLiteral("name"), qs(name)},
            {QStringLiteral("context"), qs(application::hotkeyWindowName(application::hotkeyType(action.id)))},
            {QStringLiteral("defaultKeys"), defaults[action.id]},
            {QStringLiteral("menus"), QJsonArray::fromStringList(menus.value(symbol).toStringList())},
        };
    }
    QJsonArray list;
    for (const auto &[id, command] : byId)
        list.append(command);
    return {{QStringLiteral("commands"), list}};
}

QString controlOf(const QQuickItem *item, const application::SettingDefinition &definition,
                  const application::OptionsBinding *binding)
{
    using application::OptionsControl;
    if (item && has(item, "docsControl"))
        return text(item, "docsControl");
    if (binding) {
        switch (binding->control) {
        case OptionsControl::Check: return QStringLiteral("check");
        case OptionsControl::Number: return QStringLiteral("number");
        case OptionsControl::Text:
        case OptionsControl::ZoomText: return QStringLiteral("text");
        case OptionsControl::Font: return QStringLiteral("font");
        default: return QStringLiteral("choice");
        }
    }
    if (item && has(item, "count"))
        return QStringLiteral("choice");
    switch (definition.type) {
    case application::SettingType::Bool: return QStringLiteral("check");
    case application::SettingType::Int: return QStringLiteral("number");
    default: return QStringLiteral("text");
    }
}

QString typeName(application::SettingType type)
{
    switch (type) {
    case application::SettingType::Bool: return QStringLiteral("bool");
    case application::SettingType::Int: return QStringLiteral("int");
    case application::SettingType::String: return QStringLiteral("string");
    case application::SettingType::StringList: return QStringLiteral("stringList");
    }
    return {};
}

struct Languages {
    QStringList tags, names;
};

QJsonArray entriesOf(QQuickItem *item, const application::OptionsBinding *binding, const Languages &languages)
{
    QJsonArray out;
    if (item && has(item, "docsValues")) {
        for (const QVariant &v : plain(item->property("docsValues")).toList())
            out.append(QJsonObject::fromVariantMap(v.toMap()));
        return out;
    }
    if (!binding)
        return out;
    if (binding->control == application::OptionsControl::Language) {
        for (qsizetype i = 0; i < languages.names.size() && i < languages.tags.size(); ++i)
            out.append(QJsonObject{{QStringLiteral("value"), languages.tags.at(i)},
                                   {QStringLiteral("label"), languages.names.at(i)}});
    } else if (binding->control == application::OptionsControl::IndexChoice && item) {
        const int count = item->property("count").toInt();
        for (int i = 0; i < count; ++i) {
            QString label;
            QMetaObject::invokeMethod(item, "textAt", Q_RETURN_ARG(QString, label), Q_ARG(int, i));
            out.append(QJsonObject{{QStringLiteral("value"), i}, {QStringLiteral("label"), label}});
        }
    } else if (binding->control == application::OptionsControl::ComboText) {
        for (std::string_view fps : application::optionsConversionFps())
            out.append(QJsonObject{{QStringLiteral("value"), qs(fps)}, {QStringLiteral("label"), qs(fps)}});
    }
    return out;
}

QString sectionKeyAbove(QQuickItem *item)
{
    for (QQuickItem *p = item->parentItem(); p; p = p->parentItem())
        if (has(p, "key") && has(p, "ruled"))
            return text(p, "key");
    return {};
}

struct SettingsExport {
    QObject *dialog;
    ui::SettingsStore *store;
    Languages languages;
    QVariantMap shown;
    QJsonArray settings;
    QSet<QString> seen;

    QQuickItem *control(const QString &key) const
    {
        return dialog->findChild<QQuickItem *>(QStringLiteral("setting_") + key);
    }

    void add(const QString &key, const QString &section, QQuickItem *item, const QJsonObject &extra = {})
    {
        const application::SettingDefinition *definition = application::findSetting(key.toStdString());
        if (!definition || seen.contains(key))
            return;
        seen.insert(key);
        const application::OptionsBinding *binding = application::findOptionsBinding(definition->id);
        const QString kind = extra.contains(QStringLiteral("control")) ? extra.value(QStringLiteral("control")).toString()
                                                                       : controlOf(item, *definition, binding);
        QJsonObject o{
            {QStringLiteral("key"), key},
            {QStringLiteral("page"), section.left(section.indexOf(QLatin1Char('.')))},
            {QStringLiteral("section"), ui::SettingsCatalogue::sectionTitle(section)},
            {QStringLiteral("label"), ui::SettingsCatalogue::label(key)},
            {QStringLiteral("hint"), ui::SettingsCatalogue::description(key)},
            {QStringLiteral("control"), kind},
            {QStringLiteral("type"), typeName(definition->type)},
            {QStringLiteral("default"), QJsonValue::fromVariant(store->value(key))},
        };
        if (kind == QLatin1String("number")) {
            if (shown.contains(key) && shown.value(key).toString() != store->value(key).toString())
                o.insert(QStringLiteral("default"), QJsonValue::fromVariant(shown.value(key)));
            const bool bound = binding && binding->control == application::OptionsControl::Number;
            o.insert(QStringLiteral("min"), bound ? qint64(binding->min) : (item ? item->property("from").toInt() : 0));
            o.insert(QStringLiteral("max"), bound ? qint64(binding->max) : (item ? item->property("to").toInt() : 0));
            if (const QString unit = item && has(item, "unit") ? text(item, "unit") : QString(); !unit.isEmpty())
                o.insert(QStringLiteral("unit"), unit);
        }
        if (kind == QLatin1String("choice"))
            o.insert(QStringLiteral("values"), entriesOf(item, binding, languages));
        for (auto it = extra.begin(); it != extra.end(); ++it)
            if (!it.value().toString().isEmpty() || !it.value().isString())
                o.insert(it.key(), it.value());
        settings.append(o);
        if (kind == QLatin1String("font") && binding && !binding->sizeSetting.empty())
            add(qs(binding->sizeSetting), section, nullptr, {{QStringLiteral("control"), QStringLiteral("number")}});
    }

    // MUST KILL: values the catalogue doesn't hold yet (theme lists, Windows-only rows) come from docs* properties.
    void addHooks(QQuickItem *item)
    {
        if (has(item, "docsEntries")) {
            const QString section = sectionKeyAbove(item);
            for (const QVariant &v : plain(item->property("docsEntries")).toList()) {
                const QVariantMap entry = v.toMap();
                QJsonObject extra = QJsonObject::fromVariantMap(entry);
                extra.remove(QStringLiteral("setting"));
                add(entry.value(QStringLiteral("setting")).toString(), section, item, extra);
            }
            return;
        }
        if (has(item, "docsSetting")) {
            const QString key = text(item, "docsSetting");
            QJsonObject extra;
            if (has(item, "docsLabel"))
                extra.insert(QStringLiteral("label"), text(item, "docsLabel"));
            add(key, sectionKeyAbove(item), item, extra);
            return;
        }
        for (QQuickItem *child : item->childItems())
            addHooks(child);
    }
};

QJsonObject settings(Application &application, QObject *dialog)
{
    const QVariantMap opened = application.openSettingsDialog();
    for (const char *list : {"languages", "dictionaries", "catalogs", "styles", "renderers", "rendererLabels",
                             "outputDevices", "values"})
        dialog->setProperty(list, opened.value(QLatin1String(list)));
    QMetaObject::invokeMethod(dialog, "reloaded");

    SettingsExport out{dialog,
                       application.settingsStore(),
                       {opened.value(QStringLiteral("languageTags")).toStringList(),
                        opened.value(QStringLiteral("languages")).toStringList()},
                       opened.value(QStringLiteral("values")).toMap(),
                       {},
                       {}};
    if (auto *window = qobject_cast<QQuickWindow *>(dialog))
        out.addHooks(window->contentItem());
    for (const QString &key : ui::SettingsCatalogue::settings())
        if (application::optionsBindingShown(key.toStdString()))
            out.add(key, ui::SettingsCatalogue::sectionOf(key), out.control(key));

    QJsonArray pages;
    for (const QVariant &v : ui::SettingsCatalogue::pages()) {
        const QVariantMap page = v.toMap();
        pages.append(QJsonObject{{QStringLiteral("id"), page.value(QStringLiteral("id")).toString()},
                                 {QStringLiteral("name"), page.value(QStringLiteral("name")).toString()}});
    }
    return {{QStringLiteral("pages"), pages}, {QStringLiteral("settings"), out.settings}};
}

QJsonObject guideLinks()
{
    QJsonArray links;
    for (const application::GuideLink &link : application::guideLinks())
        links.append(QJsonObject{{QStringLiteral("kind"), qs(application::guideSurfaceName(link.kind))},
                                 {QStringLiteral("id"), qs(link.id)},
                                 {QStringLiteral("path"), qs(link.path)}});
    return {{QStringLiteral("site"), qs(application::kSiteUrl)},
            {QStringLiteral("home"), qs(application::kGuideHome)},
            {QStringLiteral("links"), links}};
}

QJsonObject meta()
{
    return {{QStringLiteral("schema"), kDocsSchema},
            {QStringLiteral("application"), QStringLiteral("HikariSub")},
            {QStringLiteral("version"), QStringLiteral(HIKARI_VERSION)},
            {QStringLiteral("commit"), QStringLiteral(HIKARI_COMMIT)},
            {QStringLiteral("site"), qs(application::kSiteUrl)},
            {QStringLiteral("api"), api::kApiVersion},
            {QStringLiteral("files"), QJsonArray{QStringLiteral("commands.json"), QStringLiteral("settings.json"),
                                                 QStringLiteral("guide-links.json"), QStringLiteral("api.json"),
                                                 QStringLiteral("api.md")}}};
}

}

bool exportDocs(const QString &dir, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (QStringLiteral(HIKARI_COMMIT).isEmpty())
        return fail(QStringLiteral("This build does not know its commit"));
    if (!QDir().mkpath(dir))
        return fail(QStringLiteral("Cannot make %1").arg(dir));
    QTemporaryDir profile;
    if (!profile.isValid())
        return fail(QStringLiteral("Cannot make a temporary profile: %1").arg(profile.errorString()));
    Application::Options options;
    options.settingsFile = profile.filePath(QStringLiteral("settings.toml"));
    options.recoveryDir = profile.filePath(QStringLiteral("Recovery"));
    options.userAutomationDir = profile.filePath(QStringLiteral("Automation"));
    options.systemUiLanguages = {QStringLiteral("en")};
    options.playbackAudio = false;
    options.spellingBackend = {};
    options.externalAgents = false;
    Application application(options);

    hikari::ui::theme::chooseControlsStyle();
    QQmlApplicationEngine engine;
    refuseRemoteUrls(engine);
    hikari::ui::attachDocking(engine);
    attachStylePreview(engine, application.styleManager());
    attachFontCatalogPreview(engine, application.fontCatalogs());
    attachAlignToVideoStill(engine, application.alignToVideo());
    attachVideoAnalysisStill(engine, application.videoAnalysis());
    attachMarketplaceImages(engine, *application.pluginMarketplace());
    engine.setInitialProperties(application.qmlProperties());
    engine.loadFromModule("Hikari.Ui", "Main");
    if (engine.rootObjects().isEmpty())
        return fail(QStringLiteral("Cannot make the main window"));
    QObject *root = engine.rootObjects().first();

    QVariant menus;
    QMetaObject::invokeMethod(root, "commandMenus", Q_RETURN_ARG(QVariant, menus));
    QVariant dialog;
    QMetaObject::invokeMethod(root, "settingsWindow", Q_RETURN_ARG(QVariant, dialog));
    auto *settingsDialog = plain(dialog).value<QObject *>();
    if (!settingsDialog)
        return fail(QStringLiteral("Cannot make the Settings window"));

    return write(dir, QStringLiteral("commands.json"), commands(plain(menus).toMap()), error) &&
           write(dir, QStringLiteral("settings.json"), settings(application, settingsDialog), error) &&
           write(dir, QStringLiteral("guide-links.json"), guideLinks(), error) &&
           write(dir, QStringLiteral("api.json"), QByteArray::fromStdString(api::apiReference().dump(2) + "\n"), error) &&
           write(dir, QStringLiteral("api.md"), QByteArray::fromStdString(api::referenceMarkdown(api::apiReference())), error) &&
           write(dir, QStringLiteral("meta.json"), meta(), error);
}

}
