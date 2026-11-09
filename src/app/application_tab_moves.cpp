#include "hikari/app/application.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickWindow>

#include <limits>

namespace hikari::app {

namespace {

constexpr QPoint kTabGrabbedAt{60, 16};

}

bool Application::movableTab(application::DocumentId document) const
{
    return m_workspace.window(document) &&
           !(m_workspace.reference() == document && !m_workspace.referenceIsTab());
}

bool Application::usableShellWindow(int shellWindow) const
{
    if (shellWindow == 0)
        return m_firstWindow && !m_firstWindowHidden;
    const auto found = m_shellWindows.find(shellWindow);
    return found != m_shellWindows.end() && found->second.window && !found->second.closing;
}

bool Application::moveTab(qulonglong document, int shellWindow, int index)
{
    const application::DocumentId id{document};
    if (!movableTab(id) || !usableShellWindow(shellWindow))
        return false;
    const int from = *m_workspace.window(id);
    const std::size_t at = index < 0 ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(index);
    if (from == shellWindow) {
        m_workspace.moveTab(id, shellWindow, at);
        refreshViews();
        saveLastSession();
        return true;
    }
    carryTabArrangement(id, from, shellWindow);
    m_workspace.moveTab(id, shellWindow, at);
    showTabArrangement(from);
    refreshViews();
    m_scrollRestoring = true;
    emit tabShown(m_tabMedia[id.value].scroll);
    saveLastSession();
    if (m_workspace.tabs(from).empty())
        closeEmptiedWindow(from);
    return true;
}

bool Application::moveTabToNewWindow(qulonglong document, QPointF global, bool place)
{
    const application::DocumentId id{document};
    QQmlEngine *engine = m_firstWindow ? qmlEngine(m_firstWindow.data()) : nullptr;
    if (!movableTab(id) || !engine)
        return false;
    const QQuickWindow *source = shellQuickWindow(*m_workspace.window(id));
    const QSize size = source ? source->size() : QSize();
    const auto opened = shellWindowOf(openShellWindow(*engine));
    if (!opened)
        return false;
    QQuickWindow &window = *shellQuickWindow(*opened);
    if (place && !size.isEmpty()) {
        const QPoint at = global.toPoint() - kTabGrabbedAt;
        application::SessionWindow placed;
        placed.geometry = application::SessionGeometry{at.x(), at.y(), size.width(), size.height()};
        placeShellWindow(window, placed);
    } else if (!size.isEmpty()) {
        window.resize(size);
    }
    return moveTab(document, *opened, 0);
}

void Application::dropTab(qulonglong document, QPointF global)
{
    const QPoint point = global.toPoint();
    QWindow *over = QGuiApplication::topLevelAt(point);
    if (!over) {
        moveTabToNewWindow(document, global, true);
        return;
    }
    const auto shellWindow = shellWindowOf(over);
    QQuickWindow *window = shellWindow ? shellQuickWindow(*shellWindow) : nullptr;
    QVariant index;
    if (window && QMetaObject::invokeMethod(window, "tabDropIndex", Q_RETURN_ARG(QVariant, index),
                                            Q_ARG(QVariant, point.x()), Q_ARG(QVariant, point.y())) &&
        index.toInt() >= 0)
        moveTab(document, *shellWindow, index.toInt());
}

QVariantList Application::otherWindows(int shellWindow) const
{
    std::vector<int> windows;
    if (shellWindow != 0 && usableShellWindow(0))
        windows.push_back(0);
    for (const auto &[id, shell] : m_shellWindows)
        if (id != shellWindow && usableShellWindow(id))
            windows.push_back(id);
    QVariantList rows;
    for (const int window : windows) {
        const auto current = m_workspace.currentTab(window);
        const std::string *title = current ? m_workspace.title(*current) : nullptr;
        rows << QVariantMap{{QStringLiteral("window"), window},
                            {QStringLiteral("title"), title ? QString::fromStdString(*title)
                                                            : QCoreApplication::translate("HikariSub.Home", "Home")}};
    }
    return rows;
}

void Application::carryTabArrangement(application::DocumentId document, int from, int to)
{
    auto &source = layoutOf(from);
    auto &destination = layoutOf(to);
    if (const auto arrangement = source.tabArrangement(document.value))
        destination.setTabArrangement(document.value, arrangement->serialise());
    source.forgetTab(document.value);
    if (const auto shown = m_layoutDocuments.find(from); shown != m_layoutDocuments.end() && shown->second == document)
        m_layoutDocuments.erase(shown);
}

void Application::closeEmptiedWindow(int shellWindow)
{
    if (shellWindow == 0) {
        hideFirstWindow();
        return;
    }
    if (QQuickWindow *window = shellQuickWindow(shellWindow))
        QMetaObject::invokeMethod(window, &QWindow::close, Qt::QueuedConnection);
}

}
