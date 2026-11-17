#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

// Every row of the Settings window: its page, section, label and description.
class SettingsCatalogue : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;

    // In the interface language; empty for a setting the window has no row for.
    Q_INVOKABLE static QString label(const QString &setting);
    Q_INVOKABLE static QString description(const QString &setting);
    // "<page>.<section>"; empty when uncatalogued.
    Q_INVOKABLE static QString sectionOf(const QString &setting);
    Q_INVOKABLE static QString pageOf(const QString &setting);
    Q_INVOKABLE static bool projectable(const QString &setting);
    // [{id, name, icon, sections: [{id, title, project}]}] in window order; `project` is whether the
    // Project tab shows the section.
    Q_INVOKABLE static QVariantList pages();
    Q_INVOKABLE static QString sectionTitle(const QString &section);

    // `query` holds words and the tokens @modified and @plugin:<plugin id>. `plugins` lists plugin rows
    // {setting, plugin, pluginName, label, description, keywords, projectable}; `changed` is the setting ids
    // changed on the tab. Returns {settings: {id: true}, sections: {id: count}, pages: {id: count}, count,
    // modified, plugin}; a matching section or page name matches all of its rows, and a section without
    // catalogued rows counts once.
    Q_INVOKABLE static QVariantMap search(const QString &query, const QVariantList &plugins = {},
                                          const QStringList &changed = {}, bool project = false);

    static QStringList settings();
};

}
