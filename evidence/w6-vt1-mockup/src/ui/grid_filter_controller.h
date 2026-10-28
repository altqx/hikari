#pragma once

// The Grid's filtering preferences (G8; legacy GRID_FILTER_BY,
// GRID_FILTER_STYLES, GRID_FILTER_INVERTED, GRID_ADD_TO_FILTER,
// GRID_FILTER_AFTER_LOAD and GRID_IGNORE_FILTERING at 20d647c4), shown in the
// Grid menu's Filtering submenu and kept in the settings registry (grid.filter*,
// grid.addToFilter, grid.ignoreFiltering). The filter itself runs in the
// application (grid_filtering.h).

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

    // A filter-by item (styles 1, selection 2, comments 4, unconfirmed 8,
    // untranslated 16) checked or unchecked.
    Q_INVOKABLE void setFilterBy(int bit, bool on);
    // A Style item under "Hide lines with styles": the styles filter follows
    // whether any Style is chosen (legacy ID_FILTERING_STYLES).
    Q_INVOKABLE void setStyle(const QString &name, bool on);

    // Reads the settings again.
    void reload();
    // After "Set default": the filter takes the defaults, "Ignore filtering
    // in some actions" stays as it was (legacy keeps it per open grid).
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

} // namespace hikari::ui
