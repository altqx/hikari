#include "node_effects_controller.h"

#include "hikari/application/edit_session.h"
#include "hikari/application/hotkeys.h"
#include "hikari/core/ass_load.h"

#include <QKeyEvent>
#include <QKeySequence>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickView>
#include <QtTest>

#include <cstring>
#include <optional>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
using ui::NodeEffectsController;

namespace {

core::Document fixture()
{
    QFile f(QStringLiteral(HIKARI_KARAOKE_PRESET_FIXTURES "/fixture.ass"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    std::vector<std::byte> bytes(static_cast<std::size_t>(data.size()));
    std::memcpy(bytes.data(), data.constData(), bytes.size());
    return core::loadAss(bytes).document;
}

}

class DefaultNodeGraphKeys : public QObject {
    Q_OBJECT

public:
    Q_INVOKABLE QString actionFor(int window, int key, int modifiers) const
    {
        if (window != application::NodeGraphHotkey)
            return {};
        const QKeySequence pressed(QKeyCombination(Qt::KeyboardModifiers(modifiers & ~Qt::KeypadModifier), Qt::Key(key)));
        for (const auto &[id, hotkey] : application::defaultHotkeys())
            if (id.type == application::NodeGraphHotkey
                && QKeySequence(QString::fromStdString(application::qtKeysOfAccel(hotkey.accel))) == pressed)
            {
                const std::string_view symbol = application::hotkeySymbol(id.id);
                return QString::fromUtf8(symbol.data(), qsizetype(symbol.size()));
            }
        return {};
    }
};

class NodeCanvasTests : public QObject {
    Q_OBJECT

    std::optional<application::EditSession> m_session;
    std::unique_ptr<NodeEffectsController> m_controller;
    DefaultNodeGraphKeys m_keys;
    std::unique_ptr<QQuickView> m_view;

    NodeEffectsController &controller() { return *m_controller; }
    QObject *canvas() const { return m_view->rootObject(); }
    QQuickItem *keyTarget() const { return canvas()->property("keyTarget").value<QQuickItem *>(); }

    static QQuickItem *named(QQuickItem *from, const QString &name)
    {
        if (!from || from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = named(child, name))
                return found;
        return nullptr;
    }

    void showCanvas()
    {
        m_view = std::make_unique<QQuickView>();
        m_view->setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(m_controller.get())},
                                      {QStringLiteral("hotkeys"), QVariant::fromValue<QObject *>(&m_keys)}});
        m_view->loadFromModule("Hikari.Ui", "NodeGraphCanvas");
        QVERIFY2(m_view->rootObject(), qPrintable(m_view->errors().isEmpty() ? QString() : m_view->errors().front().toString()));
        m_view->setResizeMode(QQuickView::SizeRootObjectToView);
        m_view->resize(800, 600);
        m_view->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_view.get()));
        keyTarget()->forceActiveFocus();
        QTRY_VERIFY(keyTarget()->hasActiveFocus());
    }

    void press(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::keyClick(m_view.get(), key, modifiers);
    }

    bool claims(Qt::Key key, Qt::KeyboardModifiers modifiers)
    {
        QKeyEvent probe(QEvent::ShortcutOverride, key, modifiers);
        probe.setAccepted(false);
        QCoreApplication::sendEvent(keyTarget(), &probe);
        return probe.isAccepted();
    }

    QPointF positionOf(const QString &id) const
    {
        for (const QVariant &n : m_controller->nodes())
            if (n.toMap().value(QStringLiteral("id")).toString() == id)
                return {n.toMap().value(QStringLiteral("x")).toDouble(), n.toMap().value(QStringLiteral("y")).toDouble()};
        return {-1, -1};
    }

private slots:
    void init()
    {
        m_session.emplace(fixture());
        m_controller = std::make_unique<NodeEffectsController>();
        m_controller->setSessionSource([this] { return &*m_session; });
        m_controller->setEditable(true);
        m_controller->setDelays(0, 60000);
        m_controller->setShown(true);
        QVERIFY(m_controller->newGraph());
    }

    void cleanup()
    {
        m_view.reset();
        m_controller.reset();
        m_session.reset();
    }

    void boundKeysAreClaimedAheadOfGlobalShortcuts()
    {
        showCanvas();
        QVERIFY(claims(Qt::Key_G, Qt::ControlModifier));
        QVERIFY(claims(Qt::Key_Left, Qt::NoModifier));
        QVERIFY(claims(Qt::Key_Home, Qt::NoModifier));
        QVERIFY(!claims(Qt::Key_S, Qt::ControlModifier));
        QVERIFY(!claims(Qt::Key_Escape, Qt::NoModifier));
    }

    void selectAllGroupAndGoInAndOut()
    {
        const QString a = controller().addNode(QStringLiteral("math.random"), 0, 0);
        const QString b = controller().addNode(QStringLiteral("math.random"), 200, 0);
        controller().commit();
        showCanvas();
        press(Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(controller().selection().size(), qsizetype(controller().nodes().size()));
        QVERIFY(controller().selection().contains(a) && controller().selection().contains(b));
        controller().setSelection({a, b});
        press(Qt::Key_G, Qt::ControlModifier);
        QTRY_VERIFY(positionOf(a) == QPointF(-1, -1));
        const QString group = controller().selected();
        QVERIFY(!group.isEmpty());
        press(Qt::Key_Return);
        QTRY_COMPARE(controller().path(), QStringList{group});
        press(Qt::Key_Backspace);
        QTRY_VERIFY(controller().path().isEmpty());
    }

    void deleteRemovesTheSelection()
    {
        const QString a = controller().addNode(QStringLiteral("math.random"), 0, 0);
        controller().commit();
        showCanvas();
        controller().setSelection({a});
        press(Qt::Key_Delete);
        QTRY_COMPARE(positionOf(a), QPointF(-1, -1));
    }

    void arrowsPickTheNearestNodeAndCtrlArrowsMoveIt()
    {
        const QString left = controller().addNode(QStringLiteral("math.random"), 5000, 5000);
        const QString right = controller().addNode(QStringLiteral("math.random"), 5300, 5000);
        controller().commit();
        showCanvas();
        controller().setSelection({left});
        press(Qt::Key_Right);
        QCOMPARE(controller().selected(), right);
        press(Qt::Key_Left);
        QCOMPARE(controller().selected(), left);
        press(Qt::Key_Down, Qt::ControlModifier);
        QTRY_COMPARE(positionOf(left), QPointF(5000, 5020));
    }

    void zoomKeysFitAndFrame()
    {
        const QString a = controller().addNode(QStringLiteral("math.random"), 0, 0);
        controller().addNode(QStringLiteral("math.random"), 1600, 1200);
        controller().commit();
        showCanvas();
        press(Qt::Key_0, Qt::ControlModifier);
        QCOMPARE(canvas()->property("zoom").toDouble(), 1.0);
        press(Qt::Key_Equal, Qt::ControlModifier);
        QCOMPARE(canvas()->property("zoom").toDouble(), 1.25);
        press(Qt::Key_Minus, Qt::ControlModifier);
        QCOMPARE(canvas()->property("zoom").toDouble(), 1.0);
        press(Qt::Key_Home);
        const double fitted = canvas()->property("zoom").toDouble();
        QVERIFY(fitted < 0.5);
        controller().setSelection({a});
        press(Qt::Key_F);
        QCOMPARE(canvas()->property("zoom").toDouble(), 1.0);
    }

    void unboundLettersStillOpenAddNodeWithTheLetter()
    {
        showCanvas();
        press(Qt::Key_Q);
        QObject *add = m_view->rootObject()->findChild<QObject *>(QStringLiteral("nodeAddPopup"));
        QVERIFY(add);
        QTRY_VERIFY(add->property("visible").toBool());
        auto *search = named(m_view->contentItem(), QStringLiteral("nodeAddSearch"));
        QVERIFY(search);
        QCOMPARE(search->property("text").toString(), QStringLiteral("q"));
        press(Qt::Key_Escape);
        QTRY_VERIFY(!add->property("visible").toBool());
        keyTarget()->forceActiveFocus();
        press(Qt::Key_A, Qt::ShiftModifier);
        QTRY_VERIFY(add->property("visible").toBool());
        QCOMPARE(named(m_view->contentItem(), QStringLiteral("nodeAddSearch"))->property("text").toString(), QString());
    }
};

QTEST_MAIN(NodeCanvasTests)
#include "node_canvas_tests.moc"
