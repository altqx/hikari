#include "text_menus.h"

#include <QDebug>
#include <QQuickItem>
#include <QtQml/qqmlcomponent.h>
#include <QtQml/qqmlengine.h>
#include <QtQuickTemplates2/private/qquickcontextmenu_p.h>

namespace hikari::ui::style {

namespace {

QString g_module;
QString g_type;

const QString kMenuName = QStringLiteral("textMenu");

}

TextMenus *TextMenus::create(QQmlEngine *engine, QJSEngine *)
{
    return new TextMenus(engine);
}

void TextMenus::setSource(const QString &module, const QString &type)
{
    g_module = module;
    g_type = type;
}

QObject *TextMenus::menuFor(QQuickItem *editor)
{
    if (!editor || g_type.isEmpty())
        return nullptr;
    if (auto *made = editor->findChild<QObject *>(kMenuName, Qt::FindDirectChildrenOnly))
        return made;
    QQmlEngine *engine = qmlEngine(editor);
    if (!engine)
        return nullptr;
    QQmlComponent component(engine, g_module, g_type);
    QObject *menu = component.createWithInitialProperties(
        {{QStringLiteral("editor"), QVariant::fromValue(editor)},
         {QStringLiteral("parent"), QVariant::fromValue(editor)}},
        qmlContext(editor));
    if (!menu) {
        qWarning().noquote() << component.errorString();
        return nullptr;
    }
    menu->setObjectName(kMenuName);
    menu->setParent(editor);
    return menu;
}

QObject *TextMenus::adopt(QQuickItem *field)
{
    if (!field)
        return nullptr;
    auto *contextMenu = qobject_cast<QQuickContextMenu *>(qmlAttachedPropertiesObject<QQuickContextMenu>(field));
    if (contextMenu)
        contextMenu->setMenu(nullptr);
    return contextMenu;
}

}
