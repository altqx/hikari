#pragma once

#include "hikari/app/settings_import_store.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace hikari::app {

class SettingsImportController : public QObject {
    Q_OBJECT
    // {path, files} per legacy root found.
    Q_PROPERTY(QVariantList roots READ roots NOTIFY rootsChanged)
    Q_PROPERTY(QString root READ root NOTIFY planChanged)
    // Rows {id, kind, source, key, raw, destination, value, current, disposition, reason,
    // selectable, paths}.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY planChanged)
    Q_PROPERTY(QStringList chosenIds READ chosenIds NOTIFY chosenChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY chosenChanged)
    Q_PROPERTY(bool ambiguousEncoding READ ambiguousEncoding NOTIFY planChanged)
    // 0: none chosen, 1: Latin-1, 2: Windows-1252 (for files that are not UTF-8).
    Q_PROPERTY(int interpretation READ interpretation WRITE setInterpretation NOTIFY planChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool pending READ pending NOTIFY stateChanged)
    Q_PROPERTY(bool canRollBack READ canRollBack NOTIFY stateChanged)
public:
    // A null store (settings in memory only) imports nothing.
    explicit SettingsImportController(SettingsImportStore *store, QObject *parent = nullptr);

    QVariantList roots() const;
    QString root() const { return m_snapshot ? m_snapshot->root : QString(); }
    QVariantList rows() const;
    QStringList chosenIds() const;
    QString summary() const;
    bool ambiguousEncoding() const { return m_plan && m_plan->ambiguousEncoding; }
    int interpretation() const { return m_interpretation; }
    void setInterpretation(int interpretation);
    QString status() const { return m_status; }
    bool available() const { return m_store != nullptr; }
    bool pending() const { return m_store && m_store->pending(); }
    bool canRollBack() const { return m_store && m_store->canRollBack(); }

    Q_INVOKABLE void discover(const QStringList &candidates = {});
    // A path or a file: URL; false when it holds no legacy settings.
    Q_INVOKABLE bool addRoot(const QString &folder);
    Q_INVOKABLE bool choose(const QString &root);
    Q_INVOKABLE void setChosen(const QString &id, bool chosen);
    // which: a disposition ("change", "missing") for its selectable rows, or "proposed".
    Q_INVOKABLE void chooseAll(const QString &which, bool chosen);
    // Takes effect at the next start; false with the reason in status.
    Q_INVOKABLE bool importChosen();
    // What a rollback would replace (changed since the active import).
    Q_INVOKABLE QStringList rollbackEdits() const;
    // What the next start keeps against a waiting import (edited since).
    Q_INVOKABLE QStringList keptEdits() const;
    Q_INVOKABLE bool rollback();

    const application::settings_import::Plan *plan() const { return m_plan ? &*m_plan : nullptr; }
    const std::set<std::string, std::less<>> &chosen() const { return m_chosen; }
    // The settings by id as importing the chosen rows would leave them; nothing is written.
    std::map<std::string, application::SettingValue, std::less<>> chosenValues() const;
    // Imports the chosen rows except those whose setting is in skip. True when staged or nothing
    // is left to import; false keeps the choices (status says why).
    bool importChosenBut(const std::set<std::string, std::less<>> &skip);

signals:
    void rootsChanged();
    void planChanged();
    void chosenChanged();
    void statusChanged();
    void stateChanged();

private:
    void rebuild();
    void propose();
    void setStatus(const QString &status);

    SettingsImportStore *m_store;
    QList<SettingsImportStore::Root> m_roots;
    std::optional<SettingsImportStore::Snapshot> m_snapshot;
    std::optional<application::settings_import::Plan> m_plan;
    std::optional<application::settings_import::Receipt> m_previous;
    QString m_revision;
    std::set<std::string, std::less<>> m_chosen;
    int m_interpretation = 0;
    QString m_status;
};

}
