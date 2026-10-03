#include "grid_filter_controller.h"

#include <QSettings>

namespace hikari::ui {

GridFilterController::GridFilterController(QString settingsFile, QObject *parent)
    : QObject(parent), m_settingsFile(std::move(settingsFile))
{
    if (m_settingsFile.isEmpty())
        return;
    const QSettings s(m_settingsFile, QSettings::IniFormat);
    m_filterBy = s.value(QStringLiteral("Grid/FilterBy"), 0).toInt();
    m_styles = s.value(QStringLiteral("Grid/FilterStyles")).toStringList();
    m_inverted = s.value(QStringLiteral("Grid/FilterInverted"), false).toBool();
    m_addToFilter = s.value(QStringLiteral("Grid/AddToFilter"), false).toBool();
    m_afterLoad = s.value(QStringLiteral("Grid/FilterAfterLoad"), false).toBool();
    m_ignore = s.value(QStringLiteral("Grid/IgnoreFiltering"), false).toBool();
}

void GridFilterController::setInverted(bool on)
{
    m_inverted = on;
    save();
    emit changed();
}

void GridFilterController::setAddToFilter(bool on)
{
    m_addToFilter = on;
    save();
    emit changed();
}

void GridFilterController::setAfterLoad(bool on)
{
    m_afterLoad = on;
    save();
    emit changed();
}

void GridFilterController::setIgnoreInActions(bool on)
{
    m_ignore = on;
    save();
    emit changed();
}

void GridFilterController::setFilterBy(int bit, bool on)
{
    m_filterBy = on ? (m_filterBy | bit) : (m_filterBy & ~bit);
    save();
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
    save();
    emit changed();
}

void GridFilterController::save() const
{
    if (m_settingsFile.isEmpty())
        return;
    QSettings s(m_settingsFile, QSettings::IniFormat);
    s.setValue(QStringLiteral("Grid/FilterBy"), m_filterBy);
    s.setValue(QStringLiteral("Grid/FilterStyles"), m_styles);
    s.setValue(QStringLiteral("Grid/FilterInverted"), m_inverted);
    s.setValue(QStringLiteral("Grid/AddToFilter"), m_addToFilter);
    s.setValue(QStringLiteral("Grid/FilterAfterLoad"), m_afterLoad);
    s.setValue(QStringLiteral("Grid/IgnoreFiltering"), m_ignore);
}

} // namespace hikari::ui
