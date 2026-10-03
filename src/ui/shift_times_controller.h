#pragma once

// F5: the shift-times panel's settings and profiles (legacy SHIFT_TIMES_*
// options and SHIFT_TIMES_PROFILES), kept in the INI file the application
// names until the settings registry owns them.

#include "hikari/application/shift_times.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class ShiftTimesController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    // {forward, byFrames, timeMs, frames, fromStartTime, moveToVideoTime,
    //  moveToAudioTime, tagTimes, whichLines, whichTimes, correctEndTimes, styles}
    Q_PROPERTY(QVariantMap settings READ settingsMap WRITE setSettingsMap NOTIFY changed)
    Q_PROPERTY(QStringList profiles READ profileNames NOTIFY changed)
public:
    explicit ShiftTimesController(QString settingsFile = {}, QObject *parent = nullptr);

    const application::ShiftTimesSettings &settings() const { return m_settings; }
    QVariantMap settingsMap() const;
    void setSettingsMap(const QVariantMap &map);
    QStringList profileNames() const;
    // Legacy CreateProfile: the current settings under `name`, first in the list.
    Q_INVOKABLE void saveProfile(const QString &name);
    Q_INVOKABLE void loadProfile(const QString &name);
    Q_INVOKABLE void removeProfile(const QString &name);

signals:
    void changed();

private:
    void save() const;

    QString m_settingsFile;
    application::ShiftTimesSettings m_settings;
    QStringList m_profiles; // legacy profile lines
};

} // namespace hikari::ui
