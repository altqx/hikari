#include "hikari/app/docs_export.h"
#include "docking.h"
#include "theme.h"

#include "hikari/app/application.h"
#include "hikari/app/font_catalogs_controller.h"
#include "hikari/app/style_manager_controller.h"
#include "hikari/application/guide_links.h"
#include "hikari/application/hotkeys.h"
#include "hikari/application/options_dialog.h"
#include "hikari/application/settings.h"
#include "settings_store.h"

#include <QDir>
#include <QFile>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QSaveFile>
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

// A QML value as plain variants (a `var` property or a function's result
// can come as a QJSValue).
QVariant plain(const QVariant &value)
{
    return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}

bool write(const QString &dir, const QString &name, const QJsonObject &object, QString *error)
{
    QSaveFile file(QDir(dir).filePath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0 ||
        !file.commit()) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(file.fileName(), file.errorString());
        return false;
    }
    return true;
}

// commands.json: the Hotkeys page's commands (named, not retired) by id.
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

QQuickItem *ancestorWith(QQuickItem *item, const char *property)
{
    for (QQuickItem *p = item ? item->parentItem() : nullptr; p; p = p->parentItem())
        if (has(p, property))
            return p;
    return nullptr;
}

// Its Accessible.name (the attached object is the item's child).
QString accessibleName(const QQuickItem *item)
{
    for (QObject *child : item->children())
        if (child->inherits("QQuickAccessibleAttached"))
            return text(child, "name");
    return {};
}

// The muted line under a control in a column (SettingNote, the Appearance
// page's hints): the label right after it.
QString noteAfter(QQuickItem *item)
{
    QQuickItem *parent = item->parentItem();
    if (!parent || !parent->inherits("QQuickColumnLayout"))
        return {};
    const QList<QQuickItem *> siblings = parent->childItems();
    const qsizetype at = siblings.indexOf(item);
    if (at < 0 || at + 1 >= siblings.size())
        return {};
    QQuickItem *next = siblings.at(at + 1);
    return next->inherits("QQuickText") && !has(next, "setting") ? text(next, "text") : QString();
}

QString controlOf(QQuickItem *item, const application::OptionsBinding *binding)
{
    if (has(item, "docsControl"))
        return text(item, "docsControl");
    if (has(item, "sizeSetting") || (binding && binding->control == application::OptionsControl::Font))
        return QStringLiteral("font");
    if (has(item, "checked"))
        return QStringLiteral("check");
    if (has(item, "from") && has(item, "to"))
        return QStringLiteral("number");
    if (has(item, "count") || (binding && binding->control == application::OptionsControl::IndexChoice))
        return QStringLiteral("choice");
    return QStringLiteral("text");
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

// A choice's entries, when they are the program's own (not what this
// machine has: dictionaries, catalogs, styles, renderers, devices, fonts).
struct Languages {
    QStringList tags, names;
};

QJsonArray entriesOf(QQuickItem *item, const application::OptionsBinding *binding, const Languages &languages)
{
    QJsonArray out;
    if (has(item, "docsValues")) {
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
    } else if (binding->control == application::OptionsControl::IndexChoice) {
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

struct SettingsWalk {
    ui::SettingsStore *store;
    Languages languages;
    QString page;
    QJsonArray settings;

    void add(QQuickItem *item, const QString &key, const QString &control, QJsonObject extra = {})
    {
        const application::SettingDefinition *definition = application::findSetting(key.toStdString());
        if (!definition)
            return;
        const application::OptionsBinding *binding = application::findOptionsBinding(definition->id);
        QQuickItem *row = ancestorWith(item, "labelWidth"); // SettingsRow
        QQuickItem *section = ancestorWith(item, "ruled");  // SettingsSection
        QString label = has(item, "docsLabel") ? text(item, "docsLabel") : QString();
        if (label.isEmpty() && has(item, "checked"))
            label = text(item, "text");
        if (label.isEmpty())
            label = accessibleName(item);
        if (label.isEmpty() && has(item, "name"))
            label = text(item, "name");
        if (label.isEmpty() && row)
            label = text(row, "text");
        QStringList hints;
        for (const QString &hint : {row ? text(row, "note") : QString(), has(item, "tip") ? text(item, "tip") : QString(),
                                    noteAfter(item)})
            if (!hint.isEmpty())
                hints << hint;
        QJsonObject o{
            {QStringLiteral("key"), key},
            {QStringLiteral("page"), page},
            {QStringLiteral("section"), section ? text(section, "title") : QString()},
            {QStringLiteral("label"), label},
            {QStringLiteral("hint"), hints.join(QLatin1Char(' '))},
            {QStringLiteral("control"), control},
            {QStringLiteral("type"), typeName(definition->type)},
            {QStringLiteral("default"), QJsonValue::fromVariant(store->value(key))},
        };
        if (control == QLatin1String("number")) {
            o.insert(QStringLiteral("min"), item->property("from").toInt());
            o.insert(QStringLiteral("max"), item->property("to").toInt());
            if (const QString unit = text(item, "unit"); !unit.isEmpty())
                o.insert(QStringLiteral("unit"), unit);
        }
        if (control == QLatin1String("choice"))
            o.insert(QStringLiteral("values"), entriesOf(item, binding, languages));
        for (auto it = extra.begin(); it != extra.end(); ++it)
            o.insert(it.key(), it.value());
        settings.append(o);
    }

    void walk(QQuickItem *item)
    {
        // A control for each theme's setting, only one of them shown at a
        // time (the accents, the high contrast colors), or one only a
        // platform's window makes (Windows' player and audio API): every
        // one, named.
        if (has(item, "docsEntries")) {
            for (const QVariant &v : plain(item->property("docsEntries")).toList()) {
                const QVariantMap entry = v.toMap();
                QJsonObject extra{{QStringLiteral("label"), entry.value(QStringLiteral("label")).toString()}};
                if (entry.contains(QStringLiteral("values")))
                    extra.insert(QStringLiteral("values"), QJsonArray::fromVariantList(entry.value(QStringLiteral("values")).toList()));
                for (const QString &field : {QStringLiteral("hint"), QStringLiteral("platform")})
                    if (entry.contains(field))
                        extra.insert(field, entry.value(field).toString());
                add(item, entry.value(QStringLiteral("setting")).toString(), entry.value(QStringLiteral("control")).toString(), extra);
            }
            return;
        }
        QString key = has(item, "docsSetting") ? text(item, "docsSetting") : QString();
        if (key.isEmpty() && has(item, "setting") && item->property("setting").userType() == QMetaType::QString)
            key = text(item, "setting");
        if (key.isEmpty() && item->objectName().startsWith(QLatin1String("setting_")))
            key = item->objectName().mid(8);
        if (!key.isEmpty()) {
            const application::OptionsBinding *binding = application::findOptionsBinding(key.toStdString());
            const QString control = controlOf(item, binding);
            add(item, key, control);
            if (control == QLatin1String("font") && has(item, "sizeSetting"))
                add(item, text(item, "sizeSetting"), QStringLiteral("number"),
                    {{QStringLiteral("label"), text(item, "name") + QStringLiteral(" size")},
                     {QStringLiteral("min"), 1},
                     {QStringLiteral("max"), 200}});
            return;
        }
        for (QQuickItem *child : item->childItems())
            walk(child);
    }
};

// settings.json: the Settings window's pages and bound controls.
QJsonObject settings(Application &application, QObject *dialog)
{
    const QVariantMap opened = application.openSettingsDialog();
    for (const char *list : {"languages", "dictionaries", "catalogs", "styles", "renderers", "rendererLabels",
                             "outputDevices", "values"})
        dialog->setProperty(list, opened.value(QLatin1String(list)));
    QMetaObject::invokeMethod(dialog, "reloaded");

    SettingsWalk walk{application.settingsStore(),
                      {opened.value(QStringLiteral("languageTags")).toStringList(),
                       opened.value(QStringLiteral("languages")).toStringList()},
                      {},
                      {}};
    QJsonArray pages;
    auto *stack = dialog->findChild<QQuickItem *>(QStringLiteral("settingsPageStack"));
    const QVariantList pageList = plain(dialog->property("pages")).toList();
    const QList<QQuickItem *> pageItems = stack ? stack->childItems() : QList<QQuickItem *>();
    for (qsizetype i = 0; i < pageList.size(); ++i) {
        const QVariantMap page = pageList.at(i).toMap();
        walk.page = page.value(QStringLiteral("id")).toString();
        pages.append(QJsonObject{{QStringLiteral("id"), walk.page},
                                 {QStringLiteral("name"), page.value(QStringLiteral("name")).toString()}});
        if (i < pageItems.size())
            walk.walk(pageItems.at(i));
    }
    return {{QStringLiteral("pages"), pages}, {QStringLiteral("settings"), walk.settings}};
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
            {QStringLiteral("files"), QJsonArray{QStringLiteral("commands.json"), QStringLiteral("settings.json"),
                                                 QStringLiteral("guide-links.json")}}};
}

} // namespace

bool exportDocs(const QString &dir, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!QDir().mkpath(dir))
        return fail(QStringLiteral("Cannot make %1").arg(dir));
    QTemporaryDir profile;
    if (!profile.isValid())
        return fail(QStringLiteral("Cannot make a temporary profile: %1").arg(profile.errorString()));
    Application::Options options;
    options.settingsFile = profile.filePath(QStringLiteral("hikari.ini"));
    options.recoveryDir = profile.filePath(QStringLiteral("Recovery"));
    options.userAutomationDir = profile.filePath(QStringLiteral("Automation"));
    options.systemUiLanguages = {QStringLiteral("en")};
    options.playbackAudio = false;
    options.spellingBackend = {};
    Application application(options);

    hikari::ui::theme::chooseControlsStyle();
    QQmlApplicationEngine engine;
    hikari::ui::attachDocking(engine);
    attachStylePreview(engine, application.styleManager());
    attachFontCatalogPreview(engine, application.fontCatalogs());
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
           write(dir, QStringLiteral("meta.json"), meta(), error);
}

} // namespace hikari::app
