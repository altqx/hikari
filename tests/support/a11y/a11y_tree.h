#pragma once

#include <QAccessible>
#include <QString>
#include <QStringList>

class QQuickWindow;

namespace hikari::testing {

struct AccessibleNode {
    QAccessible::Role role;
    QString name;
    QAccessible::State state;
    int depth;
};

// Makes Qt build accessibility interfaces without an assistive client.
void enableAccessibility();

// Depth-first walk of the window's accessibility tree.
QList<AccessibleNode> accessibleTree(QQuickWindow *window);

// "depth role 'name' [focusable,focused,...]" per node, for failure output.
QString describe(const QList<AccessibleNode> &nodes);

// True if a node with this role and exact name exists.
bool hasNode(const QList<AccessibleNode> &nodes, QAccessible::Role role, const QString &name);

// Accessible names in keyboard (Tab) focus order, starting from the first
// focusable item, until the chain wraps.
QStringList tabOrder(QQuickWindow *window);

} // namespace hikari::testing
