#pragma once

#include "hikari/application/shift_times.h"
#include "presets_file.h"
#include "settings_store.h"
#include "tiered_presets.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
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
    // [{name, tier ("project", "mine"), hidden}]: the Project's presets, then the user's.
    Q_PROPERTY(QVariantList shiftPresetRows READ shiftPresetRows NOTIFY presetsChanged)
    Q_PROPERTY(QVariantList postprocessorPresetRows READ postprocessorPresetRows NOTIFY presetsChanged)
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY presetsChanged)
    // The preset the panel's values came from; empty once a value changes.
    Q_PROPERTY(QString shiftPreset READ shiftPreset NOTIFY presetsChanged)
    Q_PROPERTY(QString postprocessorPreset READ postprocessorPreset NOTIFY presetsChanged)
    // The tier of the preset the values came from, "project" or "mine".
    Q_PROPERTY(QString shiftPresetTier READ shiftPresetTier NOTIFY presetsChanged)
    Q_PROPERTY(QString postprocessorPresetTier READ postprocessorPresetTier NOTIFY presetsChanged)
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
    QVariantList shiftPresetRows() const { return m_shiftPresets.rowMaps(); }
    QVariantList postprocessorPresetRows() const { return m_postprocessorPresets.rowMaps(); }
    bool hasProject() const { return !m_projectFile.root().isEmpty(); }
    QString shiftPreset() const { return m_shiftPreset; }
    QString postprocessorPreset() const { return m_postprocessorPreset; }
    QString shiftPresetTier() const { return tierText(m_shiftPresetTier); }
    QString postprocessorPresetTier() const { return tierText(m_postprocessorPresetTier); }
    QString timingPreset() const { return m_timingPreset; }
    void setTimingPreset(const QString &name);

    // The Project folder the presets' Project tier comes from; empty for none.
    void setProjectRoot(const QString &root);

    // `tool`: "shift" or "postprocessor"; `tier`: "project" or "mine", where empty saves to Mine and
    // finds the Project's preset first. Saving over an existing name in the tier replaces it. A user
    // preset the Project hides can be applied, copied and moved, but not renamed or deleted.
    Q_INVOKABLE bool savePreset(const QString &tool, const QString &name, const QString &tier = {});
    Q_INVOKABLE bool applyPreset(const QString &tool, const QString &name, const QString &tier = {});
    Q_INVOKABLE bool renamePreset(const QString &tool, const QString &from, const QString &to, const QString &tier = {});
    Q_INVOKABLE bool deletePreset(const QString &tool, const QString &name, const QString &tier = {});
    // [{name, suggestion}] when the other tier already has `name`, else [].
    Q_INVOKABLE QVariantList presetClashes(const QString &tool, const QString &name, const QString &tier) const;
    // `choices`: [{name, action ("replace", "rename", "skip"), newName}]. Returns the name it landed
    // under, "" when skipped.
    Q_INVOKABLE QString copyPreset(const QString &tool, const QString &name, const QString &tier,
                                   const QVariantList &choices = {});
    Q_INVOKABLE QString movePreset(const QString &tool, const QString &name, const QString &tier,
                                   const QVariantList &choices = {});

signals:
    void changed();
    void presetsChanged();

private:
    static QStringList names(const TieredPresets &presets);
    static QList<NamedPreset> readPresets(const QStringList &stored);
    static QStringList writePresets(const QList<NamedPreset> &presets);
    TieredPresets *presetsOf(const QString &tool);
    QString *currentOf(const QString &tool);
    application::Tier *currentTierOf(const QString &tool);
    std::optional<application::Tier> tierFor(const QString &tool, const QString &name, const QString &tier) const;
    QString sendPreset(const QString &tool, const QString &name, const QString &tier, const QVariantList &choices,
                       bool move);
    void loadProjectPresets();
    QJsonObject valuesOf(const QString &tool) const;
    void load();
    void assign(const QVariantMap &map);
    void save() const;
    void savePresets();

    std::unique_ptr<SettingsStore> m_ownedStore;
    SettingsStore *m_store;
    application::ShiftTimesSettings m_settings;
    int m_ppWhichLines = 0;
    QString m_ppStyles;
    PresetsFile m_projectFile;
    TieredPresets m_shiftPresets;
    TieredPresets m_postprocessorPresets;
    QString m_shiftPreset;
    QString m_postprocessorPreset;
    application::Tier m_shiftPresetTier = application::Tier::Mine;
    application::Tier m_postprocessorPresetTier = application::Tier::Mine;
    QString m_timingPreset;
};

}
