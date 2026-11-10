#include "hikari/app/forwarded_launches.h"

#include "hikari/app/application.h"
#include "hikari/app/composition.h"
#include "hikari/plugins/marketplace.h"

#include <QGuiApplication>
#include <QLoggingCategory>
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

Q_LOGGING_CATEGORY(lcLinks, "hikari.links")

constexpr int kGatherMs = 400;
constexpr int kBusyRetryMs = 100;
constexpr qsizetype kLoggedLinkLength = 200;

QString installLinkId(const QString &link)
{
    const auto id = plugins::installLinkId(link.toStdString());
    return id ? QString::fromStdString(*id) : QString();
}

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

}

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
        activate(target(), std::exchange(m_quitToken, QString()));
    });
}

QWindow *ForwardedLaunches::target() const
{
    if (!m_window)
        return nullptr;
    if (QQuickWindow *used = m_application.lastUsedShellWindow())
        return used;
    return m_application.firstWindowHidden() ? nullptr : m_window.data();
}

void ForwardedLaunches::setWindow(QWindow *window)
{
    m_window = window;
    if (m_activate) {
        m_activate = false;
        activate(target(), std::exchange(m_token, QString()));
    }
    if (pending())
        m_timer.start(kGatherMs);
}

void ForwardedLaunches::keep(const QStringList &arguments)
{
    for (const QString &argument : arguments) {
        if (!isAppLink(argument))
            m_files << argument;
        else if (!installLinkId(argument).isEmpty())
            m_link = argument;
        else
            qCWarning(lcLinks, "Ignored a link HikariSub doesn't take: %s",
                      argument.left(kLoggedLinkLength).toUtf8().toPercentEncoding(":/?=&.-_~").constData());
    }
}

void ForwardedLaunches::take(const SingleInstance::Request &request)
{
    const qsizetype files = m_files.size();
    keep(request.files);
    if (m_files.size() != files)
        m_request = request.window;
    QString token = request.activationToken;
    if (m_application.quitting() && !token.isEmpty()) {
        m_quitToken = std::exchange(token, QString());
        m_quitTokenReview = m_application.quitReviews();
    }
    if (m_window)
        activate(target(), token);
    else {
        m_activate = true;
        m_token = token;
    }
    if (pending() && m_window)
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
    QStringList unopened = std::exchange(m_files, QStringList());
    if (!m_link.isEmpty())
        unopened << std::exchange(m_link, QString());
    return unopened;
}

bool ForwardedLaunches::busy() const
{
    return m_application.forwardedOpenWaits() || QGuiApplication::modalWindow() || modalPopupOpen(target());
}

void ForwardedLaunches::open()
{
    if (!m_window || !pending())
        return;
    if (busy()) {
        m_timer.start(kBusyRetryMs);
        return;
    }
    if (m_files.isEmpty()) {
        QMetaObject::invokeMethod(target(), "openInstallLink", Q_ARG(QVariant, installLinkId(std::exchange(m_link, QString()))));
        return;
    }
    if (!m_link.isEmpty())
        m_timer.start(kBusyRetryMs);
    const QVariantMap result = m_application.openForwarded(std::exchange(m_files, QStringList()),
                                                           static_cast<int>(std::exchange(m_request, application::WindowRequest::Default)));
    const QString kind = result.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("video"))
        openStartVideo(target(), result.value(QStringLiteral("path")).toString());
    else if (kind == QLatin1String("session"))
        QMetaObject::invokeMethod(target(), "openSessionFile", Q_ARG(QVariant, result.value(QStringLiteral("path"))));
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

}
