#pragma once

#include "hikari/application/misspell_replacer.h"
#include "hikari/application/settings_import.h"

#include <QString>
#include <QStringList>

#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

// Bytes that do not decode give the shipped rules.
application::ReplacerRules readLegacyRules(const QByteArray &bytes);
// nullopt when the file cannot be read.
std::optional<application::ReplacerRules> readLegacyRulesFile(const std::filesystem::path &path);
std::string legacyRulesToml(const QByteArray &bytes);

// An activated import takes effect at the next start (recover()); values edited since staging
// are kept over it.
class SettingsImportStore {
public:
    SettingsImportStore(ui::SettingsStore &settings, QString folder, QString profileName = QStringLiteral("default"));
    // Empty `folder`: the settings folder's Dictionary; empty `bundled`: the program folder's.
    void setDictionaryFolder(QString folder, QString bundled = {})
    {
        m_dictionaryFolder = std::move(folder);
        m_bundledDictionaryFolder = std::move(bundled);
    }

    struct Root {
        QString path;
        QStringList files;  // relative, e.g. "Config/Config.txt"
        QStringList themes; // Themes/*.txt, never imported
    };
    // nullopt unless the folder holds a known source.
    static std::optional<Root> inspectRoot(const QString &path);
    // Each root once, in the order given.
    static QList<Root> discoverRoots(const QStringList &candidates);
    static QStringList defaultCandidates();

    struct Snapshot {
        QString id;
        QString dir;
        QString root;
        std::vector<application::settings_import::SourceFile> sources;
        QStringList themeFiles;
    };
    // The same bytes again reuse the snapshot. nullopt with `error` when a file cannot be read.
    std::optional<Snapshot> snapshot(const Root &root, QString *error = nullptr);
    static bool sourcesUnchanged(const Snapshot &snapshot);

    // The pending generation's profile when one waits for the next start, else the live one.
    application::settings_import::Destination destination() const;
    application::settings_import::Profile liveProfile() const;
    QString revision() const;
    std::optional<application::settings_import::Receipt> activeReceipt() const;

    int activeGeneration() const;   // 0: none
    int previousGeneration() const; // 0: none
    bool pending() const;           // waiting for the next start
    bool canRollBack() const { return previousGeneration() > 0; }
    QStringList incompleteStaging() const;

    enum class Result {
        Activated,
        NoChange,          // nothing staged
        StaleSources,
        StaleDestination,
        StagingFailed,     // the previous generation stays active
        NothingToRollBack,
    };
    Result activate(const application::settings_import::Plan &plan,
                    const std::set<std::string, std::less<>> &chosen, const Snapshot &snapshot,
                    const QString &planRevision);

    // Setting ids and file paths changed since the active generation.
    QStringList editsSinceActivation() const;
    // Setting ids and file paths the waiting import changes that were edited since staging.
    QStringList editsKeptOverImport() const;
    Result rollback();

    // Call at start before the profile is read. Returns whether it changed anything.
    bool recover();

    std::function<std::string(std::string_view alias)> macroProblem;
    std::function<bool(std::string_view name)> rendererInstalled;

    // Tests: returning true fails that step.
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
    QString diskPath(const QString &path) const;
    QStringList seededDictionaries() const;

    QString m_folder;
    QString m_dictionaryFolder;
    QString m_bundledDictionaryFolder;
    QString m_profileName;
};

}
