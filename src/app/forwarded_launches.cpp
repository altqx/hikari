#include "hikari/app/forwarded_launches.h"

#include "hikari/app/application.h"
#include "hikari/app/composition.h"

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWindow>

#include <algorithm>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hikari::app {

namespace {

constexpr int kGatherMs = 400;
constexpr int kBusyRetryMs = 100;

bool modalPopupOpen(QWindow *window)
{
    auto open = [](const QObject *root) {
        const auto objects = root->findChildren<QObject *>();
        return std::any_of(objects.begin(), objects.end(), [](const QObject *object) {
            return object->inherits("QQuickPopup") && object->property("opened").toBool() &&
                   object->property("modal").toBool();
        });
    };
    auto *quick = qobject_cast<QQuickWindow *>(window);
    return window && (open(window) || (quick && open(quick->contentItem())));
}

} // namespace

ForwardedLaunches::ForwardedLaunches(Application &application, QObject *parent)
    : QObject(parent)
    , m_application(application)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &ForwardedLaunches::open);
    connect(&m_application, &Application::quitCancelled, this, [this] {
        if (m_quitToken.isEmpty() || m_quitTokenReview != m_application.quitReviews())
            return;
        m_quitTokenReview = 0;
        activate(m_window, std::exchange(m_quitToken, QString()));
    });
}

void ForwardedLaunches::setWindow(QWindow *window)
{
    m_window = window;
    if (m_activate) {
        m_activate = false;
        activate(window, std::exchange(m_token, QString()));
    }
    if (!m_files.isEmpty())
        m_timer.start(kGatherMs);
}

void ForwardedLaunches::take(const SingleInstance::Request &request)
{
    m_files += request.files;
    QString token = request.activationToken;
    if (m_application.quitting() && !token.isEmpty()) {
        m_quitToken = std::exchange(token, QString());
        m_quitTokenReview = m_application.quitReviews();
    }
    if (m_window)
        activate(m_window, token);
    else {
        m_activate = true;
        m_token = token;
    }
    if (!m_files.isEmpty() && m_window)
        m_timer.start(kGatherMs);
    if (request.files.isEmpty() && m_application.quitting())
        m_afterQuitReview = m_application.quitReviews();
}

bool ForwardedLaunches::startAfterQuit() const
{
    return m_afterQuitReview > 0 && m_afterQuitReview == m_application.quitReviews() && m_application.quitApproved();
}

QString ForwardedLaunches::activationTokenAfterQuit() const
{
    return m_quitTokenReview == m_application.quitReviews() && m_application.quitApproved() ? m_quitToken : QString();
}

QStringList ForwardedLaunches::takeUnopened()
{
    m_timer.stop();
    return std::exchange(m_files, QStringList());
}

bool ForwardedLaunches::busy() const
{
    return m_application.forwardedOpenWaits() || QGuiApplication::modalWindow() || modalPopupOpen(m_window);
}

void ForwardedLaunches::open()
{
    if (!m_window || m_files.isEmpty())
        return;
    if (busy()) {
        m_timer.start(kBusyRetryMs);
        return;
    }
    const QVariantMap result = m_application.openForwarded(std::exchange(m_files, QStringList()));
    const QString kind = result.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("video"))
        openStartVideo(m_window, result.value(QStringLiteral("path")).toString());
    else if (kind == QLatin1String("session"))
        QMetaObject::invokeMethod(m_window, "openSessionFile", Q_ARG(QVariant, result.value(QStringLiteral("path"))));
}

void ForwardedLaunches::activate(QWindow *window, const QString &activationToken)
{
    if (!window)
        return;
    if (window->windowStates() & Qt::WindowMinimized)
        window->setWindowStates(window->windowStates() & ~Qt::WindowMinimized);
    if (!window->isVisible())
        window->show();
    QWindow *target = QGuiApplication::modalWindow() ? QGuiApplication::modalWindow() : window;
    // Qt's xdg-shell activation reads XDG_ACTIVATION_TOKEN; unset it so children don't inherit it.
    const bool wayland = QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
    if (wayland && !activationToken.isEmpty())
        qputenv("XDG_ACTIVATION_TOKEN", activationToken.toUtf8());
    target->raise();
    target->requestActivate();
    if (wayland)
        qunsetenv("XDG_ACTIVATION_TOKEN");
#ifdef _WIN32
    ::SetForegroundWindow(reinterpret_cast<HWND>(target->winId()));
#endif
}

} // namespace hikari::app
