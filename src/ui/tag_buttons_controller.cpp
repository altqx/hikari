#include "tag_buttons_controller.h"

#include <QSettings>
#include <QVariantMap>

#include <algorithm>

namespace hikari::ui {

namespace {

QString key(int index)
{
    return QStringLiteral("Editor/TagButton%1").arg(index + 1);
}

} // namespace

TagButtonsController::TagButtonsController(QString settingsFile, QObject *parent)
    : QObject(parent), m_settingsFile(std::move(settingsFile))
{
    for (int i = 0; i < kMaximum; ++i)
        m_buttons[static_cast<std::size_t>(i)] = fromLegacy(QString(), i);
    if (m_settingsFile.isEmpty())
        return;
    const QSettings settings(m_settingsFile, QSettings::IniFormat);
    m_count = std::clamp(settings.value(QStringLiteral("Editor/TagButtons"), 0).toInt(), 0, kMaximum);
    for (int i = 0; i < kMaximum; ++i)
        m_buttons[static_cast<std::size_t>(i)] = fromLegacy(settings.value(key(i)).toString(), i);
}

QVariantList TagButtonsController::buttons() const
{
    QVariantList out;
    for (int i = 0; i < m_count; ++i) {
        const auto &b = m_buttons[static_cast<std::size_t>(i)];
        out << QVariantMap{{QStringLiteral("name"), b.name}, {QStringLiteral("tag"), b.tag}, {QStringLiteral("type"), b.type}};
    }
    return out;
}

void TagButtonsController::setCount(int count)
{
    count = std::clamp(count, 0, kMaximum);
    if (count == m_count)
        return;
    m_count = count;
    save();
    emit changed();
}

void TagButtonsController::edit(int index, const QString &name, const QString &tag, int type)
{
    if (index < 0 || index >= kMaximum)
        return;
    m_buttons[static_cast<std::size_t>(index)] = Button{tag, std::clamp(type, 0, 2), name};
    save();
    emit changed();
}

TagButtonsController::Button TagButtonsController::fromLegacy(const QString &value, int index)
{
    QStringList table;
    // Remove "{\n" and "\n}", split on "\n" keeping empty entries, drop each tab.
    if (value.size() > 4)
        for (const QString &entry : value.mid(2, value.size() - 4).split(QLatin1Char('\n')))
            table << entry.mid(1);
    Button b;
    b.name = table.size() > 2 ? table[2] : QStringLiteral("T%1").arg(index + 1);
    b.type = table.size() > 1 ? table[1].toInt() : 0; // wxAtoi: 0 when it is not a number
    b.tag = table.isEmpty() ? QString() : table[0];
    return b;
}

QString TagButtonsController::toLegacy(const Button &button)
{
    return QStringLiteral("{\n\t") + button.tag + QStringLiteral("\n\t") + QString::number(button.type) +
           QStringLiteral("\n\t") + button.name + QStringLiteral("\n}");
}

void TagButtonsController::save() const
{
    if (m_settingsFile.isEmpty())
        return;
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    settings.setValue(QStringLiteral("Editor/TagButtons"), m_count);
    for (int i = 0; i < kMaximum; ++i)
        settings.setValue(key(i), toLegacy(m_buttons[static_cast<std::size_t>(i)]));
}

} // namespace hikari::ui
