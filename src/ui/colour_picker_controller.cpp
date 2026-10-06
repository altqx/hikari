#include "colour_picker_controller.h"

#include "hikari/core/colour_space.h"
#include "hikari/core/editor_font_colour.h"

namespace hikari::ui {

namespace {

constexpr const char *kKey = "colourPicker.recentColours";
constexpr const char *kSwitchClicks = "colourPicker.switchClicks";

core::legacy::TagColour colourOf(const QVariantMap &m)
{
    return {m.value(QStringLiteral("r")).toInt(), m.value(QStringLiteral("g")).toInt(), m.value(QStringLiteral("b")).toInt(),
            m.value(QStringLiteral("a")).toInt()};
}

QVariantMap mapOf(const core::legacy::TagColour &c)
{
    return {{QStringLiteral("r"), c.r}, {QStringLiteral("g"), c.g}, {QStringLiteral("b"), c.b}, {QStringLiteral("a"), c.a}};
}

QVariantList listOf(const core::legacy::Channels &c)
{
    return {c.a, c.b, c.c};
}

} // namespace

ColourPickerController::ColourPickerController(QString settingsFile, QObject *parent)
    : QObject(parent), m_ownedSettings(std::make_unique<SettingsStore>(std::move(settingsFile))),
      m_settings(m_ownedSettings.get())
{
    loadFromString(m_settings->text(kKey));
    connect(m_settings, &SettingsStore::changed, this, [this](const QString &id) {
        if (id == QLatin1String(kSwitchClicks))
            emit switchClicksChanged();
    });
}

ColourPickerController::ColourPickerController(SettingsStore &settings, QObject *parent)
    : QObject(parent), m_settings(&settings)
{
    loadFromString(m_settings->text(kKey));
    connect(m_settings, &SettingsStore::changed, this, [this](const QString &id) {
        if (id == QLatin1String(kSwitchClicks))
            emit switchClicksChanged();
    });
}

bool ColourPickerController::switchClicks() const
{
    return m_settings->boolean(kSwitchClicks);
}

void ColourPickerController::setSwitchClicks(bool on)
{
    // Options.SetBool at each click of the box; the store's change signal
    // notifies.
    m_settings->set(kSwitchClicks, on);
}

void ColourPickerController::addRecentFromSimplePicker(const QVariantMap &colour)
{
    if (m_owner) {
        addRecent(colour);
        return;
    }
    // Y7-recent-option-text (approved departure from ColorPicker.cpp:755-766,
    // which edited the text in place and left double spaces that cost a full
    // option its last colour): the option is read as its colours, the colour
    // moves to the front and the 32 newest are written one space apart.
    const QString stringColor = assText(colour, true);
    QStringList colours{stringColor};
    for (const QString &token : m_settings->text(kKey).split(QLatin1Char(' '), Qt::SkipEmptyParts))
        if (token.compare(stringColor, Qt::CaseInsensitive) != 0 && colours.size() < kRecent)
            colours.append(token);
    const QString recentString = colours.join(QLatin1Char(' '));
    m_settings->set(kKey, recentString);
    // Without a picker the list is the option (the next one reads it).
    loadFromString(recentString);
}

QVariantList ColourPickerController::rgbToHsl(int r, int g, int b) const
{
    return listOf(core::legacy::rgbToHsl(r, g, b));
}

QVariantList ColourPickerController::rgbToHsv(int r, int g, int b) const
{
    return listOf(core::legacy::rgbToHsv(r, g, b));
}

QVariantList ColourPickerController::hslToRgb(int h, int s, int l) const
{
    return listOf(core::legacy::hslToRgb(h, s, l));
}

QVariantList ColourPickerController::hsvToRgb(int h, int s, int v) const
{
    return listOf(core::legacy::hsvToRgb(h, s, v));
}

QVariantList ColourPickerController::hslToHsv(int h, int s, int l) const
{
    return listOf(core::legacy::hslToHsv(h, s, l));
}

QVariantList ColourPickerController::hsvToHsl(int h, int s, int v) const
{
    return listOf(core::legacy::hsvToHsl(h, s, v));
}

QVariantMap ColourPickerController::htmlColour(const QString &text) const
{
    const auto c = core::legacy::htmlToColour(text.toStdU16String());
    return {{QStringLiteral("r"), c.a}, {QStringLiteral("g"), c.b}, {QStringLiteral("b"), c.c}};
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

void ColourPickerController::opened(const QString &owner)
{
    // DialogColorPicker::Get: a picker kept for another window is destroyed,
    // and the new one reads COLORPICKER_RECENT_COLORS.
    if (!m_owner || *m_owner != owner)
        loadFromString(m_settings->text(kKey));
    m_owner = owner;
}

void ColourPickerController::settingsReset()
{
    // Without a DialogColorPicker the option is the list (AddRecent edits
    // it); a created picker keeps its recent_box.
    if (!m_owner)
        loadFromString(m_settings->text(kKey));
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
    // color_to_html (colorspace.cpp:370-373)
    const auto c = colourOf(colour);
    return QString::fromStdU16String(core::legacy::colourToHtml(c.r, c.g, c.b));
}

void ColourPickerController::save() const
{
    m_settings->set(kKey, storeToString());
}

} // namespace hikari::ui
