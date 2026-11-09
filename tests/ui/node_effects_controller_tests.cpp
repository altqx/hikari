#include "node_catalogue_text.h"
#include "node_effects_controller.h"
#include "template_editor_controller.h"

#include "hikari/application/edit_session.h"
#include "hikari/application/node_catalogue.h"
#include "hikari/application/node_graph.h"
#include "hikari/application/node_graph_editing.h"
#include "hikari/core/ass_load.h"

#include <QFile>
#include <QSignalSpy>
#include <QtTest>

#include <cstring>
#include <optional>

using namespace hikari;
using ui::NodeEffectsController;
using ui::TemplateEditorController;

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

int presetIndex(const QString &file)
{
    const auto all = application::nodeGraphPresets();
    for (std::size_t i = 0; i < all.size(); ++i)
        if (QString::fromUtf8(all[i].templateFile.data(), qsizetype(all[i].templateFile.size())) == file)
            return int(i);
    return -1;
}

std::vector<std::string> historyNames(const application::EditSession &s)
{
    std::vector<std::string> out;
    for (const auto &step : s.history())
        out.push_back(step.name);
    return out;
}

class FortyTwo : public application::LuaNodePort {
public:
    std::expected<std::vector<std::vector<application::NodeValue>>, Failure> run(const Call &call) override
    {
        ++calls;
        return std::vector<std::vector<application::NodeValue>>(call.rows.size(), {42.0});
    }
    int calls = 0;
};

QVariantMap nodeNamed(const NodeEffectsController &c, const QString &id)
{
    for (const QVariant &n : c.nodes())
        if (n.toMap().value(QStringLiteral("id")).toString() == id)
            return n.toMap();
    return {};
}

}

class NodeEffectsControllerTests : public QObject {
    Q_OBJECT

    std::optional<application::EditSession> m_session;
    std::unique_ptr<NodeEffectsController> m_controller;

    NodeEffectsController &controller() { return *m_controller; }

private slots:
    void init()
    {
        m_session.emplace(fixture());
        m_controller = std::make_unique<NodeEffectsController>();
        m_controller->setSessionSource([this] { return &*m_session; });
        m_controller->setEditable(true);
        m_controller->setDelays(0, 60000);
        m_controller->setShown(true);
    }

    void everyCatalogueNodeHasTranslatableText()
    {
        const auto missing = ui::untranslatedCatalogueText(application::nodeCatalogue());
        QVERIFY2(missing.empty(), missing.empty() ? "" : missing.front().c_str());
    }

    void aFileWithoutGraphsOffersPresets()
    {
        QVERIFY(!controller().hasGraph());
        QCOMPARE(controller().graphs().size(), 0);
        QCOMPARE(controller().presets().size(), qsizetype(application::nodeGraphPresets().size()));
        for (const QVariant &p : controller().presets())
            QVERIFY(!p.toMap().value(QStringLiteral("title")).toString().isEmpty());
    }

    void aPresetIsOneUndoStepAndItsLinesPreview()
    {
        const std::size_t before = m_session->historySize();
        QVERIFY(controller().addPreset(presetIndex(QStringLiteral("10-palette.ass"))));
        QCOMPARE(m_session->historySize(), before + 1);
        QCOMPARE(historyNames(*m_session).back(), std::string("New graph"));
        QVERIFY(controller().hasGraph());
        QCOMPARE(controller().style(), QStringLiteral("Romaji"));
        QCOMPARE(controller().sources().size(), 3);
        QVERIFY(controller().problems().isEmpty());
        QTRY_VERIFY(!controller().previewLines().isEmpty());
        QVERIFY(controller().canApply());
    }

    void canvasEditsMergeIntoOneEditGraphStep()
    {
        QVERIFY(controller().newGraph());
        const std::size_t before = m_session->historySize();
        const QString random = controller().addNode(QStringLiteral("math.random"), 400, 40);
        QCOMPARE(random, QStringLiteral("random"));
        controller().moveNode(random, 420, 60);
        controller().setField(random, QStringLiteral("max"), 5);
        QCOMPARE(m_session->historySize(), before);
        controller().commit();
        QCOMPARE(m_session->historySize(), before + 1);
        QCOMPARE(historyNames(*m_session).back(), std::string("Edit graph"));
        QCOMPARE(nodeNamed(controller(), random).value(QStringLiteral("x")).toDouble(), 420.0);
        QVERIFY(m_session->undo());
        controller().refresh();
        QVERIFY(nodeNamed(controller(), random).isEmpty());
    }

    void refusedWiresSayWhy()
    {
        QVERIFY(controller().addPreset(presetIndex(QStringLiteral("10-palette.ass"))));
        QCOMPARE(controller().wireProblem(QStringLiteral("palette"), QStringLiteral("value"), QStringLiteral("place"),
                                          QStringLiteral("b")),
                 QStringLiteral("A Colour can’t connect to a Number input."));
        QCOMPARE(controller().wireProblem(QStringLiteral("fill"), QStringLiteral("tags"), QStringLiteral("fade"),
                                          QStringLiteral("before")),
                 QStringLiteral("That wire would make a loop."));
        const auto wires = controller().wires().size();
        QVERIFY(!controller().connectPorts(QStringLiteral("palette"), QStringLiteral("value"), QStringLiteral("place"),
                                           QStringLiteral("b")).isEmpty());
        QCOMPARE(controller().wires().size(), wires);
    }

    void addNodeFromADroppedWireWiresIt()
    {
        QVERIFY(controller().newGraph());
        QVERIFY(controller().nodeTypesTaking(QStringLiteral("Units")).contains(QStringLiteral("output.emit")));
        QVERIFY(!controller().nodeTypesTaking(QStringLiteral("Units")).contains(QStringLiteral("values.number")));
        const QString info = controller().addNode(QStringLiteral("text.join"), 300, 200, QStringLiteral("syl"),
                                                  QStringLiteral("units"));
        QVERIFY(std::ranges::any_of(controller().wires(), [&](const QVariant &w) {
            const auto m = w.toMap();
            return m.value(QStringLiteral("toNode")).toString() == info && m.value(QStringLiteral("fromNode")).toString() == "syl";
        }));
        QCOMPARE(controller().selection(), QStringList{info});
        QCOMPARE(controller().recent().front().toMap().value(QStringLiteral("type")).toString(), QStringLiteral("text.join"));
    }

    void theInspectorShowsTheLastRunsValues()
    {
        QVERIFY(controller().addPreset(presetIndex(QStringLiteral("10-palette.ass"))));
        controller().setSelection({QStringLiteral("palette")});
        QTRY_VERIFY(!controller().inspector().value(QStringLiteral("values")).toList().isEmpty());
        const QVariantMap inspector = controller().inspector();
        QCOMPARE(inspector.value(QStringLiteral("title")).toString(), QStringLiteral("Choose"));
        QCOMPARE(inspector.value(QStringLiteral("valuesLine")).toInt(), controller().sourceNumber());
        QVERIFY(!inspector.value(QStringLiteral("summary")).toString().isEmpty());
    }

    void applyIsOneStepAndReportsItsLines()
    {
        QVERIFY(controller().addPreset(presetIndex(QStringLiteral("01-classic-fill.ass"))));
        const std::size_t before = m_session->historySize();
        QVERIFY(controller().apply(false));
        QCOMPARE(m_session->historySize(), before + 1);
        QCOMPARE(historyNames(*m_session).back(), std::string("Apply node effect"));
        QCOMPARE(controller().lastApply().value(QStringLiteral("sources")).toInt(), 3);
        QVERIFY(controller().lastApply().value(QStringLiteral("lines")).toInt() > 0);
    }

    void presetNamesAreGraphNames()
    {
        for (const QVariant &p : controller().presets())
            QVERIFY2(controller().graphNameProblem(p.toMap().value(QStringLiteral("title")).toString()).isEmpty(),
                     qPrintable(p.toMap().value(QStringLiteral("title")).toString()));
    }

    void renameAndDeleteAreUndoSteps()
    {
        QVERIFY(controller().newGraph());
        QVERIFY(controller().renameGraph(QStringLiteral("glow")));
        QCOMPARE(controller().name(), QStringLiteral("glow"));
        QCOMPARE(historyNames(*m_session).back(), std::string("Rename graph"));
        QVERIFY(controller().deleteGraph());
        QVERIFY(!controller().hasGraph());
        QCOMPARE(historyNames(*m_session).back(), std::string("Delete graph"));
    }

    void luaWaitsForTheTemplateEditorsTrustChoice()
    {
        TemplateEditorController editor;
        editor.setSessionSource([this] { return &*m_session; });
        controller().shareTrustWith(&editor);
        QVERIFY(controller().newGraph());
        QVERIFY(controller().trusted());
        const QString lua = controller().addNode(QStringLiteral("lua.code"), 300, 200);
        controller().commit();
        QVERIFY(controller().hasLua());
        QVERIFY(!controller().trusted());
        QVERIFY(!editor.trusted());
        QVERIFY(!controller().canApply());

        QSignalSpy changed(&controller(), &NodeEffectsController::changed);
        controller().trustDocument();
        QVERIFY(editor.trusted());
        QVERIFY(controller().trusted());
        QVERIFY(!changed.isEmpty());

        controller().setField(lua, QStringLiteral("code"), QStringLiteral("out = 2"));
        controller().commit();
        QVERIFY(controller().trusted());
        QVERIFY(editor.trusted());
    }

    void aTrustedLuaNodeRunsInTheHelper()
    {
        TemplateEditorController editor;
        editor.setSessionSource([this] { return &*m_session; });
        controller().shareTrustWith(&editor);
        FortyTwo lua;
        int started = 0;
        controller().setLuaNodes([&]() -> application::LuaNodePort * {
            ++started;
            return &lua;
        });
        QVERIFY(controller().addPreset(presetIndex(QStringLiteral("10-palette.ass"))));
        QTRY_VERIFY(!controller().previewLines().isEmpty());
        QCOMPARE(started, 0);

        const QString node = controller().addNode(QStringLiteral("lua.code"), 300, 200);
        controller().setField(node, QStringLiteral("code"), QStringLiteral("out = 42"));
        controller().setField(node, QStringLiteral("outputs"),
                              QVariantList{QVariantMap{{QStringLiteral("name"), QStringLiteral("out")},
                                                       {QStringLiteral("type"), QStringLiteral("Number")}}});
        QVERIFY(controller().connectPorts(node, QStringLiteral("out"), QStringLiteral("place"), QStringLiteral("b")).isEmpty());
        controller().commit();
        QTest::qWait(50);
        QCOMPARE(lua.calls, 0);

        controller().trustDocument();
        QTRY_VERIFY(lua.calls > 0);
        QVERIFY(started > 0);
    }

    void groupingAndEnteringAGroup()
    {
        QVERIFY(controller().addPreset(presetIndex(QStringLiteral("10-palette.ass"))));
        controller().setSelection({QStringLiteral("place"), QStringLiteral("palette")});
        QVERIFY(controller().groupSelection());
        controller().commit();
        const QString group = controller().selected();
        QVERIFY(nodeNamed(controller(), group).value(QStringLiteral("group")).toBool());
        QVERIFY(controller().enterGroup(group));
        QCOMPARE(controller().path(), QStringList{group});
        QVERIFY(!nodeNamed(controller(), QStringLiteral("palette")).isEmpty());
        QVERIFY(controller().leaveGroup());
        QVERIFY(controller().path().isEmpty());
        QVERIFY(controller().problems().isEmpty());
    }
};

QTEST_MAIN(NodeEffectsControllerTests)
#include "node_effects_controller_tests.moc"
