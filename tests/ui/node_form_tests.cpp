#include "node_effects_controller.h"

#include "hikari/application/edit_session.h"
#include "hikari/application/node_catalogue.h"
#include "hikari/application/node_graph_editing.h"
#include "hikari/core/ass_load.h"

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

QVariantMap formField(const NodeEffectsController &c, const QString &name)
{
    for (const QVariant &f : c.form().value(QStringLiteral("fields")).toList())
        if (f.toMap().value(QStringLiteral("name")).toString() == name)
            return f.toMap();
    return {};
}

QVariant stored(NodeEffectsController &c, const QString &node, const QString &field)
{
    const QStringList kept = c.selection();
    c.setSelection({node});
    QVariant out;
    for (const QVariant &f : c.inspector().value(QStringLiteral("fields")).toList())
        if (f.toMap().value(QStringLiteral("name")).toString() == field)
            out = f.toMap().value(QStringLiteral("value"));
    c.setSelection(kept);
    return out;
}

}

class NodeFormTests : public QObject {
    Q_OBJECT

    std::optional<application::EditSession> m_session;
    std::unique_ptr<NodeEffectsController> m_controller;
    std::unique_ptr<QQuickView> m_view;

    NodeEffectsController &controller() { return *m_controller; }

    static QQuickItem *named(QQuickItem *from, const QString &name)
    {
        if (!from || from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = named(child, name))
                return found;
        return nullptr;
    }

    QQuickItem *item(const QString &name) const
    {
        return m_view ? named(m_view->rootObject(), name) : nullptr;
    }

    void showPanel()
    {
        m_view = std::make_unique<QQuickView>();
        m_view->setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(m_controller.get())}});
        m_view->loadFromModule("Hikari.Ui", "NodeInspector");
        QVERIFY2(m_view->rootObject(), qPrintable(m_view->errors().isEmpty() ? QString() : m_view->errors().front().toString()));
        m_view->setResizeMode(QQuickView::SizeRootObjectToView);
        m_view->resize(340, 640);
        m_view->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_view.get()));
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
        application::setPluginNodes({}, {});
        m_view.reset();
        m_controller.reset();
        m_session.reset();
    }

    void aFieldShowsItsRangeAndRefusesValuesOutsideIt()
    {
        const QString random = controller().addNode(QStringLiteral("math.random"), 0, 0);
        controller().setSelection({random});
        const QVariantMap step = formField(controller(), QStringLiteral("step"));
        QCOMPARE(step.value(QStringLiteral("range")).toString(), QStringLiteral("At least 0"));
        QCOMPARE(controller().fieldRefusal(QStringLiteral("step"), -1), QStringLiteral("At least 0"));
        QCOMPARE(controller().fieldRefusal(QStringLiteral("step"), QStringLiteral("many")), QStringLiteral("Enter a number"));
        QVERIFY(!controller().setSelectedField(QStringLiteral("step"), -1));
        QCOMPARE(stored(controller(), random, QStringLiteral("step")).toDouble(), 0.0);
        QVERIFY(controller().setSelectedField(QStringLiteral("step"), QStringLiteral("0.5")));
        QCOMPARE(stored(controller(), random, QStringLiteral("step")).toDouble(), 0.5);
    }

    void alphaIsBetweenZeroAnd255AndTimeShowsMilliseconds()
    {
        const QString tag = controller().addNode(QStringLiteral("tags.tag"), 0, 0);
        controller().setSelection({tag});
        QCOMPARE(formField(controller(), QStringLiteral("alpha")).value(QStringLiteral("range")).toString(), QStringLiteral("0 – 255"));
        QCOMPARE(controller().fieldRefusal(QStringLiteral("alpha"), 300), QStringLiteral("Between 0 and 255"));
        QCOMPARE(controller().fieldRefusal(QStringLiteral("colour"), QStringLiteral("red")), QStringLiteral("Enter a colour as #RRGGBB"));
        QVERIFY(controller().fieldRefusal(QStringLiteral("colour"), QStringLiteral("#A6E3E9")).isEmpty());
        const QString fade = controller().addNode(QStringLiteral("tags.fade"), 0, 100);
        controller().setSelection({fade});
        QCOMPARE(formField(controller(), QStringLiteral("fade_in")).value(QStringLiteral("unit")).toString(), QStringLiteral("ms"));
        const QString strips = controller().addNode(QStringLiteral("units.clip_gradient"), 0, 200);
        controller().setSelection({strips});
        QCOMPARE(formField(controller(), QStringLiteral("padding")).value(QStringLiteral("unit")).toString(), QStringLiteral("px"));
    }

    void aChangedFieldResetsToItsDefaultInTheSameStep()
    {
        const QString fade = controller().addNode(QStringLiteral("tags.fade"), 0, 0);
        controller().setSelection({fade});
        controller().commit();
        const std::size_t before = m_session->historySize();
        QVERIFY(!formField(controller(), QStringLiteral("fade_in")).value(QStringLiteral("changed")).toBool());
        QVERIFY(controller().setSelectedField(QStringLiteral("fade_in"), 150));
        QVERIFY(formField(controller(), QStringLiteral("fade_in")).value(QStringLiteral("changed")).toBool());
        controller().resetSelectedField(QStringLiteral("fade_in"));
        QVERIFY(!formField(controller(), QStringLiteral("fade_in")).value(QStringLiteral("changed")).toBool());
        QCOMPARE(formField(controller(), QStringLiteral("fade_in")).value(QStringLiteral("value")).toDouble(), 0.0);
        controller().commit();
        QCOMPARE(m_session->historySize(), before + 1);
        QCOMPARE(m_session->history().back().name, std::string("Edit graph"));
    }

    void severalNodesOfOneTypeEditTogetherAndShowMixed()
    {
        const QString a = controller().addNode(QStringLiteral("tags.fade"), 0, 0);
        const QString b = controller().addNode(QStringLiteral("tags.fade"), 0, 100);
        controller().setSelection({a});
        QVERIFY(controller().setSelectedField(QStringLiteral("fade_in"), 150));
        controller().setSelection({a, b});
        const QVariantMap form = controller().form();
        QCOMPARE(form.value(QStringLiteral("count")).toInt(), 2);
        QVERIFY(form.value(QStringLiteral("oneType")).toBool());
        QVERIFY(!form.contains(QStringLiteral("wired")));
        QVERIFY(formField(controller(), QStringLiteral("fade_in")).value(QStringLiteral("mixed")).toBool());
        QVERIFY(!formField(controller(), QStringLiteral("fade_out")).value(QStringLiteral("mixed")).toBool());
        QVERIFY(controller().setSelectedField(QStringLiteral("fade_in"), 80));
        QCOMPARE(stored(controller(), a, QStringLiteral("fade_in")).toDouble(), 80.0);
        QCOMPARE(stored(controller(), b, QStringLiteral("fade_in")).toDouble(), 80.0);
        QVERIFY(!formField(controller(), QStringLiteral("fade_in")).value(QStringLiteral("mixed")).toBool());
    }

    void mixedTypesShowNoFields()
    {
        const QString a = controller().addNode(QStringLiteral("tags.fade"), 0, 0);
        const QString b = controller().addNode(QStringLiteral("math.random"), 0, 100);
        controller().setSelection({a, b});
        const QVariantMap form = controller().form();
        QVERIFY(!form.value(QStringLiteral("oneType")).toBool());
        QVERIFY(form.value(QStringLiteral("fields")).toList().isEmpty());
        QVERIFY(!controller().setSelectedField(QStringLiteral("fade_in"), 10));
    }

    void aWiredFieldNamesItsSource()
    {
        const QString number = controller().addNode(QStringLiteral("values.number"), 0, 0);
        const QString random = controller().addNode(QStringLiteral("math.random"), 200, 0);
        QVERIFY(controller().connectPorts(number, QStringLiteral("value"), random, QStringLiteral("max")).isEmpty());
        controller().setSelection({random});
        const QVariantMap max = formField(controller(), QStringLiteral("max"));
        QCOMPARE(max.value(QStringLiteral("from")).toString(), QStringLiteral("%1 › value").arg(number));
        QCOMPARE(max.value(QStringLiteral("fromNode")).toString(), number);
        QVERIFY(formField(controller(), QStringLiteral("min")).value(QStringLiteral("from")).toString().isEmpty());
    }

    void aPointIsPickedOnTheVideoOnce()
    {
        const QString tag = controller().addNode(QStringLiteral("tags.tag"), 0, 0);
        controller().setSelection({tag});
        controller().pickPoint(QStringLiteral("value"));
        QVERIFY(controller().pickingPoint().isEmpty());
        controller().pickPoint(QStringLiteral("point"));
        QCOMPARE(controller().pickingPoint(), QStringLiteral("point"));
        const std::size_t before = m_session->historySize();
        QVERIFY(controller().pointPicked(640, 360));
        QVERIFY(controller().pickingPoint().isEmpty());
        QCOMPARE(stored(controller(), tag, QStringLiteral("point")).toList(), (QVariantList{640.0, 360.0}));
        QCOMPARE(m_session->historySize(), before + 1);
        QVERIFY(!controller().pointPicked(1, 2));
    }

    void aRefusedNodeNameSaysWhy()
    {
        const QString a = controller().addNode(QStringLiteral("tags.fade"), 0, 0);
        const QString b = controller().addNode(QStringLiteral("tags.fade"), 0, 100);
        QCOMPARE(controller().nodeNameRefusal(b, a), QStringLiteral("Another node is called “%1”.").arg(a));
        QCOMPARE(controller().nodeNameRefusal(b, QStringLiteral("a b")), QStringLiteral("Use only letters, digits, _ and -"));
        QCOMPARE(controller().nodeNameRefusal(b, QString()), QStringLiteral("Enter a name"));
        QVERIFY(controller().nodeNameRefusal(b, QStringLiteral("pop")).isEmpty());
    }

    void thePanelScrubsStepsAndRefusesTypedValues()
    {
        const QString random = controller().addNode(QStringLiteral("math.random"), 0, 0);
        controller().setSelection({random});
        showPanel();
        QQuickItem *field = item(QStringLiteral("nodeFieldValue_step"));
        QVERIFY(field);
        QCOMPARE(field->height(), 24.0);

        QQuickItem *up = item(QStringLiteral("nodeFieldValue_stepUp"));
        QVERIFY(up);
        QTest::mouseClick(m_view.get(), Qt::LeftButton, {}, up->mapToScene(QPointF(up->width() / 2, up->height() / 2)).toPoint());
        QTRY_COMPARE(stored(controller(), random, QStringLiteral("step")).toDouble(), 1.0);
        QTRY_VERIFY(item(QStringLiteral("nodeField_stepReset"))->isVisible());
        QCOMPARE(field->property("stateText").toString(), QStringLiteral("Changed from the default"));

        QQuickItem *drag = item(QStringLiteral("nodeFieldValue_stepDrag"));
        const QPoint at = drag->mapToScene(QPointF(drag->width() / 2, drag->height() / 2)).toPoint();
        QTest::mousePress(m_view.get(), Qt::LeftButton, {}, at);
        for (int dx = 4; dx <= 40; dx += 4)
            QTest::mouseMove(m_view.get(), at + QPoint(dx, 0));
        QTest::mouseRelease(m_view.get(), Qt::LeftButton, {}, at + QPoint(40, 0));
        QTRY_COMPARE(stored(controller(), random, QStringLiteral("step")).toDouble(), 11.0);

        QTest::mouseClick(m_view.get(), Qt::LeftButton, {}, at);
        QQuickItem *input = item(QStringLiteral("nodeFieldValue_stepInput"));
        QTRY_VERIFY(input->hasActiveFocus());
        QTest::keyClick(m_view.get(), Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(m_view.get(), '-');
        QTest::keyClick(m_view.get(), '3');
        QTest::keyClick(m_view.get(), Qt::Key_Return);
        QTRY_VERIFY(item(QStringLiteral("nodeField_stepRefusal"))->isVisible());
        QCOMPARE(item(QStringLiteral("nodeField_stepRefusal"))->property("text").toString(), QStringLiteral("At least 0"));
        QCOMPARE(stored(controller(), random, QStringLiteral("step")).toDouble(), 11.0);

        QQuickItem *reset = item(QStringLiteral("nodeField_stepReset"));
        QTest::mouseClick(m_view.get(), Qt::LeftButton, {}, reset->mapToScene(QPointF(reset->width() / 2, reset->height() / 2)).toPoint());
        QTRY_COMPARE(stored(controller(), random, QStringLiteral("step")).toDouble(), 0.0);
        QTRY_VERIFY(!item(QStringLiteral("nodeField_stepRefusal"))->isVisible());
    }

    void noFieldLabelIsCutOff()
    {
        for (const char *type : {"math.random", "tags.fade"}) {
            const QString node = controller().addNode(QString::fromLatin1(type), 0, 0);
            controller().setSelection({node});
            showPanel();
            for (const QVariant &f : controller().form().value(QStringLiteral("fields")).toList()) {
                const QString name = f.toMap().value(QStringLiteral("name")).toString();
                QQuickItem *label = item(QStringLiteral("nodeField_%1Label").arg(name));
                QVERIFY2(label, qPrintable(name));
                QVERIFY2(!label->property("truncated").toBool(), qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(type), name)));
            }
        }
    }

    void thePanelSaysMixedForSeveralNodes()
    {
        const QString a = controller().addNode(QStringLiteral("tags.fade"), 0, 0);
        const QString b = controller().addNode(QStringLiteral("tags.fade"), 0, 100);
        controller().setSelection({a});
        QVERIFY(controller().setSelectedField(QStringLiteral("fade_in"), 150));
        controller().setSelection({a, b});
        showPanel();
        QCOMPARE(item(QStringLiteral("nodeInspectorTitle"))->property("text").toString(), QStringLiteral("2 Fade nodes"));
        QCOMPARE(item(QStringLiteral("nodeFieldValue_fade_inInput"))->property("text").toString(), QStringLiteral("Mixed"));
        QVERIFY(item(QStringLiteral("nodeSelectionGroup"))->isVisible());
        QVERIFY(item(QStringLiteral("nodeSelectionDelete"))->isVisible());
        QVERIFY(!item(QStringLiteral("nodeInspectorValuesTitle"))->isVisible());
    }

    void aValueFromZeroToOneIsASliderWithItsNumber()
    {
        application::PluginNode plugin{"example.mix", "Mix", "Mix", "return {}", {}};
        plugin.spec.id = application::pluginNodeType("example.mix", "mix");
        plugin.spec.category = "example.mix";
        application::FieldSpec amount{"amount", application::FieldKind::Number, json::Json(0.5), "How much."};
        amount.minimum = 0;
        amount.maximum = 1;
        plugin.spec.fields.push_back(amount);
        application::setPluginNodes({plugin}, {});
        const QString mix = controller().addNode(QString::fromStdString(plugin.spec.id), 0, 0);
        QVERIFY(!mix.isEmpty());
        controller().setSelection({mix});
        QCOMPARE(controller().fieldRefusal(QStringLiteral("amount"), 1.5), QStringLiteral("Between 0 and 1"));
        showPanel();
        QQuickItem *slider = item(QStringLiteral("nodeFieldSlider_amount"));
        QVERIFY(slider);
        QVERIFY(item(QStringLiteral("nodeFieldValue_amount")));
        QCOMPARE(slider->height(), 24.0);
        QTest::mouseClick(m_view.get(), Qt::LeftButton, {}, slider->mapToScene(QPointF(slider->width() - 2, slider->height() / 2)).toPoint());
        QTRY_VERIFY(stored(controller(), mix, QStringLiteral("amount")).toDouble() > 0.9);
    }

    void smallChoicesAreSegmented()
    {
        const QString random = controller().addNode(QStringLiteral("math.random"), 0, 0);
        controller().setSelection({random});
        showPanel();
        QQuickItem *line = item(QStringLiteral("nodeFieldChoice_scope_line"));
        QVERIFY(line);
        QTest::mouseClick(m_view.get(), Qt::LeftButton, {}, line->mapToScene(QPointF(line->width() / 2, line->height() / 2)).toPoint());
        QTRY_COMPARE(stored(controller(), random, QStringLiteral("scope")).toString(), QStringLiteral("line"));
    }
};

QTEST_MAIN(NodeFormTests)
#include "node_form_tests.moc"
