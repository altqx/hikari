#include "node_catalogue_text.h"
#include "node_effects_controller.h"
#include "template_editor_controller.h"

#include "hikari/application/edit_session.h"
#include "hikari/application/node_catalogue.h"
#include "hikari/application/node_graph.h"
#include "hikari/application/node_graph_editing.h"
#include "hikari/application/plugin_content.h"
#include "hikari/application/typesetting_graphs.h"
#include "hikari/core/ass_load.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
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

std::vector<core::LineId> lineIds(const application::EditSession &s)
{
    std::vector<core::LineId> out;
    for (const core::LineRecord *l : s.document().lines())
        out.push_back(l->id);
    return out;
}

const core::LineRecord *lineOf(const core::Document &doc, core::LineId id)
{
    for (const core::LineRecord *l : doc.lines())
        if (l->id == id)
            return l;
    return nullptr;
}

bool setLineText(application::EditSession &s, core::LineId id, std::u8string text)
{
    application::Command command;
    command.name = "Edit Line";
    command.expectedRevision = s.revision();
    command.apply = [&](core::Document &d) {
        return d.editLine(id, [&](core::LineRecord &l) { l.text = text; });
    };
    return s.run(command).has_value();
}

int shakeIndex()
{
    const auto all = application::typesettingGraphs();
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].id == "shake")
            return int(i);
    return -1;
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

    void exportAddsTemplateLinesOrNamesTheNodesThatBlockIt()
    {
        QVERIFY(controller().addPreset(presetIndex(QStringLiteral("10-palette.ass"))));
        const std::size_t before = m_session->historySize();
        QVERIFY(controller().exportAsTemplate(false).isEmpty());
        QCOMPARE(m_session->historySize(), before + 1);
        QCOMPARE(historyNames(*m_session).back(), std::string("Export as template lines"));
        const auto templates = std::ranges::count_if(m_session->document().lines(), [](const core::LineRecord *l) {
            return l->comment && l->effect.starts_with(u8"template ");
        });
        QCOMPARE(templates, 2);

        const QString random = controller().addNode(QStringLiteral("math.random"), 40, 40);
        QVERIFY(controller().connectPorts(random, QStringLiteral("value"), QStringLiteral("place"), QStringLiteral("b")).isEmpty());
        const std::size_t edited = m_session->historySize();
        const QVariantList blockers = controller().exportAsTemplate(false);
        QCOMPARE(blockers.size(), 1);
        QCOMPARE(blockers.front().toMap().value(QStringLiteral("node")).toString(), random);
        QCOMPARE(blockers.front().toMap().value(QStringLiteral("viewNode")).toString(), random);
        QCOMPARE(m_session->historySize(), edited + 1);
        QCOMPARE(historyNames(*m_session).back(), std::string("Edit graph"));
    }

    void exportRemembersItsFormatAndAKaraOKExportSetsTheModeLine()
    {
        application::MemorySettingsStorage storage;
        application::Settings settings(storage);
        controller().setSettings(&settings);
        QVERIFY(controller().exportKaraOK());
        QVERIFY(controller().addPreset(presetIndex(QStringLiteral("10-palette.ass"))));
        QVERIFY(controller().exportAsTemplate(false).isEmpty());
        QVERIFY(!controller().exportKaraOK());
        QCOMPARE(settings.text("nodeEffects.exportFormat"), std::string("stock"));
        QVERIFY(!m_session->document().scriptInfo(u8"Karaoke Templater"));
        QVERIFY(controller().exportAsTemplate(true).isEmpty());
        QVERIFY(controller().exportKaraOK());
        QCOMPARE(m_session->document().scriptInfo(u8"Karaoke Templater"), std::optional<std::u8string>(u8"karaOK"));
        controller().setSettings(nullptr);
    }

    void presetNamesAreGraphNames()
    {
        for (const QVariant &p : controller().presets())
            QVERIFY2(controller().graphNameProblem(p.toMap().value(QStringLiteral("title")).toString()).isEmpty(),
                     qPrintable(p.toMap().value(QStringLiteral("title")).toString()));
    }

    void aPluginsGraphsGroupByPluginAndAddAsOneStep()
    {
        QTemporaryDir dir;
        const QByteArray json = QByteArray::fromStdString(application::graphJson(application::NodeGraph{}).dump());
        QVERIFY(application::parseGraph(json.toStdString()));
        const auto file = [&](const char *name) {
            QFile f(dir.filePath(QLatin1String(name)));
            if (f.open(QIODevice::WriteOnly))
                f.write(json);
            return std::filesystem::path(f.fileName().toStdU16String());
        };
        application::setPluginContent({.graphs = {{"com.example.kit", u8"Kit", u8"Signs", file("signs.json")},
                                                  {"com.example.kit", u8"Kit", u8"Glow", file("glow.json")},
                                                  {"com.example.more", u8"More", u8"Sparkle", file("sparkle.json")}}});
        QSignalSpy changed(&controller(), &NodeEffectsController::libraryChanged);
        controller().pluginContentChanged();
        QCOMPARE(changed.count(), 1);
        const QVariantList groups = controller().pluginLibrary();
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups[0].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Kit"));
        const QVariantList kit = groups[0].toMap().value(QStringLiteral("graphs")).toList();
        QCOMPARE(kit.size(), 2);
        QCOMPARE(kit[1].toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Glow"));
        QCOMPARE(groups[1].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("More"));

        const QString signs = kit[0].toMap().value(QStringLiteral("file")).toString();
        const std::size_t before = m_session->historySize();
        QVERIFY(controller().addFromPlugin(signs));
        QCOMPARE(m_session->historySize(), before + 1);
        QCOMPARE(controller().name(), QStringLiteral("Signs"));
        QVERIFY(controller().addFromPlugin(signs));
        QCOMPARE(controller().name(), QStringLiteral("Signs 2"));
        QVERIFY(!controller().addFromPlugin(dir.filePath(QStringLiteral("elsewhere.json"))));
        application::setPluginContent({});
        QVERIFY(!controller().addFromPlugin(signs));
        QVERIFY(controller().pluginLibrary().isEmpty());
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

    void typesettingGraphsRunOnTheTargetLines()
    {
        QCOMPARE(controller().typesettingPresets().size(), qsizetype(application::typesettingGraphs().size()));
        std::vector<core::LineId> dialogue;
        for (const core::LineRecord *l : m_session->document().lines())
            dialogue.push_back(l->id);
        controller().setTargetLines({dialogue[0], dialogue[2]});
        QVERIFY(controller().addTypesettingGraph(1));
        QVERIFY(controller().typesetting());
        QCOMPARE(controller().sources().size(), 2);
        QCOMPARE(controller().sourceSelection().size(), 2);
        QVERIFY(controller().problems().isEmpty());
        QTRY_VERIFY(!controller().previewLines().isEmpty());
        const std::size_t before = m_session->historySize();
        QVERIFY(controller().apply(true));
        QCOMPARE(m_session->historySize(), before + 1);
        QCOMPARE(historyNames(*m_session).back(), std::string("Apply node effect"));
        QCOMPARE(controller().lastApply().value(QStringLiteral("sources")).toInt(), 2);
        const auto lines = m_session->document().lines();
        const auto at = std::ranges::find_if(lines, [&](const core::LineRecord *l) { return l->id == dialogue[2]; });
        QVERIFY(at != lines.end() && (*at)->comment);
        QVERIFY(application::isGeneratedBy(**(at + 1), u8"Typewriter"));
    }

    void anUndoUnderAPendingEditIsNotOverwritten()
    {
        QVERIFY(controller().newGraph());
        const QString random = controller().addNode(QStringLiteral("math.random"), 400, 40);
        controller().commit();
        const std::size_t before = m_session->historyCursor();
        controller().moveNode(random, 600, 300);
        QVERIFY(m_session->undo());
        controller().refresh();
        QVERIFY(nodeNamed(controller(), random).isEmpty());
        controller().commit();
        QCOMPARE(m_session->historyCursor(), before - 1);
        QVERIFY(m_session->canRedo());
        controller().refresh();
        QVERIFY(nodeNamed(controller(), random).isEmpty());
    }

    void aCommitBeforeUndoMakesOneUndoTakeBackTheLastGraphEdit()
    {
        QVERIFY(controller().newGraph());
        const QString random = controller().addNode(QStringLiteral("math.random"), 400, 40);
        controller().commit();
        controller().moveNode(random, 600, 300);
        controller().commit();
        QVERIFY(m_session->undo());
        controller().refresh();
        QCOMPARE(nodeNamed(controller(), random).value(QStringLiteral("x")).toDouble(), 400.0);
    }

    void aLineEditUnderAPendingEditIsNotOverwritten()
    {
        QVERIFY(controller().newGraph());
        const QString random = controller().addNode(QStringLiteral("math.random"), 400, 40);
        controller().commit();
        const core::LineId graphLine{controller().current()};
        controller().moveNode(random, 600, 300);
        const std::u8string typed = lineOf(m_session->document(), graphLine)->text + u8" ";
        QVERIFY(setLineText(*m_session, graphLine, typed));
        controller().commit();
        QCOMPARE(lineOf(m_session->document(), graphLine)->text, typed);
        QCOMPARE(historyNames(*m_session).back(), std::string("Edit Line"));
    }

    void anEditElsewhereKeepsThePendingEdit()
    {
        const core::LineId other = lineIds(*m_session).front();
        QVERIFY(controller().newGraph());
        const QString random = controller().addNode(QStringLiteral("math.random"), 400, 40);
        controller().commit();
        controller().moveNode(random, 600, 300);
        QVERIFY(setLineText(*m_session, other, u8"hello"));
        controller().refresh();
        controller().commit();
        QCOMPARE(lineOf(m_session->document(), other)->text, std::u8string(u8"hello"));
        QCOMPARE(historyNames(*m_session).back(), std::string("Edit graph"));
        QCOMPARE(nodeNamed(controller(), random).value(QStringLiteral("x")).toDouble(), 600.0);
    }

    void aTypesettingGraphPreviewsTheLinesItWasAppliedTo()
    {
        const auto dialogue = lineIds(*m_session);
        controller().setTargetLines({dialogue[1]});
        QVERIFY(controller().addTypesettingGraph(shakeIndex()));
        QVERIFY(controller().apply(false));

        NodeEffectsController reopened;
        reopened.setSessionSource([this] { return &*m_session; });
        reopened.setEditable(true);
        reopened.setDelays(0, 60000);
        reopened.setShown(true);
        QVERIFY(reopened.typesetting());
        QCOMPARE(reopened.source(), qulonglong(dialogue[1].value));
        QVERIFY(reopened.previewBlocked().isEmpty());
        QTRY_VERIFY(!reopened.previewLines().isEmpty());
        QVERIFY(reopened.videoPreview());
    }

    void thePreviewFollowsTheGridsLineWhenItIsASource()
    {
        const auto dialogue = lineIds(*m_session);
        controller().setTargetLines({dialogue[0], dialogue[3]});
        QVERIFY(controller().addTypesettingGraph(shakeIndex()));
        QCOMPARE(controller().source(), qulonglong(dialogue[0].value));
        QSignalSpy preview(&controller(), &NodeEffectsController::previewChanged);
        m_session->setSelection({dialogue[3], {dialogue[3]}, {}, {}});
        controller().refresh();
        QCOMPARE(controller().source(), qulonglong(dialogue[3].value));
        QVERIFY(!preview.isEmpty());
        m_session->setSelection({dialogue[2], {dialogue[2]}, {}, {}});
        controller().refresh();
        QCOMPARE(controller().source(), qulonglong(dialogue[3].value));
    }

    void withNoChosenLinesATypesettingGraphPreviewsOnTheGridsLine()
    {
        const auto dialogue = lineIds(*m_session);
        m_session->setSelection({dialogue[2], {dialogue[2]}, {}, {}});
        QVERIFY(controller().addTypesettingGraph(shakeIndex()));
        QVERIFY(controller().sources().isEmpty());
        QCOMPARE(controller().source(), qulonglong(dialogue[2].value));
        QTRY_VERIFY(controller().videoPreview());
        QCOMPARE(controller().previewBlocked(), QStringLiteral("noSource"));
        QVERIFY(!controller().canApply());
        m_session->setSelection({dialogue[0], {dialogue[0]}, {}, {}});
        controller().refresh();
        QCOMPARE(controller().source(), qulonglong(dialogue[0].value));
        TemplateEditorController editor;
        editor.setSessionSource([this] { return &*m_session; });
        controller().shareTrustWith(&editor);
        controller().addNode(QStringLiteral("lua.code"), 300, 200);
        controller().commit();
        QCOMPARE(controller().previewBlocked(), QStringLiteral("untrusted"));
    }

    void aValueChangeRunsThePreviewAgain()
    {
        const auto dialogue = lineIds(*m_session);
        controller().setTargetLines({dialogue[0]});
        QVERIFY(controller().addTypesettingGraph(shakeIndex()));
        QTRY_VERIFY(!controller().previewLines().isEmpty());
        const qsizetype before = controller().previewLines().size();
        QSignalSpy video(&controller(), &NodeEffectsController::videoChanged);
        controller().setField(QStringLiteral("step"), QStringLiteral("value"), 1000);
        QTRY_VERIFY(controller().previewLines().size() < before);
        QVERIFY(!video.isEmpty());
        QVERIFY(!controller().previewKept());
    }

    void theVideoShowsTheSourceAsApplyLeavesIt()
    {
        const auto dialogue = lineIds(*m_session);
        controller().setTargetLines({dialogue[0]});
        QVERIFY(controller().addTypesettingGraph(shakeIndex()));
        QTRY_VERIFY(controller().videoPreview());
        const auto shown = controller().videoDocument(m_session->document());
        QVERIFY(shown);
        QVERIFY(lineOf(*shown, dialogue[0])->comment);
        QVERIFY(!lineOf(*shown, dialogue[1])->comment);
    }

    void theBadgeSaysWhyThereIsNoPreview()
    {
        const auto dialogue = lineIds(*m_session);
        QVERIFY(controller().addTypesettingGraph(shakeIndex()));
        QCOMPARE(controller().source(), qulonglong(0));
        QCOMPARE(controller().previewBlocked(), QStringLiteral("noSource"));
        QVERIFY(!controller().videoPreview());

        controller().setTargetLines({dialogue[0]});
        controller().disconnectInput(QStringLiteral("emit"), QStringLiteral("units"));
        controller().commit();
        QTRY_COMPARE(controller().previewBlocked(), QStringLiteral("problems"));

        TemplateEditorController editor;
        editor.setSessionSource([this] { return &*m_session; });
        controller().shareTrustWith(&editor);
        controller().addNode(QStringLiteral("lua.code"), 300, 200);
        controller().commit();
        QCOMPARE(controller().previewBlocked(), QStringLiteral("untrusted"));

        controller().setPreviewOnVideo(false);
        QVERIFY(controller().previewBlocked().isEmpty());
    }

    void untrustedLuaListsNoPreviewLines()
    {
        const auto dialogue = lineIds(*m_session);
        controller().setTargetLines({dialogue[0]});
        QVERIFY(controller().addTypesettingGraph(shakeIndex()));
        QTRY_VERIFY(!controller().previewLines().isEmpty());

        TemplateEditorController editor;
        editor.setSessionSource([this] { return &*m_session; });
        controller().shareTrustWith(&editor);
        controller().addNode(QStringLiteral("lua.code"), 300, 200);
        controller().commit();
        QCOMPARE(controller().previewBlocked(), QStringLiteral("untrusted"));
        QVERIFY(controller().previewLines().isEmpty());
        QVERIFY(!controller().previewKept());
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
