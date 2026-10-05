#pragma once

// O3: the importer's transaction over the settings folder (S44-import,
// docs/qt/proposals/settings-import.md "Import transaction"): legacy roots
// listed, never combined; the chosen root's files copied to a preserved
// snapshot with their hashes and encoding evidence; staged generations of
// the complete destination profile (registry values and the authored
// collections beside hikari.ini) under <settings folder>/Import; activation
// through one manifest switch; rollback to the previous complete generation.
//
// A generation activates at the next start (recover(), before anything
// reads the profile): settings are read once by many windows, so a running
// session keeps what it has. The import's own changes win over the live
// profile, values edited after it was staged are kept; a rollback restores
// the previous generation whole and reports what it replaces. Legacy files
// are only read.
//
// Layout under Import/:
//   manifest.json                {active, previous, pending, rollback}: the switch
//   snapshots/<id>/<path>        the legacy bytes as read, and snapshot.json
//   generations/<n>/profile.json the profile, its base and the receipt
//   generations/<n>/complete     written last when staging succeeded
//   live                         the generation the live profile holds

#include "hikari/application/settings_import.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

class SettingsImportStore {
public:
    // `settings` is the live registry; `folder` the settings folder (beside
    // hikari.ini: Rules.txt, Dictionary/).
    SettingsImportStore(ui::SettingsStore &settings, QString folder, QString profileName = QStringLiteral("default"));

    // ---- Discovery

    struct Root {
        QString path;
        QStringList files;  // known sources found, relative ("Config/Config.txt")
        QStringList themes; // Themes/*.txt (excluded, named in the plan)
    };
    // A legacy installation's folder (legacy pathfull, the executable's): a
    // root when it holds any known source. Folders are never combined.
    static std::optional<Root> inspectRoot(const QString &path);
    // The candidates that are roots, each once, in the order given.
    static QList<Root> discoverRoots(const QStringList &candidates);
    // Executable-adjacent places to look: the program folder and, on
    // Windows, the usual installation folders.
    static QStringList defaultCandidates();

    // ---- Snapshot

    struct Snapshot {
        QString id; // over the sources' paths and hashes
        QString dir;
        QString root;
        std::vector<application::settings_import::SourceFile> sources;
        QStringList themeFiles;
    };
    // Copies the root's sources (bytes as read) into a snapshot; the same
    // bytes again reuse it. nullopt with `error` when a file cannot be read.
    std::optional<Snapshot> snapshot(const Root &root, QString *error = nullptr);
    // Whether the root's files still have the snapshot's hashes.
    static bool sourcesUnchanged(const Snapshot &snapshot);

    // ---- The destination

    // What a plan compares with: the pending generation's profile when one
    // waits for the next start, else the live one.
    application::settings_import::Destination destination() const;
    application::settings_import::Profile liveProfile() const;
    // The destination's revision (a hash of its profile).
    QString revision() const;
    std::optional<application::settings_import::Receipt> activeReceipt() const;

    // ---- Generations

    int activeGeneration() const;   // 0: none
    int previousGeneration() const; // 0: none
    bool pending() const;           // activated, waiting for the next start
    bool canRollBack() const { return previousGeneration() > 0; }
    // Staging directories left without their "complete" marker.
    QStringList incompleteStaging() const;

    enum class Result {
        Activated,
        NoChange,          // the same accepted plan again: nothing staged
        StaleSources,      // the legacy files changed since the snapshot
        StaleDestination,  // the destination changed since the plan
        StagingFailed,     // before the switch: the previous generation stays active
        NothingToRollBack,
    };
    Result activate(const application::settings_import::Plan &plan,
                    const std::set<std::string, std::less<>> &chosen, const Snapshot &snapshot,
                    const QString &planRevision);

    // What a rollback would replace: settings and files changed since the
    // active generation (setting ids, file paths).
    QStringList editsSinceActivation() const;
    Result rollback();

    // At start, before the profile is read: brings the live profile to the
    // active generation when it does not hold it yet. Returns whether it
    // changed anything.
    bool recover();

    // An imported macro binding's problem with the scripts loaded now
    // (Destination::macroProblem).
    std::function<std::string(std::string_view alias)> macroProblem;

    // Tests: a step that fails (returning true makes it fail).
    enum class Step { Backup, Profile, Files, Complete, Manifest };
    std::function<bool(Step)> failAt;

    QString importDir() const { return m_folder + QStringLiteral("/Import"); }

private:
    struct Manifest {
        int active = 0;
        int previous = 0;
        bool pending = false;
        bool rollback = false;
    };
    struct Generation {
        application::settings_import::Profile profile;
        application::settings_import::Profile base;
        std::optional<application::settings_import::Receipt> receipt;
    };
    Manifest readManifest() const;
    bool writeManifest(const Manifest &manifest);
    std::optional<Generation> readGeneration(int number) const;
    bool stage(int number, const Generation &generation);
    int nextGenerationNumber() const;
    void materialize(const application::settings_import::Profile &profile);
    int liveGeneration() const;
    void setLiveGeneration(int number);
    application::settings_import::Destination destinationOf(const application::settings_import::Profile &profile) const;

    ui::SettingsStore &m_settings;
    QString m_folder;
    QString m_profileName;
};

} // namespace hikari::app
