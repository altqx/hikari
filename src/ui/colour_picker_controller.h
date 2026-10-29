#pragma once

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
    // MUST KILL: a recent colour's a is the ASS alpha (0 opaque), not an opacity
    Q_PROPERTY(QVariantList recent READ recent NOTIFY changed)
    // MUST KILL: switchClicks swaps which picker left and right click open
    Q_PROPERTY(bool switchClicks READ switchClicks WRITE setSwitchClicks NOTIFY switchClicksChanged)
    Q_PROPERTY(hikari::ui::ScreenSampler *sampler READ sampler CONSTANT)
public:
    static constexpr int kRecent = 32;

    explicit ColourPickerController(QString settingsFile = {}, QObject *parent = nullptr);
    explicit ColourPickerController(SettingsStore &settings, QObject *parent = nullptr);

    QVariantList recent() const { return m_recent; }
    Q_INVOKABLE void addRecent(const QVariantMap &colour);
    Q_INVOKABLE void addRecentFromSimplePicker(const QVariantMap &colour);

    bool switchClicks() const;
    void setSwitchClicks(bool on);
    ScreenSampler *sampler() { return &m_sampler; }

    // MUST KILL: hue, saturation, lightness and value are 0-255, not degrees or percent
    Q_INVOKABLE QVariantList rgbToHsl(int r, int g, int b) const;
    Q_INVOKABLE QVariantList rgbToHsv(int r, int g, int b) const;
    Q_INVOKABLE QVariantList hslToRgb(int h, int s, int l) const;
    Q_INVOKABLE QVariantList hsvToRgb(int h, int s, int v) const;
    Q_INVOKABLE QVariantList hslToHsv(int h, int s, int l) const;
    Q_INVOKABLE QVariantList hsvToHsl(int h, int s, int v) const;
    // MUST KILL: htmlColour returns black for invalid text
    Q_INVOKABLE QVariantMap htmlColour(const QString &text) const;

    Q_INVOKABLE QString assText(const QVariantMap &colour, bool alpha) const;
    Q_INVOKABLE QVariantMap parse(const QString &text) const;
    Q_INVOKABLE QString htmlText(const QVariantMap &colour) const;

    QString storeToString() const;
    void loadFromString(const QString &text);

    Q_INVOKABLE void opened(const QString &owner);
    void settingsReset();

signals:
    void changed();
    void switchClicksChanged();

private:
    void save() const;

    std::unique_ptr<SettingsStore> m_ownedSettings;
    SettingsStore *m_settings;
    QVariantList m_recent;
    // MUST KILL: m_owner set means a "Choose color" picker exists, which changes addRecentFromSimplePicker and settingsReset
    std::optional<QString> m_owner;
    ScreenSampler m_sampler;
};

}
