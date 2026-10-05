#pragma once

// O3: the "Import legacy settings" window's state. Lists the legacy roots
// found (or one the user names), snapshots the chosen one, shows the review
// plan (source, value, destination, current value, disposition and why),
// lets the user choose which changes to import, activates the import for
// the next start and rolls back to the previous generation
// (docs/qt/proposals/settings-import.md; SettingsImportStore).

#include "hikari/app/settings_import_store.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

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
    // {id, kind, source, key, raw, destination, value, current, disposition,
    //  reason, selectable, chosen, paths}
    Q_PROPERTY(QVariantList rows READ rows NOTIFY planChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY planChanged)
    Q_PROPERTY(bool ambiguousEncoding READ ambiguousEncoding NOTIFY planChanged)
    // 0: none chosen, 1: Latin-1, 2: Windows-1252 (for files that are not UTF-8).
    Q_PROPERTY(int interpretation READ interpretation WRITE setInterpretation NOTIFY planChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool pending READ pending NOTIFY stateChanged)
    Q_PROPERTY(bool canRollBack READ canRollBack NOTIFY stateChanged)
public:
    // No store (settings kept in memory only): nothing can be imported.
    explicit SettingsImportController(SettingsImportStore *store, QObject *parent = nullptr);

    QVariantList roots() const;
    QString root() const { return m_snapshot ? m_snapshot->root : QString(); }
    QVariantList rows() const;
    QString summary() const;
    bool ambiguousEncoding() const { return m_plan && m_plan->ambiguousEncoding; }
    int interpretation() const { return m_interpretation; }
    void setInterpretation(int interpretation);
    QString status() const { return m_status; }
    bool available() const { return m_store != nullptr; }
    bool pending() const { return m_store && m_store->pending(); }
    bool canRollBack() const { return m_store && m_store->canRollBack(); }

    // Lists the legacy roots beside the program (and the usual places).
    Q_INVOKABLE void discover(const QStringList &candidates = {});
    // A folder the user names (a path or a file: URL); false when it holds no
    // legacy settings.
    Q_INVOKABLE bool addRoot(const QString &folder);
    // Snapshots the root and builds its plan.
    Q_INVOKABLE bool choose(const QString &root);
    Q_INVOKABLE void setChosen(const QString &id, bool chosen);
    // Every selectable row of `disposition` ("change", "missing"), or the
    // proposal again ("proposed").
    Q_INVOKABLE void chooseAll(const QString &which, bool chosen);
    // Stages and activates the chosen rows; the import takes effect at the
    // next start. Returns false and says why in `status` otherwise.
    Q_INVOKABLE bool importChosen();
    // What a rollback would replace (changed since the active import).
    Q_INVOKABLE QStringList rollbackEdits() const;
    Q_INVOKABLE bool rollback();

    const application::settings_import::Plan *plan() const { return m_plan ? &*m_plan : nullptr; }
    const std::set<std::string, std::less<>> &chosen() const { return m_chosen; }

signals:
    void rootsChanged();
    void planChanged();
    void statusChanged();
    void stateChanged();

private:
    void rebuild();
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

} // namespace hikari::app
