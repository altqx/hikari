#include "hikari/app/application.h"
#include "hikari/core/text_projection.h"
#include "docking.h"
#include "line_table_model.h"
#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
using application::Problem;
using application::ProblemField;
using application::ProblemFix;
using application::ProblemSeverity;

namespace {

QString writeAss(const QString &path, const QStringList &texts)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
        for (qsizetype i = 0; i < texts.size(); ++i)
            f.write("Dialogue: 0,0:00:0" + QByteArray::number(2 * i + 1) + ".00,0:00:0" + QByteArray::number(2 * i + 2) +
                    ".00,Default,,0,0,0,," + texts[i].toUtf8() + "\n");
    }
    return path;
}

QString qt(const std::u8string &text)
{
    return QString::fromStdU16String(core::toUtf16(text));
}

QVariantMap at(const QVariantList &list, qsizetype i)
{
    return i < list.size() ? list[i].toMap() : QVariantMap();
}

}

class ProblemsUiTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;

    QString home() const { return dir.filePath(QLatin1String(QTest::currentTestFunction())); }
    void start(const QStringList &texts)
    {
        app::Application::Options o;
        o.settingsFile = home() + QStringLiteral("/settings.toml");
        o.playbackAudio = false;
        o.spellingBackend = {};
        application = std::make_unique<app::Application>(o);
        engine = std::make_unique<QQmlApplicationEngine>();
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(engine->rootObjects().first(), "buildMenus"));
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/problems.ass"), texts)));
        QTRY_COMPARE(item("lineText")->property("text").toString(), texts.front());
    }
    static QQuickItem *itemNamed(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = itemNamed(child, name))
                return found;
        return nullptr;
    }
    QObject *named(const QString &name) const
    {
        if (QObject *found = window->findChild<QObject *>(name))
            return found;
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *quick = qobject_cast<QQuickWindow *>(w))
                if (QQuickItem *found = itemNamed(quick->contentItem(), name))
                    return found;
        return nullptr;
    }
    QQuickItem *item(const char *name) const { return qobject_cast<QQuickItem *>(named(QLatin1String(name))); }
    application::DocumentId document() const { return *application->workspace().editingTarget(); }
    application::EditSession *target() const { return application->files().session(document()); }
    Problem problem(std::size_t row, ProblemSeverity severity, std::string message,
                    std::optional<std::pair<std::size_t, std::size_t>> range = std::nullopt,
                    std::vector<ProblemFix> fixes = {}, ProblemField field = ProblemField::Text) const
    {
        const core::LineRecord &line = *target()->document().lines()[row];
        Problem p;
        p.line = line.id;
        p.field = field;
        if (range) {
            p.from = range->first;
            p.to = range->second;
        }
        p.severity = severity;
        p.message = std::move(message);
        p.fixes = std::move(fixes);
        p.against = application::problemFieldText(line, field);
        return p;
    }
    void set(const std::string &source, const std::string &name, std::vector<Problem> problems)
    {
        QVERIFY(application->problems().set("plugin:" + source, source, name, document(), "default", std::move(problems)));
    }
    QString lineText(std::size_t row) const { return qt(target()->document().lines()[row]->text); }
    void showList()
    {
        QVERIFY(QMetaObject::invokeMethod(named("showQcNotes")->property("action").value<QObject *>(), "trigger"));
        QTRY_VERIFY(item("qcNotesTool") && item("qcNotesTool")->isVisible());
    }
    QQuickItem *field() const { return item("lineText"); }
    void focusField(int caret)
    {
        field()->forceActiveFocus();
        QTRY_VERIFY(field()->hasActiveFocus());
        field()->setProperty("cursorPosition", caret);
    }

private slots:
    void initTestCase() { QVERIFY(dir.isValid()); }
    void cleanup()
    {
        engine.reset();
        application.reset();
        window = nullptr;
    }

    void theListGroupsByLineAndFilters()
    {
        start({QStringLiteral("I has a apple"), QStringLiteral("second"), QStringLiteral("third one")});
        QVERIFY(application->qcNotes().add(3500, QStringLiteral("TLC"), QStringLiteral("too fast")));
        QVERIFY(application->qcNotes().add(60000, QStringLiteral("TLC"), QStringLiteral("nowhere")));
        set("grammar", "Grammar check",
            {problem(2, ProblemSeverity::Warning, "Odd word", std::pair{6, 9}),
             problem(0, ProblemSeverity::Error, "Wrong verb", std::pair{2, 5}),
             problem(0, ProblemSeverity::Error, "Wrong article", std::pair{6, 7})});
        set("style", "Style check", {problem(0, ProblemSeverity::Hint, "Lower case")});
        showList();
        auto &list = application->problemsList();
        QCOMPARE(list.problemCount(), 4);
        const QVariantList entries = list.entries();
        QStringList shown;
        for (const QVariant &e : entries) {
            const QVariantMap m = e.toMap();
            shown << m.value(QStringLiteral("group")).toString() + " | " +
                         (m.value(QStringLiteral("kind")) == QStringLiteral("note") ? m.value(QStringLiteral("text")).toString()
                                                                                 : m.value(QStringLiteral("message")).toString());
        }
        QCOMPARE(shown, (QStringList{QStringLiteral("Line 1 · I has a apple | Lower case"),
                                     QStringLiteral("Line 1 · I has a apple | Wrong verb"),
                                     QStringLiteral("Line 1 · I has a apple | Wrong article"),
                                     QStringLiteral("Line 2 · second | too fast"),
                                     QStringLiteral("Line 3 · third one | Odd word"),
                                     QStringLiteral("No line | nowhere")}));
        QCOMPARE(at(entries, 0).value(QStringLiteral("sourceName")).toString(), QStringLiteral("Style check"));
        QCOMPARE(at(entries, 3).value(QStringLiteral("section")).toString(), QStringLiteral("TLC"));

        QStringList sources;
        for (const QVariant &s : list.sources())
            sources << s.toMap().value(QStringLiteral("name")).toString() + " " + s.toMap().value(QStringLiteral("count")).toString();
        QCOMPARE(sources, (QStringList{QStringLiteral("QC notes 2"), QStringLiteral("Grammar check 3"), QStringLiteral("Style check 1")}));
        QStringList severities;
        for (const QVariant &s : list.severities())
            severities << s.toMap().value(QStringLiteral("id")).toString() + " " + s.toMap().value(QStringLiteral("count")).toString();
        QCOMPARE(severities, (QStringList{QStringLiteral("error 2"), QStringLiteral("warning 1"), QStringLiteral("info 2"),
                                          QStringLiteral("hint 1")}));
        QTRY_VERIFY(item("problemsSeverities")->isVisible());
        QVERIFY(item("problemsSources")->isVisible());
        QTRY_VERIFY(item("problemsSource_grammar"));

        list.setSeverity(QStringLiteral("error"));
        QCOMPARE(list.entries().size(), 2);
        list.setSeverity(QStringLiteral("info"));
        QCOMPARE(list.entries().size(), 2);
        QCOMPARE(at(list.entries(), 0).value(QStringLiteral("kind")).toString(), QStringLiteral("note"));
        list.setSeverity({});
        QVERIFY(QMetaObject::invokeMethod(item("problemsSource_style"), "clicked"));
        QCOMPARE(list.source(), QStringLiteral("style"));
        QCOMPARE(list.entries().size(), 1);
        QTRY_VERIFY(!item("qcNotesSections")->isVisible());
        list.setSource(QStringLiteral("qc"));
        QCOMPARE(list.entries().size(), 2);
        QTRY_VERIFY(item("qcNotesSections")->isVisible());
        list.setSource({});

        QVERIFY(list.goTo(at(list.entries(), 4).value(QStringLiteral("id")).toString()));
        QTRY_COMPARE(target()->selection().active, std::optional(target()->document().lines()[2]->id));
    }

    void aQuickFixFromTheListIsOneUndoStep()
    {
        start({QStringLiteral("I has a apple"), QStringLiteral("second")});
        set("grammar", "Grammar check",
            {problem(0, ProblemSeverity::Error, "Use an", std::pair{6, 7}, {ProblemFix{"Use “an”", {{6, 7, "an"}}}}),
             problem(1, ProblemSeverity::Warning, "Shout", std::nullopt,
                     {ProblemFix{"Upper case", {{0, 6, "SECOND"}}}, ProblemFix{"Title case", {{0, 1, "S"}}}})});
        showList();
        const std::size_t steps = target()->history().size();
        auto *panel = item("qcNotesTool");
        const QVariantList entries = application->problemsList().entries();
        QCOMPARE(entries.size(), 2);
        QCOMPARE(at(entries, 1).value(QStringLiteral("fixes")).toStringList(),
                 (QStringList{QStringLiteral("Upper case"), QStringLiteral("Title case")}));

        auto *row = item("qcNotesList");
        QVERIFY(row);
        QVERIFY(QMetaObject::invokeMethod(panel, "openFixMenu", Q_ARG(QVariant, entries[1]),
                                          Q_ARG(QVariant, QVariant::fromValue<QObject *>(row))));
        auto *menu = named(QStringLiteral("problemFixMenu"));
        QTRY_VERIFY(menu->property("visible").toBool());
        QObject *title = nullptr;
        QTRY_VERIFY((title = named(QStringLiteral("problemFix1"))));
        QCOMPARE(title->property("text").toString(), QStringLiteral("Title case"));
        QCOMPARE(title->property("detailText").toString(), QStringLiteral("Grammar check"));
        QVERIFY(QMetaObject::invokeMethod(title, "triggered"));
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_COMPARE(lineText(1), QStringLiteral("Second"));
        QCOMPARE(target()->history().size(), steps + 1);
        QCOMPARE(target()->history().back().name, std::string("Grammar check: Title case"));
        QTRY_COMPARE(application->problemsList().problemCount(), 1);

        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        auto *list = item("qcNotesList");
        list->forceActiveFocus();
        QTRY_VERIFY(list->hasActiveFocus());
        panel->setProperty("chosen", at(application->problemsList().entries(), 0).value(QStringLiteral("key")));
        QTest::keyClick(window, Qt::Key_Return, Qt::AltModifier);
        QTRY_COMPARE(lineText(0), QStringLiteral("I has an apple"));
        QCOMPARE(target()->history().size(), steps + 2);
        QCOMPARE(target()->history().back().name, std::string("Grammar check: Use “an”"));
        QTRY_COMPARE(application->problemsList().problemCount(), 0);
        QVERIFY(application->editor().undo());
        QTRY_COMPARE(lineText(0), QStringLiteral("I has a apple"));
        QCOMPARE(lineText(1), QStringLiteral("Second"));
    }

    void altEnterInTheLineEditorOffersTheFixesAtTheCaret()
    {
        start({QStringLiteral("I has a apple")});
        set("grammar", "Grammar check",
            {problem(0, ProblemSeverity::Error, "Wrong verb", std::pair{2, 5},
                     {ProblemFix{"Use “have”", {{2, 5, "have"}}}, ProblemFix{"Use “had”", {{2, 5, "had"}}}}),
             problem(0, ProblemSeverity::Error, "Use an", std::pair{6, 7}, {ProblemFix{"Use “an”", {{6, 7, "an"}}}})});
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        const std::size_t steps = target()->history().size();

        focusField(0);
        QTest::keyClick(window, Qt::Key_Return, Qt::AltModifier);
        QCOMPARE(target()->history().size(), steps);
        QCOMPARE(lineText(0), QStringLiteral("I has a apple"));

        focusField(3);
        QTest::keyClick(window, Qt::Key_Return, Qt::AltModifier);
        auto *menu = named(QStringLiteral("quickFixMenu"));
        QTRY_VERIFY(menu->property("visible").toBool());
        QObject *had = nullptr;
        QTRY_VERIFY((had = named(QStringLiteral("quickFix1"))));
        QCOMPARE(had->property("text").toString(), QStringLiteral("Use “had”"));
        QVERIFY(QMetaObject::invokeMethod(had, "triggered"));
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_COMPARE(lineText(0), QStringLiteral("I had a apple"));
        QCOMPARE(target()->history().size(), steps + 1);
        QCOMPARE(target()->history().back().name, std::string("Grammar check: Use “had”"));

        set("grammar", "Grammar check",
            {problem(0, ProblemSeverity::Error, "Use an", std::pair{6, 7}, {ProblemFix{"Use “an”", {{6, 7, "an"}}}})});
        QTRY_COMPARE(field()->property("text").toString(), QStringLiteral("I had a apple"));
        focusField(7);
        QTest::keyClick(window, Qt::Key_Return, Qt::AltModifier);
        QTRY_COMPARE(lineText(0), QStringLiteral("I had an apple"));
        QCOMPARE(target()->history().size(), steps + 2);
        QCOMPARE(target()->history().back().name, std::string("Grammar check: Use “an”"));
    }

    void squigglesFollowSeverityAndHover()
    {
        start({QStringLiteral(R"({\i1}Their{\i0} going to the store)")});
        set("check", "Checker",
            {problem(0, ProblemSeverity::Error, "Wrong word", std::pair{5, 10}),
             problem(0, ProblemSeverity::Warning, "Slang", std::pair{16, 21}),
             problem(0, ProblemSeverity::Info, "Place", std::pair{29, 34}),
             problem(0, ProblemSeverity::Hint, "Article", std::pair{25, 28}),
             problem(0, ProblemSeverity::Error, "Whole line")});
        auto *f = field();
        QTRY_COMPARE(f->property("problemMarks").toList().size(), 4);
        QStringList marks;
        for (const QVariant &m : f->property("problemMarks").toList())
            marks << QStringLiteral("%1-%2 %3").arg(m.toMap().value(QStringLiteral("from")).toInt())
                         .arg(m.toMap().value(QStringLiteral("to")).toInt())
                         .arg(m.toMap().value(QStringLiteral("severity")).toString());
        QCOMPARE(marks, (QStringList{QStringLiteral("5-10 error"), QStringLiteral("16-21 warning"), QStringLiteral("29-34 info"),
                                     QStringLiteral("25-28 hint")}));
        auto *waves = item("lineTextProblems");
        QVERIFY(waves);
        QTRY_VERIFY(waves->isVisible());
        QCOMPARE(waves->property("rows").toList().size(), 4);
        const auto &roles = ui::theme::current().roles;
        QCOMPARE(named(QStringLiteral("lineTextProblemsError"))->property("strokeColor").value<QColor>(), roles.danger);
        QCOMPARE(named(QStringLiteral("lineTextProblemsWarning"))->property("strokeColor").value<QColor>(), roles.warning);
        QCOMPARE(named(QStringLiteral("lineTextProblemsInfo"))->property("strokeColor").value<QColor>(), roles.accent);
        auto *hint = named(QStringLiteral("lineTextProblemsHint"));
        QCOMPARE(hint->property("strokeColor").value<QColor>(), roles.muted);
        QCOMPARE(hint->property("strokeStyle").toInt(), 2);

        QRectF rect;
        QVERIFY(QMetaObject::invokeMethod(f, "positionToRectangle", Q_RETURN_ARG(QRectF, rect), Q_ARG(int, 18)));
        QVariant found;
        QVERIFY(QMetaObject::invokeMethod(f, "problemAt", Q_RETURN_ARG(QVariant, found), Q_ARG(QVariant, rect.x() + 1),
                                          Q_ARG(QVariant, rect.center().y())));
        QCOMPARE(found.toMap().value(QStringLiteral("message")).toString(), QStringLiteral("Slang"));
        QCOMPARE(found.toMap().value(QStringLiteral("source")).toString(), QStringLiteral("Checker"));
        QTest::mouseMove(window, f->mapToScene(QPointF(rect.x() + 1, rect.center().y())).toPoint());
        auto *tip = named(QStringLiteral("lineTextProblemTip"));
        QVERIFY(tip);
        QTRY_COMPARE(tip->property("text").toString(), QStringLiteral("Slang · Checker"));

        application->editor().setShowTags(false);
        QTRY_COMPARE(f->property("text").toString(), QStringLiteral("Their going to the store"));
        QTRY_COMPARE(at(f->property("problemMarks").toList(), 0).value(QStringLiteral("from")).toInt(), 0);
        QCOMPARE(at(f->property("problemMarks").toList(), 1).value(QStringLiteral("from")).toInt(), 6);
        application->editor().setShowTags(true);

        QVERIFY(QMetaObject::invokeMethod(f, "insert", Q_ARG(int, 0), Q_ARG(QString, QStringLiteral("So "))));
        QTRY_VERIFY(f->property("problemMarks").toList().isEmpty());
    }

    void gridRowsMarkTheirWorstProblem()
    {
        start({QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three")});
        QVERIFY(application->qcNotes().add(5500, QStringLiteral("TLC"), QStringLiteral("late")));
        set("check", "Checker",
            {problem(0, ProblemSeverity::Warning, "Odd"), problem(0, ProblemSeverity::Error, "Bad"),
             problem(1, ProblemSeverity::Hint, "Maybe")});
        auto *lines = application->shell().lines();
        const auto colours = [&](int row) { return lines->index(row, 0).data(ui::LineTableModel::RowMarkColoursRole).toList(); };
        const auto notes = [&](int row) { return lines->index(row, 0).data(ui::LineTableModel::RowMarkNotesRole).toStringList(); };
        QTRY_COMPARE(colours(0), QVariantList{int(application::MarkColour::Danger)});
        QCOMPARE(notes(0), QStringList{QStringLiteral("2 problem(s): 1 error(s), 1 warning(s)")});
        QCOMPARE(colours(1), QVariantList{int(application::MarkColour::Muted)});
        QCOMPARE(notes(1), QStringList{QStringLiteral("1 problem(s): 1 hint(s)")});
        QVERIFY(colours(2).isEmpty());
        QCOMPARE(lines->index(2, 0).data(ui::LineTableModel::QcNotesRole).toStringList().size(), 1);

        application->problems().clear("check", document(), std::nullopt);
        QTRY_VERIFY(colours(0).isEmpty());
        QVERIFY(colours(1).isEmpty());
    }
};

QTEST_MAIN(ProblemsUiTests)
#include "problems_ui_tests.moc"
