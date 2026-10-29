// KE2: the Template editor's controller over the bundled karaoke templater.
// Edits go into the template Line, one undo step per pause; each edit
// previews the templater on the current karaoke Line without changing the
// Document; problems are placed in the text; Apply is the stock run, one
// undo step; the video draws the preview in place of the Line's Lines. The
// panel (TemplateEditor.qml) shows and edits it. KE3: the built-in effect
// presets insert from New as template and code Lines that preview and apply.
#include "template_editor_controller.h"

#include "hikari/application/edit_session.h"
#include "hikari/application/karaoke_presets.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/application/script_resolution.h"
#include "hikari/application/template_preview.h"
#include "hikari/backends/karaoke_preview_runner.h"
#include "hikari/backends/lua_script_host.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/editor_syntax.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QtTest>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>

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

// KE3: the Document karaoke_presets_tests applies each preset to, and that
// test's golden for a preset file.
core::Document presetFixture()
{
    QFile f(QStringLiteral(HIKARI_KARAOKE_PRESET_FIXTURES "/fixture.ass"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    std::vector<std::byte> bytes(static_cast<std::size_t>(data.size()));
    std::memcpy(bytes.data(), data.constData(), bytes.size());
    return core::loadAss(bytes).document;
}

std::string presetGolden(const QString &file)
{
    QFile f(QStringLiteral(HIKARI_KARAOKE_PRESET_FIXTURES "/") + QFileInfo(file).completeBaseName() +
            QStringLiteral(".out.ass"));
    return f.open(QIODevice::ReadOnly) ? f.readAll().toStdString() : std::string();
}

std::optional<application::KaraokePreset> presetFile(const QString &file)
{
    QFile f(QStringLiteral(":/hikari/ui/karaoke-presets/") + file);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray data = f.readAll();
    return application::readKaraokePreset(
        std::as_bytes(std::span(data.constData(), static_cast<std::size_t>(data.size()))));
}

QString qs(const std::u8string &s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
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
        std::string text;
        for (const auto &d : snapshot.dialogues)
            text += d.text + '\n';
        texts.push_back(std::move(text));
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
    std::vector<std::string> texts; // each run's Lines' text, joined
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
    // The templates without Lua (no `!` expression, no code Line), so that
    // karaoke text edits keep the choice.
    static core::Document withoutLua(core::Document doc)
    {
        std::vector<core::LineId> lua;
        for (const auto *l : doc.lines())
            if (application::templateKindOf(*l) && application::macroLineOf(*l).text.find('!') != std::string::npos)
                lua.push_back(l->id);
        for (const auto id : lua)
            doc.editLine(id, [](core::LineRecord &l) { l.text = u8"{\\bord1}"; });
        doc.removeLinesIf([](const core::LineRecord &l) {
            return application::templateKindOf(l) == application::TemplateKind::Code;
        });
        return doc;
    }
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

    // A problem the last karaoke Line's preview had goes with that Line: no
    // preview is left to say otherwise.
    void aProblemGoesWithTheLastKaraokeLine()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(previewCame());
        editor->setText(QStringLiteral("{\\fscx!nosuch.field!}") + editor->text());
        editor->commit();
        QVERIFY(QTest::qWaitFor([&] { return editor->hasProblems() && !editor->previewPending(); }, 20'000));
        QVERIFY(change([](core::Document &d) {
            return d.removeLinesIf([](const core::LineRecord &l) {
                       return application::isKaraokeLine(l) && !application::templateKindOf(l);
                   }) > 0;
        }));
        editor->refresh();
        QCOMPARE(editor->source(), 0);
        QVERIFY(!editor->hasProblems());
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
    // resolution): a change of it previews again. Tag-only templates: under
    // Lua the change is not one the user chose to run
    // (luaReadingTheHeadTrustsItsStylesAndInfo).
    void aScriptInfoChangePreviewsAgain()
    {
        openDocument(withoutLua(loadFile(QStringLiteral("stock-tutorials.ass"))), true);
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
        editor->applied(templater->report);
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

    // The stock furigana run makes its `-furigana` Style when the file has
    // none (karaskel.collect_head): Apply puts it in, so the video draws the
    // preview with it; the file stays without it.
    void theVideoDrawsWithTheStylesTheRunMakes()
    {
        core::Document doc = loadFile(QStringLiteral("stock-furigana.ass"));
        QVERIFY(doc.removeStyle(u8"Default-furigana"));
        openDocument(std::move(doc), true);
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
        const auto styled = [](const core::Document &d, std::u8string_view name) {
            for (const auto &section : d.sections())
                for (const auto &record : section.records)
                    if (const auto *st = std::get_if<core::StyleRecord>(&record); st && st->name == name)
                        return true;
            return false;
        };
        QVERIFY(styled(*shown, u8"Default-furigana"));
        QVERIFY(!styled(session->document(), u8"Default-furigana"));
        bool furigana = false;
        for (const auto *x : shown->lines())
            furigana = furigana || x->style == u8"Default-furigana";
        QVERIFY(furigana);
    }

    // The matching run for a file without marks runs the templates as the
    // file holds them, not the Line editor's draft: it runs only once the user
    // chose that state too, so a draft taking Lua out of a template and then
    // Preview never runs the Lua.
    void theMatchingRunNeedsTheFilesOwnStateChosen()
    {
        open(QStringLiteral("stock-tutorials.out.ass"), false);
        std::optional<core::LineId> lua;
        std::string luaText;
        for (const auto *l : session->document().lines())
            if (!lua && application::templateKindOf(*l) && application::macroLineOf(*l).text.find('!') != std::string::npos) {
                lua = l->id;
                luaText = application::macroLineOf(*l).text;
            }
        QVERIFY(lua);
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(session->editDraftText(*lua, u8"{\\bord9}"));
        editor->refresh();
        QVERIFY(!editor->trusted());
        editor->trustDocument();
        QVERIFY(editor->trusted());
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QVERIFY(templater->previews > 0);
        for (const auto &text : templater->texts)
            QVERIFY(text.find(luaText) == std::string::npos);
        // The draft goes in: the file's state is the one chosen, and matching
        // runs.
        QVERIFY(session->commitDraft());
        editor->refresh();
        const int before = templater->previews;
        QVERIFY(QTest::qWaitFor([&] { return templater->previews > before && settled(); }, 20'000));
        QVERIFY(std::ranges::any_of(templater->texts, [](const std::string &t) { return t.find("{\\bord9}") != std::string::npos; }));
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

    // A file opened without marks where a karaoke Line made no Lines (its
    // template came after): its first preview stands before the Lines of
    // the karaoke Lines after it, as a whole Apply writes them.
    void aFirstPreviewInAnUnmarkedFileStandsBeforeLaterLines()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.out.ass"));
        std::optional<core::LineId> source;
        for (const auto *l : doc.lines())
            if (!source && application::isKaraokeLine(*l) && !application::templateKindOf(*l) && l->style == u8"T1b")
                source = l->id;
        QVERIFY(source);
        QCOMPARE(doc.removeLinesIf([](const core::LineRecord &l) {
            return application::isGeneratedLine(l) && l.style == u8"T1b";
        }), std::size_t{1});
        openDocument(std::move(doc), true);
        activate(*source);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled() && !previewTexts().isEmpty(); }, 20'000));
        QTest::qWait(500);
        QVERIFY(settled());
        const auto shown = editor->videoDocument(session->document(), 0, true); // every Line
        QVERIFY(shown);
        const QStringList preview = previewTexts();
        int first = -1, later = -1, at = 0;
        for (const auto *x : shown->lines()) {
            const QString text = QString::fromStdString(lineText(*x)).section(QLatin1Char(','), 9);
            if (first < 0 && preview.contains(text))
                first = at;
            if (later < 0 && application::isGeneratedLine(*x) && x->style == u8"T2a")
                later = at;
            ++at;
        }
        QVERIFY(first >= 0 && later >= 0);
        QVERIFY2(first < later, qPrintable(QStringLiteral("preview at %1, T2a's Lines at %2").arg(first).arg(later)));
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
            return doc.editLine(*source, [](core::LineRecord &l) {
                l.end.value = core::DocumentTime(l.end.value.microseconds() + 100'000);
            });
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

    // An edit typed before its file was reloaded has no Document left to go
    // into: it is dropped, not committed into the reloaded one.
    void anEditTypedBeforeAReloadIsDropped()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        std::pair<std::uint64_t, std::uint64_t> key{1, 1};
        editor->setDocumentKey([&] { return key; });
        editor->setSessionOf([](std::pair<std::uint64_t, std::uint64_t>) -> application::EditSession * { return nullptr; });
        editor->refresh();
        editor->trustDocument();
        editor->setDelays(60'000, 60'000);
        const QString before = editor->text();
        editor->setText(before + QStringLiteral("{\\bord3}"));
        session = std::make_unique<application::EditSession>(loadFile(QStringLiteral("stock-tutorials.ass")));
        key = {1, 2}; // the same Document, loaded again
        const std::size_t steps = session->historySize();
        editor->refresh();
        editor->commit();
        QCOMPARE(session->historySize(), steps);
        QCOMPARE(editor->text(), before);
        editor->setSessionOf({});
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
            return doc.editLine(*source, [](core::LineRecord &l) {
                l.end.value = core::DocumentTime(l.end.value.microseconds() + 100'000);
            });
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

    // A Line editor draft that gives the template a translation, or takes
    // it away, decides which text the panel shows, as the preview runs it.
    void theDraftsTranslationChoosesTheTextShown()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        std::optional<core::LineId> tl;
        for (const auto *l : doc.lines())
            if (!tl && application::templateKindOf(*l))
                tl = l->id;
        QVERIFY(tl);
        openDocument(std::move(doc), true);
        editor->setCurrent(tl->value);
        const auto rowText = [&] {
            for (const QVariant &v : editor->templates())
                if (v.toMap().value(QStringLiteral("id")).toULongLong() == tl->value)
                    return v.toMap().value(QStringLiteral("text")).toString();
            return QString();
        };
        application::DraftChange translated;
        translated.translation = u8"{\\bord6}";
        QVERIFY(session->editDraft(*tl, translated));
        editor->refresh();
        QCOMPARE(editor->text(), QStringLiteral("{\\bord6}"));
        QCOMPARE(rowText(), QStringLiteral("{\\bord6}"));
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

    // What the user chose is each whole state: a template from one preview
    // and an Actor from another were never run together, so their mix asks.
    void templatesAndActorsFromTwoChoicesAsk()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        std::optional<core::LineId> tl, karaoke;
        for (const auto *l : doc.lines()) {
            if (!tl && application::templateKindOf(*l) == application::TemplateKind::Template)
                tl = l->id;
            if (!karaoke && application::isKaraokeLine(*l) && !application::templateKindOf(*l))
                karaoke = l->id;
        }
        QVERIFY(tl && karaoke);
        QVERIFY(doc.editLine(*tl, [](core::LineRecord &l) { l.text = u8"{\\bord1}$actor"; }));
        QVERIFY(doc.editLine(*karaoke, [](core::LineRecord &l) { l.actor = u8"plain"; }));
        openDocument(std::move(doc), true);
        QVERIFY(editor->trusted());
        // Pasted over: a literal template and an expression for an Actor,
        // previewed as such.
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*tl, [](core::LineRecord &l) { l.text = u8"{\\bord1}literal"; }) &&
                   d.editLine(*karaoke, [](core::LineRecord &l) { l.actor = u8"!_G.marker = 1!"; });
        }));
        editor->refresh();
        QVERIFY(!editor->trusted());
        editor->trustDocument();
        QVERIFY(editor->trusted());
        // The first template's text pasted back: never run with that Actor.
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*tl, [](core::LineRecord &l) { l.text = u8"{\\bord1}$actor"; });
        }));
        editor->refresh();
        QVERIFY(!editor->trusted());
        // Back as it was chosen first, it runs again.
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*karaoke, [](core::LineRecord &l) { l.actor = u8"plain"; });
        }));
        editor->refresh();
        QVERIFY(editor->trusted());
    }

    // A template that puts an Actor or Style into an expression (`!$actor!`)
    // makes any Actor Lua: then every karaoke Line is trusted as a whole, so
    // a blank one given text asks again.
    void aTemplateRunningActorsTrustsWholeLines()
    {
        // The templater reads a variable's name in any case.
        for (const std::u8string expression : {u8"!$actor!", u8"!$ACTOR!", u8"!$Style!"}) {
            core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
            std::optional<core::LineId> tl;
            std::vector<core::LineId> karaoke;
            for (const auto *l : doc.lines()) {
                if (!tl && application::templateKindOf(*l) == application::TemplateKind::Template)
                    tl = l->id;
                if (application::isKaraokeLine(*l) && !application::templateKindOf(*l))
                    karaoke.push_back(l->id);
            }
            QVERIFY(tl && !karaoke.empty());
            QVERIFY(doc.editLine(*tl, [&](core::LineRecord &l) { l.text = u8"{\\bord1}" + expression; }));
            QVERIFY(doc.editLine(karaoke.front(), [](core::LineRecord &l) {
                l.text.clear();
                l.actor = u8"_G.marker = 1";
            }));
            openDocument(std::move(doc), true);
            QVERIFY(editor->trusted());
            QVERIFY(change([&](core::Document &d) {
                return d.editLine(karaoke.front(), [](core::LineRecord &l) { l.text = u8"{\\k50}ka"; });
            }));
            editor->refresh();
            QVERIFY(!editor->trusted());
        }
    }

    // Lua in the templates (a code Line, or a `!` expression in a template)
    // can read a karaoke Line's text (`orgline.text`): then a new text,
    // translation or Effect asks again, a retime doesn't. Templates with no
    // Lua leave the text to the user.
    void luaReadingTheLinesTrustsTheirText()
    {
        const auto openWith = [&](const std::u8string &effect, const std::u8string &text) {
            core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
            std::vector<core::LineId> templates;
            std::optional<core::LineId> karaoke;
            for (const auto *l : doc.lines()) {
                if (application::templateKindOf(*l))
                    templates.push_back(l->id);
                else if (!karaoke && application::isKaraokeLine(*l))
                    karaoke = l->id;
            }
            for (const auto id : templates)
                doc.editLine(id, [](core::LineRecord &l) { l.text = u8"{\\bord1}"; });
            core::LineRecord added;
            added.comment = true;
            added.effect = effect;
            added.text = text;
            for (const auto *l : doc.lines())
                if (l->id == templates.front())
                    added.style = l->style;
            doc.appendLine(added);
            openDocument(std::move(doc), true);
            return *karaoke;
        };
        const auto edit = [&](core::LineId id, auto how) {
            QVERIFY(change([&](core::Document &d) { return d.editLine(id, how); }));
            editor->refresh();
        };
        // Tag-only templates: the text is the user's.
        auto karaoke = openWith(u8"template line", u8"{\\bord2}");
        QVERIFY(editor->trusted());
        edit(karaoke, [](core::LineRecord &l) { l.text = u8"{\\k50}zzq"; });
        QVERIFY(editor->trusted());
        for (const auto &[effect, text] : {std::pair<std::u8string, std::u8string>{u8"code line", u8"_G.marker = orgline.text"},
                                           {u8"template line", u8"{\\bord!1!}"}}) {
            karaoke = openWith(effect, text);
            QVERIFY(editor->trusted());
            edit(karaoke, [](core::LineRecord &l) {
                l.start.value = core::DocumentTime(l.start.value.microseconds() + 100'000);
                l.end.value = core::DocumentTime(l.end.value.microseconds() + 100'000);
            });
            QVERIFY(editor->trusted());
            edit(karaoke, [](core::LineRecord &l) { l.text = u8"{\\k50}zzq"; });
            QVERIFY(!editor->trusted());
        }
    }

    // Lua reads the file's Styles (`orgline.styleref`) and Script Info
    // (`meta`) too: a Style or property changed since (an import) is not
    // what the user chose to run. Tag-only templates read neither.
    void luaReadingTheHeadTrustsItsStylesAndInfo()
    {
        const auto openWith = [&](const std::u8string &effect, const std::u8string &text) {
            core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
            std::vector<core::LineId> templates;
            for (const auto *l : doc.lines())
                if (application::templateKindOf(*l))
                    templates.push_back(l->id);
            for (const auto id : templates)
                doc.editLine(id, [](core::LineRecord &l) { l.text = u8"{\\bord1}"; });
            core::LineRecord added;
            added.comment = true;
            added.effect = effect;
            added.text = text;
            for (const auto *l : doc.lines())
                if (l->id == templates.front())
                    added.style = l->style;
            doc.appendLine(added);
            openDocument(std::move(doc), true);
        };
        const auto firstStyle = [&] {
            for (const auto &section : session->document().sections())
                for (const auto &record : section.records)
                    if (const auto *st = std::get_if<core::StyleRecord>(&record))
                        return st->fields;
            return std::vector<std::u8string>{};
        };
        const auto restyle = [&] {
            auto fields = firstStyle();
            QVERIFY(fields.size() > 1);
            fields[1] = u8"Imported Font";
            QVERIFY(change([&](core::Document &d) { return d.editStyle(0, fields); }));
            editor->refresh();
        };
        const auto retitle = [&] {
            QVERIFY(change([](core::Document &d) { return d.setScriptInfo(u8"Title", u8"Imported"); }));
            editor->refresh();
        };
        openWith(u8"template line", u8"{\\bord2}");
        QVERIFY(editor->trusted());
        restyle();
        retitle();
        QVERIFY(editor->trusted());
        openWith(u8"code line", u8"_G.marker = orgline.styleref.fontname");
        QVERIFY(editor->trusted());
        restyle();
        QVERIFY(!editor->trusted());
        openWith(u8"template line", u8"{\\bord!1!}");
        QVERIFY(editor->trusted());
        retitle();
        QVERIFY(!editor->trusted());
    }

    // An Actor or Style holding a `!` is an expression once a template puts
    // it in (`$actor`): Lua, which reads the Styles, Script Info and every
    // karaoke Line, as a template's own would.
    void anExpressionInAnActorTrustsTheHeadAndLines()
    {
        core::Document doc = withoutLua(loadFile(QStringLiteral("stock-tutorials.ass")));
        std::vector<core::LineId> templates, karaoke;
        for (const auto *l : doc.lines()) {
            if (application::templateKindOf(*l))
                templates.push_back(l->id);
            else if (application::isKaraokeLine(*l))
                karaoke.push_back(l->id);
        }
        QVERIFY(!templates.empty() && karaoke.size() >= 2);
        for (const auto id : templates)
            doc.editLine(id, [](core::LineRecord &l) { l.text = u8"{\\bord1}$actor"; });
        doc.editLine(karaoke.front(), [](core::LineRecord &l) { l.actor = u8"!_G.marker = orgline.styleref.fontname!"; });
        openDocument(std::move(doc), true);
        QVERIFY(editor->trusted());
        std::vector<std::u8string> fields;
        for (const auto &section : session->document().sections())
            for (const auto &record : section.records)
                if (const auto *st = std::get_if<core::StyleRecord>(&record); st && fields.empty())
                    fields = st->fields;
        QVERIFY(fields.size() > 1);
        fields[1] = u8"Imported Font";
        QVERIFY(change([&](core::Document &d) { return d.editStyle(0, fields); }));
        editor->refresh();
        QVERIFY(!editor->trusted());
        QVERIFY(session->undo());
        editor->refresh();
        QVERIFY(editor->trusted());
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(karaoke.back(), [](core::LineRecord &l) { l.text = u8"{\\k50}zzq"; });
        }));
        editor->refresh();
        QVERIFY(!editor->trusted());
    }

    // A named template's parts are joined before the templater runs them:
    // an expression split across two parts is Lua too.
    void anExpressionSplitAcrossNamedPartsTrustsWholeLines()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        std::vector<core::LineId> templates, karaoke;
        for (const auto *l : doc.lines()) {
            if (application::templateKindOf(*l))
                templates.push_back(l->id);
            else if (application::isKaraokeLine(*l))
                karaoke.push_back(l->id);
        }
        QVERIFY(templates.size() >= 2 && !karaoke.empty());
        for (const auto id : templates)
            QVERIFY(doc.editLine(id, [](core::LineRecord &l) { l.text = u8"{\\bord1}"; }));
        QVERIFY(doc.editLine(templates[0], [](core::LineRecord &l) {
            l.effect = u8"template line joined";
            l.text = u8"{\\bord1}!$act";
        }));
        QVERIFY(doc.editLine(templates[1], [&](core::LineRecord &l) {
            l.effect = u8"template line joined";
            l.text = u8"or!";
        }));
        QVERIFY(doc.editLine(karaoke.front(), [](core::LineRecord &l) {
            l.text.clear();
            l.actor = u8"_G.marker = 1";
        }));
        openDocument(std::move(doc), true);
        QVERIFY(editor->trusted());
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(karaoke.front(), [](core::LineRecord &l) { l.text = u8"{\\k50}ka"; });
        }));
        editor->refresh();
        QVERIFY(!editor->trusted());
    }

    // Whether a karaoke Line's Actor reaches the templates hangs on its other
    // fields too (a blank Line `noblank` passes over): an Actor or Style
    // holding `!` is trusted with the whole Line, so a new text asks again.
    // It is Lua in the file, so the other karaoke Lines' text is chosen too
    // (anExpressionInAnActorTrustsTheHeadAndLines).
    void anActorHoldingAnExpressionIsTrustedWithItsWholeLine()
    {
        core::Document doc = withoutLua(loadFile(QStringLiteral("stock-tutorials.ass")));
        std::vector<core::LineId> karaoke;
        for (const auto *l : doc.lines())
            if (application::isKaraokeLine(*l) && !application::templateKindOf(*l))
                karaoke.push_back(l->id);
        QVERIFY(karaoke.size() >= 2);
        const std::optional<core::LineId> blank = karaoke[0], plain = karaoke[1];
        QVERIFY(doc.editLine(*blank, [](core::LineRecord &l) {
            l.text.clear();
            l.actor = u8"!_G.marker = 1!";
        }));
        QVERIFY(doc.editLine(*plain, [](core::LineRecord &l) { l.actor = u8"Aiko"; }));
        openDocument(std::move(doc), true);
        QVERIFY(editor->trusted());
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*plain, [](core::LineRecord &l) { l.text = u8"{\\k50}ka"; });
        }));
        editor->refresh();
        QVERIFY(!editor->trusted());
        QVERIFY(session->undo());
        editor->refresh();
        QVERIFY(editor->trusted());
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*blank, [](core::LineRecord &l) { l.text = u8"{\\k50}ka"; });
        }));
        editor->refresh();
        QVERIFY(!editor->trusted());
    }

    // The stock selected-Lines run takes karaoke Lines and the generated
    // Lines marked with theirs: with only a file's unmarked Lines selected
    // it would refuse, so the panel offers it only when it can run.
    void applyToTheSelectedLinesNeedsAKaraokeLineSelected()
    {
        open(QStringLiteral("stock-tutorials.out.ass"));
        std::optional<core::LineId> unmarked;
        for (const auto *g : session->document().lines())
            if (!unmarked && application::isGeneratedLine(*g) && g->generatedFrom.value == 0)
                unmarked = g->id;
        QVERIFY(unmarked);
        editor->setShown(true);
        activate(*unmarked);
        QVERIFY(QTest::qWaitFor([&] { return editor->canApply(); }, 20'000));
        QVERIFY(!editor->canApplySelected());
        activate(*firstKaraoke());
        QVERIFY(QTest::qWaitFor([&] { return editor->canApplySelected(); }, 20'000));
        // A Line marked with its karaoke Line counts too.
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*unmarked, [&](core::LineRecord &l) { l.generatedFrom = *firstKaraoke(); });
        }));
        activate(*unmarked);
        QVERIFY(QTest::qWaitFor([&] { return editor->canApplySelected(); }, 20'000));
    }

    // An unmarked generated Line the Line editor's draft makes a karaoke
    // Line is the one Apply runs on: its old karaoke Line no longer counts.
    void aDraftThatEndsAnUnmarkedLinesOutputPreviewsIt()
    {
        open(QStringLiteral("stock-tutorials.out.ass"));
        std::optional<core::LineId> clicked;
        for (const auto *g : session->document().lines())
            if (!clicked && application::isGeneratedLine(*g))
                clicked = g->id;
        QVERIFY(clicked);
        editor->setShown(true);
        activate(*clicked);
        QVERIFY(QTest::qWaitFor([&] { return editor->source() != 0 && settled(); }, 20'000));
        QVERIFY(editor->source() != clicked->value);
        application::DraftChange effect;
        effect.effect = u8"karaoke";
        QVERIFY(session->editDraft(*clicked, effect));
        editor->refresh();
        QCOMPARE(editor->source(), clicked->value);
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

    // A named template joins its Lines' text in file order, and code Lines
    // run in it: the same Lines in another order are other Lua, so a sort
    // or a move outside the panel asks first.
    void templatesReorderedElsewhereAskFirst()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        std::vector<core::LineId> templ;
        for (const auto *l : doc.lines())
            if (application::templateKindOf(*l))
                templ.push_back(l->id);
        QVERIFY(templ.size() >= 2);
        openDocument(std::move(doc), true);
        QVERIFY(editor->trusted());
        QVERIFY(change([&](core::Document &d) { return d.moveLine(templ[1], templ[0]); }));
        editor->refresh();
        QVERIFY(!editor->trusted());
    }

    // Apply commits the Line editor's draft before it runs, so the preview
    // runs on the draft too; and a drafted Actor is one the user has not
    // chosen to run yet.
    void theLineEditorsDraftIsPreviewed()
    {
        openDocument(withoutLua(loadFile(QStringLiteral("stock-tutorials.ass"))), true);
        const auto source = firstKaraoke();
        activate(*source);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QVERIFY(!previewTexts().join(QString()).contains(QStringLiteral("zzq")));
        const auto revision = session->revision();
        QVERIFY(session->editDraftText(*source, u8"{\\k50}zzq{\\k50}yyq"));
        editor->refresh();
        QVERIFY(QTest::qWaitFor(
            [&] { return settled() && previewTexts().join(QString()).contains(QStringLiteral("zzq")); }, 20'000));
        QCOMPARE(session->revision(), revision); // still a draft
        application::DraftChange actor;
        actor.actor = u8"!_G.marker = 1!";
        QVERIFY(session->editDraft(*source, actor));
        editor->refresh();
        QVERIFY(!editor->trusted());
    }

    // The `$` values of a TLMode Line are read from the translation, where
    // the templater writes them.
    void aTranslatedLinesValuesAreRead()
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
        editor->requestValues();
        QVERIFY(QTest::qWaitFor([&] { return !editor->values().isEmpty(); }, 20'000));
        bool number = false;
        editor->values().value(QStringLiteral("swidth")).toString().toDouble(&number);
        QVERIFY(number);
    }

    // A file whose generated Lines its templates no longer give: the matching
    // run's refusal is the preview's problem, as Apply's would be, and is
    // not kept as a match: once the Lines match again, it previews.
    void aFailedMatchIsNotKept()
    {
        open(QStringLiteral("stock-tutorials.out.ass"));
        const auto source = firstKaraoke();
        std::optional<core::LineId> generated;
        std::u8string was;
        // Another karaoke Line's (the last one's): those must match.
        for (const auto *l : session->document().lines())
            if (application::isGeneratedLine(*l) && l->generatedFrom.value == 0) {
                generated = l->id;
                was = l->text;
            }
        QVERIFY(generated);
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*generated, [](core::LineRecord &l) { l.text += u8"{\\bord9}"; });
        }));
        activate(*source);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return !editor->previewPending() && !editor->problem().isEmpty(); }, 20'000));
        QVERIFY(!editor->hasPreview());
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*generated, [&](core::LineRecord &l) { l.text = was; });
        }));
        editor->refresh();
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QVERIFY(editor->problem().isEmpty());
    }

    // Another karaoke Line's generated Line changed, its id kept: the file's
    // Lines are matched again, and the refusal Apply would give is the
    // preview's problem.
    void aGeneratedLineChangedInPlaceIsMatchedAgain()
    {
        open(QStringLiteral("stock-tutorials.out.ass"));
        const auto source = firstKaraoke();
        std::optional<core::LineId> generated;
        for (const auto *l : session->document().lines())
            if (application::isGeneratedLine(*l) && l->generatedFrom.value == 0)
                generated = l->id; // the last karaoke Line's
        QVERIFY(generated);
        activate(*source);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled() && editor->hasPreview(); }, 20'000));
        QVERIFY(editor->problem().isEmpty());
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*generated, [](core::LineRecord &l) { l.text += u8"{\\bord9}"; });
        }));
        editor->refresh();
        QVERIFY(QTest::qWaitFor([&] { return !editor->previewPending() && !editor->problem().isEmpty(); }, 20'000));
    }

    // While a macro holds the Document, an edit that can't go in keeps its
    // template current: choosing another one would lose it.
    void anEditThatCantGoInKeepsItsTemplate()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return editor->current() != 0; }, 20'000));
        const qulonglong first = editor->current();
        qulonglong other = 0;
        for (const QVariant &v : editor->templates())
            if (const auto id = v.toMap().value(QStringLiteral("id")).toULongLong(); id != first && !other)
                other = id;
        QVERIFY(other);
        editor->setDelays(60'000, 60'000);
        const QString typed = editor->text() + QStringLiteral("{\\bord8}");
        session->setReadOnly(true);
        editor->setText(typed);
        editor->setCurrent(other);
        QCOMPARE(editor->current(), first);
        QCOMPARE(editor->text(), typed);
        session->setReadOnly(false);
        editor->commit();
        for (const auto *l : session->document().lines())
            if (l->id.value == first)
                QCOMPARE(QString::fromStdString(std::string(l->text.begin(), l->text.end())), typed);
        editor->setCurrent(other);
        QCOMPARE(editor->current(), other);
    }

    // Apply to the selected Lines runs on the active Line as the draft makes
    // it: an Effect typed that makes it a karaoke Line makes it the Line
    // previewed.
    void aDraftThatMakesAKaraokeLineIsPreviewed()
    {
        core::Document doc = withoutLua(loadFile(QStringLiteral("stock-tutorials.ass")));
        std::vector<core::LineId> karaoke;
        for (const auto *l : doc.lines())
            if (application::isKaraokeLine(*l) && !application::templateKindOf(*l))
                karaoke.push_back(l->id);
        QVERIFY(karaoke.size() >= 2);
        QVERIFY(doc.editLine(karaoke[1], [](core::LineRecord &l) { l.effect = u8"foo"; }));
        openDocument(std::move(doc), true);
        activate(karaoke[1]);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QCOMPARE(editor->source(), karaoke[0].value);
        application::DraftChange effect;
        effect.effect = u8"karaoke";
        QVERIFY(session->editDraft(karaoke[1], effect));
        editor->refresh();
        QVERIFY(QTest::qWaitFor([&] { return editor->source() == karaoke[1].value && settled(); }, 20'000));
    }

    // A generated Line clicked that no karaoke Line gives with these
    // templates previews no other Line in its place, and says why.
    void aGeneratedLineNoTemplateGivesPreviewsNothingElse()
    {
        open(QStringLiteral("stock-tutorials.out.ass"));
        std::optional<core::LineId> clicked;
        for (const auto *l : session->document().lines())
            if (application::isGeneratedLine(*l) && l->generatedFrom.value == 0)
                clicked = l->id;
        QVERIFY(clicked);
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*clicked, [](core::LineRecord &l) { l.text += u8"{\\bord9}"; });
        }));
        editor->setShown(true);
        activate(*clicked);
        QVERIFY(QTest::qWaitFor([&] { return editor->sourceUnknown(); }, 20'000));
        QCOMPARE(editor->source(), 0ull);
        QVERIFY(!editor->hasPreview());
    }

    // Loop the Line plays the times the preview ran on: the draft's.
    void theLoopPlaysTheDraftsTimes()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const auto source = firstKaraoke();
        activate(*source);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        const int end = editor->sourceEndMs();
        application::DraftChange times;
        times.end = core::DocumentTime((static_cast<std::int64_t>(end) + 1000) * 1000);
        QVERIFY(session->editDraft(*source, times));
        editor->refresh();
        QCOMPARE(editor->sourceEndMs(), end + 1000);
    }

    // A modifier's argument may be a modifier's word (`fx loop`: the
    // syllables marked `loop`); the picker leaves it alone.
    void aModifiersArgumentIsNotAModifier()
    {
        TemplateEditorController e;
        QVERIFY(!e.hasModifier(QStringLiteral("template syl fx loop"), QStringLiteral("loop 2")));
        QVERIFY(e.hasModifier(QStringLiteral("template syl fx loop"), QStringLiteral("fx name")));
        QCOMPARE(e.effectWithModifier(QStringLiteral("template syl fx loop"), QStringLiteral("loop 2"), false),
                 QStringLiteral("template syl fx loop"));
        QCOMPARE(e.effectWithModifier(QStringLiteral("template syl fx loop"), QStringLiteral("loop 2"), true),
                 QStringLiteral("template syl fx loop loop 2"));
        QCOMPARE(e.effectWithModifier(QStringLiteral("template syl fx loop loop 2"), QStringLiteral("loop 2"), false),
                 QStringLiteral("template syl fx loop"));
    }

    // `repeat` is the templater's other word for `loop`: the picker's Repeat
    // reads and removes either.
    void repeatIsTheLoopModifiersOtherWord()
    {
        TemplateEditorController e;
        QVERIFY(e.hasModifier(QStringLiteral("template syl repeat 3"), QStringLiteral("loop 2")));
        QCOMPARE(e.effectWithModifier(QStringLiteral("template syl repeat 3 noblank"), QStringLiteral("loop 2"), false),
                 QStringLiteral("template syl noblank"));
        QCOMPARE(e.effectWithModifier(QStringLiteral("template syl REPEAT 3"), QStringLiteral("loop 2"), true),
                 QStringLiteral("template syl loop 2"));
        QVERIFY(!e.hasModifier(QStringLiteral("template syl fx repeat"), QStringLiteral("loop 2")));
    }

    // The panel's own edit that takes a Line out of the templates keeps the
    // user's choice for the others.
    void anEffectEditThatEndsATemplateKeepsTheChoice()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QVERIFY(editor->trusted());
        editor->setEffect(QStringLiteral("note"));
        editor->commit();
        editor->refresh();
        QVERIFY(editor->trusted());
    }

    // A TLMode Line's generated Lines differ in their translations only:
    // the current template's own are told apart by them.
    void aTranslatedLinesOwnLinesAreMarked()
    {
        const auto own = [&] {
            QList<bool> out;
            for (const QVariant &v : editor->previewLines())
                out << v.toMap().value(QStringLiteral("own")).toBool();
            return out;
        };
        open(QStringLiteral("stock-environment.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled() && own().contains(true); }, 20'000));
        QTest::qWait(300);
        const QList<bool> plain = own();
        QVERIFY(plain.contains(false));
        core::Document doc = loadFile(QStringLiteral("stock-environment.ass"));
        std::optional<core::LineId> source;
        for (const auto *l : doc.lines())
            if (!source && application::isKaraokeLine(*l) && !application::templateKindOf(*l))
                source = l->id;
        QVERIFY(doc.editLine(*source, [](core::LineRecord &l) {
            l.translation = l.text;
            l.text = u8"original";
        }));
        openDocument(std::move(doc), true);
        activate(*source);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled() && own().contains(true); }, 20'000));
        QTest::qWait(300);
        QCOMPARE(own(), plain);
    }

    // Another tab's generated Lines and their karaoke Lines are not this
    // one's: a tab chosen finds its own.
    void anotherTabsOwnersAreNotThisOnes()
    {
        open(QStringLiteral("stock-tutorials.out.ass"));
        // A generated Line of the last karaoke Line.
        std::optional<core::LineId> clicked;
        for (const auto *l : session->document().lines())
            if (application::isGeneratedLine(*l) && l->generatedFrom.value == 0)
                clicked = l->id;
        editor->setShown(true);
        activate(*clicked);
        QVERIFY(QTest::qWaitFor([&] { return editor->source() != 0 && settled(); }, 20'000));
        const auto owner = editor->source();
        // The same file in another tab at the same revision, that Line no
        // karaoke Line's there: the old tab's answer would pick its owner.
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.out.ass"));
        QVERIFY(doc.editLine(*clicked, [](core::LineRecord &l) { l.text += u8"{\\bord9}"; }));
        auto next = std::make_unique<application::EditSession>(std::move(doc));
        QCOMPARE(next->revision(), session->revision());
        session = std::move(next);
        editor->trustDocument();
        activate(*clicked);
        QVERIFY(QTest::qWaitFor([&] { return editor->sourceUnknown(); }, 20'000));
        QVERIFY(editor->source() != owner);
    }

    // An Apply whose run had problems on other karaoke Lines says so, not
    // that it went well.
    void anApplyWithProblemsSaysSo()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        editor->setShown(true);
        showPanel();
        QTRY_VERIFY(item(QStringLiteral("templateApply"))->isEnabled());
        click(item(QStringLiteral("templateApply")));
        std::string report = templater->report;
        const auto at = report.find("\"problems\":0");
        QVERIFY(at != std::string::npos);
        report.replace(at, 12, "\"problems\":2");
        editor->applied(report);
        editor->refresh();
        QTRY_VERIFY(textOf(QStringLiteral("templateStatus")).startsWith(QStringLiteral("Applied with problems")));
    }

    // The Line editor holding a draft of the template edited: the edit goes
    // in after it, not left pending for the timer (Save writes both).
    void aTemplateEditGoesInAfterTheLineEditorsDraft()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return editor->current() != 0; }, 20'000));
        const core::LineId templ{editor->current()};
        application::DraftChange actor;
        actor.actor = u8"drafted";
        QVERIFY(session->editDraft(templ, actor));
        editor->setText(editor->text() + QStringLiteral("{\\bord4}"));
        editor->commit(); // once, as Save's runs it
        QVERIFY(!session->draftLine());
        for (const auto *l : session->document().lines())
            if (l->id == templ) {
                QVERIFY(std::u8string_view(l->text).ends_with(u8"{\\bord4}"));
                QVERIFY(l->actor == u8"drafted");
            }
    }

    // The preview's heading names the Line as the draft has it.
    // Panel typing that ends at the committed text still overrides a Line
    // editor draft of that template: the draft goes in, then the panel's.
    void aPanelEditBackToTheCommittedTextOverridesTheDraft()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return editor->current() != 0; }, 20'000));
        const core::LineId templ{editor->current()};
        editor->setDelays(60'000, 60'000);
        const QString committed = editor->text();
        QVERIFY(session->editDraftText(templ, u8"{\\bord9}"));
        editor->setText(committed + QStringLiteral("x"));
        editor->setText(committed);
        editor->commit();
        QVERIFY(!session->draftLine());
        for (const auto *l : session->document().lines())
            if (l->id == templ)
                QCOMPARE(QString::fromStdString(std::string(l->text.begin(), l->text.end())), committed);
    }

    void thePreviewsHeadingReadsTheDraft()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const auto source = firstKaraoke();
        activate(*source);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QVERIFY(session->editDraftText(*source, u8"{\\k50}zzq"));
        editor->refresh();
        QCOMPARE(editor->sourceText(), QStringLiteral("zzq"));
    }

    // The picker's class is one choice: choosing another replaces every
    // class the Effect had.
    void choosingAClassReplacesEveryClass()
    {
        TemplateEditorController e;
        QCOMPARE(e.effectWithClass(QStringLiteral("template syl furi noblank"), QStringLiteral("line")),
                 QStringLiteral("template line noblank"));
        QCOMPARE(e.effectWithClass(QStringLiteral("code line syl"), QStringLiteral("once")),
                 QStringLiteral("code once"));
        QCOMPARE(e.effectParts(QStringLiteral("template syl furi noblank")).value(QStringLiteral("modifiers")).toStringList(),
                 QStringList{QStringLiteral("noblank")});
    }

    // A `template line karaoke` the panel moves to another Style is the
    // user's own edit: its new Style and Actor pairing is theirs too.
    void aPanelStyleOnATemplateThatIsAlsoAKaraokeLineKeepsTheChoice()
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
        openDocument(std::move(doc), true);
        QVERIFY(editor->trusted());
        editor->setCurrent(id->value);
        QVERIFY(editor->style() != QStringLiteral("T1a"));
        editor->setStyle(QStringLiteral("T1a")); // no T1a karaoke Line has the Actor `plain`
        editor->commit();
        editor->refresh();
        QVERIFY(editor->trusted());
    }

    // A Line editor draft of the template goes in before a panel edit of
    // another field: the draft is not the user's choice, so it still asks.
    void aDraftCommittedWithAPanelEditStillAsks()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return editor->current() != 0; }, 20'000));
        QVERIFY(editor->trusted());
        const core::LineId templ{editor->current()};
        QVERIFY(session->editDraftText(templ, u8"!_G.marker = 1!"));
        editor->refresh();
        QVERIFY(!editor->trusted());
        editor->setLayer(editor->layer() + 1);
        QVERIFY(!session->draftLine());
        editor->refresh();
        QVERIFY(!editor->trusted());
        // An Actor drafted on a `template line karaoke`, then its Style
        // changed in the panel: the drafted pairing asks too.
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
        openDocument(std::move(doc), true);
        editor->setCurrent(id->value);
        QVERIFY(editor->trusted());
        application::DraftChange actor;
        actor.actor = u8"!_G.marker = 1!";
        QVERIFY(session->editDraft(*id, actor));
        QVERIFY(editor->style() != QStringLiteral("T1a"));
        editor->setStyle(QStringLiteral("T1a"));
        editor->commit();
        QVERIFY(!session->draftLine());
        editor->refresh();
        QVERIFY(!editor->trusted());
    }

    // A pending Effect, Style or Layer goes onto the Line editor's draft of
    // the template, as the commit puts it: the drafted Layer stays.
    void aPendingEffectKeepsTheLineEditorsDraft()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(previewCame());
        const core::LineId templ{editor->current()};
        application::DraftChange layer;
        layer.layer = 7;
        QVERIFY(session->editDraft(templ, layer));
        editor->setDelays(10, 60'000);
        editor->setEffect(editor->effect() + QStringLiteral(" keeptags"));
        QVERIFY(editor->trusted());
        QVERIFY(QTest::qWaitFor(
            [&] {
                if (!settled())
                    return false;
                for (const QVariant &v : editor->previewLines())
                    if (v.toMap().value(QStringLiteral("own")).toBool() &&
                        v.toMap().value(QStringLiteral("layer")).toInt() == 7)
                        return true;
                return false;
            },
            20'000));
    }

    // An Effect still pending that makes the active template a karaoke Line
    // too previews that Line, as the commit will.
    void aPendingEffectChoosesTheKaraokeLine()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        std::optional<core::LineId> templ;
        for (const auto *l : session->document().lines())
            if (application::templateKindOf(*l) == application::TemplateKind::Template && l->effect == u8"template line")
                templ = l->id;
        QVERIFY(templ);
        activate(*templ);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled() && editor->source() != 0; }, 20'000));
        QVERIFY(editor->source() != templ->value);
        QCOMPARE(editor->current(), templ->value);
        editor->setDelays(10, 60'000);
        editor->setEffect(QStringLiteral("template line karaoke"));
        QVERIFY(QTest::qWaitFor([&] { return editor->source() == templ->value; }, 5'000));
    }

    // The `$` values of a `template line karaoke` source come from the probe
    // alone: its own template writes nothing in their place.
    void theValuesOfATemplateThatIsAlsoAKaraokeLine()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        core::LineRecord both;
        for (const auto *l : doc.lines())
            if (application::isKaraokeLine(*l) && !application::templateKindOf(*l))
                both = *l;
        both.comment = true;
        both.effect = u8"template line karaoke";
        both.text = u8"{\\k50}ka{\\k50}ze";
        both.actor = u8"plain";
        const auto id = doc.appendLine(both);
        QVERIFY(id);
        openDocument(std::move(doc), true);
        activate(*id);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled() && editor->source() == id->value; }, 20'000));
        editor->requestValues();
        QVERIFY(QTest::qWaitFor([&] { return !editor->values().isEmpty(); }, 20'000));
        QCOMPARE(editor->values().value(QStringLiteral("actor")).toString(), QStringLiteral("plain"));
        QCOMPARE(editor->values().value(QStringLiteral("style")).toString(), QString::fromStdString(std::string(both.style.begin(), both.style.end())));
    }

    // A `template line karaoke` source's text still pending in the panel is
    // the one its `$` values come from, as the preview's.
    void theValuesReadAPendingTextOfATemplateThatIsAlsoAKaraokeLine()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        core::LineRecord both;
        for (const auto *l : doc.lines())
            if (application::isKaraokeLine(*l) && !application::templateKindOf(*l))
                both = *l;
        both.comment = true;
        both.effect = u8"template line karaoke";
        both.text = u8"{\\k50}ka{\\k50}ze";
        const auto id = doc.appendLine(both);
        QVERIFY(id);
        openDocument(std::move(doc), true);
        activate(*id);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled() && editor->source() == id->value; }, 20'000));
        editor->setCurrent(id->value);
        editor->setDelays(10, 60'000);
        editor->setText(QStringLiteral("{\\k50}ka{\\k50}ze{\\k50}ru"));
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        editor->requestValues();
        QVERIFY(QTest::qWaitFor([&] { return !editor->values().isEmpty(); }, 20'000));
        QCOMPARE(editor->values().value(QStringLiteral("syln")).toString(), QStringLiteral("3"));
    }

    // A `template line karaoke` both chosen and previewed keeps its own
    // Lines marked: the run that tells them apart still has it as the
    // karaoke Line.
    void aTemplateThatIsAlsoTheKaraokeLineMarksItsOwnLines()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        core::LineRecord both;
        for (const auto *l : doc.lines())
            if (application::isKaraokeLine(*l) && !application::templateKindOf(*l))
                both = *l;
        both.comment = true;
        both.effect = u8"template line karaoke";
        both.text = u8"{\\k50}ka{\\k50}ze";
        const auto id = doc.appendLine(both);
        QVERIFY(id);
        openDocument(std::move(doc), true);
        activate(*id);
        editor->setShown(true);
        editor->setCurrent(id->value);
        QVERIFY(QTest::qWaitFor(
            [&] {
                if (!settled() || editor->source() != id->value || editor->current() != id->value)
                    return false;
                for (const QVariant &v : editor->previewLines())
                    if (v.toMap().value(QStringLiteral("own")).toBool())
                        return true;
                return false;
            },
            20'000));
    }

    // The heading of a `template line karaoke` source reads the text still
    // pending in the panel, as its preview does.
    void thePreviewsHeadingReadsAPendingTextOfATemplateThatIsAlsoAKaraokeLine()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        core::LineRecord both;
        for (const auto *l : doc.lines())
            if (application::isKaraokeLine(*l) && !application::templateKindOf(*l))
                both = *l;
        both.comment = true;
        both.effect = u8"template line karaoke";
        both.text = u8"{\\k50}ka{\\k50}ze";
        const auto id = doc.appendLine(both);
        QVERIFY(id);
        openDocument(std::move(doc), true);
        activate(*id);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled() && editor->source() == id->value; }, 20'000));
        editor->setCurrent(id->value);
        editor->setDelays(10, 60'000);
        editor->setText(QStringLiteral("{\\k50}ka{\\k50}ze{\\k50}ru"));
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        QCOMPARE(editor->sourceText(), QStringLiteral("kazeru"));
    }

    // A Line editor draft of the template that changes nothing goes away at
    // the commit, and the panel's edit goes in then, not after a pause.
    void aPanelEditGoesInOverADraftThatChangesNothing()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return editor->current() != 0; }, 20'000));
        const core::LineId templ{editor->current()};
        editor->setDelays(60'000, 60'000);
        const QString committed = editor->text();
        QVERIFY(session->editDraftText(templ, u8"{\\bord9}"));
        const QByteArray back = committed.toUtf8();
        QVERIFY(session->editDraftText(templ, std::u8string(back.begin(), back.end())));
        QCOMPARE(session->draftLine(), std::optional(templ));
        editor->setText(committed + QStringLiteral("x"));
        editor->commit();
        QVERIFY(!session->draftLine());
        for (const auto *l : session->document().lines())
            if (l->id == templ)
                QCOMPARE(QString::fromStdString(std::string(l->text.begin(), l->text.end())), committed + QStringLiteral("x"));
    }

    // The panel shows the Line editor's draft of the current template, the
    // one the preview runs and Apply commits.
    void thePanelShowsTheLineEditorsDraftOfTheTemplate()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return editor->current() != 0; }, 20'000));
        const core::LineId templ{editor->current()};
        QVERIFY(session->editDraftText(templ, u8"{\\bord9}"));
        application::DraftChange layer;
        layer.layer = 7;
        QVERIFY(session->editDraft(templ, layer));
        editor->refresh();
        QCOMPARE(editor->text(), QStringLiteral("{\\bord9}"));
        QCOMPARE(editor->layer(), 7);
    }

    // New with the Line editor's draft on the karaoke Line it goes above:
    // the draft goes in, then the template.
    void newGoesInOverADraftOfTheLineBelow()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const auto first = firstKaraoke();
        activate(*first);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        const int count = editor->templateCount();
        QVERIFY(session->editDraftText(*first, u8"{\\k50}zzq"));
        editor->refresh();
        QVERIFY(editor->insertTemplate(QStringLiteral("template syl")));
        QCOMPARE(editor->templateCount(), count + 1);
        QVERIFY(!session->draftLine());
        for (const auto *l : session->document().lines())
            if (l->id == *first)
                QCOMPARE(QString::fromStdString(std::string(l->text.begin(), l->text.end())), QStringLiteral("{\\k50}zzq"));
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

    // A `template line karaoke` is a karaoke Line too: New goes above it,
    // in its Style, when it is the only one.
    void newPutsATemplateAboveATemplateThatIsAlsoAKaraokeLine()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        core::LineRecord both;
        for (const auto *l : doc.lines())
            if (application::isKaraokeLine(*l) && !application::templateKindOf(*l) && l->style == u8"T2b")
                both = *l;
        QVERIFY(both.style == u8"T2b");
        doc.removeLinesIf([](const core::LineRecord &l) { return application::isKaraokeLine(l); });
        both.comment = true;
        both.effect = u8"template line karaoke";
        both.text = u8"{\\k50}ka";
        const auto id = doc.appendLine(both);
        QVERIFY(id);
        openDocument(std::move(doc), true);
        QVERIFY(editor->insertTemplate(QStringLiteral("template syl")));
        const auto lines = session->document().lines();
        std::size_t at = 0;
        for (std::size_t k = 0; k < lines.size(); ++k)
            if (lines[k]->id == *id)
                at = k;
        QVERIFY(at > 0);
        QCOMPARE(lines[at - 1]->id.value, editor->current());
        QVERIFY(lines[at - 1]->style == u8"T2b");
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

    // Before the first template there is nothing to preview, and nothing went
    // wrong: the panel says the templates are still to come.
    void noTemplatesYetIsNoProblem()
    {
        core::Document plain = loadFile(QStringLiteral("stock-tutorials.ass"));
        plain.removeLinesIf([](const core::LineRecord &l) { return application::templateKindOf(l).has_value(); });
        openDocument(std::move(plain), true);
        activate(*firstKaraoke());
        editor->setShown(true);
        showPanel();
        QCOMPARE(editor->templateCount(), 0);
        QTRY_VERIFY(item(QStringLiteral("templatePreviewEmpty"))->isVisible());
        QTRY_VERIFY(textOf(QStringLiteral("templatePreviewEmpty")).startsWith(QStringLiteral("No templates yet")));
        QVERIFY(!textOf(QStringLiteral("templatePreviewEmpty")).contains(QStringLiteral("problem")));
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

    void aStyleInTheEffectPickerIsShownAsText()
    {
        // The "all" modifier names the template's Style, which is the file's.
        const std::u8string img = u8"<img src=\"http://127.0.0.1:9/x\">";
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        std::optional<core::LineId> tl;
        for (const auto *l : doc.lines())
            if (!tl && application::templateKindOf(*l) == application::TemplateKind::Template)
                tl = l->id;
        QVERIFY(tl);
        QVERIFY(doc.editLine(*tl, [&](core::LineRecord &l) { l.style = img; }));
        openDocument(std::move(doc), true);
        editor->setCurrent(tl->value);
        editor->setShown(true);
        showPanel(900);
        auto *popup = object(QStringLiteral("templateEffectPopover"));
        QVERIFY(popup);
        QMetaObject::invokeMethod(popup, "open");
        QTRY_VERIFY(popup->property("opened").toBool());
        int seen = 0;
        const std::function<void(QQuickItem *)> walk = [&](QQuickItem *at) {
            for (QQuickItem *child : at->childItems()) {
                if (child->inherits("QQuickText") && !child->property("text").toString().isEmpty()) {
                    if (child->property("text").toString().contains(QLatin1String("<img")))
                        ++seen;
                    QCOMPARE(child->property("textFormat").toInt(), int(Qt::PlainText));
                }
                walk(child);
            }
        };
        walk(popup->property("contentItem").value<QQuickItem *>());
        QCOMPARE(seen, 1);
        QMetaObject::invokeMethod(popup, "close");
    }

    // Apply to the selected Lines leaves the other Lines' output from the
    // old templates: an edit since stays marked as not applied.
    void aSelectedApplyKeepsTheChangedMarks()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return editor->canApply(); }, 20'000));
        QVERIFY(editor->apply(false));
        editor->applied(templater->report);
        QCOMPARE(editor->changedCount(), 0);
        editor->setText(editor->text() + QStringLiteral("{\\bord3}"));
        editor->commit();
        editor->refresh();
        QCOMPARE(editor->changedCount(), 1);
        QVERIFY(QTest::qWaitFor([&] { return editor->canApplySelected(); }, 20'000));
        QVERIFY(editor->apply(true));
        QVERIFY(templater->report.find("\"mode\":\"selected\"") != std::string::npos);
        editor->applied(templater->report);
        editor->refresh();
        QCOMPARE(editor->changedCount(), 1);
        QVERIFY(QTest::qWaitFor([&] { return editor->canApply(); }, 20'000));
        QVERIFY(editor->apply(false));
        editor->applied(templater->report);
        editor->refresh();
        QCOMPARE(editor->changedCount(), 0);
    }

    // Two templates made one named template are reported under its first
    // Line: the second no longer shows the counts it had on its own.
    void aTemplateJoinedIntoANamedOneLosesItsCounts()
    {
        core::Document doc = loadFile(QStringLiteral("stock-tutorials.ass"));
        std::optional<core::LineId> first;
        core::LineRecord second;
        for (const auto *l : doc.lines())
            if (!first && application::templateKindOf(*l) == application::TemplateKind::Template &&
                l->effect == u8"template line") {
                first = l->id;
                second = *l;
            }
        QVERIFY(first);
        second.text = u8"{\\bord2}";
        const auto added = doc.insertLineBefore(*first, second);
        QVERIFY(added);
        openDocument(std::move(doc), true);
        activate(*firstKaraoke());
        editor->setShown(true);
        const auto generated = [&](core::LineId id) {
            for (const QVariant &v : editor->templates())
                if (v.toMap().value(QStringLiteral("id")).toULongLong() == id.value)
                    return v.toMap().value(QStringLiteral("generated")).toInt();
            return -2;
        };
        QVERIFY(QTest::qWaitFor([&] { return settled() && generated(*first) > 0 && generated(*added) > 0; }, 20'000));
        for (const core::LineId id : {*added, *first}) {
            editor->setCurrent(id.value);
            editor->setEffect(QStringLiteral("template line joined"));
            editor->commit();
        }
        QVERIFY(editor->trusted());
        QVERIFY(QTest::qWaitFor([&] { return settled() && generated(*added) > 0 && generated(*first) < 0; }, 20'000));
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
        // The arrow: the selected Lines (a karaoke Line), once the Undo's
        // preview came.
        activate(*firstKaraoke());
        QTRY_VERIFY(item(QStringLiteral("templateApplyArrow"))->isEnabled());
        QTRY_VERIFY(editor->canApplySelected());
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

    // KE3: the presets are the files in :/hikari/ui/karaoke-presets, named
    // and described as each file says (for the translators, the controller
    // lists the same words).
    void presetsAreTheirFilesInOrder()
    {
        const QVariantList presets = TemplateEditorController::presets();
        QCOMPARE(presets.size(), 10);
        QStringList files;
        for (const QVariant &v : presets) {
            const QVariantMap p = v.toMap();
            const QString file = p.value(QStringLiteral("file")).toString();
            files << file;
            const auto preset = presetFile(file);
            QVERIFY2(preset.has_value(), qPrintable(file));
            QCOMPARE(p.value(QStringLiteral("title")).toString(), qs(preset->title));
            QCOMPARE(p.value(QStringLiteral("description")).toString(), qs(preset->description));
        }
        QStringList shipped = QDir(QStringLiteral(":/hikari/ui/karaoke-presets")).entryList(QDir::Files, QDir::Name);
        QCOMPARE(files, shipped);
        openDocument(presetFixture(), true);
        QVERIFY(!editor->insertPreset(-1));
        QVERIFY(!editor->insertPreset(static_cast<int>(presets.size())));
        QCOMPARE(editor->templateCount() + editor->codeCount(), 0);
    }

    // A preset goes in above the first karaoke Line as its template and
    // code Lines, with that Line's Style, in one undo step; the first is
    // current and editable. It previews on each karaoke Line what Apply then
    // writes, and Apply gives karaoke_presets_tests' golden. Palette is the
    // preset that reads the Line's index ($li), which a preview leaving the
    // Lines before it out must keep; KaraokePresets.EachPresetGeneratesItsGolden
    // checks every preset's output (TP1 #462).
    void aPresetInsertsPreviewsAndApplies()
    {
        const QVariantList presets = TemplateEditorController::presets();
        const auto palette = std::ranges::find_if(presets, [](const QVariant &p) {
            return p.toMap().value(QStringLiteral("file")).toString() == u"10-palette.ass";
        });
        QVERIFY(palette != presets.end());
        const int i = static_cast<int>(palette - presets.begin());
        const QString file = palette->toMap().value(QStringLiteral("file")).toString();
        const auto preset = presetFile(file);
        QVERIFY2(preset.has_value(), qPrintable(file));
        openDocument(presetFixture(), true);
        editor->refresh();
        const core::LineId source = *firstKaraoke();
        activate(source);
        editor->setShown(true);
        const std::string before = bytesOf(session->document());

        QVERIFY2(editor->insertPreset(i), qPrintable(file));
        editor->refresh();
        QCOMPARE(static_cast<std::size_t>(editor->templateCount() + editor->codeCount()), preset->lines.size());
        const auto lines = session->document().lines();
        for (std::size_t n = 0; n < preset->lines.size(); ++n) {
            const auto *l = lines[n];
            QVERIFY(l->comment);
            QCOMPARE(qs(l->style), QStringLiteral("Romaji"));
            QCOMPARE(l->layer.value, preset->lines[n].layer);
            QCOMPARE(qs(l->effect), qs(preset->lines[n].effect));
            QCOMPARE(qs(l->text), qs(preset->lines[n].text));
        }
        QCOMPARE(lines[preset->lines.size()]->id, source);
        QCOMPARE(editor->current(), static_cast<qulonglong>(lines.front()->id.value));
        QCOMPARE(editor->text(), qs(preset->lines.front().text));

        QVERIFY2(previewCame(), qPrintable(file));
        // Each karaoke Line of the Style previews what Apply writes for
        // it, the later ones too (their Lines before them left out).
        std::vector<std::pair<core::LineId, QStringList>> previews;
        for (const auto *l : session->document().lines())
            if (application::isKaraokeLine(*l) && l->style == u8"Romaji")
                previews.emplace_back(l->id, QStringList());
        QCOMPARE(previews.size(), std::size_t{3});
        for (auto &[id, preview] : previews) {
            activate(id);
            QVERIFY(QTest::qWaitFor([&] { return settled() && editor->source() == id.value; }, 20'000));
            QVERIFY2(!editor->hasProblems(), qPrintable(editor->problem()));
            preview = previewTexts();
            QVERIFY(!preview.isEmpty());
        }

        QVERIFY(QTest::qWaitFor([&] { return editor->canApply(); }, 20'000));
        QVERIFY(editor->apply(false));
        editor->applied(templater->report);
        QCOMPARE(bytesOf(session->document()), presetGolden(file));
        for (const auto &[id, preview] : previews) {
            QStringList written;
            for (const auto *l : session->document().lines())
                if (l->generatedFrom == id)
                    written << QString::fromStdString(lineText(*l)).section(QLatin1Char(','), 9);
            QCOMPARE(preview, written);
        }

        QVERIFY(session->undo());
        QVERIFY(session->undo());
        QCOMPARE(bytesOf(session->document()), before);
    }

    // A preset's Lines are the user's to edit like any other template.
    void aPresetLineEditsLikeAnyTemplate()
    {
        openDocument(presetFixture(), true);
        editor->refresh();
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(editor->insertPreset(0));
        const auto first = editor->current();
        editor->setText(QStringLiteral("!retime(\"line\",-300,300)!{\\fad(300,300)\\k30\\bord4}"));
        editor->commit();
        editor->refresh();
        QCOMPARE(editor->current(), first);
        QCOMPARE(qs(session->document().lines().front()->text),
                 QStringLiteral("!retime(\"line\",-300,300)!{\\fad(300,300)\\k30\\bord4}"));
        QVERIFY(previewCame());
        QTRY_VERIFY_WITH_TIMEOUT(settled(), 20'000);
        QVERIFY(previewTexts().front().startsWith(QStringLiteral("{\\fad(300,300)\\k30\\bord4}")));
    }

    // New's "From a preset" lists every preset by name, its description on
    // hover; choosing one inserts it.
    void newOffersThePresets()
    {
        openDocument(presetFixture(), true);
        showPanel();
        click(item(QStringLiteral("templateNew")));
        auto *menu = object(QStringLiteral("templateNewMenu"));
        QTRY_VERIFY(menu->property("opened").toBool());
        auto *presetMenu = object(QStringLiteral("templatePresetMenu"));
        QVERIFY(presetMenu);
        QCOMPARE(presetMenu->property("title").toString(), QStringLiteral("From a preset"));
        QVERIFY(QMetaObject::invokeMethod(presetMenu, "open"));
        QTRY_VERIFY(presetMenu->property("opened").toBool());
        const QVariantList presets = TemplateEditorController::presets();
        for (int i = 0; i < presets.size(); ++i) {
            auto *entry = object(QStringLiteral("templatePreset_%1").arg(i));
            QVERIFY(entry);
            QCOMPARE(entry->property("text").toString(), presets[i].toMap().value(QStringLiteral("title")).toString());
            QCOMPARE(entry->property("help").toString(),
                     presets[i].toMap().value(QStringLiteral("description")).toString());
        }
        QVERIFY(!object(QStringLiteral("templatePreset_%1").arg(presets.size())));
        QCOMPARE(editor->templateCount() + editor->codeCount(), 0);
        QVERIFY(QMetaObject::invokeMethod(object(QStringLiteral("templatePreset_9")), "triggered"));
        QCOMPARE(editor->templateCount(), 2);
        QCOMPARE(editor->codeCount(), 1);
        QCOMPARE(editor->effect(), QStringLiteral("code once"));
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
