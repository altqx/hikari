#pragma once

// F5: the shift-times panel's settings and profiles (legacy SHIFT_TIMES_*,
// POSTPROCESSOR_* and SHIFT_TIMES_PROFILES), kept in the settings registry;
// the six switches are the bits of shiftTimes.options as legacy packs them.

#include "hikari/application/shift_times.h"
#include "settings_store.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace hikari::ui {

class ShiftTimesController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    // {forward, byFrames, timeMs, frames, fromStartTime, moveToVideoTime,
    //  moveToAudioTime, tagTimes, whichLines, whichTimes, correctEndTimes, styles,
    //  postprocessor, leadIn, leadOut, thresholdStart, thresholdEnd,
    //  keyframeBeforeStart, keyframeAfterStart, keyframeBeforeEnd, keyframeAfterEnd}
    Q_PROPERTY(QVariantMap settings READ settingsMap WRITE setSettingsMap NOTIFY changed)
    Q_PROPERTY(QStringList profiles READ profileNames NOTIFY changed)
public:
    explicit ShiftTimesController(QString settingsFile = {}, QObject *parent = nullptr);
    explicit ShiftTimesController(SettingsStore &settings, QObject *parent = nullptr);

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
    void load();
    void assign(const QVariantMap &map);
    void save() const;

    std::unique_ptr<SettingsStore> m_ownedStore;
    SettingsStore *m_store;
    application::ShiftTimesSettings m_settings;
    QStringList m_profiles; // legacy profile lines
};

} // namespace hikari::ui
