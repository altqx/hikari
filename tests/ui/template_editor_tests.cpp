// KE2: the Template editor's controller over the bundled karaoke templater.
// Edits go into the template Line, one undo step per pause; each edit
// previews the templater on the current karaoke Line without changing the
// Document; problems are placed in the text; Apply is the stock run, one
// undo step; the video draws the preview in place of the Line's Lines. The
// panel (TemplateEditor.qml) shows and edits it.
#include "template_editor_controller.h"

#include "hikari/application/edit_session.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/application/script_resolution.h"
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
#include <functional>
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
    bool available() const override { return !down && runner.ready(); }
    void preview(const application::MacroSnapshot &snapshot, PreviewDone done) override
    {
        ++previews;
        sizes.push_back(snapshot.dialogues.size());
        if (dropPreviews > 0) { // as a run cancelled by a newer one: no answer
            --dropPreviews;
            return;
        }
        if (holdPreviews > 0) { // answered when the test says
            --holdPreviews;
            held.emplace_back(snapshot, std::move(done));
            return;
        }
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
    int dropPreviews = 0;
    int holdPreviews = 0;
    bool down = false; // as while the script reloads
    std::vector<std::pair<application::MacroSnapshot, PreviewDone>> held;
    std::vector<std::size_t> sizes; // each run's Lines
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

    // `trust`: the user chose to preview this file's templates (most tests).
    void open(const QString &name, bool trust = true) { openDocument(loadFile(name), trust); }
    void openDocument(core::Document doc, bool trust)
    {
        view.reset();
        editor.reset();
        templater.reset();
        session = std::make_unique<application::EditSession>(std::move(doc));
        templater = std::make_unique<Templater>(*session);
        QVERIFY(QTest::qWaitFor([&] { return templater->available(); }, 20'000));
        editor = std::make_unique<TemplateEditorController>();
        editor->setDelays(10, 60);
        editor->setEditable(true);
        editor->setTemplater(templater.get());
        editor->setSessionSource([this] { return session.get(); });
        QObject::connect(editor.get(), &TemplateEditorController::documentEdited, editor.get(),
                         [this] { editor->refresh(); });
        if (trust)
            editor->trustDocument();
    }

    const core::LineRecord *lineAt(std::size_t i) const { return session->document().lines()[i]; }

    std::optional<core::LineId> firstKaraoke() const
    {
        for (const auto *l : session->document().lines())
            if (application::isKaraokeLine(*l))
                return l->id;
        return std::nullopt;
    }

    // One step on the session, as the Grid or another panel makes it.
    bool change(std::function<bool(core::Document &)> apply)
    {
        application::Command command;
        command.name = "Edit";
        command.expectedRevision = session->revision();
        command.apply = std::move(apply);
        return session->run(command).has_value();
    }
    // Answers the run held first, as its runner would.
    bool answerHeld()
    {
        auto [snapshot, done] = std::move(templater->held.front());
        templater->held.erase(templater->held.begin());
        bool answered = false;
        templater->runner.run(snapshot, [&, done](application::KaraokeTemplaterPort::Preview p) {
            answered = true;
            done(std::move(p));
        });
        return QTest::qWaitFor([&] { return answered; }, 20'000);
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
    // The current text's preview has come.
    bool settled() const { return editor->hasPreview() && !editor->previewPending(); }
    // In the middle of `source`'s time the video holds no second copy of a
    // preview Line (the file's old Line beside it).
    bool oneCopyOnTheVideo(core::LineId source)
    {
        const core::LineRecord *l = nullptr;
        for (const auto *x : session->document().lines())
            if (x->id == source)
                l = x;
        if (!l)
            return false;
        const std::int64_t mid = (l->start.value.microseconds() + l->end.value.microseconds()) / 2000;
        const auto shown = editor->videoDocument(session->document(), mid, false);
        if (!shown)
            return false;
        QStringList texts;
        for (const auto *x : shown->lines())
            texts << QString::fromStdString(lineText(*x)).section(QLatin1Char(','), 9);
        const QStringList preview = previewTexts();
        for (const QString &t : preview)
            if (texts.count(t) > preview.count(t)) {
                qWarning() << "on the video" << texts.count(t) << "times:" << t;
                return false;
            }
        return true;
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

    // A later Line's index ($li) is the one a whole run gives it, the Lines
    // before it left out of the preview's run.
    void aLaterLinesIndexIsTheWholeRunsOne()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        std::optional<core::LineId> last;
        for (const auto *l : session->document().lines())
            if (application::isKaraokeLine(*l))
                last = l->id;
        QVERIFY(last && last != firstKaraoke());
        activate(*last);
        editor->setShown(true);
        QVERIFY(previewCame());
        QCOMPARE(editor->source(), last->value);
        // A template for the Line's Style.
        std::u8string style;
        for (const auto *l : session->document().lines())
            if (l->id == *last)
                style = l->style;
        for (const auto *l : session->document().lines())
            if (application::templateKindOf(*l) == application::TemplateKind::Template && l->style == style)
                editor->setCurrent(l->id.value);
        editor->setText(QStringLiteral("{$li}") + editor->text());
        editor->commit();
        QVERIFY(QTest::qWaitFor(
            [&] { return settled() && previewTexts().join(QString()).contains(QStringLiteral("{$li}")) == false; },
            20'000));
        application::EditSession copy(session->document());
        Templater whole(copy);
        QVERIFY(QTest::qWaitFor([&] { return whole.available(); }, 20'000));
        QVERIFY(whole.apply(false));
        QStringList applied;
        for (const auto *l : copy.document().lines())
            if (l->generatedFrom == *last)
                applied << QString::fromStdString(lineText(*l)).section(QLatin1Char(','), 9);
        QVERIFY(!applied.isEmpty());
        QCOMPARE(previewTexts(), applied);
        editor->requestValues();
        QVERIFY(QTest::qWaitFor([&] { return !editor->values().isEmpty(); }, 20'000));
        const QString li = editor->values().value(QStringLiteral("li")).toString();
        QVERIFY(!li.isEmpty());
        QVERIFY(std::ranges::any_of(applied, [&](const QString &t) {
            return t.startsWith(QLatin1Char('{') + li + QLatin1Char('}'));
        }));
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
        QVERIFY(QTest::qWaitFor([&] { return editor->canApply(); }, 20'000));
        QVERIFY(editor->apply(false));
        editor->applied(templater->report);
        editor->refresh();
        QCOMPARE(bytesOf(session->document()), bytesOf(copy.document()));
        QCOMPARE(editor->lastApply().value(QStringLiteral("lines")).toInt() > 0, true);
        QVERIFY(editor->templates().front().toMap().value(QStringLiteral("generated")).toInt() >= 0);
        QVERIFY(session->undo());
        QCOMPARE(bytesOf(session->document()), before);
    }

    // Script Info is part of what the templates read (meta, karaskel's
    // resolution): a change of it previews again.
    void aScriptInfoChangePreviewsAgain()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QTest::qWait(500); // the run marking the current template's own Lines
        const int before = templater->previews;
        editor->refresh();
        QTest::qWait(300);
        QCOMPARE(templater->previews, before); // nothing changed: no run
        QVERIFY(application::setScriptResolution(*session, {1920, 1080}).has_value());
        editor->refresh();
        QVERIFY(QTest::qWaitFor([&] { return templater->previews > before && settled(); }, 20'000));
    }

    // The video, keyframes, audio selection, frame shown and file name are
    // read by the templates too: a change of them previews again.
    void aHostChangePreviewsAgain()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const auto host = std::make_shared<std::string>("video a");
        editor->setHostState([host] { return *host; });
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QTest::qWait(500); // the run marking the current template's own Lines
        const int before = templater->previews;
        editor->hostChanged();
        QTest::qWait(300);
        QCOMPARE(templater->previews, before); // the same host: no run
        *host = "video b";
        editor->hostChanged();
        QVERIFY(QTest::qWaitFor([&] { return templater->previews > before && settled(); }, 20'000));
        const int after = templater->previews;
        *host = "video c"; // seen at the next refresh too
        editor->refresh();
        QVERIFY(QTest::qWaitFor([&] { return templater->previews > after && settled(); }, 20'000));
    }

    // Apply waits for the preview of the last edit: a problem typed just
    // before it is known before the stock macro runs.
    void applyWaitsForTheLastEditsPreview()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(previewCame());
        QVERIFY(QTest::qWaitFor([&] { return editor->canApply(); }, 20'000));
        editor->setDelays(60'000, 60'000);
        const std::string before = bytesOf(session->document());
        editor->setText(QStringLiteral("{\\fscx!nosuch.field!}") + editor->text());
        QVERIFY(!editor->canApply());
        QVERIFY(!editor->apply(false));
        QCOMPARE(templater->applies, 0);
        QVERIFY(QTest::qWaitFor([&] { return editor->hasProblems(); }, 20'000));
        QVERIFY(!editor->canApply());
        QVERIFY(!editor->apply(false));
        QCOMPARE(templater->applies, 0);
        // Only the template Line's edit went in.
        QVERIFY(bytesOf(session->document()) != before);
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
        QVERIFY(QTest::qWaitFor([&] { return editor->canApply(); }, 20'000));
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

    // An edit that cancels the matching run, and an Undo that brings the
    // unmarked Lines back after Apply, match again: the frame never holds the
    // file's old Lines beside the preview.
    void anEditDuringTheMatchingRunMatchesAgain()
    {
        // The file's generated Lines the frame keeps beside the preview.
        const auto fileLines = [&](core::LineId source) {
            const core::LineRecord *l = nullptr;
            for (const auto *x : session->document().lines())
                if (x->id == source)
                    l = x;
            const std::int64_t mid = (l->start.value.microseconds() + l->end.value.microseconds()) / 2000;
            const auto shown = editor->videoDocument(session->document(), mid, false);
            const QStringList preview = previewTexts();
            QStringList out;
            for (const auto *x : shown->lines()) {
                const QString text = QString::fromStdString(lineText(*x)).section(QLatin1Char(','), 9);
                if (application::isGeneratedLine(*x) && x->generatedFrom.value == 0 && !preview.contains(text))
                    out << text;
            }
            out.sort();
            return out;
        };
        open(QStringLiteral("stock-tutorials.out.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        const QStringList kept = fileLines(*firstKaraoke());
        // Again, the matching run cancelled by an edit still pending.
        open(QStringLiteral("stock-tutorials.out.ass"));
        const auto source = firstKaraoke();
        activate(*source);
        editor->setDelays(10, 60'000);
        templater->dropPreviews = 1; // the matching run
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return templater->previews >= 1; }, 20'000));
        editor->setText(editor->text() + QStringLiteral("{\\bord3}"));
        QVERIFY(QTest::qWaitFor(
            [&] { return settled() && previewTexts().join(QString()).contains(QStringLiteral("bord3")); }, 20'000));
        QCOMPARE(fileLines(*source), kept);
    }

    // A matching run that answers after another tab is chosen is that
    // tab's no more: the new one matches for itself.
    void aMatchFromAnotherTabIsDropped()
    {
        open(QStringLiteral("stock-tutorials.out.ass"));
        activate(*firstKaraoke());
        templater->holdPreviews = 1; // the matching run
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return templater->held.size() == 1; }, 20'000));
        // The same file in another tab, not trusted yet: nothing new runs.
        auto first = std::move(session);
        session = std::make_unique<application::EditSession>(loadFile(QStringLiteral("stock-tutorials.out.ass")));
        editor->refresh();
        QVERIFY(!editor->trusted());
        auto [snapshot, done] = std::move(templater->held.front());
        templater->held.clear();
        bool answered = false;
        templater->runner.run(snapshot, [&, done](application::KaraokeTemplaterPort::Preview p) {
            answered = true;
            done(std::move(p));
        });
        QVERIFY(QTest::qWaitFor([&] { return answered; }, 20'000));
        const std::size_t before = templater->sizes.size();
        activate(*firstKaraoke());
        editor->trustDocument();
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        const std::size_t whole = session->document().lines().size();
        QVERIFY(std::ranges::any_of(templater->sizes.begin() + static_cast<std::ptrdiff_t>(before),
                                    templater->sizes.end(), [&](std::size_t n) { return n == whole; }));
    }

    // A run that answers after the last karaoke Line went shows nothing:
    // its Line is no source any more.
    void aRunForALineGoneIsDropped()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        templater->holdPreviews = 1;
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return templater->held.size() == 1; }, 20'000));
        QVERIFY(change([](core::Document &doc) {
            return doc.removeLinesIf([](const core::LineRecord &l) {
                       return application::isKaraokeLine(l) && !application::templateKindOf(l);
                   }) > 0;
        }));
        editor->refresh();
        QVERIFY(!firstKaraoke());
        QVERIFY(answerHeld());
        QTest::qWait(100);
        QVERIFY(!editor->hasPreview());
        QVERIFY(!editor->previewPending());
    }

    // With the last template gone the preview goes, the one running too.
    void noTemplatesLeftClearsThePreview()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const auto source = firstKaraoke();
        activate(*source);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QTest::qWait(500); // the run marking the current template's own Lines
        QVERIFY(editor->videoPreview());
        templater->holdPreviews = 1;
        QVERIFY(change([&](core::Document &doc) {
            return doc.editLine(*source, [](core::LineRecord &l) { l.text += u8"{\\bord3}"; });
        }));
        editor->refresh();
        QVERIFY(QTest::qWaitFor([&] { return templater->held.size() == 1; }, 20'000));
        QVERIFY(change([](core::Document &doc) {
            return doc.removeLinesIf([](const core::LineRecord &l) {
                       return application::templateKindOf(l).has_value();
                   }) > 0;
        }));
        editor->refresh();
        QTest::qWait(100);
        QCOMPARE(editor->templateCount(), 0);
        QVERIFY(!editor->hasPreview());
        QVERIFY(!editor->videoPreview());
        QVERIFY(answerHeld());
        QTest::qWait(100);
        QVERIFY(!editor->hasPreview());
        QVERIFY(!editor->previewPending());
    }

    // The last template turned into code in the panel while its first
    // preview runs, or after it failed: the run's answer and its problems go
    // with it.
    void noTemplatesLeftEndsAnUnfinishedPreview()
    {
        // One template only, as `text` has it.
        const auto oneTemplate = [](const std::u8string &text) {
            core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
            std::optional<core::LineId> kept;
            for (const auto *l : doc.lines())
                if (!kept && application::templateKindOf(*l) == application::TemplateKind::Template)
                    kept = l->id;
            doc.removeLinesIf([&](const core::LineRecord &l) {
                return application::templateKindOf(l) == application::TemplateKind::Template && l.id != *kept;
            });
            doc.editLine(*kept, [&](core::LineRecord &l) {
                l.effect = u8"template syl";
                if (!text.empty())
                    l.text = text;
            });
            return std::pair{std::move(doc), *kept};
        };
        auto [doc, id] = oneTemplate({});
        openDocument(std::move(doc), true);
        activate(*firstKaraoke());
        editor->setCurrent(id.value);
        templater->holdPreviews = 1;
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return templater->held.size() == 1; }, 20'000));
        QVERIFY(!editor->hasPreview());
        editor->setEffect(QStringLiteral("code line"));
        QVERIFY(editor->trusted());
        QTest::qWait(100);
        QVERIFY(answerHeld());
        QTest::qWait(100);
        QVERIFY(!editor->hasPreview());
        QVERIFY(!editor->previewPending());
        // A first preview that failed.
        auto [broken, brokenId] = oneTemplate(u8"{\\fscx!nosuch.field!}");
        openDocument(std::move(broken), true);
        activate(*firstKaraoke());
        editor->setCurrent(brokenId.value);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return editor->hasProblems() && !editor->previewPending(); }, 20'000));
        editor->setEffect(QStringLiteral("code line"));
        QVERIFY(editor->trusted());
        QTest::qWait(300);
        QVERIFY(!editor->hasProblems());
    }

    // The values asked for on one tab stay that tab's: the same file in
    // another tab (the same Line ids) shows none until asked there.
    void valuesFromAnotherTabAreDropped()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QTest::qWait(500); // the run marking the current template's own Lines
        templater->holdPreviews = 1;
        editor->requestValues();
        QVERIFY(QTest::qWaitFor([&] { return templater->held.size() == 1; }, 20'000));
        auto first = std::move(session);
        session = std::make_unique<application::EditSession>(loadFile(QStringLiteral("stock-tutorials.ass")));
        editor->refresh();
        QVERIFY(!editor->trusted());
        QVERIFY(answerHeld());
        QTest::qWait(100);
        QVERIFY(editor->values().isEmpty());
        // Asked on this tab, they come.
        activate(*firstKaraoke());
        editor->trustDocument();
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        editor->requestValues();
        QVERIFY(QTest::qWaitFor([&] { return !editor->values().isEmpty(); }, 20'000));
    }

    // A template edited since Apply stays marked after a visit to another
    // tab: its generated Lines are still the old Apply's.
    void aChangeSinceApplyOutlastsATabSwitch()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        std::pair<std::uint64_t, std::uint64_t> key{1, 1};
        editor->setDocumentKey([&] { return key; });
        editor->refresh();
        editor->trustDocument();
        const auto changed = [&] {
            for (const QVariant &v : editor->templates())
                if (v.toMap().value(QStringLiteral("id")).toULongLong() == editor->current())
                    return v.toMap().value(QStringLiteral("changed")).toBool();
            return false;
        };
        QVERIFY(editor->current() != 0);
        QVERIFY(!changed());
        editor->setText(editor->text() + QStringLiteral("{\\bord3}"));
        editor->commit();
        editor->refresh();
        QVERIFY(changed());
        auto first = std::move(session);
        session = std::make_unique<application::EditSession>(loadFile(QStringLiteral("stock-tutorials.ass")));
        key = {2, 1};
        editor->refresh();
        QVERIFY(!changed());
        session = std::move(first);
        key = {1, 1};
        editor->refresh();
        QVERIFY(changed());
        editor->setDocumentKey({});
    }

    void anUndoneApplyMatchesAgain()
    {
        open(QStringLiteral("stock-tutorials.out.ass"));
        const auto source = firstKaraoke();
        QVERIFY(source);
        activate(*source);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QVERIFY(oneCopyOnTheVideo(*source));
        QVERIFY(editor->apply(false));
        editor->applied(templater->report);
        editor->refresh();
        QVERIFY(session->undo());
        editor->refresh();
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QVERIFY(oneCopyOnTheVideo(*source));
    }

    // Choosing another template marks its own Lines, the Document unchanged.
    void anotherTemplateChosenMarksItsOwnLines()
    {
        open(QStringLiteral("stock-environment.ass")); // six syllable templates on one Style
        activate(*firstKaraoke());
        editor->setShown(true);
        const auto own = [&] {
            QList<bool> out;
            for (const QVariant &v : editor->previewLines())
                out << v.toMap().value(QStringLiteral("own")).toBool();
            return out;
        };
        QVERIFY(QTest::qWaitFor([&] { return settled() && own().contains(true); }, 20'000));
        QTest::qWait(300);
        const QList<bool> first = own();
        qulonglong other = 0;
        for (const QVariant &v : editor->templates()) {
            const auto t = v.toMap();
            if (t.value(QStringLiteral("id")).toULongLong() != editor->current() &&
                t.value(QStringLiteral("generated")).toInt() > 0 && t.value(QStringLiteral("kind")).toString() != QStringLiteral("code"))
                other = t.value(QStringLiteral("id")).toULongLong();
        }
        QVERIFY(other);
        editor->setCurrent(other);
        QVERIFY(QTest::qWaitFor([&] { return settled() && own() != first && own().contains(true); }, 20'000));
    }

    // A reloaded templater may write otherwise: the preview runs again.
    void aReloadedTemplaterPreviewsAgain()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QTest::qWait(500);
        const int before = templater->previews;
        templater->down = true;
        editor->refresh();
        templater->down = false;
        editor->refresh();
        QVERIFY(QTest::qWaitFor([&] { return templater->previews > before && settled(); }, 20'000));
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
        // The class may come after modifiers, as the stock parser reads it,
        // but not as a modifier's argument.
        const QVariantMap late = e.effectParts(QStringLiteral("template noblank line named"));
        QCOMPARE(late.value(QStringLiteral("className")).toString(), QStringLiteral("line"));
        QCOMPARE(late.value(QStringLiteral("name")).toString(), QStringLiteral("named"));
        QCOMPARE(late.value(QStringLiteral("modifiers")).toStringList(), (QStringList{QStringLiteral("noblank")}));
        QCOMPARE(e.effectParts(QStringLiteral("code all syl")).value(QStringLiteral("className")).toString(),
                 QStringLiteral("syl"));
        QCOMPARE(e.effectWithClass(QStringLiteral("template noblank line"), QStringLiteral("syl")),
                 QStringLiteral("template syl noblank"));
        const QVariantMap argument = e.effectParts(QStringLiteral("template fx line char"));
        QCOMPARE(argument.value(QStringLiteral("className")).toString(), QStringLiteral("char"));
        QCOMPARE(argument.value(QStringLiteral("modifiers")).toStringList(),
                 (QStringList{QStringLiteral("fx"), QStringLiteral("line")}));
    }

    // A file's templates and code run Lua: nothing of them runs until the
    // user previews the file or applies it, as the Automation menu runs
    // them only when asked.
    void aFilesTemplatesRunOnlyOnceTheUserPreviewsIt()
    {
        open(QStringLiteral("stock-tutorials.ass"), false);
        activate(*firstKaraoke());
        QVERIFY(!editor->trusted());
        editor->setShown(true);
        editor->requestValues();
        QTest::qWait(300);
        QCOMPARE(templater->previews, 0);
        QVERIFY(!editor->hasPreview());
        QVERIFY(!editor->previewPending());
        QVERIFY(editor->canApply()); // Apply is the user's to run, as in the Automation menu
        editor->trustDocument();
        QVERIFY(editor->trusted());
        QVERIFY(previewCame());
    }

    void applyTrustsTheFile()
    {
        open(QStringLiteral("stock-tutorials.ass"), false);
        editor->setShown(true);
        QVERIFY(editor->apply(false));
        editor->applied(templater->report);
        QVERIFY(editor->trusted());
    }

    // A file without templates runs no code of its own, but its Actor and
    // Style values reach the templates the user writes ($actor, $style, the
    // values probe): those run only once the user previews them.
    void aFileWithoutTemplatesAsksBeforeItsValuesRun()
    {
        core::Document plain = loadFile(QStringLiteral("stock-tutorials.ass"));
        plain.removeLinesIf([](const core::LineRecord &l) { return application::templateKindOf(l).has_value(); });
        for (const auto *l : plain.lines())
            if (application::isKaraokeLine(*l)) {
                plain.editLine(l->id, [](core::LineRecord &r) { r.actor = u8"!_G.marker = 1!"; });
                break;
            }
        openDocument(std::move(plain), false);
        editor->refresh();
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(!editor->trusted());
        QVERIFY(editor->insertTemplate(QStringLiteral("template syl")));
        editor->setText(QStringLiteral("{\\bord2}$actor"));
        editor->requestValues();
        QTest::qWait(300);
        QCOMPARE(templater->previews, 0);
        editor->trustDocument();
        QVERIFY(editor->trusted());
        // Asked, it runs (the Actor is no expression the templater can
        // evaluate: a problem, no preview).
        QVERIFY(QTest::qWaitFor([&] { return templater->previews > 0 && !editor->previewPending(); }, 20'000));
    }

    // Template or code Lines that come another way than New (a paste, an
    // import) are Lua the user did not write: the panel asks before it runs
    // them, on a file without templates the user previewed, and again after.
    void templatesPastedInAskFirst()
    {
        const core::Document file = loadFile(QStringLiteral("stock-tutorials.ass"));
        std::vector<core::LineRecord> pasted;
        for (const auto *l : file.lines())
            if (application::templateKindOf(*l))
                pasted.push_back(*l);
        QVERIFY(!pasted.empty());
        const auto paste = [&] {
            return change([&](core::Document &doc) {
                for (const auto &l : pasted)
                    if (!doc.appendLine(l))
                        return false;
                return true;
            });
        };
        core::Document plain = file;
        plain.removeLinesIf([](const core::LineRecord &l) { return application::templateKindOf(l).has_value(); });
        openDocument(std::move(plain), true);
        editor->refresh();
        QVERIFY(editor->trusted());
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(paste());
        editor->refresh();
        QVERIFY(!editor->trusted());
        QTest::qWait(300);
        QCOMPARE(templater->previews, 0);
        QVERIFY(!editor->hasPreview());
        editor->trustDocument();
        QVERIFY(editor->trusted());
        QVERIFY(previewCame());
        // Previewed once; more pasted in asks again.
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QTest::qWait(500); // the run marking the current template's own Lines
        const int before = templater->previews;
        QVERIFY(paste());
        editor->refresh();
        QVERIFY(!editor->trusted());
        QTest::qWait(300);
        QCOMPARE(templater->previews, before);
    }

    // A template Line keeps its id when another way changes its Lua (a
    // translation pasted over it): the panel asks before it runs the new
    // text. The user's own edits and their Undo stay trusted.
    void aTemplateRewrittenElsewhereAsksFirst()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QTest::qWait(500); // the run marking the current template's own Lines
        // The panel's edit, committed, and its Undo.
        editor->setText(editor->text() + QStringLiteral("{\\bord3}"));
        editor->commit();
        editor->refresh();
        QVERIFY(editor->trusted());
        QVERIFY(session->undo());
        editor->refresh();
        QVERIFY(editor->trusted());
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QTest::qWait(500);
        const int before = templater->previews;
        const auto code = core::LineId{editor->current()};
        QVERIFY(change([&](core::Document &doc) {
            return doc.editLine(code, [](core::LineRecord &l) { l.text += u8"!_G.marker = 1!"; });
        }));
        editor->refresh();
        QVERIFY(!editor->trusted());
        QTest::qWait(300);
        QCOMPARE(templater->previews, before);
    }

    // The templater puts a Line's Actor and Style into a template before it
    // runs the template's !expressions!: a Line pasted in with a new one
    // asks first; one with the values already there does not.
    void aNewActorOrStyleAsksFirst()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const auto source = firstKaraoke();
        activate(*source);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QTest::qWait(500); // the run marking the current template's own Lines
        core::LineRecord row = *lineAt(0);
        for (const auto *l : session->document().lines())
            if (l->id == *source)
                row = *l;
        QVERIFY(change([&](core::Document &doc) { return doc.appendLine(row).has_value(); }));
        editor->refresh();
        QVERIFY(editor->trusted());
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QTest::qWait(500);
        const int before = templater->previews;
        row.actor = u8"!_G.marker = 1!";
        QVERIFY(change([&](core::Document &doc) { return doc.appendLine(row).has_value(); }));
        editor->refresh();
        QVERIFY(!editor->trusted());
        row.actor = (*lineAt(0)).actor;
        row.style = u8"!_G.marker = 1!";
        QVERIFY(change([&](core::Document &doc) {
            return doc.removeLinesIf([](const core::LineRecord &l) { return l.actor == u8"!_G.marker = 1!"; }) == 1 &&
                   doc.appendLine(row).has_value();
        }));
        editor->refresh();
        QVERIFY(!editor->trusted());
        QTest::qWait(300);
        QCOMPARE(templater->previews, before);
        editor->trustDocument();
        QVERIFY(editor->trusted());
    }

    // A preview made before trust went is not this file's result: it goes
    // with the run making one.
    void revokedTrustTakesThePreviewAway()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const auto source = firstKaraoke();
        activate(*source);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QVERIFY(editor->videoPreview());
        templater->holdPreviews = 1;
        QVERIFY(change([&](core::Document &doc) {
            return doc.editLine(*source, [](core::LineRecord &l) { l.text += u8"{\\bord3}"; });
        }));
        editor->refresh();
        QVERIFY(QTest::qWaitFor([&] { return templater->held.size() == 1; }, 20'000));
        QVERIFY(change([&](core::Document &doc) {
            return doc.editLine(core::LineId{editor->current()},
                                [](core::LineRecord &l) { l.text += u8"!_G.marker = 1!"; });
        }));
        editor->refresh();
        QVERIFY(!editor->trusted());
        QVERIFY(!editor->hasPreview());
        QVERIFY(!editor->videoPreview());
        QVERIFY(answerHeld());
        QTest::qWait(100);
        QVERIFY(!editor->hasPreview());
    }

    // A code Line made a template in the panel previews before the Effect
    // is committed, as the first template of its file.
    void aFirstTemplateByItsEffectPreviews()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        bool first = true;
        doc.removeLinesIf([&](const core::LineRecord &l) {
            if (!application::templateKindOf(l))
                return false;
            const bool keep = first;
            first = false;
            return !keep;
        });
        for (const auto *l : doc.lines())
            if (application::templateKindOf(*l))
                doc.editLine(l->id, [](core::LineRecord &r) { r.effect = u8"code line"; });
        openDocument(std::move(doc), true);
        QCOMPARE(editor->templateCount(), 0);
        activate(*firstKaraoke());
        editor->setDelays(10, 60'000);
        editor->setShown(true);
        editor->setEffect(QStringLiteral("template line"));
        QVERIFY(previewCame());
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
    }

    // Typing that ends where it began changes nothing: no step, no save
    // question.
    void anEditBackToTheStartIsNoStep()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        editor->setDelays(10, 60'000);
        const std::size_t steps = session->historySize();
        const QString text = editor->text();
        editor->setText(text + QStringLiteral("x"));
        editor->setText(text);
        editor->commit();
        QCOMPARE(session->historySize(), steps);
        QVERIFY(!session->isDirty());
    }

    // In TLMode the templater reads a Line's translation: the panel shows
    // and edits that, and the preview runs it.
    void aTranslatedTemplateEditsItsTranslation()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        std::optional<core::LineId> tl;
        std::u8string original;
        for (const auto *l : doc.lines())
            if (!tl && application::templateKindOf(*l)) {
                tl = l->id;
                original = l->text;
            }
        QVERIFY(tl);
        doc.editLine(*tl, [](core::LineRecord &l) { l.translation = l.text + u8"{\\bord4}"; });
        openDocument(std::move(doc), true);
        editor->setCurrent(tl->value);
        QVERIFY(editor->text().endsWith(QStringLiteral("{\\bord4}")));
        const auto rowText = [&] {
            for (const QVariant &v : editor->templates())
                if (v.toMap().value(QStringLiteral("id")).toULongLong() == tl->value)
                    return v.toMap().value(QStringLiteral("text")).toString();
            return QString();
        };
        QVERIFY(rowText().endsWith(QStringLiteral("{\\bord4}"))); // the list shows it too
        activate(*firstKaraoke());
        editor->setDelays(10, 60'000);
        editor->setShown(true);
        editor->setText(editor->text() + QStringLiteral("{\\bord5}"));
        QVERIFY(rowText().endsWith(QStringLiteral("{\\bord5}")));
        QVERIFY(QTest::qWaitFor(
            [&] { return settled() && previewTexts().join(QString()).contains(QStringLiteral("bord5")); }, 20'000));
        editor->commit();
        const core::LineRecord *l = nullptr;
        for (const auto *x : session->document().lines())
            if (x->id == *tl)
                l = x;
        QVERIFY(l);
        QVERIFY(l->text == original);
        QVERIFY(std::u8string_view(l->translation).ends_with(u8"{\\bord4}{\\bord5}"));
    }

    // In TLMode a karaoke Line is templated from its translation: the
    // preview lists the generated text Apply writes there, not the original.
    void aTranslatedLinesPreviewListsWhatApplyWrites()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        std::optional<core::LineId> source;
        for (const auto *l : doc.lines())
            if (!source && application::isKaraokeLine(*l) && !application::templateKindOf(*l))
                source = l->id;
        QVERIFY(source);
        doc.editLine(*source, [](core::LineRecord &l) {
            l.translation = l.text;
            l.text = u8"original";
        });
        openDocument(std::move(doc), true);
        activate(*source);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        application::EditSession copy(session->document());
        Templater whole(copy);
        QVERIFY(QTest::qWaitFor([&] { return whole.available(); }, 20'000));
        QVERIFY(whole.apply(false));
        QStringList applied;
        for (const auto *l : copy.document().lines())
            if (l->generatedFrom == *source)
                applied << QString::fromUtf8(reinterpret_cast<const char *>(l->translation.data()),
                                             static_cast<qsizetype>(l->translation.size()));
        QVERIFY(!applied.isEmpty());
        QCOMPARE(previewTexts(), applied);
        QVERIFY(editor->sourceText() != QStringLiteral("original")); // the heading names what ran
        QVERIFY(!editor->sourceText().contains(QLatin1Char('{')));
    }

    // Apply's summary is of the Document Apply left: an Undo or another
    // edit after it takes it away.
    void anUndoneApplyLeavesNoSummary()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return editor->canApply(); }, 20'000));
        QVERIFY(editor->apply(false));
        editor->applied(templater->report);
        editor->refresh();
        QVERIFY(!editor->lastApply().isEmpty());
        editor->refresh();
        QVERIFY(!editor->lastApply().isEmpty());
        QVERIFY(session->undo());
        editor->refresh();
        QVERIFY(editor->lastApply().isEmpty());
    }

    // A templater unloaded from the Automation panel takes its preview with
    // it, and the run it had going.
    void anUnloadedTemplaterTakesThePreviewAway()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        templater->holdPreviews = 1;
        editor->setText(editor->text() + QStringLiteral("{\\bord3}"));
        QVERIFY(QTest::qWaitFor([&] { return templater->held.size() == 1; }, 20'000));
        templater->down = true;
        editor->refresh();
        QVERIFY(!editor->hasPreview());
        QVERIFY(!editor->videoPreview());
        QVERIFY(!editor->previewPending());
    }

    // A problem found before trust went is the old text's: it no longer
    // keeps Apply off.
    void revokedTrustDropsTheOldProblem()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        editor->setText(QStringLiteral("{\\fscx!nosuch.field!}") + editor->text());
        QVERIFY(QTest::qWaitFor([&] { return editor->hasProblems() && !editor->previewPending(); }, 20'000));
        QVERIFY(!editor->canApply());
        editor->commit();
        QVERIFY(change([&](core::Document &doc) {
            return doc.editLine(core::LineId{editor->current()}, [](core::LineRecord &l) {
                l.text = std::u8string(l.text).substr(std::u8string_view(u8"{\\fscx!nosuch.field!}").size()) +
                         u8"{\\bord1}"; // fixed in the Line editor
            });
        }));
        editor->refresh();
        QVERIFY(!editor->trusted());
        QVERIFY(!editor->hasProblems());
        QVERIFY(editor->canApply());
    }

    // A generated Line clicked in a file opened without marks previews the
    // karaoke Line it came from, found as KE1 finds it, not the first one.
    void anUnmarkedGeneratedLinePreviewsItsOwnKaraokeLine()
    {
        open(QStringLiteral("stock-tutorials.out.ass"));
        const auto first = firstKaraoke();
        QVERIFY(first);
        // The last generated Line whose Style and times are one karaoke
        // Line's alone.
        std::optional<core::LineId> clicked, owner;
        for (const auto *g : session->document().lines()) {
            if (!application::isGeneratedLine(*g))
                continue;
            std::vector<core::LineId> same;
            for (const auto *k : session->document().lines())
                if (application::isKaraokeLine(*k) && !application::templateKindOf(*k) &&
                    k->style == g->style && k->start.value == g->start.value && k->end.value == g->end.value)
                    same.push_back(k->id);
            if (same.size() == 1 && same.front() != *first) {
                clicked = g->id;
                owner = same.front();
            }
        }
        QVERIFY(clicked);
        editor->setShown(true);
        activate(*clicked);
        QVERIFY(QTest::qWaitFor([&] { return editor->source() == owner->value && settled(); }, 20'000));
    }

    // Only the Lines the templater runs give trusted Actor and Style
    // values, each in its own role: a value a comment held, or a Style's
    // name, put as a runnable Line's Actor asks first.
    void onlyRunnableLinesValuesAreTrusted()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        core::LineRecord dormant = *doc.lines().back();
        dormant.comment = true;
        dormant.effect = u8"note";
        dormant.actor = u8"!_G.marker = 1!";
        QVERIFY(doc.appendLine(dormant));
        openDocument(std::move(doc), true);
        const auto source = firstKaraoke();
        activate(*source);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QTest::qWait(500); // the run marking the current template's own Lines
        const int before = templater->previews;
        core::LineRecord row;
        for (const auto *l : session->document().lines())
            if (l->id == *source)
                row = *l;
        row.actor = u8"!_G.marker = 1!";
        QVERIFY(change([&](core::Document &d) { return d.appendLine(row).has_value(); }));
        editor->refresh();
        QVERIFY(!editor->trusted());
        QTest::qWait(300);
        QCOMPARE(templater->previews, before);
        // A Style's name as an Actor.
        QVERIFY(change([&](core::Document &d) {
            return d.removeLinesIf([](const core::LineRecord &l) {
                       return !l.comment && l.actor == u8"!_G.marker = 1!";
                   }) == 1;
        }));
        editor->refresh();
        QVERIFY(editor->trusted());
        row.actor = row.style;
        QVERIFY(change([&](core::Document &d) { return d.appendLine(row).has_value(); }));
        editor->refresh();
        QVERIFY(!editor->trusted());
    }

    // A Style picks the templates an Actor goes into: a runnable Line pairing
    // a trusted Actor with another trusted Style asks first.
    void aNewPairingOfActorAndStyleAsksFirst()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        std::vector<core::LineId> karaoke;
        for (const auto *l : doc.lines())
            if (application::isKaraokeLine(*l) && !application::templateKindOf(*l))
                karaoke.push_back(l->id);
        QVERIFY(karaoke.size() >= 2);
        QVERIFY(doc.editLine(karaoke[0], [](core::LineRecord &l) {
            l.style = u8"T1a";
            l.actor = u8"!_G.marker = 1!";
        }));
        QVERIFY(doc.editLine(karaoke[1], [](core::LineRecord &l) {
            l.style = u8"T1b";
            l.actor = u8"plain";
        }));
        core::LineRecord pairing;
        for (const auto *l : doc.lines())
            if (l->id == karaoke[1])
                pairing = *l;
        openDocument(std::move(doc), true);
        activate(karaoke[1]);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QVERIFY(editor->trusted());
        pairing.actor = u8"!_G.marker = 1!"; // T1b's templates with T1a's Actor
        QVERIFY(change([&](core::Document &d) { return d.appendLine(pairing).has_value(); }));
        editor->refresh();
        QVERIFY(!editor->trusted());
    }

    // The Style picks the karaoke Lines a template runs on, and so whose
    // Actor goes into its `$actor`: a Style changed outside the panel asks
    // first.
    void aTemplatesStyleChangedElsewhereAsksFirst()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        std::optional<core::LineId> templ;
        for (const auto *l : doc.lines())
            if (application::templateKindOf(*l) && !templ)
                templ = l->id;
        QVERIFY(templ);
        openDocument(std::move(doc), true);
        QVERIFY(editor->trusted());
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*templ, [](core::LineRecord &l) { l.style = u8"T1b"; });
        }));
        editor->refresh();
        QVERIFY(!editor->trusted());
    }

    // `template line karaoke` is a template the stock templater also runs as
    // a karaoke Line: a new Actor on it asks first too.
    void aTemplateThatIsAlsoAKaraokeLineHasItsActorTrusted()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        core::LineRecord both;
        for (const auto *l : doc.lines())
            if (application::isKaraokeLine(*l) && !application::templateKindOf(*l))
                both = *l;
        both.comment = true;
        both.effect = u8"template line karaoke";
        both.text = u8"{\\bord2}";
        both.actor = u8"plain";
        const auto id = doc.appendLine(both);
        QVERIFY(id);
        QVERIFY(application::templateKindOf(both) && application::isKaraokeLine(both));
        openDocument(std::move(doc), true);
        QVERIFY(editor->trusted());
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*id, [](core::LineRecord &l) { l.actor = u8"!_G.marker = 1!"; });
        }));
        editor->refresh();
        QVERIFY(!editor->trusted());
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

    // The templater's helper still loading when the panel shows: the preview
    // runs once it is ready (the application refreshes on its readiness).
    void thePreviewRunsOnceTheTemplaterIsReady()
    {
        view.reset();
        editor.reset();
        templater.reset();
        session = std::make_unique<application::EditSession>(loadFile(QStringLiteral("stock-tutorials.ass")));
        templater = std::make_unique<Templater>(*session);
        editor = std::make_unique<TemplateEditorController>();
        editor->setDelays(10, 60);
        editor->setEditable(true);
        editor->setTemplater(templater.get());
        editor->setSessionSource([this] { return session.get(); });
        editor->trustDocument();
        QVERIFY(!templater->available());
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return templater->available(); }, 20'000));
        QTest::qWait(50);
        editor->refresh();
        QVERIFY(previewCame());
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
        // A wavy line under the expression, from its first character.
        auto *wave = item(QStringLiteral("templateProblemWave"));
        QVERIFY(wave);
        auto *field = item(QStringLiteral("templateText"));
        QRectF first;
        QVERIFY(QMetaObject::invokeMethod(field, "positionToRectangle", Q_RETURN_ARG(QRectF, first),
                                          Q_ARG(int, editor->problemRange().front().toInt())));
        QTRY_VERIFY(!wave->property("segments").toList().isEmpty());
        const QVariantList segment = wave->property("segments").toList().front().toList();
        QCOMPARE(segment.value(0).toReal(), first.x());
        QVERIFY(segment.value(2).toReal() > 0);
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
        QTRY_VERIFY(wave->property("segments").toList().isEmpty());
        QTRY_VERIFY(item(QStringLiteral("templateApply"))->isEnabled());
    }

    // Until the user previews a file, the panel says why nothing runs and
    // offers to.
    void thePanelAsksBeforeRunningAFilesTemplates()
    {
        open(QStringLiteral("stock-tutorials.ass"), false);
        activate(*firstKaraoke());
        editor->setShown(true);
        showPanel();
        auto *notice = item(QStringLiteral("templatePreviewTrust"));
        QTRY_VERIFY(notice->isVisible());
        QVERIFY(!item(QStringLiteral("templatePreviewEmpty"))->isVisible());
        QCOMPARE(templater->previews, 0);
        click(item(QStringLiteral("templatePreviewTrustButton")));
        QVERIFY(editor->trusted());
        QVERIFY(previewCame());
        QTRY_VERIFY(!notice->isVisible());
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
        // Text put in from elsewhere (another template, an undo) opens nothing,
        // the cursor inside an expression or not.
        text->setProperty("cursorPosition", 3);
        editor->setText(QStringLiteral("!ret!{\\b1}"));
        QTest::qWait(200);
        QCOMPARE(text->property("text").toString(), QStringLiteral("!ret!{\\b1}"));
        QVERIFY(!list->property("opened").toBool());
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

    // A template's Layer may be negative, as the Line editor's may.
    void theLayerFieldTakesANegativeLayer()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        std::optional<core::LineId> tl;
        for (const auto *l : doc.lines())
            if (!tl && application::templateKindOf(*l))
                tl = l->id;
        QVERIFY(tl);
        QVERIFY(doc.editLine(*tl, [](core::LineRecord &l) { l.layer.value = -3; }));
        openDocument(std::move(doc), true);
        editor->setCurrent(tl->value);
        showPanel();
        auto *layer = item(QStringLiteral("templateLayer"));
        QTRY_COMPARE(layer->property("value").toInt(), -3);
        QVERIFY(layer->property("from").toInt() <= -10'000'000);
        QCOMPARE(layer->property("to").toInt(), 10'000'000);
    }

    // A first preview that fails has ended: the panel says so, not that the
    // templates are still running.
    void aFailedFirstPreviewIsNotStillRunning()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        for (const auto *l : doc.lines())
            if (application::templateKindOf(*l) == application::TemplateKind::Template)
                QVERIFY(doc.editLine(l->id, [](core::LineRecord &t) { t.text = u8"{\\fscx!nosuch.field!}"; }));
        openDocument(std::move(doc), true);
        activate(*firstKaraoke());
        editor->setShown(true);
        showPanel();
        QVERIFY(QTest::qWaitFor([&] { return editor->hasProblems() && !editor->previewPending(); }, 20'000));
        QVERIFY(!editor->hasPreview());
        QTRY_VERIFY(!textOf(QStringLiteral("templatePreviewEmpty")).startsWith(QStringLiteral("Running")));
        QVERIFY(textOf(QStringLiteral("templatePreviewEmpty")).contains(QStringLiteral("problem")));
    }

    // Markup in a Line's fields is text: as rich text an <img> would be
    // fetched (from a server, on Windows a share) before the user previews.
    void markupInTheFileIsShownAsText()
    {
        const std::u8string img = u8"<img src=\"http://127.0.0.1:9/x\">";
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        std::optional<core::LineId> tl, source;
        for (const auto *l : doc.lines()) {
            if (!tl && application::templateKindOf(*l) == application::TemplateKind::Template)
                tl = l->id;
            if (!source && application::isKaraokeLine(*l) && !application::templateKindOf(*l))
                source = l->id;
        }
        QVERIFY(tl && source);
        QVERIFY(doc.editLine(*tl, [&](core::LineRecord &l) {
            l.effect += u8" " + img;
            l.text += img;
        }));
        QVERIFY(doc.editLine(*source, [&](core::LineRecord &l) {
            l.text += img;
            l.actor = img;
        }));
        openDocument(std::move(doc), true);
        editor->setCurrent(tl->value);
        activate(*source);
        editor->setShown(true);
        showPanel(900);
        QVERIFY(QTest::qWaitFor([&] { return !editor->previewPending(); }, 20'000));
        editor->requestValues();
        QTest::qWait(500);
        int seen = 0;
        const std::function<void(QQuickItem *)> walk = [&](QQuickItem *at) {
            for (QQuickItem *child : at->childItems()) {
                const QString text = child->property("text").toString();
                if (child->inherits("QQuickText") && text.contains(QLatin1String("<img"))) {
                    ++seen;
                    QCOMPARE(child->property("textFormat").toInt(), int(Qt::PlainText));
                }
                walk(child);
            }
        };
        walk(view->rootObject());
        QVERIFY(seen >= 3); // the row's text, the source note, the Effect note
        // The Style list's names too.
        auto *popup = item(QStringLiteral("templateStyle"))->property("popup").value<QObject *>();
        QVERIFY(popup);
        QMetaObject::invokeMethod(popup, "open");
        QTRY_VERIFY(popup->property("opened").toBool());
        int names = 0;
        const std::function<void(QQuickItem *)> walkNames = [&](QQuickItem *at) {
            for (QQuickItem *child : at->childItems()) {
                if (child->inherits("QQuickText") && !child->property("text").toString().isEmpty() &&
                    child->parentItem() && child->parentItem()->inherits("QQuickItemDelegate")) {
                    ++names;
                    QCOMPARE(child->property("textFormat").toInt(), int(Qt::PlainText));
                }
                walkNames(child);
            }
        };
        walkNames(popup->property("contentItem").value<QQuickItem *>());
        QVERIFY(names > 0);
        QMetaObject::invokeMethod(popup, "close");
        for (const char *name : {"templateProblemToolTip"}) {
            auto *tip = object(QString::fromLatin1(name));
            QVERIFY2(tip, name);
            auto *content = tip->property("contentItem").value<QQuickItem *>();
            QVERIFY(content);
            QCOMPARE(content->property("textFormat").toInt(), int(Qt::PlainText));
        }
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
        // The arrow: the selected Lines, once the Undo's preview came.
        editor->refresh();
        QTRY_VERIFY(item(QStringLiteral("templateApplyArrow"))->isEnabled());
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
        // Every stock kind, the furigana ones too.
        QStringList effects;
        for (int i = 0; auto *o = object(QStringLiteral("templateNew_%1").arg(i)); ++i)
            effects << o->property("text").toString().section(QLatin1Char('\t'), 1);
        QCOMPARE(effects, (QStringList{QStringLiteral("template syl"), QStringLiteral("template line"),
                                       QStringLiteral("template pre-line"), QStringLiteral("template char"),
                                       QStringLiteral("template furi"), QStringLiteral("code once"),
                                       QStringLiteral("code line"), QStringLiteral("code syl"),
                                       QStringLiteral("code furi")}));
    }
};

QTEST_MAIN(TemplateEditorTests)
#include "template_editor_tests.moc"
