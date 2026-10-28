#pragma once

// CR1 (#452): what a crash, freeze or issue report holds, built on this
// machine and shown in full before anything leaves it.
// * Scrubber: takes out of a Log line what could name the user's files or
//   hold their subtitles: a path, a file name (fileN.ext in order of first
//   use, the extension kept), a Line's text and any quoted text (‹text›, or
//   ‹font› after "font"), and any other folder (‹folder›).
// * Report: the crash or freeze record, the system, the settings as ids and
//   values from a closed list, and the Log's last lines (scrubbed when they
//   were written); as the text the window shows, as PostHog's events and as
//   the GitHub issue's fields. A note the user types goes as typed.
// * Issue: Help › Report an issue's draft, as a prefilled new-issue link
//   that matches .github/ISSUE_TEMPLATE, or the Markdown when the link would
//   be too long.

#include "hikari/app/crash_record.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <optional>

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app::report {

class Scrubber {
public:
    // The names of the files open now (documents, video, audio, keyframes)
    // and the texts of their Lines; asked for at each scrub().
    void setKnownNames(std::function<QStringList()> names) { m_names = std::move(names); }
    void setKnownTexts(std::function<QStringList()> texts) { m_texts = std::move(texts); }

    QString scrub(const QString &text);

private:
    QString placeholder(const QString &fileName);

    std::function<QStringList()> m_names;
    std::function<QStringList()> m_texts;
    QHash<QString, QString> m_files; // file name, case folded => fileN.ext
};

// This system, as app_started describes it (ids only).
struct System {
    QString version;
    QString channel;  // stable, prerelease
    QString package;  // installer, portable, appimage, deb, rpm, aur, build_tree
    QString os;       // windows, linux, other
    QString osVersion;
    QString arch;
    QString qtVersion;
    int screens = 1;
    QString scale;    // Analytics::scaleBucket
    QString layout;   // a preset's id, "saved" or "custom"

    static System current(const QString &version, const QString &package, int screens, qreal scale,
                          const QString &layout);
};

struct Setting {
    QString id;
    QString value;
};
// The settings a report names: ids and values from a closed list (a value
// that is not one becomes "custom"), never a folder, a font or a name.
QList<Setting> settingsSummary(const ui::SettingsStore &settings);

// The Log's last lines a report holds.
constexpr int kLogLines = 200;

struct Report {
    QString id;                         // a UUID, shared by its events
    std::optional<crash::Record> record; // none: an issue report
    QDateTime when;                     // when it crashed or froze
    System system;
    QList<Setting> settings;
    QStringList log;                    // scrubbed, oldest first
    bool exceptionSent = false;         // usage data sent its $exception already
    bool recovered = false;             // a freeze the window came back from

    QString kind() const; // crash, freeze or issue
    // What the window shows and what goes: the note first when there is one.
    QString text(const QString &note) const;
    // The crash record's own section, the system's and the settings' (text).
    QString recordText() const;
    QString systemText() const;
    QString settingsText() const;
    QString logText() const;

    // PostHog: $exception (the record, as AN1 sends it) and crash_report.
    QJsonObject exceptionProperties() const;
    QJsonObject reportProperties(const QString &note) const;

    // The report kept on disk until it is sent or dismissed.
    QJsonObject toJson() const;
    static std::optional<Report> fromJson(const QJsonObject &json);
};

// Help › Report an issue, and a crash or freeze report taken to GitHub.
struct Issue {
    enum Kind { Bug, Feature, Question };
    Kind kind = Bug;
    QString title;
    // The form's fields by id (bug.yml: what-happened, steps, expected,
    // crash-report, system, log; feature.yml: wanted, why, system;
    // question.yml: question, system).
    QList<std::pair<QString, QString>> fields;

    static QString templateFile(Kind kind);
    // The fields under their headings, as Copy writes them.
    QString markdown() const;
    // The prefilled new-issue link; when it would be longer than
    // `maxLength`, the template's form with the title only (the rest goes to
    // the clipboard).
    struct Link {
        QUrl url;
        bool complete = true;
    };
    Link link(int maxLength = kMaxLinkLength) const;
    static constexpr int kMaxLinkLength = 8000;
    static QUrl newIssueBase();

    // A crash or freeze report as a Bug issue.
    static Issue fromReport(const Report &report, const QString &note);
};

} // namespace hikari::app::report
