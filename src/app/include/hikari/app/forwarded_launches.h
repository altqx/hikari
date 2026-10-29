#pragma once

#include "hikari/app/single_instance.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

class QWindow;

namespace hikari::app {

class Application;

class ForwardedLaunches : public QObject {
    Q_OBJECT
public:
    explicit ForwardedLaunches(Application &application, QObject *parent = nullptr);

    // Requests taken before the window is set are carried out then.
    void setWindow(QWindow *window);
    void take(const SingleInstance::Request &request);
    bool pending() const { return !m_files.isEmpty(); }
    QStringList takeUnopened();
    // True when a start without files came during the quit that went ahead.
    bool startAfterQuit() const;
    // The Wayland activation token of the last start during the quit that went ahead, else "".
    QString activationTokenAfterQuit() const;

    // On Windows it takes the foreground only if the handing start called AllowSetForegroundWindow.
    static void activate(QWindow *window, const QString &activationToken);

private:
    void open();
    bool busy() const;

    Application &m_application;
    QPointer<QWindow> m_window;
    QStringList m_files;
    QString m_token;
    bool m_activate = false;
    int m_afterQuitReview = 0;
    QString m_quitToken;
    int m_quitTokenReview = 0;
    QTimer m_timer;
};

}
