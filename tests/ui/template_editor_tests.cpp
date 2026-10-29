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
        if (dropPreviews > 0) {
            --dropPreviews;
            return;
        }
        if (holdPreviews > 0) {
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
    bool down = false;
    std::vector<std::pair<application::MacroSnapshot, PreviewDone>> held;
    std::vector<std::size_t> sizes;
    std::vector<std::string> texts;
    int applies = 0;
    bool lastSelectedOnly = false;

private:
    application::EditSession &m_session;
};

}

class TemplateEditorTests : public QObject {
    Q_OBJECT

    std::unique_ptr<application::EditSession> session;
    std::unique_ptr<Templater> templater;
    std::unique_ptr<TemplateEditorController> editor;
    std::unique_ptr<QQuickView> view;

    void open(const QString &name, bool trust = true) { openDocument(loadFile(name), trust); }
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

    bool change(std::function<bool(core::Document &)> apply)
    {
        application::Command command;
        command.name = "Edit";
        command.expectedRevision = session->revision();
        command.apply = std::move(apply);
        return session->run(command).has_value();
    }
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
    bool settled() const { return editor->hasPreview() && !editor->previewPending(); }
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
    // Repeater delegates and Popup contents are not QObject children of the root: walk the item tree.
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
        QCOMPARE(session->historySize(), steps);
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
        application::EditSession copy(session->document());
        Templater whole(copy);
        QVERIFY(QTest::qWaitFor([&] { return whole.available(); }, 20'000));
        QVERIFY(whole.apply(false));
        QStringList applied;
        for (const auto *l : copy.document().lines())
            if (l->generatedFrom == *source)
                applied << QString::fromStdString(lineText(*l)).section(QLatin1Char(','), 9);
        QCOMPARE(previewTexts(), applied);
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

    void aScriptInfoChangePreviewsAgain()
    {
        openDocument(withoutLua(loadFile(QStringLiteral("stock-tutorials.ass"))), true);
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        // MUST KILL: fixed 500 ms sleep for the background marking run instead of a condition
        QTest::qWait(500);
        const int before = templater->previews;
        editor->refresh();
        QTest::qWait(300);
        QCOMPARE(templater->previews, before);
        QVERIFY(application::setScriptResolution(*session, {1920, 1080}).has_value());
        editor->refresh();
        QVERIFY(QTest::qWaitFor([&] { return templater->previews > before && settled(); }, 20'000));
    }

    void aHostChangePreviewsAgain()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const auto host = std::make_shared<std::string>("video a");
        editor->setHostState([host] { return *host; });
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        // MUST KILL: fixed 500 ms sleep for the background marking run instead of a condition
        QTest::qWait(500);
        const int before = templater->previews;
        editor->hostChanged();
        QTest::qWait(300);
        QCOMPARE(templater->previews, before);
        *host = "video b";
        editor->hostChanged();
        QVERIFY(QTest::qWaitFor([&] { return templater->previews > before && settled(); }, 20'000));
        const int after = templater->previews;
        *host = "video c";
        editor->refresh();
        QVERIFY(QTest::qWaitFor([&] { return templater->previews > after && settled(); }, 20'000));
    }

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
        QVERIFY(bytesOf(session->document()) != before);
    }

    void aPendingEditGoesInBeforeApply()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        editor->setDelays(10, 60'000);
        const std::size_t steps = session->historySize();
        editor->setText(editor->text() + QStringLiteral("{\\be1}"));
        QVERIFY(editor->apply(false));
        QCOMPARE(session->historySize(), steps + 2);
    }

    void theVideoDrawsThePreviewInPlaceOfTheLinesOwn()
    {
        open(QStringLiteral("stock-tutorials.ass"));
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
        QStringList texts;
        for (const auto *x : shown->lines())
            texts << QString::fromStdString(lineText(*x)).section(QLatin1Char(','), 9);
        for (const QString &t : previewTexts())
            if (texts.contains(t))
                QCOMPARE(texts.count(t), previewTexts().count(t));
    }

    void anEditDuringTheMatchingRunMatchesAgain()
    {
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
        open(QStringLiteral("stock-tutorials.out.ass"));
        const auto source = firstKaraoke();
        activate(*source);
        editor->setDelays(10, 60'000);
        templater->dropPreviews = 1;
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return templater->previews >= 1; }, 20'000));
        editor->setText(editor->text() + QStringLiteral("{\\bord3}"));
        QVERIFY(QTest::qWaitFor(
            [&] { return settled() && previewTexts().join(QString()).contains(QStringLiteral("bord3")); }, 20'000));
        QCOMPARE(fileLines(*source), kept);
    }

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
        const auto shown = editor->videoDocument(session->document(), 0, true);
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

    void aMatchFromAnotherTabIsDropped()
    {
        open(QStringLiteral("stock-tutorials.out.ass"));
        activate(*firstKaraoke());
        templater->holdPreviews = 1;
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return templater->held.size() == 1; }, 20'000));
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

    void noTemplatesLeftClearsThePreview()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const auto source = firstKaraoke();
        activate(*source);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        // MUST KILL: fixed 500 ms sleep for the background marking run instead of a condition
        QTest::qWait(500);
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

    void noTemplatesLeftEndsAnUnfinishedPreview()
    {
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

    void valuesFromAnotherTabAreDropped()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        // MUST KILL: fixed 500 ms sleep for the background marking run instead of a condition
        QTest::qWait(500);
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
        activate(*firstKaraoke());
        editor->trustDocument();
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        editor->requestValues();
        QVERIFY(QTest::qWaitFor([&] { return !editor->values().isEmpty(); }, 20'000));
    }

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
        key = {1, 2};
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

    void anotherTemplateChosenMarksItsOwnLines()
    {
        open(QStringLiteral("stock-environment.ass"));
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
        QVERIFY(editor->canApply());
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
        QVERIFY(QTest::qWaitFor([&] { return templater->previews > 0 && !editor->previewPending(); }, 20'000));
    }

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
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        // MUST KILL: fixed 500 ms sleep for the background marking run instead of a condition
        QTest::qWait(500);
        const int before = templater->previews;
        QVERIFY(paste());
        editor->refresh();
        QVERIFY(!editor->trusted());
        QTest::qWait(300);
        QCOMPARE(templater->previews, before);
    }

    void aTemplateRewrittenElsewhereAsksFirst()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        activate(*firstKaraoke());
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        // MUST KILL: fixed 500 ms sleep for the background marking run instead of a condition
        QTest::qWait(500);
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

    void aNewActorOrStyleAsksFirst()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        const auto source = firstKaraoke();
        activate(*source);
        editor->setShown(true);
        QVERIFY(QTest::qWaitFor([&] { return settled(); }, 20'000));
        // MUST KILL: fixed 500 ms sleep for the background marking run instead of a condition
        QTest::qWait(500);
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
        QVERIFY(rowText().endsWith(QStringLiteral("{\\bord4}")));
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
        QVERIFY(editor->sourceText() != QStringLiteral("original"));
        QVERIFY(!editor->sourceText().contains(QLatin1Char('{')));
    }

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
                         u8"{\\bord1}";
            });
        }));
        editor->refresh();
        QVERIFY(!editor->trusted());
        QVERIFY(!editor->hasProblems());
        QVERIFY(editor->canApply());
    }

    void anUnmarkedGeneratedLinePreviewsItsOwnKaraokeLine()
    {
        open(QStringLiteral("stock-tutorials.out.ass"));
        const auto first = firstKaraoke();
        QVERIFY(first);
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
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*tl, [](core::LineRecord &l) { l.text = u8"{\\bord1}literal"; }) &&
                   d.editLine(*karaoke, [](core::LineRecord &l) { l.actor = u8"!_G.marker = 1!"; });
        }));
        editor->refresh();
        QVERIFY(!editor->trusted());
        editor->trustDocument();
        QVERIFY(editor->trusted());
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*tl, [](core::LineRecord &l) { l.text = u8"{\\bord1}$actor"; });
        }));
        editor->refresh();
        QVERIFY(!editor->trusted());
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*karaoke, [](core::LineRecord &l) { l.actor = u8"plain"; });
        }));
        editor->refresh();
        QVERIFY(editor->trusted());
    }

    void aTemplateRunningActorsTrustsWholeLines()
    {
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
        QVERIFY(change([&](core::Document &d) {
            return d.editLine(*unmarked, [&](core::LineRecord &l) { l.generatedFrom = *firstKaraoke(); });
        }));
        activate(*unmarked);
        QVERIFY(QTest::qWaitFor([&] { return editor->canApplySelected(); }, 20'000));
    }

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
        // MUST KILL: fixed 500 ms sleep for the background marking run instead of a condition
        QTest::qWait(500);
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
        pairing.actor = u8"!_G.marker = 1!";
        QVERIFY(change([&](core::Document &d) { return d.appendLine(pairing).has_value(); }));
        editor->refresh();
        QVERIFY(!editor->trusted());
    }

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
        QCOMPARE(session->revision(), revision);
        application::DraftChange actor;
        actor.actor = u8"!_G.marker = 1!";
        QVERIFY(session->editDraft(*source, actor));
        editor->refresh();
        QVERIFY(!editor->trusted());
    }

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

    void aFailedMatchIsNotKept()
    {
        open(QStringLiteral("stock-tutorials.out.ass"));
        const auto source = firstKaraoke();
        std::optional<core::LineId> generated;
        std::u8string was;
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

    void aGeneratedLineChangedInPlaceIsMatchedAgain()
    {
        open(QStringLiteral("stock-tutorials.out.ass"));
        const auto source = firstKaraoke();
        std::optional<core::LineId> generated;
        for (const auto *l : session->document().lines())
            if (application::isGeneratedLine(*l) && l->generatedFrom.value == 0)
                generated = l->id;
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

    void anotherTabsOwnersAreNotThisOnes()
    {
        open(QStringLiteral("stock-tutorials.out.ass"));
        std::optional<core::LineId> clicked;
        for (const auto *l : session->document().lines())
            if (application::isGeneratedLine(*l) && l->generatedFrom.value == 0)
                clicked = l->id;
        editor->setShown(true);
        activate(*clicked);
        QVERIFY(QTest::qWaitFor([&] { return editor->source() != 0 && settled(); }, 20'000));
        const auto owner = editor->source();
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
        editor->commit();
        QVERIFY(!session->draftLine());
        for (const auto *l : session->document().lines())
            if (l->id == templ) {
                QVERIFY(std::u8string_view(l->text).ends_with(u8"{\\bord4}"));
                QVERIFY(l->actor == u8"drafted");
            }
    }

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
        editor->setStyle(QStringLiteral("T1a"));
        editor->commit();
        editor->refresh();
        QVERIFY(editor->trusted());
    }

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
        QCOMPARE(editor->source(), karaoke->value);
    }

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

    void thePanelListsTheTemplatesAndEditsTheOneClicked()
    {
        open(QStringLiteral("stock-tutorials.ass"));
        showPanel();
        const QVariantList rows = editor->templates();
        QVERIFY(rows.size() > 1);
        QTRY_VERIFY(row("templateRow_", rows.size() - 1));
        QVERIFY(!row("templateRow_", rows.size()));
        QVERIFY(object(QStringLiteral("templateSummary"))->property("note").toString().contains(
            QStringLiteral(" karaoke lines")));
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
        auto *marks = object(QStringLiteral("templateTextHighlighter"));
        QVERIFY(marks);
        QVariantList applied;
        QTRY_VERIFY((QMetaObject::invokeMethod(marks, "appliedColours", Q_RETURN_ARG(QVariantList, applied)),
                     !applied.isEmpty()));
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
        QVERIFY2(problem.contains(QStringLiteral("Template line: %1").arg(editor->currentNumber())), qPrintable(problem));
        QVERIFY2(problem.contains(QStringLiteral("!nosuch.field!")), qPrintable(problem));
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
                 QStringLiteral("Preview on line %1").arg(editor->sourceNumber()));
        QVERIFY(textOf(QStringLiteral("templatePreviewSummary"))
                    .startsWith(n == 1 ? QStringLiteral("1 line · ") : QStringLiteral("%1 lines · ").arg(n)));
        QVERIFY(item(QStringLiteral("templatePreviewList"))->isVisible());
        QVERIFY(!item(QStringLiteral("templatePreviewKept"))->isVisible());
    }

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
        QVERIFY(text->hasActiveFocus());
        QTest::keyClick(view.get(), Qt::Key_Down);
        QTest::keyClick(view.get(), Qt::Key_Return);
        QTRY_VERIFY2(!list->property("opened").toBool(),
                     qPrintable(editor->text().left(40) + QLatin1Char('|') + list->property("chosen").toString()));
        const QString second = entries[1].toMap().value(QStringLiteral("name")).toString();
        QVERIFY2(editor->text().startsWith(QStringLiteral("{\\pos(") + second), qPrintable(editor->text()));
        type(QStringLiteral(",$"));
        QTRY_VERIFY(list->property("opened").toBool());
        QTest::keyClick(view.get(), Qt::Key_Escape);
        QTRY_VERIFY(!list->property("opened").toBool());
        QVERIFY(editor->text().startsWith(QStringLiteral("{\\pos(") + second + QStringLiteral(",$")));
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
        QVERIFY(seen >= 3);
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
        auto *text = item(QStringLiteral("templateText"));
        QVERIFY(text->mapToScene(QPointF(text->width(), 0)).x() <= 360);
        auto *apply = item(QStringLiteral("templateApplyArrow"));
        QVERIFY(apply->mapToScene(QPointF(apply->width(), 0)).x() <= 360);
    }

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
