#include "tag_buttons_controller.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QVariantMap>

#include <algorithm>

namespace hikari::ui {

namespace {

QString key(int index)
{
    return QStringLiteral("editor.tagButton%1").arg(index + 1);
}

QVariantMap toMap(const TagButtonsController::Button &b)
{
    return {{QStringLiteral("name"), b.name}, {QStringLiteral("tag"), b.tag}, {QStringLiteral("type"), b.type},
            {QStringLiteral("icon"), b.icon}};
}

bool operator==(const TagButtonsController::Button &a, const TagButtonsController::Button &b)
{
    return a.tag == b.tag && a.type == b.type && a.name == b.name && a.icon == b.icon;
}

QString knownIcon(const QString &icon)
{
    return TagButtonsController::iconRoles().contains(icon) ? icon : QString();
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
    for (int i = 0; i < m_count; ++i)
        out << toMap(m_buttons[static_cast<std::size_t>(i)]);
    return out;
}

QStringList TagButtonsController::iconRoles()
{
    // The set's icons of override tags (docs/ux/icons.md: one icon, one
    // meaning): \b \i \u \s, \fn, \1c-\4c, \an1-9, \fscx \fscy, \frz
    // \frx/\fry, \pos, \move, \clip (rectangle, vector), \p, \k.
    static const QStringList roles{
        QStringLiteral("tag-bold"), QStringLiteral("tag-italic"), QStringLiteral("tag-underline"),
        QStringLiteral("tag-strikeout"), QStringLiteral("tag-font"), QStringLiteral("colour-primary"),
        QStringLiteral("colour-secondary"), QStringLiteral("colour-outline"), QStringLiteral("colour-shadow"),
        QStringLiteral("alignment-1"), QStringLiteral("alignment-2"), QStringLiteral("alignment-3"),
        QStringLiteral("alignment-4"), QStringLiteral("alignment-5"), QStringLiteral("alignment-6"),
        QStringLiteral("alignment-7"), QStringLiteral("alignment-8"), QStringLiteral("alignment-9"),
        QStringLiteral("scale-x"), QStringLiteral("scale-y"), QStringLiteral("tool-rotate-z"),
        QStringLiteral("tool-rotate-xy"), QStringLiteral("tool-position"), QStringLiteral("tool-move"),
        QStringLiteral("tool-clip-rect"), QStringLiteral("tool-clip-vector"), QStringLiteral("tool-drawing"),
        QStringLiteral("karaoke")};
    return roles;
}

void TagButtonsController::store(int index)
{
    m_settings->setValue(key(index), toLegacy(m_buttons[static_cast<std::size_t>(index)]));
}

void TagButtonsController::storeCount()
{
    m_settings->set("editor.tagButtons", m_count);
}

bool TagButtonsController::add(const QString &name, const QString &tag, int type, const QString &icon)
{
    if (m_count >= kMaximum)
        return false;
    // As "Change number of buttons" one more, then that button's Save tag.
    m_buttons[static_cast<std::size_t>(m_count)] = Button{tag, std::clamp(type, 0, 2), name, knownIcon(icon)};
    store(m_count);
    ++m_count;
    storeCount();
    emit changed();
    return true;
}

void TagButtonsController::remove(int index)
{
    if (index < 0 || index >= m_count)
        return;
    for (int i = index; i + 1 < m_count; ++i) {
        m_buttons[static_cast<std::size_t>(i)] = m_buttons[static_cast<std::size_t>(i) + 1];
        store(i);
    }
    --m_count;
    // The freed place starts empty, as a button never defined.
    m_buttons[static_cast<std::size_t>(m_count)] = fromLegacy({}, m_count);
    m_settings->reset(key(m_count));
    storeCount();
    emit changed();
}

bool TagButtonsController::move(int index, int step)
{
    const int to = index + step;
    if (index < 0 || index >= m_count || to < 0 || to >= m_count || to == index)
        return false;
    std::swap(m_buttons[static_cast<std::size_t>(index)], m_buttons[static_cast<std::size_t>(to)]);
    store(index);
    store(to);
    emit changed();
    return true;
}

void TagButtonsController::replace(const QVariantList &list)
{
    const int count = std::min<int>(static_cast<int>(list.size()), kMaximum);
    bool any = count != m_count;
    for (int i = 0; i < count; ++i) {
        const QVariantMap m = list[i].toMap();
        const Button b{m.value(QStringLiteral("tag")).toString(), std::clamp(m.value(QStringLiteral("type")).toInt(), 0, 2),
                       m.value(QStringLiteral("name")).toString(), knownIcon(m.value(QStringLiteral("icon")).toString())};
        if (i < m_count && m_buttons[static_cast<std::size_t>(i)] == b)
            continue;
        m_buttons[static_cast<std::size_t>(i)] = b;
        store(i);
        any = true;
    }
    for (int i = count; i < m_count; ++i) {
        m_buttons[static_cast<std::size_t>(i)] = fromLegacy({}, i);
        m_settings->reset(key(i));
    }
    if (!any)
        return;
    m_count = count;
    storeCount();
    emit changed();
}

QString TagButtonsController::problem(const QString &tag, int type) const
{
    if (tag.trimmed().isEmpty())
        return type == 2 ? QCoreApplication::translate("HikariSub.Editor", "Enter the text the button puts in")
                         : QCoreApplication::translate("HikariSub.Editor", "Enter the tag, for example \\blur2");
    if (type == 2)
        return {};
    if (tag.contains(QLatin1Char('{')) || tag.contains(QLatin1Char('}')))
        return QCoreApplication::translate("HikariSub.Editor",
                                           "Leave out the braces: the button puts the tag in an override block");
    // One or more override tags: each a backslash and a letter or digit.
    static const QRegularExpression tags(QStringLiteral("^(\\\\[A-Za-z0-9][^\\\\]*)+$"));
    if (!tags.match(tag).hasMatch())
        return QCoreApplication::translate("HikariSub.Editor", "A tag starts with a backslash, for example \\blur2");
    return {};
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

void TagButtonsController::edit(int index, const QString &name, const QString &tag, int type, const QString &icon)
{
    if (index < 0 || index >= kMaximum)
        return;
    m_buttons[static_cast<std::size_t>(index)] = Button{tag, std::clamp(type, 0, 2), name, knownIcon(icon)};
    // OnEditTag writes this button's option only.
    store(index);
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
    b.icon = table.size() > 3 ? knownIcon(table[3]) : QString(); // LE5
    return b;
}

QString TagButtonsController::toLegacy(const Button &button)
{
    QString text = QStringLiteral("{\n\t") + button.tag + QStringLiteral("\n\t") + QString::number(button.type) +
                   QStringLiteral("\n\t") + button.name;
    if (!button.icon.isEmpty())
        text += QStringLiteral("\n\t") + button.icon; // LE5: only with an icon, so legacy's text stays as it was
    return text + QStringLiteral("\n}");
}

} // namespace hikari::ui
