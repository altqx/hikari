#include "grid_filter_controller.h"

namespace hikari::ui {

GridFilterController::GridFilterController(QString settingsFile, QObject *parent)
    : QObject(parent), m_ownedSettings(std::make_unique<SettingsStore>(std::move(settingsFile))),
      m_settings(m_ownedSettings.get())
{
    load();
}

GridFilterController::GridFilterController(SettingsStore &settings, QObject *parent)
    : QObject(parent), m_settings(&settings)
{
    load();
}

void GridFilterController::reload()
{
    load();
    emit changed();
}

void GridFilterController::settingsReset()
{
    // Legacy SubsGrid reads these when it filters; each open grid keeps the
    // ignoreFiltered it read when it was created.
    const bool ignore = m_ignore;
    load();
    m_ignore = ignore;
    emit changed();
}

void GridFilterController::load()
{
    m_filterBy = m_settings->integer("grid.filterBy");
    m_styles = m_settings->list("grid.filterStyles");
    m_inverted = m_settings->boolean("grid.filterInverted");
    m_addToFilter = m_settings->boolean("grid.addToFilter");
    m_afterLoad = m_settings->boolean("grid.filterAfterLoad");
    m_ignore = m_settings->boolean("grid.ignoreFiltering");
}

void GridFilterController::setInverted(bool on)
{
    m_inverted = on;
    m_settings->set("grid.filterInverted", m_inverted);
    emit changed();
}

void GridFilterController::setAddToFilter(bool on)
{
    m_addToFilter = on;
    m_settings->set("grid.addToFilter", m_addToFilter);
    emit changed();
}

void GridFilterController::setAfterLoad(bool on)
{
    m_afterLoad = on;
    m_settings->set("grid.filterAfterLoad", m_afterLoad);
    emit changed();
}

void GridFilterController::setIgnoreInActions(bool on)
{
    m_ignore = on;
    m_settings->set("grid.ignoreFiltering", m_ignore);
    emit changed();
}

void GridFilterController::setFilterBy(int bit, bool on)
{
    m_filterBy = on ? (m_filterBy | bit) : (m_filterBy & ~bit);
    m_settings->set("grid.filterBy", m_filterBy);
    emit changed();
}

void GridFilterController::setStyle(const QString &name, bool on)
{
    m_styles.removeAll(name);
    if (on)
        m_styles << name;
    if (m_styles.isEmpty())
        m_filterBy &= ~1;
    else
        m_filterBy |= 1;
    m_settings->set("grid.filterStyles", m_styles);
    m_settings->set("grid.filterBy", m_filterBy);
    emit changed();
}

} // namespace hikari::ui
