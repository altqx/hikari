#include "a11y_tree.h"

#include <QQuickItem>
#include <QQuickWindow>

namespace hikari::testing {

namespace {

void walk(QAccessibleInterface *iface, int depth, QList<AccessibleNode> &out)
{
    if (!iface || !iface->isValid())
        return;
    out.append({iface->role(), iface->text(QAccessible::Name), iface->state(), depth});
    for (int i = 0; i < iface->childCount(); ++i)
        walk(iface->child(i), depth + 1, out);
}

QString stateText(const QAccessible::State &s)
{
    QStringList parts;
    if (s.focusable) parts << "focusable";
    if (s.focused) parts << "focused";
    if (s.disabled) parts << "disabled";
    if (s.invisible) parts << "invisible";
    if (s.checkable) parts << "checkable";
    if (s.checked) parts << "checked";
    if (s.editable) parts << "editable";
    return parts.join(',');
}

QString accessibleName(QQuickItem *item)
{
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(item);
    return iface ? iface->text(QAccessible::Name) : QString();
}

} // namespace

void enableAccessibility()
{
    QAccessible::setActive(true);
}

QList<AccessibleNode> accessibleTree(QQuickWindow *window)
{
    QList<AccessibleNode> nodes;
    walk(QAccessible::queryAccessibleInterface(window), 0, nodes);
    return nodes;
}

QString describe(const QList<AccessibleNode> &nodes)
{
    QString out;
    for (const auto &n : nodes)
        out += QStringLiteral("%1 %2 '%3' [%4]\n")
                   .arg(n.depth)
                   .arg(int(n.role), 0, 16)
                   .arg(n.name, stateText(n.state));
    return out;
}

bool hasNode(const QList<AccessibleNode> &nodes, QAccessible::Role role, const QString &name)
{
    for (const auto &n : nodes)
        if (n.role == role && n.name == name)
            return true;
    return false;
}

QStringList tabOrder(QQuickWindow *window)
{
    QStringList names;
    QQuickItem *first = window->contentItem()->nextItemInFocusChain(true);
    QQuickItem *item = first;
    while (item && names.size() < 64) {
        names << accessibleName(item);
        item = item->nextItemInFocusChain(true);
        if (item == first)
            break;
    }
    return names;
}

} // namespace hikari::testing
