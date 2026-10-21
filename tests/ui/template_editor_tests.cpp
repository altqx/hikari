// KE2: the Template editor's controller over the bundled karaoke templater.
// Edits go into the template Line, one undo step per pause; each edit
// previews the templater on the current karaoke Line without changing the
// Document; problems are placed in the text; Apply is the stock run, one
// undo step; the video draws the preview in place of the Line's Lines. The
// panel (TemplateEditor.qml) shows and edits it.
#include "template_editor_controller.h"

#include "hikari/application/edit_session.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/application/template_preview.h"
#include "hikari/backends/karaoke_preview_runner.h"
#include "hikari/backends/lua_script_host.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/editor_syntax.h"

#include <QFile>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QtTest>

#include <cstring>
#include <memory>
#include <optional>

using namespace hikari;
using application::HostService;
using application::HostServiceReply;
using application::HostServiceRequest;
using backends::LuaScriptHost;
using ui::TemplateEditorController;

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

namespace {

core::Document loadFile(const QString &name)
{
    QFile f(QStringLiteral(HIKARI_KARAOKE_CORPUS) + QLatin1Char('/') + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    std::vector<std::byte> bytes(static_cast<std::size_t>(data.size()));
    std::memcpy(bytes.data(), data.constData(), bytes.size());
    return core::loadAss(bytes).document;
}

HostServiceReply textExtents(const HostServiceRequest &r)
{
    const auto number = [&](std::size_t i) { return i < r.style.size() ? std::stod(r.style[i]) : 0.0; };
    const double size = number(2), scaleX = number(11) / 100, scaleY = number(12) / 100, spacing = number(13);
    double ems = 0;
    int chars = 0;
    for (const char32_t c : QString::fromStdString(r.strings.at(0)).toUcs4()) {
        ems += c >= 0x2E80 ? 1.0 : 0.55;
        ++chars;
    }
    HostServiceReply out;
    out.numbers = {ems * size * scaleX + spacing * chars, size * scaleY, size * 0.21, 0};
    return out;
}

void services(const HostServiceRequest &r, LuaScriptHost::ServiceReply reply)
{
    reply(r.service == HostService::TextExtents ? textExtents(r) : HostServiceReply::unavailable());
}

std::string bytesOf(const core::Document &doc)
{
    const auto b = core::encodeAss(doc);
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

std::string lineText(const core::LineRecord &l)
{
    const auto t = core::legacy::assLineText(l);
    return std::string(t.begin(), t.end());
}

// The templater as the app gives it: previews in a runner of their own, and
// Apply as the Automation menu runs the stock macro on the session.
class Templater : public application::KaraokeTemplaterPort {
public:
    explicit Templater(application::EditSession &session) : m_session(session)
    {
        runner.setServiceHandler(services);
        runner.setScript(QStringLiteral(HIKARI_KARAOKE_TEMPLATER));
    }
    bool available() const override { return runner.ready(); }
    void preview(const application::MacroSnapshot &snapshot, PreviewDone done) override
    {
        ++previews;
        runner.run(snapshot, std::move(done));
    }
    void cancelPreview() override { runner.cancel(); }
    bool apply(bool selectedOnly) override
    {
        ++applies;
        lastSelectedOnly = selectedOnly;
        LuaScriptHost host(QStringLiteral(HIKARI_LUA_HELPER), QStringLiteral(HIKARI_KARAOKE_TEMPLATER),
                           QStringLiteral(HIKARI_LUA_INCLUDE));
        host.setServiceHandler(services);
        std::optional<LuaScriptHost::RunOutcome> outcome;
        QObject::connect(&host, &LuaScriptHost::finished,
                         [&](LuaScriptHost::RunOutcome o, const QString &) { outcome = o; });
        host.load();
        if (!QTest::qWaitFor([&] { return host.state() == LuaScriptHost::State::Ready; }, 20'000))
            return false;
        const auto snapshot = application::snapshotForMacro(m_session);
        if (!snapshot || !host.run(selectedOnly ? 1 : 0, *snapshot, true) ||
            !QTest::qWaitFor([&] { return outcome.has_value(); }, 20'000) ||
            *outcome != LuaScriptHost::RunOutcome::Ok)
            return false;
        if (!application::applyMacroResult(m_session, *snapshot, *host.lastResult(), "Apply karaoke template"))
            return false;
        report = host.lastResult()->report;
        return true;
    }

    backends::KaraokePreviewRunner runner{QStringLiteral(HIKARI_LUA_HELPER), QStringLiteral(HIKARI_LUA_INCLUDE)};
    std::string report;
    int previews = 0;
    int applies = 0;
    bool lastSelectedOnly = false;

private:
    application::EditSession &m_session;
};

} // namespace

class TemplateEditorTests : public QObject {
    Q_OBJECT

    std::unique_ptr<application::EditSession> session;
    std::unique_ptr<Templater> templater;
    std::unique_ptr<TemplateEditorController> editor;
    std::unique_ptr<QQuickView> view;

    void open(const QString &name)
    {
        view.reset();
        editor.reset();
        templater.reset();
        session = std::make_unique<application::EditSession>(loadFile(name));
        templater = std::make_unique<Templater>(*session);
        QVERIFY(QTest::qWaitFor([&] { return templater->available(); }, 20'000));
        editor = std::make_unique<TemplateEditorController>();
        editor->setDelays(10, 60);
        editor->setEditable(true);
        editor->setTemplater(templater.get());
        editor->setSessionSource([this] { return session.get(); });
        QObject::connect(editor.get(), &TemplateEditorController::documentEdited, editor.get(),
                         [this] { editor->refresh(); });
    }

    const core::LineRecord *lineAt(std::size_t i) const { return session->document().lines()[i]; }

    std::optional<core::LineId> firstKaraoke() const
    {
        for (const auto *l : session->document().lines())
            if (application::isKaraokeLine(*l))
                return l->id;
        return std::nullopt;
    }

    void activate(core::LineId id)
    {
        application::Selection s;
        s.active = id;
        s.selected = {id};
        session->setSelection(s);
        editor->refresh();
    }

    bool previewCame()
    {
        return QTest::qWaitFor([&] { return editor->hasPreview(); }, 20'000);
    }

    // The panel over the editor, `width` wide.
    void showPanel(int width = 640)
    {
        view = std::make_unique<QQuickView>();
        view->setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(editor.get())}});
        view->loadFromModule("Hikari.Ui", "TemplateEditor");
        QVERIFY(view->rootObject());
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->resize(width, 1000);
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));
    }
    // By objectName: declared objects are the root's children, a Repeater's
    // delegates and a popup's contents are found through the items.
    static QObject *findIn(QQuickItem *at, const QString &name)
    {
        if (!at)
            return nullptr;
        if (at->objectName() == name)
            return at;
        if (QObject *o = at->findChild<QObject *>(name, Qt::FindDirectChildrenOnly))
            return o;
        for (QQuickItem *child : at->childItems())
            if (QObject *found = findIn(child, name))
                return found;
        return nullptr;
    }
    QObject *object(const QString &name) const
    {
        if (QObject *o = view->rootObject()->findChild<QObject *>(name))
            return o;
        return findIn(view->contentItem(), name);
    }
    QQuickItem *item(const QString &name) const { return qobject_cast<QQuickItem *>(object(name)); }
    QQuickItem *row(const char *prefix, qsizetype index) const
    {
        return item(QLatin1String(prefix) + QString::number(index));
    }
    void click(QQuickItem *at)
    {
        QVERIFY(at);
        QTest::mouseClick(view.get(), Qt::LeftButton, {},
                          at->mapToScene(QPointF(at->width() / 2, at->height() / 2)).toPoint());
    }
    // Types `text` into the focused item, one key at a time.
    void type(const QString &text)
    {
        for (const QChar c : text)
            QTest::keyClick(view.get(), c.toLatin1());
    }
    QString textOf(const QString &name) const
    {
        const QObject *o = object(name);
        return o ? o->property("text").toString() : QString();
    }

    QStringList previewTexts() const
    {
        QStringList out;
        for (const QVariant &v : editor->previewLines())
            out << v.toMap().value(QStringLiteral("text")).toString();
        return out;
    }

private slots:
    void cleanup()
    {
        view.reset();
    }

    void listsTheTemplateAndCodeLinesInGridOrder()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const QVariantList rows = editor->templates();
        QCOMPARE(rows.size(), editor->templateCount() + editor->codeCount());
        QVERIFY(editor->templateCount() > 0);
        QVERIFY(editor->karaokeCount() > 0);
        int last = 0;
        for (const QVariant &v : rows) {
            const QVariantMap row = v.toMap();
            QVERIFY(row.value(QStringLiteral("number")).toInt() > last);
            last = row.value(QStringLiteral("number")).toInt();
            QVERIFY(row.value(QStringLiteral("effect")).toString().startsWith(row.value(QStringLiteral("kind")).toString(),
                                                                             Qt::CaseInsensitive));
            QCOMPARE(row.value(QStringLiteral("changed")).toBool(), false);
        }
        QCOMPARE(editor->current(), rows.front().toMap().value(QStringLiteral("id")).toULongLong());
        QVERIFY(!editor->text().isEmpty());
    }

    void editsReachTheTemplateLineAsOneStepAfterAPause()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const auto id = core::LineId{editor->current()};
        const QString before = editor->text();
        const std::size_t steps = session->historySize();
        editor->setText(before + QStringLiteral("{"));
        editor->setText(before + QStringLiteral("{\\b"));
        editor->setText(before + QStringLiteral("{\\b1}"));
        QCOMPARE(editor->text(), before + QStringLiteral("{\\b1}"));
        QCOMPARE(session->historySize(), steps); // nothing in the Document while typing
        QVERIFY(editor->templates().front().toMap().value(QStringLiteral("changed")).toBool());
        QVERIFY(QTest::qWaitFor([&] { return session->historySize() == steps + 1; }, 5'000));
        const auto *l = [&]() -> const core::LineRecord * {
            for (const auto *x : session->document().lines())
                if (x->id == id)
                    return x;
            return nullptr;
        }();
        QVERIFY(l);
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(l->text.data()), l->text.size()),
                 before + QStringLiteral("{\\b1}"));
        QVERIFY(session->undo());
        editor->refresh();
        QCOMPARE(editor->text(), before);
    }

    void eachEditPreviewsTheCurrentLineWithoutChangingTheDocument()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const auto source = firstKaraoke();
        QVERIFY(source);
        activate(*source);
        editor->setShown(true);
        QVERIFY(previewCame());
        QCOMPARE(editor->source(), source->value);
        QVERIFY(!editor->previewLines().isEmpty());
        // The preview is the Lines Apply writes for that Line.
        application::EditSession copy(session->document());
        Templater whole(copy);
        QVERIFY(QTest::qWaitFor([&] { return whole.available(); }, 20'000));
        QVERIFY(whole.apply(false));
        QStringList applied;
        for (const auto *l : copy.document().lines())
            if (l->generatedFrom == *source)
                applied << QString::fromStdString(lineText(*l)).section(QLatin1Char(','), 9);
        QCOMPARE(previewTexts(), applied);
        // An edit gives a new preview; every Line but the template Line stays
        // as it was.
        const auto others = [&] {
            std::vector<std::string> out;
            for (const auto *l : session->document().lines())
                if (l->id.value != editor->current())
                    out.push_back(lineText(*l));
            return out;
        };
        const auto before = others();
        const QStringList first = previewTexts();
        editor->setText(editor->text() + QStringLiteral("{\\bord9}"));
        QVERIFY(QTest::qWaitFor([&] { return previewTexts() != first; }, 20'000));
        QVERIFY(previewTexts().front().contains(QStringLiteral("\\bord9")) ||
                previewTexts().back().contains(QStringLiteral("\\bord9")));
        QVERIFY(others() == before);
        QVERIFY(editor->previewMs() >= 0);
    }

    void problemsArePlacedInTheTextAndKeepTheLastPreview()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(previewCame());
        const QStringList good = previewTexts();
        const QString text = editor->text();
        const QString broken = QStringLiteral("{\\fscx!nosuch.field!}") + text;
        editor->setText(broken);
        QVERIFY(QTest::qWaitFor([&] { return editor->hasProblems(); }, 20'000));
        QVERIFY(editor->problem().startsWith(QStringLiteral("Runtime error in template expression")));
        QCOMPARE(editor->problemRange(), (QVariantList{6, 20}));
        QVERIFY(editor->previewKept());
        QCOMPARE(previewTexts(), good);
        QVERIFY(!editor->canApply());
        bool marked = false;
        for (const QVariant &v : editor->templates())
            marked |= v.toMap().value(QStringLiteral("problem")).toBool();
        QVERIFY(marked);
        editor->setText(text);
        QVERIFY(QTest::qWaitFor([&] { return !editor->hasProblems(); }, 20'000));
        QVERIFY(!editor->previewKept());
        QVERIFY(QTest::qWaitFor([&] { return editor->canApply(); }, 5'000));
    }

    void applyIsTheStockRunAndOneUndoStep()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        editor->setShown(true);
        const std::string before = bytesOf(session->document());
        application::EditSession copy(session->document());
        Templater whole(copy);
        QVERIFY(QTest::qWaitFor([&] { return whole.available(); }, 20'000));
        QVERIFY(whole.apply(false));
        QVERIFY(editor->canApply());
        QVERIFY(editor->apply(false));
        editor->applied(templater->report);
        editor->refresh();
        QCOMPARE(bytesOf(session->document()), bytesOf(copy.document()));
        QCOMPARE(editor->lastApply().value(QStringLiteral("lines")).toInt() > 0, true);
        QVERIFY(editor->templates().front().toMap().value(QStringLiteral("generated")).toInt() >= 0);
        QVERIFY(session->undo());
        QCOMPARE(bytesOf(session->document()), before);
    }

    void aPendingEditGoesInBeforeApply()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        editor->setDelays(10, 60'000);
        const std::size_t steps = session->historySize();
        editor->setText(editor->text() + QStringLiteral("{\\be1}"));
        QVERIFY(editor->apply(false));
        QCOMPARE(session->historySize(), steps + 2); // the edit, then Apply
    }

    void theVideoDrawsThePreviewInPlaceOfTheLinesOwn()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        // Apply once so every generated Line is marked with its source.
        editor->setShown(true);
        QVERIFY(editor->apply(false));
        editor->refresh();
        const auto source = firstKaraoke();
        QVERIFY(source);
        activate(*source);
        editor->setText(editor->text() + QStringLiteral("{\\bord7}"));
        QVERIFY(QTest::qWaitFor([&] { return previewTexts().join(QString()).contains(QStringLiteral("bord7")); },
                                20'000));
        const auto *l = [&]() -> const core::LineRecord * {
            for (const auto *x : session->document().lines())
                if (x->id == *source)
                    return x;
            return nullptr;
        }();
        const std::int64_t mid = (l->start.value.microseconds() + l->end.value.microseconds()) / 2000;
        const auto shown = editor->videoDocument(session->document(), mid, false);
        QVERIFY(shown);
        int preview = 0;
        for (const auto *x : shown->lines()) {
            QVERIFY(x->generatedFrom == *source || x->generatedFrom.value == 0 ||
                    x->generatedFrom != *source);
            QVERIFY(!x->comment);
            preview += QString::fromStdString(lineText(*x)).contains(QStringLiteral("bord7"));
        }
        QVERIFY(preview > 0);
        for (const auto *x : session->document().lines())
            if (x->generatedFrom == *source)
                for (const auto *y : shown->lines())
                    QVERIFY(y->id != x->id);
        editor->setPreviewOnVideo(false);
        QVERIFY(!editor->videoDocument(session->document(), mid, false));
        editor->setPreviewOnVideo(true);
        editor->setShown(false);
        QVERIFY(!editor->videoDocument(session->document(), mid, false));
    }

    void aFileWithoutMarksLeavesOutTheLinesOldLinesFoundByMatching()
    {
        open(QStringLiteral("stock-tutorials.out.ass"));
        const auto source = firstKaraoke();
        QVERIFY(source);
        activate(*source);
        editor->setShown(true);
        QVERIFY(previewCame());
        const auto *l = [&]() -> const core::LineRecord * {
            for (const auto *x : session->document().lines())
                if (x->id == *source)
                    return x;
            return nullptr;
        }();
        const std::int64_t mid = (l->start.value.microseconds() + l->end.value.microseconds()) / 2000;
        const auto shown = editor->videoDocument(session->document(), mid, false);
        QVERIFY(shown);
        // The frame holds the preview's Lines and no second copy of them.
        QStringList texts;
        for (const auto *x : shown->lines())
            texts << QString::fromStdString(lineText(*x)).section(QLatin1Char(','), 9);
        for (const QString &t : previewTexts())
            if (texts.contains(t))
                QCOMPARE(texts.count(t), previewTexts().count(t));
    }

    void theCurrentTemplatesOwnLinesAreMarked()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(previewCame());
        QVERIFY(QTest::qWaitFor(
            [&] {
                for (const QVariant &v : editor->previewLines())
                    if (v.toMap().value(QStringLiteral("own")).toBool())
                        return true;
                return false;
            },
            20'000));
    }

    void dollarListsTheVariablesWithTheirValues()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        const QString text = QStringLiteral("{\\pos($s");
        QVariantList list = editor->completions(text, static_cast<int>(text.size()));
        QVERIFY(list.size() > 5);
        QCOMPARE(list.front().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("$sstart"));
        QCOMPARE(list.front().toMap().value(QStringLiteral("from")).toInt(), 6);
        for (const QVariant &v : list)
            QVERIFY(v.toMap().value(QStringLiteral("name")).toString().startsWith(QStringLiteral("$s")));
        editor->requestValues();
        QVERIFY(QTest::qWaitFor([&] { return !editor->values().isEmpty(); }, 20'000));
        bool number = false;
        editor->values().value(QStringLiteral("swidth")).toString().toDouble(&number);
        QVERIFY(number);
        list = editor->completions(text, static_cast<int>(text.size()));
        QVERIFY(!list.front().toMap().value(QStringLiteral("value")).toString().isEmpty());
        // `!` lists the template environment inside an expression only.
        const QString expression = QStringLiteral("{\\fscx!re");
        list = editor->completions(expression, static_cast<int>(expression.size()));
        QStringList names;
        for (const QVariant &v : list)
            names << v.toMap().value(QStringLiteral("name")).toString();
        QCOMPARE(names, (QStringList{QStringLiteral("retime"), QStringLiteral("relayer"), QStringLiteral("restyle"),
                                     QStringLiteral("remember"), QStringLiteral("remember_if"),
                                     QStringLiteral("recall")}));
        const QString closed = QStringLiteral("{\\fscx!1!re");
        QVERIFY(editor->completions(closed, static_cast<int>(closed.size())).isEmpty());
    }

    void theEffectPickerWritesStockSyntax()
    {
        TemplateEditorController e;
        const QVariantMap parts = e.effectParts(QStringLiteral("template syl noblank loop 3"));
        QCOMPARE(parts.value(QStringLiteral("kind")).toString(), QStringLiteral("template"));
        QCOMPARE(parts.value(QStringLiteral("className")).toString(), QStringLiteral("syl"));
        QCOMPARE(parts.value(QStringLiteral("modifiers")).toStringList(),
                 (QStringList{QStringLiteral("noblank"), QStringLiteral("loop"), QStringLiteral("3")}));
        QCOMPARE(e.effectWithClass(QStringLiteral("template syl noblank"), QStringLiteral("char")),
                 QStringLiteral("template char noblank"));
        QCOMPARE(e.effectWithClass(QStringLiteral("template line named keeptags"), QStringLiteral("pre-line")),
                 QStringLiteral("template pre-line named keeptags"));
        QCOMPARE(e.effectWithModifier(QStringLiteral("template syl noblank loop 3"), QStringLiteral("loop"), false),
                 QStringLiteral("template syl noblank"));
        QCOMPARE(e.effectWithModifier(QStringLiteral("template syl"), QStringLiteral("loop 2"), true),
                 QStringLiteral("template syl loop 2"));
        QCOMPARE(e.effectWithModifier(QStringLiteral("code once"), QStringLiteral("all"), true),
                 QStringLiteral("code once all"));
        QCOMPARE(e.effectParts(QStringLiteral("template line named")).value(QStringLiteral("name")).toString(),
                 QStringLiteral("named"));
    }

    void newPutsATemplateAboveTheFirstKaraokeLine()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const auto karaoke = firstKaraoke();
        const std::size_t steps = session->historySize();
        QVERIFY(editor->insertTemplate(QStringLiteral("template syl")));
        QCOMPARE(session->historySize(), steps + 1);
        const auto lines = session->document().lines();
        std::size_t at = 0;
        for (std::size_t k = 0; k < lines.size(); ++k)
            if (lines[k]->id == *karaoke)
                at = k;
        QVERIFY(at > 0);
        QCOMPARE(lines[at - 1]->id.value, editor->current());
        QVERIFY(lines[at - 1]->comment);
        QVERIFY(lines[at - 1]->effect == u8"template syl");
        QVERIFY(lines[at - 1]->style == lines[at]->style);
        QCOMPARE(editor->text(), QString());
    }

    void theCurrentTemplateFollowsTheActiveTemplateLine()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const QVariantList rows = editor->templates();
        QVERIFY(rows.size() > 1);
        const auto second = core::LineId{rows[1].toMap().value(QStringLiteral("id")).toULongLong()};
        const auto karaoke = firstKaraoke();
        activate(*karaoke);
        activate(second);
        QCOMPARE(editor->current(), second.value);
        QCOMPARE(editor->source(), karaoke->value); // editing a template keeps the karaoke Line
    }

    void colourRunsAreATemplatesOrACodeLines()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const auto roles = [&](const QString &text) {
            QList<int> out;
            const QVariantList runs = editor->colourRuns(text);
            for (qsizetype i = 2; i < runs.size(); i += 3)
                out << runs[i].toInt();
            return out;
        };
        const QString text = QStringLiteral("!retime(\"syl\")!{\\pos($x,$y)}");
        QVERIFY(roles(text).contains(int(core::syntax::Role::TemplateVariables)));
        QVERIFY(roles(text).contains(int(core::syntax::Role::TemplateCodeMarks)));
    }

    // The panel: the list, the current Line's fields and text, typed into.
    void thePanelListsTheTemplatesAndEditsTheOneClicked()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        showPanel();
        const QVariantList rows = editor->templates();
        QVERIFY(rows.size() > 1);
        QTRY_VERIFY(row("templateRow_", rows.size() - 1));
        QVERIFY(!row("templateRow_", rows.size()));
        QVERIFY(object(QStringLiteral("templateSummary"))->property("note").toString().contains(
            QStringLiteral(" karaoke Lines")));
        click(row("templateRow_", 1));
        QCOMPARE(editor->current(), rows[1].toMap().value(QStringLiteral("id")).toULongLong());
        QCOMPARE(object(QStringLiteral("templateLineTitle"))->property("title").toString(),
                 QStringLiteral("Line %1").arg(rows[1].toMap().value(QStringLiteral("number")).toInt()));
        auto *text = item(QStringLiteral("templateText"));
        QTRY_COMPARE(text->property("text").toString(), editor->text());
        QCOMPARE(textOf(QStringLiteral("templateEffect")), editor->effect());
        const QString before = editor->text();
        text->forceActiveFocus();
        text->setProperty("cursorPosition", before.size());
        type(QStringLiteral("{\\b1}"));
        QCOMPARE(editor->text(), before + QStringLiteral("{\\b1}"));
        QTRY_VERIFY(row("templateRowChanged_", 1)->isVisible());
        // Coloured as the Line editor colours a template.
        auto *marks = object(QStringLiteral("templateTextHighlighter"));
        QVERIFY(marks);
        QVariantList applied;
        QTRY_VERIFY((QMetaObject::invokeMethod(marks, "appliedColours", Q_RETURN_ARG(QVariantList, applied)),
                     !applied.isEmpty()));
        // The edit goes into the Line when the field is left.
        const std::size_t steps = session->historySize();
        text->setFocus(false);
        QTRY_COMPARE(session->historySize(), steps + 1);
    }

    void aProblemIsUnderlinedWhereItIsAndApplyWaits()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        showPanel();
        QVERIFY(previewCame());
        const QString text = editor->text();
        editor->setText(QStringLiteral("{\\fscx!nosuch.field!}") + text);
        QVERIFY(QTest::qWaitFor([&] { return editor->hasProblems(); }, 20'000));
        QTRY_VERIFY(item(QStringLiteral("templateProblem"))->isVisible());
        const QString problem = textOf(QStringLiteral("templateProblemText"));
        QVERIFY2(problem.contains(QStringLiteral("Template Line: %1").arg(editor->currentNumber())), qPrintable(problem));
        QVERIFY2(problem.contains(QStringLiteral("!nosuch.field!")), qPrintable(problem));
        auto *marks = object(QStringLiteral("templateTextHighlighter"));
        QVariantList underlined;
        QTRY_VERIFY((QMetaObject::invokeMethod(marks, "appliedUnderlines", Q_RETURN_ARG(QVariantList, underlined)),
                     underlined == editor->problemRange()));
        QVERIFY(!item(QStringLiteral("templateApply"))->isEnabled());
        QCOMPARE(textOf(QStringLiteral("templateStatus")), QStringLiteral("Apply is off while a template has a problem."));
        QVERIFY(item(QStringLiteral("templatePreviewKept"))->isVisible());
        const QVariantList rows = editor->templates();
        for (qsizetype i = 0; i < rows.size(); ++i)
            if (rows[i].toMap().value(QStringLiteral("id")).toULongLong() == editor->current()) {
                QVERIFY(row("templateRowProblem_", i)->isVisible());
                QCOMPARE(row("templateRowStatus_", i)->property("text").toString(), QStringLiteral("Problem"));
            }
        editor->setText(text);
        QTRY_VERIFY_WITH_TIMEOUT(!item(QStringLiteral("templateProblem"))->isVisible(), 20'000);
        QVariantList none{0};
        QTRY_VERIFY((QMetaObject::invokeMethod(marks, "appliedUnderlines", Q_RETURN_ARG(QVariantList, none)),
                     none.isEmpty()));
        QTRY_VERIFY(item(QStringLiteral("templateApply"))->isEnabled());
    }

    void thePreviewListsTheLinesApplyWouldWrite()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        showPanel();
        QVERIFY(previewCame());
        const qsizetype n = editor->previewLines().size();
        QVERIFY(n > 0);
        QTRY_VERIFY(row("templatePreviewRow_", n - 1));
        QVERIFY(!row("templatePreviewRow_", n));
        QCOMPARE(object(QStringLiteral("templatePreviewTitle"))->property("title").toString(),
                 QStringLiteral("Preview on Line %1").arg(editor->sourceNumber()));
        QVERIFY(textOf(QStringLiteral("templatePreviewSummary"))
                    .startsWith(n == 1 ? QStringLiteral("1 Line · ") : QStringLiteral("%1 Lines · ").arg(n)));
        QVERIFY(item(QStringLiteral("templatePreviewList"))->isVisible());
        QVERIFY(!item(QStringLiteral("templatePreviewKept"))->isVisible());
    }

    // `$` lists the variables with their values on the current Line; ↓ and
    // Enter insert one.
    void dollarOpensTheVariablesAndEnterInsertsOne()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        showPanel();
        QVERIFY(previewCame());
        auto *text = item(QStringLiteral("templateText"));
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 0);
        type(QStringLiteral("{\\pos($s"));
        auto *list = object(QStringLiteral("templateCompletion"));
        QVERIFY(list);
        QTRY_VERIFY(list->property("opened").toBool());
        const QVariantList entries = list->property("entries").toList();
        QVERIFY(entries.size() > 5);
        QTRY_VERIFY(row("templateCompletionValue_", 0) &&
                    !row("templateCompletionValue_", 0)->property("text").toString().isEmpty());
        QVERIFY(text->hasActiveFocus()); // the list does not take the keys
        QTest::keyClick(view.get(), Qt::Key_Down);
        QTest::keyClick(view.get(), Qt::Key_Return);
        QTRY_VERIFY2(!list->property("opened").toBool(),
                     qPrintable(editor->text().left(40) + QLatin1Char('|') + list->property("chosen").toString()));
        const QString second = entries[1].toMap().value(QStringLiteral("name")).toString();
        QVERIFY2(editor->text().startsWith(QStringLiteral("{\\pos(") + second), qPrintable(editor->text()));
        // Esc closes it, the text as typed.
        type(QStringLiteral(",$"));
        QTRY_VERIFY(list->property("opened").toBool());
        QTest::keyClick(view.get(), Qt::Key_Escape);
        QTRY_VERIFY(!list->property("opened").toBool());
        QVERIFY(editor->text().startsWith(QStringLiteral("{\\pos(") + second + QStringLiteral(",$")));
    }

    void theEffectPickerWritesTheEffectField()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        for (const QVariant &v : editor->templates())
            if (v.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("template")) {
                editor->setCurrent(v.toMap().value(QStringLiteral("id")).toULongLong());
                break;
            }
        showPanel();
        click(item(QStringLiteral("templateEffectPicker")));
        auto *picker = object(QStringLiteral("templateEffectPopover"));
        QTRY_VERIFY(picker->property("opened").toBool());
        QTRY_VERIFY(item(QStringLiteral("templateEffectClass_line")));
        click(item(QStringLiteral("templateEffectClass_line")));
        QTRY_VERIFY(editor->effect().startsWith(QStringLiteral("template line")));
        QVERIFY(item(QStringLiteral("templateEffectClass_line"))->property("on").toBool());
        const bool had = editor->effect().contains(QStringLiteral("noblank"));
        click(item(QStringLiteral("templateEffectModifier_noblank")));
        QTRY_COMPARE(editor->effect().contains(QStringLiteral("noblank")), !had);
        QTRY_COMPARE(textOf(QStringLiteral("templateEffect")), editor->effect());
    }

    void applyFromThePanelIsOneStepAndSaysWhatItWrote()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        editor->setShown(true);
        showPanel();
        const std::string before = bytesOf(session->document());
        QTRY_VERIFY(item(QStringLiteral("templateApply"))->isEnabled());
        click(item(QStringLiteral("templateApply")));
        QCOMPARE(templater->applies, 1);
        QVERIFY(!templater->lastSelectedOnly);
        editor->applied(templater->report);
        editor->refresh();
        QTRY_VERIFY(textOf(QStringLiteral("templateStatus")).startsWith(QStringLiteral("Applied: ")));
        QVERIFY(textOf(QStringLiteral("templateStatus")).endsWith(QStringLiteral("Undo removes them in one step.")));
        QVERIFY(session->undo());
        QCOMPARE(bytesOf(session->document()), before);
        // The arrow: the selected Lines.
        editor->refresh();
        click(item(QStringLiteral("templateApplyArrow")));
        auto *menu = object(QStringLiteral("templateApplyMenu"));
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(object(QStringLiteral("templateApplySelected")), "triggered"));
        QCOMPARE(templater->applies, 2);
        QVERIFY(templater->lastSelectedOnly);
    }

    // At its minimum width the fields stack and the preview folds into a row
    // that unfolds on a click.
    void atItsMinimumWidthThePreviewFolds()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        showPanel(360);
        QVERIFY(previewCame());
        QCOMPARE(view->rootObject()->property("minimumSize").toSize().width(), 360);
        QVERIFY(item(QStringLiteral("templatePreviewFold"))->isVisible());
        QVERIFY(!item(QStringLiteral("templatePreviewList"))->isVisible());
        click(item(QStringLiteral("templatePreviewTitle")));
        QTRY_VERIFY(item(QStringLiteral("templatePreviewList"))->isVisible());
        // The panel's parts fit its width: nothing reaches past it.
        auto *text = item(QStringLiteral("templateText"));
        QVERIFY(text->mapToScene(QPointF(text->width(), 0)).x() <= 360);
        auto *apply = item(QStringLiteral("templateApplyArrow"));
        QVERIFY(apply->mapToScene(QPointF(apply->width(), 0)).x() <= 360);
    }

    void newOffersTheStockKindsAboveTheFirstKaraokeLine()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        showPanel();
        click(item(QStringLiteral("templateNew")));
        auto *menu = object(QStringLiteral("templateNewMenu"));
        QTRY_VERIFY(menu->property("opened").toBool());
        auto *first = object(QStringLiteral("templateNew_0"));
        QVERIFY(first);
        QCOMPARE(first->property("text").toString(), QStringLiteral("Template for each syllable\ttemplate syl"));
        const int count = editor->templateCount();
        QVERIFY(QMetaObject::invokeMethod(first, "triggered"));
        QCOMPARE(editor->templateCount(), count + 1);
        QCOMPARE(editor->effect(), QStringLiteral("template syl"));
    }
};

QTEST_MAIN(TemplateEditorTests)
#include "template_editor_tests.moc"
