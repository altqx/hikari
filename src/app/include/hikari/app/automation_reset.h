#pragma once

#include <QDate>
#include <QString>
#include <QStringList>

#include <span>
#include <vector>

namespace hikari::app {

// The release that ships the native karaoke templater (TH5 #833).
inline constexpr const char *kAutomationResetVersion = "0.1.0-beta.4";

bool automationResetDue(bool enabled, const QString &version);

struct AutomationReset {
    enum class Outcome { None, Cleared, Moved, Failed };
    Outcome outcome = Outcome::None;
    // Moved: the backup folder. Failed: the one the move was going to.
    QString backup;
    // Moved: the entries, relative to the backup folder ("automation/autoload", "config").
    QStringList moved;
    // Failed: why, in the system's words.
    QString reason;
};

// Once per user folder (recorded in `.automation-reset`): moves <root>/automation and <root>/config
// into automation-backup-<yyyy-MM-dd> (then -2, -3…) beside them and leaves an empty automation
// folder. A folder holding only seeded files still as bundled loses them instead, with no backup.
// A failed move changes nothing and is not recorded. Runs before UserFolder::seed.
AutomationReset resetAutomationFolder(const QString &root, const QDate &today);

struct BackupScript {
    QString nameSpace;
    QString name;
    QString feed;
};

struct NativeScript {
    const char *nameSpace;
    // A hotkey command symbol.
    const char *command;
};

// Scripts a native command fully replaces.
std::span<const NativeScript> nativeScripts();

struct ReinstallPlan {
    std::vector<BackupScript> queued;
    std::vector<NativeScript> builtIn;
};

// From a backed-up l0.DependencyControl.json: the managed automation scripts, less those
// `bundled` (namespaces the shipped Autoload holds) and those natively built in.
ReinstallPlan reinstallPlan(const QString &configFile, const QStringList &bundled);

}
