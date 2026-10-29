#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

namespace hikari::test::desktop {

QStringList recordedSchemes();

// Exit code of an executable that refused a URL of an unrecorded scheme.
constexpr int kUnhandledExitCode = 70;

struct Started {
    QString program;
    QStringList arguments;
    bool operator==(const Started &) const = default;
};

// What would have reached the desktop since clear(), in order.
QList<QUrl> opened();
QStringList revealed();
QList<Started> started();
// URLs refused for their scheme; the run fails at exit when not empty.
QList<QUrl> refused();
void clear();

// What a detached start answers from now on; clear() sets it back to true.
void setStartResult(bool started);

void recordRevealed(const QString &path);
bool recordStarted(const QString &program, const QStringList &arguments);
void refuse(const QUrl &url);
// Whether a recording handler took `url` since opened() had `openedBefore` entries.
bool recordingHandlerTook(const QUrl &url, qsizetype openedBefore);

// Replaces what refuse() reports after recording; returns the old one (null: the default).
std::function<void(const QUrl &url)> replaceRefusalReport(std::function<void(const QUrl &url)> report);

}
