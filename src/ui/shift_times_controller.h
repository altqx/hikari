#pragma once

#include "hikari/application/shift_times.h"
#include "settings_store.h"

#include <QJsonObject>
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
    // "postprocessor" holds the rule bits: 1 lead-in, 2 lead-out, 4 close small gaps, 8 snap to keyframes.
    // "ppWhichLines" and "ppStyles" are the postprocessor's own Lines choice.
    Q_PROPERTY(QVariantMap settings READ settingsMap WRITE setSettingsMap NOTIFY changed)
    Q_PROPERTY(QStringList shiftPresets READ shiftPresetNames NOTIFY presetsChanged)
    Q_PROPERTY(QStringList postprocessorPresets READ postprocessorPresetNames NOTIFY presetsChanged)
    // The preset the panel's values came from; empty once a value changes.
    Q_PROPERTY(QString shiftPreset READ shiftPreset NOTIFY presetsChanged)
    Q_PROPERTY(QString postprocessorPreset READ postprocessorPreset NOTIFY presetsChanged)
    // The postprocessor preset the Timing layout applies; empty: none.
    Q_PROPERTY(QString timingPreset READ timingPreset WRITE setTimingPreset NOTIFY presetsChanged)
public:
    enum class Tool { Shift, Postprocessor };

    explicit ShiftTimesController(QString settingsFile = {}, QObject *parent = nullptr);
    explicit ShiftTimesController(SettingsStore &settings, QObject *parent = nullptr);

    const application::ShiftTimesSettings &settings() const { return m_settings; }
    // The values a run of `tool` passes to application::shiftTimes.
    application::ShiftTimesSettings runSettings(Tool tool) const;
    QVariantMap settingsMap() const;
    void setSettingsMap(const QVariantMap &map);

    QStringList shiftPresetNames() const { return names(m_shiftPresets); }
    QStringList postprocessorPresetNames() const { return names(m_postprocessorPresets); }
    QString shiftPreset() const { return m_shiftPreset; }
    QString postprocessorPreset() const { return m_postprocessorPreset; }
    QString timingPreset() const { return m_timingPreset; }
    void setTimingPreset(const QString &name);

    // `tool`: "shift" or "postprocessor". Saving over an existing name replaces it.
    Q_INVOKABLE bool savePreset(const QString &tool, const QString &name);
    Q_INVOKABLE bool applyPreset(const QString &tool, const QString &name);
    Q_INVOKABLE bool renamePreset(const QString &tool, const QString &from, const QString &to);
    Q_INVOKABLE bool deletePreset(const QString &tool, const QString &name);

signals:
    void changed();
    void presetsChanged();

private:
    struct Preset {
        QString name;
        QJsonObject values;
    };
    static QStringList names(const QList<Preset> &presets);
    static QList<Preset> readPresets(const QStringList &stored);
    static QStringList writePresets(const QList<Preset> &presets);
    QList<Preset> *presetsOf(const QString &tool);
    QString *currentOf(const QString &tool);
    QJsonObject valuesOf(const QString &tool) const;
    void load();
    void assign(const QVariantMap &map);
    void save() const;
    void savePresets() const;

    std::unique_ptr<SettingsStore> m_ownedStore;
    SettingsStore *m_store;
    application::ShiftTimesSettings m_settings;
    int m_ppWhichLines = 0;
    QString m_ppStyles;
    QList<Preset> m_shiftPresets;
    QList<Preset> m_postprocessorPresets;
    QString m_shiftPreset;
    QString m_postprocessorPreset;
    QString m_timingPreset;
};

}
