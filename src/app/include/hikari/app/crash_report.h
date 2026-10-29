#pragma once

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
    // Called at each scrub(): the open files' names and their Lines' texts.
    void setKnownNames(std::function<QStringList()> names) { m_names = std::move(names); }
    void setKnownTexts(std::function<QStringList()> texts) { m_texts = std::move(texts); }

    QString scrub(const QString &text);

private:
    QString placeholder(const QString &fileName);

    std::function<QStringList()> m_names;
    std::function<QStringList()> m_texts;
    QHash<QString, QString> m_files;
};

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
// Ids and values from a closed list; any other value is "custom".
QList<Setting> settingsSummary(const ui::SettingsStore &settings);

constexpr int kLogLines = 200;
constexpr int kLogChars = 64 * 1024;
constexpr int kLogLineChars = 1000;
constexpr int kNoteChars = 4000;
// The last kLogLines of lines, within kLogChars in all, each cut at kLogLineChars.
QStringList boundLog(QStringList lines);

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
    QString text(const QString &note) const;
    QString recordText() const;
    QString systemText() const;
    QString settingsText() const;
    QString logText() const;

    // PostHog's $exception and crash_report event properties.
    QJsonObject exceptionProperties() const;
    QJsonObject reportProperties(const QString &note) const;

    QJsonObject toJson() const;
    static std::optional<Report> fromJson(const QJsonObject &json);
};

struct Issue {
    enum Kind { Bug, Feature, Question };
    Kind kind = Bug;
    QString title;
    // By form field id (.github/ISSUE_TEMPLATE: bug.yml, feature.yml, question.yml).
    QList<std::pair<QString, QString>> fields;

    static QString templateFile(Kind kind);
    QString markdown() const;
    // Over maxLength the link holds the title only and complete is false.
    struct Link {
        QUrl url;
        bool complete = true;
    };
    Link link(int maxLength = kMaxLinkLength) const;
    static constexpr int kMaxLinkLength = 8000;
    static QUrl newIssueBase();

    static Issue fromReport(const Report &report, const QString &note);
};

} // namespace hikari::app::report
