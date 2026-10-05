#pragma once

// E1: the colour picker's recent colours and colour text (legacy
// ColorPickerRecent and AssColor at 20d647c4). Recent colours are an 8x4
// grid, newest first, padded with black, kept in the legacy option text
// (COLORPICKER_RECENT_COLORS: "&HAABBGGRR&" tokens separated by spaces),
// the registry's colourPicker.recentColours.
//
// Y7: the picker's colour spaces (legacy colorspace.cpp), the "swap
// shortcuts" option (COLORPICKER_SWITCH_CLICKS, colourPicker.switchClicks),
// the simple picker's recent colour (DialogColorPicker::AddRecent) and the
// screen dropper's sampler.

#include "screen_sampler.h"
#include "settings_store.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>

namespace hikari::ui {

class ColourPickerController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    // 32 colours {r, g, b, a}, a the ASS alpha.
    Q_PROPERTY(QVariantList recent READ recent NOTIFY changed)
    // Legacy SwitchClicks (ColorPicker.cpp:563-570): left click opens the
    // simple "Color picker", right click "Choose color" (EditBox.cpp:865-866).
    Q_PROPERTY(bool switchClicks READ switchClicks WRITE setSwitchClicks NOTIFY switchClicksChanged)
    Q_PROPERTY(hikari::ui::ScreenSampler *sampler READ sampler CONSTANT)
public:
    static constexpr int kRecent = 32;

    explicit ColourPickerController(QString settingsFile = {}, QObject *parent = nullptr);
    explicit ColourPickerController(SettingsStore &settings, QObject *parent = nullptr);

    QVariantList recent() const { return m_recent; }
    // Legacy AddColor after OK: moved (or added) to the front; an equal
    // colour (alpha included) is not kept twice.
    Q_INVOKABLE void addRecent(const QVariantMap &colour);
    // The simple picker's DialogColorPicker::AddRecent (ColorPicker.cpp:748-767):
    // with a "Choose color" picker created (opened) the colour goes into its
    // list as addRecent does; without one the option text is rewritten
    // (Y7-recent-option-text): its colours read, every one equal to it
    // dropped, the colour put first and the 32 newest one space apart.
    Q_INVOKABLE void addRecentFromSimplePicker(const QVariantMap &colour);

    bool switchClicks() const;
    void setSwitchClicks(bool on);
    ScreenSampler *sampler() { return &m_sampler; }

    // colorspace.cpp: [h, s, l], [h, s, v] or [r, g, b], 0-255 each.
    Q_INVOKABLE QVariantList rgbToHsl(int r, int g, int b) const;
    Q_INVOKABLE QVariantList rgbToHsv(int r, int g, int b) const;
    Q_INVOKABLE QVariantList hslToRgb(int h, int s, int l) const;
    Q_INVOKABLE QVariantList hsvToRgb(int h, int s, int v) const;
    Q_INVOKABLE QVariantList hslToHsv(int h, int s, int l) const;
    Q_INVOKABLE QVariantList hsvToHsl(int h, int s, int v) const;
    // html_to_color: {r, g, b} (black for anything but 3 or 6 hex digits).
    Q_INVOKABLE QVariantMap htmlColour(const QString &text) const;

    // AssColor::GetAss(alpha, false) ("&H[AA]BBGGRR&") and SetAss.
    Q_INVOKABLE QString assText(const QVariantMap &colour, bool alpha) const;
    Q_INVOKABLE QVariantMap parse(const QString &text) const;
    // The colour as #RRGGBB (legacy GetHex without alpha).
    Q_INVOKABLE QString htmlText(const QVariantMap &colour) const;

    // The option text, for a settings import.
    QString storeToString() const;
    void loadFromString(const QString &text);

    // The picker opens for `owner` (legacy DialogColorPicker::Get with the
    // window it belongs to: the tab's Line editor). Legacy creates the picker
    // once and keeps it while it is opened for the same window; opened for
    // another one, it is created again from the option.
    void opened(const QString &owner);
    // After "Set default": the list takes the reset option unless a picker
    // exists, which keeps its recent colours and writes them at its next
    // colour.
    void settingsReset();

signals:
    void changed();
    void switchClicksChanged();

private:
    void save() const;

    std::unique_ptr<SettingsStore> m_ownedSettings;
    SettingsStore *m_settings;
    QVariantList m_recent;
    std::optional<QString> m_owner; // the window the legacy picker was created for
    ScreenSampler m_sampler;
};

} // namespace hikari::ui
