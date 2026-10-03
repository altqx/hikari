#pragma once

// E1: the colour picker's recent colours and colour text (legacy
// ColorPickerRecent and AssColor at 20d647c4). Recent colours are an 8x4
// grid, newest first, padded with black, kept in the legacy option text
// (COLORPICKER_RECENT_COLORS: "&HAABBGGRR&" tokens separated by spaces) in
// the INI file the application names until the settings registry owns it.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class ColourPickerController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    // 32 colours {r, g, b, a}, a the ASS alpha.
    Q_PROPERTY(QVariantList recent READ recent NOTIFY changed)
public:
    static constexpr int kRecent = 32;

    explicit ColourPickerController(QString settingsFile = {}, QObject *parent = nullptr);

    QVariantList recent() const { return m_recent; }
    // Legacy AddColor after OK: moved (or added) to the front; an equal
    // colour (alpha included) is not kept twice.
    Q_INVOKABLE void addRecent(const QVariantMap &colour);
    // AssColor::GetAss(alpha, false) ("&H[AA]BBGGRR&") and SetAss.
    Q_INVOKABLE QString assText(const QVariantMap &colour, bool alpha) const;
    Q_INVOKABLE QVariantMap parse(const QString &text) const;
    // The colour as #RRGGBB (legacy GetHex without alpha).
    Q_INVOKABLE QString htmlText(const QVariantMap &colour) const;

    // The option text, for a settings import.
    QString storeToString() const;
    void loadFromString(const QString &text);

signals:
    void changed();

private:
    void save() const;

    QString m_settingsFile;
    QVariantList m_recent;
};

} // namespace hikari::ui
