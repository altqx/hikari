#include "colour_picker_controller.h"

#include "hikari/core/editor_font_colour.h"

#include <QSettings>

namespace hikari::ui {

namespace {

const QString kKey = QStringLiteral("ColorPicker/Recent");

core::legacy::TagColour colourOf(const QVariantMap &m)
{
    return {m.value(QStringLiteral("r")).toInt(), m.value(QStringLiteral("g")).toInt(), m.value(QStringLiteral("b")).toInt(),
            m.value(QStringLiteral("a")).toInt()};
}

QVariantMap mapOf(const core::legacy::TagColour &c)
{
    return {{QStringLiteral("r"), c.r}, {QStringLiteral("g"), c.g}, {QStringLiteral("b"), c.b}, {QStringLiteral("a"), c.a}};
}

} // namespace

ColourPickerController::ColourPickerController(QString settingsFile, QObject *parent)
    : QObject(parent), m_settingsFile(std::move(settingsFile))
{
    QString text;
    if (!m_settingsFile.isEmpty())
        text = QSettings(m_settingsFile, QSettings::IniFormat).value(kKey).toString();
    loadFromString(text);
}

void ColourPickerController::loadFromString(const QString &text)
{
    m_recent.clear();
    for (const QString &token : text.split(QLatin1Char(' '), Qt::SkipEmptyParts))
        if (m_recent.size() < kRecent)
            m_recent.append(mapOf(core::legacy::parseAssColour(token.toStdU16String())));
    while (m_recent.size() < kRecent)
        m_recent.append(mapOf({}));
    emit changed();
}

QString ColourPickerController::storeToString() const
{
    QStringList tokens;
    for (const QVariant &c : m_recent)
        tokens.append(QString::fromStdU16String(core::legacy::assColourText(colourOf(c.toMap()), true, false)));
    return tokens.join(QLatin1Char(' '));
}

void ColourPickerController::addRecent(const QVariantMap &colour)
{
    const auto added = colourOf(colour);
    for (qsizetype i = 0; i < m_recent.size(); ++i) {
        const auto c = colourOf(m_recent[i].toMap());
        if (c == added) {
            m_recent.removeAt(i);
            break;
        }
    }
    m_recent.prepend(mapOf(added));
    while (m_recent.size() > kRecent)
        m_recent.removeLast();
    save();
    emit changed();
}

QString ColourPickerController::assText(const QVariantMap &colour, bool alpha) const
{
    return QString::fromStdU16String(core::legacy::assColourText(colourOf(colour), alpha, false));
}

QVariantMap ColourPickerController::parse(const QString &text) const
{
    return mapOf(core::legacy::parseAssColour(text.trimmed().toStdU16String()));
}

QString ColourPickerController::htmlText(const QVariantMap &colour) const
{
    const auto c = colourOf(colour);
    return QStringLiteral("#%1%2%3")
        .arg(c.r, 2, 16, QLatin1Char('0'))
        .arg(c.g, 2, 16, QLatin1Char('0'))
        .arg(c.b, 2, 16, QLatin1Char('0'))
        .toUpper();
}

void ColourPickerController::save() const
{
    if (m_settingsFile.isEmpty())
        return;
    QSettings(m_settingsFile, QSettings::IniFormat).setValue(kKey, storeToString());
}

} // namespace hikari::ui
