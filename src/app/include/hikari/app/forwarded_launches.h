#pragma once

#include "hikari/app/single_instance.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

class QWindow;

namespace hikari::app {

class Application;

// WS5: on the instance's holder, what later starts hand over (legacy
// HikariSubIpcConnection::OnExec and hikarisubApp::OnOpen). The main window
// comes forward at once; the files are gathered while starts keep coming
// (400 ms, legacy's open timer) and open with Application::openForwarded
// once nothing waits for an answer: no review or startup session question
// (Application::forwardedOpenWaits) and no modal dialog or window, as legacy
// waited while IsBusy.
class ForwardedLaunches : public QObject {
    Q_OBJECT
public:
    explicit ForwardedLaunches(Application &application, QObject *parent = nullptr);

    // The main window, once it is up: what came before it is carried out then.
    void setWindow(QWindow *window);
    void take(const SingleInstance::Request &request);
    // Files taken and not opened yet.
    bool pending() const { return !m_files.isEmpty(); }
    // At exit: the files taken and not opened (the quit came first), which
    // are no longer opened here.
    QStringList takeUnopened();
    // At exit: a start without files came while the quit was reviewed or
    // going ahead, and that quit went ahead: the user still asked for the
    // program (a cancelled quit keeps the window, which came forward, and a
    // later quit is the user's own).
    bool startAfterQuit() const;
    // At exit: the activation token of the last start that came during the
    // quit that went ahead, for the start made after it (Wayland); empty
    // otherwise.
    QString activationTokenAfterQuit() const;

    // Brings `window` (or the modal window over it) forward: restored when
    // minimized, raised and activated, with the start's Wayland activation
    // token; on Windows it takes the foreground, which the handing start
    // allowed (AllowSetForegroundWindow).
    static void activate(QWindow *window, const QString &activationToken);

private:
    void open();
    bool busy() const;

    Application &m_application;
    QPointer<QWindow> m_window;
    QStringList m_files;
    QString m_token;
    bool m_activate = false;
    int m_afterQuitReview = 0; // the quit review a start without files came during
    QString m_quitToken;       // the last token taken during a quit
    int m_quitTokenReview = 0; // and that quit's review
    QTimer m_timer;
};

} // namespace hikari::app
