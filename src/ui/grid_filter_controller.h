#pragma once

#include "settings_store.h"

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace hikari::ui {

class GridFilterController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(int filterBy READ filterBy NOTIFY changed)
    Q_PROPERTY(QStringList styles READ styles NOTIFY changed)
    Q_PROPERTY(bool inverted READ inverted WRITE setInverted NOTIFY changed)
    Q_PROPERTY(bool addToFilter READ addToFilter WRITE setAddToFilter NOTIFY changed)
    Q_PROPERTY(bool afterLoad READ afterLoad WRITE setAfterLoad NOTIFY changed)
    Q_PROPERTY(bool ignoreInActions READ ignoreInActions WRITE setIgnoreInActions NOTIFY changed)
public:
    explicit GridFilterController(QString settingsFile = {}, QObject *parent = nullptr);
    explicit GridFilterController(SettingsStore &settings, QObject *parent = nullptr);

    int filterBy() const { return m_filterBy; }
    QStringList styles() const { return m_styles; }
    bool inverted() const { return m_inverted; }
    bool addToFilter() const { return m_addToFilter; }
    bool afterLoad() const { return m_afterLoad; }
    bool ignoreInActions() const { return m_ignore; }
    void setInverted(bool on);
    void setAddToFilter(bool on);
    void setAfterLoad(bool on);
    void setIgnoreInActions(bool on);

    // MUST KILL: setFilterBy and filterBy use magic bits (styles 1, selection 2, comments 4, unconfirmed 8, untranslated 16)
    Q_INVOKABLE void setFilterBy(int bit, bool on);
    Q_INVOKABLE void setStyle(const QString &name, bool on);

    void reload();
    // MUST KILL: settingsReset leaves ignoreInActions as it was
    void settingsReset();

signals:
    void changed();

private:
    void load();

    std::unique_ptr<SettingsStore> m_ownedSettings;
    SettingsStore *m_settings;
    int m_filterBy = 0;
    QStringList m_styles;
    bool m_inverted = false;
    bool m_addToFilter = false;
    bool m_afterLoad = false;
    bool m_ignore = false;
};

}
