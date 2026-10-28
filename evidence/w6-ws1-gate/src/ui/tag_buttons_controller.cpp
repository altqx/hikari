#include "tag_buttons_controller.h"

#include <QVariantMap>

#include <algorithm>

namespace hikari::ui {

namespace {

QString key(int index)
{
    return QStringLiteral("editor.tagButton%1").arg(index + 1);
}

} // namespace

TagButtonsController::TagButtonsController(QString settingsFile, QObject *parent)
    : QObject(parent), m_ownedSettings(std::make_unique<SettingsStore>(std::move(settingsFile))),
      m_settings(m_ownedSettings.get())
{
    load();
}

TagButtonsController::TagButtonsController(SettingsStore &settings, QObject *parent)
    : QObject(parent), m_settings(&settings)
{
    load();
}

void TagButtonsController::reload()
{
    load();
    emit changed();
}

void TagButtonsController::load()
{
    m_count = std::clamp(m_settings->integer("editor.tagButtons"), 0, kMaximum);
    for (int i = 0; i < kMaximum; ++i)
        m_buttons[static_cast<std::size_t>(i)] = fromLegacy(m_settings->value(key(i)).toString(), i);
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
    // NumTagButtons OK: Options.SetInt, then SetTagButtons adds the buttons
    // past the shown ones from their options or removes the last ones; the
    // shown ones stay as they are.
    count = std::clamp(count, 0, kMaximum);
    m_settings->set("editor.tagButtons", count);
    for (int i = m_count; i < count; ++i)
        m_buttons[static_cast<std::size_t>(i)] = fromLegacy(m_settings->value(key(i)).toString(), i);
    if (count == m_count)
        return;
    m_count = count;
    emit changed();
}

int TagButtonsController::storedCount() const
{
    return std::clamp(m_settings->integer("editor.tagButtons"), 0, kMaximum);
}

QVariantMap TagButtonsController::pressed(int index) const
{
    if (index < 0 || index >= kMaximum)
        return {};
    // config::GetTable with wxTOKEN_STRTOK (empty entries dropped).
    const QString value = m_settings->value(key(index)).toString();
    QStringList table;
    if (value.size() > 4)
        for (const QString &entry : value.mid(2, value.size() - 4).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            table << entry.mid(1);
    if (table.size() < 2)
        return {}; // wxBell
    const int type = table[1] == QLatin1String("2") ? 2 : table[1] == QLatin1String("1") ? 1 : 0;
    return {{QStringLiteral("tag"), table[0]}, {QStringLiteral("type"), type}};
}

void TagButtonsController::edit(int index, const QString &name, const QString &tag, int type)
{
    if (index < 0 || index >= kMaximum)
        return;
    m_buttons[static_cast<std::size_t>(index)] = Button{tag, std::clamp(type, 0, 2), name};
    // OnEditTag writes this button's option only.
    m_settings->setValue(key(index), toLegacy(m_buttons[static_cast<std::size_t>(index)]));
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

} // namespace hikari::ui
