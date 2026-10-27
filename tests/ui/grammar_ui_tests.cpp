// GR1 (#364): grammar check with Harper in the main window, with the pinned
// harper-ls (HikariHarper.cmake): off by default with no helper process, the
// Line editor's wavy underlines and their menu (suggestions, Ignore this rule
// with Undo, Add to dictionary), Hunspell and Harper never marking one word
// twice, the Settings page, and the Spellchecker panel's Grammar walk in the
// background.

#include "hikari/app/application.h"
#include "docking.h"
#include "line_table_model.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <functional>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

constexpr int kHarperWait = 20000; // the first check loads Harper's dictionary

QString writeAss(const QString &path, const QByteArray &events, const QByteArray &scriptInfo = {})
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        if (!scriptInfo.isEmpty())
            f.write("[Script Info]\n" + scriptInfo + "\n\n");
        f.write("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
        f.write(events);
    }
    return path;
}

QString writeSrt(const QString &path, const QString &text)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write("1\n00:00:01,000 --> 00:00:02,000\n" + text.toUtf8() + "\n\n");
    return path;
}

QByteArray dialogue(const QByteArray &text, int second = 1)
{
    return "Dialogue: 0,0:00:" + QByteArray::number(second).rightJustified(2, '0') + ".00,0:00:"
           + QByteArray::number(second).rightJustified(2, '0') + ".50,Default,,0,0,0,," + text + "\n";
}

// harper-ls processes this test started (Linux; elsewhere the Harper folder,
// made with the helper, says it).
int harperChildren()
{
#ifdef __linux__
    int count = 0;
    const QString self = QString::number(QCoreApplication::applicationPid());
    for (const QString &pid : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs)) {
        QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
        if (!stat.open(QIODevice::ReadOnly))
            continue;
        const QByteArray line = stat.readAll();
        const int open = line.indexOf('('), close = line.lastIndexOf(')');
        if (open < 0 || close < open)
            continue;
        const QList<QByteArray> rest = line.mid(close + 2).split(' ');
        if (line.mid(open + 1, close - open - 1) == "harper-ls" && rest.size() > 1 && rest[1] == self.toLatin1()
            && rest[0] != "Z")
            ++count;
    }
    return count;
#else
    return -1;
#endif
}

} // namespace

class GrammarUiTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;

    QString home() const { return dir.filePath(QLatin1String(QTest::currentTestFunction())); }
    QString grammarDir() const { return home() + QStringLiteral("/Harper"); }
    void start(bool hunspell = false)
    {
        app::Application::Options o;
        o.settingsFile = home() + QStringLiteral("/hikari.ini");
        o.playbackAudio = false;
        o.harperLs = QStringLiteral(HIKARI_HARPER_LS);
        o.grammarDir = grammarDir();
        if (hunspell) {
            // A tiny Hunspell dictionary (no affix rules), as shell_tests'.
            QDir().mkpath(home() + QStringLiteral("/Dictionary"));
            QFile aff(home() + QStringLiteral("/Dictionary/en_US.aff"));
            QVERIFY(aff.open(QIODevice::WriteOnly));
            QFile dic(home() + QStringLiteral("/Dictionary/en_US.dic"));
            QVERIFY(dic.open(QIODevice::WriteOnly));
            dic.write("6\nHello\nworld\nThe\nI\nhave\nit\n");
        } else {
            o.spellingBackend = {};
        }
        application = std::make_unique<app::Application>(o);
        engine = std::make_unique<QQmlApplicationEngine>();
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
    }
    void grammarOn(int language = 1) { application->settingsStore()->set("grammar.language", language); }
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
    application::EditSession *target() const
    {
        const auto t = application->workspace().editingTarget();
        return t ? application->files().session(*t) : nullptr;
    }
    QString gridText(int row = 0) const
    {
        return application->shell().lines()->index(row, ui::LineTableModel::TextColumn).data().toString();
    }
    QString lineText(std::size_t row = 0) const
    {
        const auto &t = target()->document().lines()[row]->text;
        return QString::fromUtf8(reinterpret_cast<const char *>(t.data()), qsizetype(t.size()));
    }
    // The field's marks as [start, end) pairs of its text.
    QList<QPair<int, int>> marks(int role = 0) const
    {
        QList<QPair<int, int>> out;
        const QVariantList flat = application->editorGrammarMarks(role);
        for (qsizetype i = 0; i + 1 < flat.size(); i += 2)
            out << qMakePair(flat[i].toInt(), flat[i + 1].toInt());
        return out;
    }
    bool marked(const QString &text, const QString &word, int role = 0) const
    {
        const int at = text.indexOf(word);
        return at >= 0 && marks(role).contains(qMakePair(at, at + int(word.size())));
    }
    QPoint fieldPoint(QQuickItem *field, int position) const
    {
        QRectF rect;
        QMetaObject::invokeMethod(field, "positionToRectangle", Q_RETURN_ARG(QRectF, rect), Q_ARG(int, position));
        return field->mapToScene(QPointF(rect.x() + 2, rect.center().y())).toPoint();
    }
    void rightClick(const QPoint &point)
    {
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, point);
    }
    static QList<QQuickItem *> menuItems(QObject *menu)
    {
        QList<QQuickItem *> out;
        const int count = menu->property("count").toInt();
        for (int i = 0; i < count; ++i) {
            QQuickItem *entry = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, entry), Q_ARG(int, i));
            out << entry;
        }
        return out;
    }
    // A toast's action button by its text.
    QQuickItem *toastButton(const QString &text) const
    {
        std::function<QQuickItem *(QQuickItem *)> find = [&](QQuickItem *from) -> QQuickItem * {
            if (from->inherits("QQuickAbstractButton") && from->property("text").toString() == text && from->isVisible())
                return from;
            for (QQuickItem *child : from->childItems())
                if (QQuickItem *found = find(child))
                    return found;
            return nullptr;
        };
        auto *stack = item("toastStack");
        return stack ? find(stack) : nullptr;
    }

private slots:
    void initTestCase() { QVERIFY(dir.isValid()); }
    void cleanup()
    {
        engine.reset();
        application.reset();
        window = nullptr;
        QTRY_COMPARE(std::max(0, harperChildren()), 0); // the helper ends with the application
    }

    // Off by default: no marks, no menu entries, and no helper process (nor
    // its folder) however long the Line is edited.
    void offByDefaultStartsNoHelper()
    {
        start();
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/off.ass"), dialogue("I has a apple."))));
        auto *field = item("lineText");
        QTRY_COMPARE(field->property("text").toString(), QStringLiteral("I has a apple."));
        QCOMPARE(application->settingsStore()->integer("grammar.language"), 0);
        QVERIFY(!application->grammarOn());
        QVERIFY(QMetaObject::invokeMethod(field, "insert", Q_ARG(int, 0), Q_ARG(QString, QStringLiteral("So "))));
        QTest::qWait(900);
        QVERIFY(marks().isEmpty());
        QVERIFY(application->editorGrammarAt(0, 6).isEmpty());
        QVERIFY(!QFileInfo::exists(grammarDir()));
        if (harperChildren() >= 0)
            QCOMPARE(harperChildren(), 0);
        // The panel's Grammar filter says it is off.
        const QVariantMap state = application->startGrammarWalk();
        QCOMPARE(state.value(QStringLiteral("on")).toBool(), false);
        QVERIFY(!state.value(QStringLiteral("found")).toBool());
        QVERIFY(!QFileInfo::exists(grammarDir()));
    }

    // On: the field is checked after typing stops, each finding under its
    // raw text (tags shown) or the shown text (tags hidden), drawn as a wavy
    // underline; its menu puts a suggestion in as one step, the tags kept.
    void underlinesAndTheirMenu()
    {
        start();
        grammarOn();
        const QString text = QStringLiteral(R"({\i1}Their{\i0} going to the store, I has a apple.)");
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/marks.ass"), dialogue(text.toUtf8()))));
        auto *field = item("lineText");
        QTRY_COMPARE(field->property("text").toString(), text);
        QTRY_VERIFY_WITH_TIMEOUT(marked(text, QStringLiteral(" a ").trimmed()) || !marks().isEmpty(), kHarperWait);
        QTRY_VERIFY(marked(text, QStringLiteral("Their")));
        const int article = text.indexOf(QStringLiteral("a apple"));
        QVERIFY(marks().contains(qMakePair(article, article + 1)));
        if (harperChildren() >= 0)
            QCOMPARE(harperChildren(), 1);
        // The wavy underlines, one row each.
        auto *wave = item("lineTextGrammar");
        QVERIFY(wave);
        QTRY_VERIFY(wave->isVisible());
        QCOMPARE(wave->property("rows").toList().size(), marks().size());

        // The finding's menu: Harper's message and group first, the
        // suggestion, Ignore this rule naming the group; no Add to
        // dictionary for a grammar rule.
        const QVariantMap finding = application->editorGrammarAt(0, article);
        QCOMPARE(finding.value(QStringLiteral("rule")).toString(), QStringLiteral("AnA"));
        QCOMPARE(finding.value(QStringLiteral("groupName")).toString(), QStringLiteral("Miscellaneous"));
        QCOMPARE(finding.value(QStringLiteral("suggestions")).toStringList(), QStringList{QStringLiteral("an")});
        QVERIFY(!finding.value(QStringLiteral("word")).toBool());
        auto *menu = field->findChild<QObject *>(QStringLiteral("lineTextMenu"));
        rightClick(fieldPoint(field, article));
        QTRY_VERIFY(menu->property("visible").toBool());
        const auto entries = menuItems(menu);
        QVERIFY(entries.size() > 2);
        QCOMPARE(entries[0]->objectName(), QStringLiteral("lineTextGrammarMessage"));
        QCOMPARE(entries[1]->property("text").toString(), QStringLiteral("an"));
        QVERIFY(entries[1]->property("font").value<QFont>().bold());
        auto *ignore = field->findChild<QQuickItem *>(QStringLiteral("lineTextIgnoreRule"));
        QVERIFY(ignore && ignore->isVisible());
        QCOMPARE(ignore->property("shortcutText").toString(), QStringLiteral("Turns off Miscellaneous"));
        QVERIFY(!field->findChild<QQuickItem *>(QStringLiteral("lineTextGrammarAddWord"))->isVisible());
        QSignalSpy tabs(application.get(), &app::Application::tabsChanged);
        QVERIFY(QMetaObject::invokeMethod(entries[1], "triggered"));
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        const QString fixed = QStringLiteral(R"({\i1}Their{\i0} going to the store, I has an apple.)");
        QTRY_COMPARE(lineText(), fixed);
        // The Grid and the tab's modified mark follow.
        QVERIFY(gridText().contains(QStringLiteral("an apple")));
        QVERIFY(!tabs.isEmpty());
        QCOMPARE(target()->history().back().name, std::string("Correcting grammar errors in the text field"));
        QTRY_VERIFY_WITH_TIMEOUT(marked(fixed, QStringLiteral("Their")), kHarperWait);
        QVERIFY(!marks().contains(qMakePair(article, article + 2)));

        // Tags hidden: the marks are under the shown text, and a suggestion
        // across the hidden tags keeps them.
        application->editor().setShowTags(false);
        const QString shown = QStringLiteral("Their going to the store, I has an apple.");
        QTRY_COMPARE(field->property("text").toString(), shown);
        QTRY_VERIFY(marked(shown, QStringLiteral("Their")));
        const QVariantMap their = application->editorGrammarAt(0, 1);
        QCOMPARE(their.value(QStringLiteral("text")).toString(), QStringLiteral("Their"));
        QVERIFY(!their.value(QStringLiteral("suggestions")).toStringList().isEmpty());
        QVERIFY(application->replaceEditorGrammar(0, 1, QStringLiteral("They're")));
        QTRY_COMPARE(lineText(), QStringLiteral(R"({\i1}They're{\i0} going to the store, I has an apple.)"));
        application->editor().setShowTags(true);
    }

    // Ignore this rule turns the rule's group off in Settings; the toast's
    // Undo turns it on again.
    void ignoreThisRuleAndUndo()
    {
        start();
        grammarOn();
        const QString text = QStringLiteral("I has a apple.");
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/ignore.ass"), dialogue(text.toUtf8()))));
        auto *field = item("lineText");
        QTRY_COMPARE(field->property("text").toString(), text);
        const int article = text.indexOf(QStringLiteral("a apple"));
        QTRY_VERIFY_WITH_TIMEOUT(marks().contains(qMakePair(article, article + 1)), kHarperWait);
        auto *menu = field->findChild<QObject *>(QStringLiteral("lineTextMenu"));
        rightClick(fieldPoint(field, article));
        QTRY_VERIFY(menu->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(field->findChild<QQuickItem *>(QStringLiteral("lineTextIgnoreRule")), "triggered"));
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QCOMPARE(application->settingsStore()->boolean("grammar.group.miscellaneous"), false);
        QTRY_VERIFY_WITH_TIMEOUT(!marks().contains(qMakePair(article, article + 1)) && !marks().isEmpty(), kHarperWait);
        QTRY_VERIFY(toastButton(QStringLiteral("Undo")));
        QVERIFY(QMetaObject::invokeMethod(toastButton(QStringLiteral("Undo")), "click"));
        QTRY_COMPARE(application->settingsStore()->boolean("grammar.group.miscellaneous"), true);
        QTRY_VERIFY(!toastButton(QStringLiteral("Undo"))); // the toast closes with it
        QTRY_VERIFY_WITH_TIMEOUT(marks().contains(qMakePair(article, article + 1)), kHarperWait);
    }

    // With Hunspell checking spelling, Harper's spelling rule is off: a
    // misspelling has Hunspell's mark only. With it off, Harper's spelling
    // finding offers Add to dictionary, which reaches UserDic.udic (so
    // Hunspell accepts the word too) and Harper's own dictionary.
    void spellingIsMarkedOnceAndWordsReachBoth()
    {
        start(true);
        grammarOn();
        application->setSpellingOn(true);
        const QString text = QStringLiteral("I have it tomorow.");
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/words.ass"), dialogue(text.toUtf8()))));
        auto *field = item("lineText");
        QTRY_COMPARE(field->property("text").toString(), text);
        const int word = text.indexOf(QStringLiteral("tomorow"));
        QTRY_VERIFY(application->editorSpellingMarks(0).contains(word));
        QTest::qWait(1500); // Harper's check of the same text, done
        QVERIFY(application->editorGrammarAt(0, word).isEmpty());
        QVERIFY(!marked(text, QStringLiteral("tomorow")));

        application->setSpellingOn(false);
        QTRY_VERIFY_WITH_TIMEOUT(marked(text, QStringLiteral("tomorow")), kHarperWait);
        const QVariantMap finding = application->editorGrammarAt(0, word);
        QVERIFY(finding.value(QStringLiteral("word")).toBool());
        QCOMPARE(finding.value(QStringLiteral("text")).toString(), QStringLiteral("tomorow"));
        auto *menu = field->findChild<QObject *>(QStringLiteral("lineTextMenu"));
        rightClick(fieldPoint(field, word));
        QTRY_VERIFY(menu->property("visible").toBool());
        auto *add = field->findChild<QQuickItem *>(QStringLiteral("lineTextGrammarAddWord"));
        QVERIFY(add && add->isVisible());
        QCOMPARE(add->property("text").toString(), QStringLiteral("Add “tomorow” to dictionary"));
        QVERIFY(QMetaObject::invokeMethod(add, "triggered"));
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(application->addedDictionaryWords().contains(QStringLiteral("tomorow")));
        QFile harperWords(grammarDir() + QStringLiteral("/dictionary.txt"));
        QVERIFY(harperWords.open(QIODevice::ReadOnly));
        QVERIFY(harperWords.readAll().split('\n').contains("tomorow"));
        QTRY_VERIFY_WITH_TIMEOUT(!marked(text, QStringLiteral("tomorow")), kHarperWait);
        application->setSpellingOn(true);
        QTRY_VERIFY(!application->editorSpellingMarks(0).contains(word));
    }

    // Translation mode: the Translated field is checked, not the Original.
    void translationModeChecksTheTranslation()
    {
        start();
        grammarOn();
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/tl.ass"),
                                               dialogue("Their going to the store.\\Nfine"), "TLMode: Yes")));
        auto *session = target();
        QVERIFY(session);
        QTRY_VERIFY(application->spellingRole() == 1);
        application->editor().translationEdited(QStringLiteral("I has a apple."), 14);
        QTRY_VERIFY_WITH_TIMEOUT(marked(QStringLiteral("I has a apple."), QStringLiteral("a"), 1)
                                     || !marks(1).isEmpty(), kHarperWait);
        QVERIFY(marks(0).isEmpty());
    }

    // Tags from original: the Translated field leaves the carried leading
    // tags out, and so do the findings' marks and the menu's hit test.
    void carriedTagsAreLeftOutOfTheMarks()
    {
        start();
        grammarOn();
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/carried.ass"), dialogue("{\\i1}Wall high"),
                                               "TLMode: Yes")));
        QTRY_VERIFY(application->spellingRole() == 1);
        QVERIFY(application->editor().moveTags());
        const QString shown = QStringLiteral("I has a apple.");
        application->editor().translationEdited(shown, int(shown.size()));
        QTRY_COMPARE(item("translationText")->property("text").toString(), shown);
        const auto draft = target()->draftRecord();
        QVERIFY(draft);
        const auto &stored = draft->translation;
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(stored.data()), qsizetype(stored.size())),
                 QStringLiteral("{\\i1}I has a apple."));
        const int article = shown.indexOf(QStringLiteral("a apple"));
        QTRY_VERIFY_WITH_TIMEOUT(marks(1).contains(qMakePair(article, article + 1)), kHarperWait);
        for (const auto &[from, to] : marks(1))
            QVERIFY(to <= shown.size());
        QCOMPARE(application->editorGrammarAt(1, article).value(QStringLiteral("rule")).toString(), QStringLiteral("AnA"));
    }

    // Another file in the editor while the walk waits for harper-ls: the walk
    // stops and says so, and the editor stays where it was.
    void walkStopsWhenAnotherFileIsEdited()
    {
        start();
        grammarOn();
        QByteArray events;
        for (int i = 0; i < 60; ++i)
            events += dialogue(i % 20 == 5 ? "I has a apple." : "This line is fine.", i % 59);
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/first.ass"), events)));
        QTRY_VERIFY(target());
        QVERIFY(application->startGrammarWalk().value(QStringLiteral("walking")).toBool());
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/second.ass"),
                                               dialogue("One.") + dialogue("Two.", 2) + dialogue("Three.", 3))));
        application->selectTab(1);
        QTRY_COMPARE(lineText(), QStringLiteral("One."));
        const auto active = target()->selection().active;
        QTRY_VERIFY_WITH_TIMEOUT(application->grammarWalkState().value(QStringLiteral("cancelled")).toBool(), kHarperWait);
        const QVariantMap state = application->grammarWalkState();
        QCOMPARE(state.value(QStringLiteral("problem")).toString(), QStringLiteral("Another file is open in the editor."));
        QVERIFY(!state.value(QStringLiteral("found")).toBool());
        QTest::qWait(500);
        QCOMPARE(target()->selection().active, active);
        QCOMPARE(lineText(), QStringLiteral("One."));
    }

    // The walk's findings follow the settings: a group turned off drops its
    // findings in place; another English walks the file again.
    void walkFollowsTheSettings()
    {
        start();
        grammarOn();
        QByteArray events;
        for (int i = 0; i < 12; ++i)
            events += dialogue(i % 4 == 1 ? "I has a apple." : "This line is fine.", i + 1);
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/follow.ass"), events)));
        QTRY_VERIFY(target());
        application->startGrammarWalk();
        QTRY_VERIFY_WITH_TIMEOUT(application->grammarWalkState().value(QStringLiteral("done")).toBool(), kHarperWait);
        QVariantMap state = application->grammarWalkState();
        QVERIFY(state.value(QStringLiteral("found")).toBool());
        const QString group = state.value(QStringLiteral("groupName")).toString();
        const int count = state.value(QStringLiteral("count")).toInt();
        QVERIFY(count >= 6);
        // The shown finding's group off (in Settings, not through the walk).
        int index = -1;
        const QVariantList groups = application->grammarGroups();
        for (int i = 0; i < groups.size(); ++i)
            if (groups[i].toMap().value(QStringLiteral("name")).toString() == group)
                index = i;
        QVERIFY(index >= 0);
        application->setGrammarGroup(index, false);
        QTRY_VERIFY(application->grammarWalkState().value(QStringLiteral("groupName")).toString() != group);
        state = application->grammarWalkState();
        QVERIFY(state.value(QStringLiteral("done")).toBool());
        QCOMPARE(state.value(QStringLiteral("count")).toInt(), count); // dropped in place, not walked again
        application->setGrammarGroup(index, true);
        // Another English: all of it again.
        grammarOn(2);
        QTRY_VERIFY(!application->grammarWalkState().value(QStringLiteral("done")).toBool()
                    || application->grammarWalkState().value(QStringLiteral("count")).toInt() != count);
        QTRY_VERIFY_WITH_TIMEOUT(application->grammarWalkState().value(QStringLiteral("done")).toBool(), kHarperWait);
        state = application->grammarWalkState();
        QCOMPARE(state.value(QStringLiteral("checked")).toInt(), 12);
        QVERIFY(state.value(QStringLiteral("found")).toBool());
        QCOMPARE(state.value(QStringLiteral("index")).toInt(), 1);
    }

    // Settings > Spelling & grammar: the groups greyed out while grammar
    // check is off; the licence line opens Help > Credits.
    void settingsPage()
    {
        start();
        QVERIFY(QMetaObject::invokeMethod(window, "openSettings", Q_ARG(QVariant, QStringLiteral("spelling")),
                                          Q_ARG(QVariant, QVariant())));
        QQuickItem *group = nullptr;
        QTRY_VERIFY((group = item("setting_grammar.group.properNouns")) && group->isVisible());
        auto *dialog = named(QStringLiteral("settingsDialog"));
        QCOMPARE(dialog->property("currentPage").toString(), QStringLiteral("spelling"));
        QVERIFY(!group->isEnabled());
        QVERIFY(group->property("checked").toBool());
        QCOMPARE(group->property("text").toString(), QStringLiteral("Proper nouns"));
        auto *choice = item("setting_grammar.language");
        QCOMPARE(choice->property("count").toInt(), 5);
        QCOMPARE(choice->property("currentIndex").toInt(), 0);
        QVERIFY(QMetaObject::invokeMethod(dialog, "put", Q_ARG(QVariant, QStringLiteral("grammar.language")),
                                          Q_ARG(QVariant, 2)));
        QTRY_VERIFY(group->isEnabled());
        QVERIFY(item("settingsGrammarLicence")->property("text").toString().contains(QStringLiteral("Harper 3.0.0")));
        QVERIFY(QMetaObject::invokeMethod(dialog, "creditsRequested"));
        auto *credits = named(QStringLiteral("creditsDialog"));
        QTRY_VERIFY(credits->property("visible").toBool());
        QStringList names;
        for (const QVariant &row : credits->property("components").toList())
            names << row.toList().value(0).toString();
        QVERIFY(names.contains(QStringLiteral("Harper")));
        // The groups' names and rule counts come from Harper's grouping.
        const QVariantList groups = application->grammarGroups();
        QCOMPARE(groups.size(), 15);
        int rules = 0;
        for (const QVariant &g : groups)
            rules += g.toMap().value(QStringLiteral("rules")).toInt();
        QCOMPARE(rules, 936);
    }

    // The panel's Grammar filter walks the whole file in the background,
    // from the active Line: the window answers while it walks, Cancel stops
    // it, and its actions act on the finding shown.
    void panelWalksTheFileInTheBackground()
    {
        start();
        grammarOn();
        QByteArray events;
        for (int i = 0; i < 120; ++i)
            events += dialogue(i % 40 == 5 ? "I has a apple." : "This line is fine.", i % 59);
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/walk.ass"), events)));
        QTRY_VERIFY(target());
        QElapsedTimer started;
        started.start();
        QVariantMap state = application->startGrammarWalk();
        QVERIFY(started.elapsed() < 500); // nothing waits for harper-ls
        QVERIFY(state.value(QStringLiteral("walking")).toBool());
        QCOMPARE(state.value(QStringLiteral("total")).toInt(), 120);
        QTRY_VERIFY_WITH_TIMEOUT(application->grammarWalkState().value(QStringLiteral("done")).toBool(), kHarperWait);
        state = application->grammarWalkState();
        QCOMPARE(state.value(QStringLiteral("checked")).toInt(), 120);
        QVERIFY(state.value(QStringLiteral("found")).toBool());
        QCOMPARE(state.value(QStringLiteral("line")).toInt(), 6);
        QCOMPARE(state.value(QStringLiteral("index")).toInt(), 1);
        const int count = state.value(QStringLiteral("count")).toInt();
        QVERIFY(count >= 3);

        // Replace: the shown finding's Line, as one step.
        for (int i = 0; i < count && state.value(QStringLiteral("rule")).toString() != QLatin1String("AnA"); ++i)
            state = application->grammarWalkIgnore();
        QCOMPARE(state.value(QStringLiteral("rule")).toString(), QStringLiteral("AnA"));
        QCOMPARE(state.value(QStringLiteral("line")).toInt(), 6);
        state = application->grammarWalkReplace(QStringLiteral("an"));
        QCOMPARE(lineText(5), QStringLiteral("I has an apple."));
        QCOMPARE(target()->history().back().name, std::string("Correcting grammar errors in the text field"));
        QVERIFY(state.value(QStringLiteral("found")).toBool());
        QVERIFY(state.value(QStringLiteral("line")).toInt() > 6);
        // Ignore this rule: the group off, its findings gone from the walk.
        const QString group = state.value(QStringLiteral("groupName")).toString();
        state = application->grammarWalkIgnoreRule();
        QVERIFY(state.value(QStringLiteral("ignoredGroup")).toInt() >= 0);
        QCOMPARE(state.value(QStringLiteral("ignoredGroupName")).toString(), group);
        if (state.value(QStringLiteral("found")).toBool())
            QVERIFY(state.value(QStringLiteral("groupName")).toString() != group);
        application->setGrammarGroup(state.value(QStringLiteral("ignoredGroup")).toInt(), true);

        // Cancel while it walks; the panel shows the walk, Check again starts it over.
        auto *dock = named(QStringLiteral("spellDock"));
        QVERIFY(QMetaObject::invokeMethod(window, "showPanel", Q_ARG(QVariant, QVariant::fromValue(dock))));
        auto *panel = named(QStringLiteral("spellCheckerPanel"));
        QTRY_VERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "setMode", Q_ARG(QVariant, QStringLiteral("grammar"))));
        QVERIFY(QMetaObject::invokeMethod(panel, "start"));
        QVERIFY(panel->property("grammar").toMap().value(QStringLiteral("walking")).toBool());
        auto *cancel = item("grammarCancel");
        QVERIFY(cancel);
        QTRY_VERIFY(cancel->isVisible());
        QVERIFY(QMetaObject::invokeMethod(cancel, "click"));
        state = application->grammarWalkState();
        QVERIFY(state.value(QStringLiteral("cancelled")).toBool());
        QVERIFY(!state.value(QStringLiteral("walking")).toBool());
        QVERIFY(QMetaObject::invokeMethod(panel, "start"));
        QTRY_VERIFY_WITH_TIMEOUT(item("grammarMessage") && item("grammarMessage")->isVisible(), kHarperWait);
        QVERIFY(!item("grammarMessage")->property("text").toString().isEmpty());
        QVERIFY(item("grammarIgnoreRule")->isVisible());
        QVERIFY(!item("spellReplaceAll")->isVisible());
        QVERIFY(QMetaObject::invokeMethod(panel, "setMode", Q_ARG(QVariant, QStringLiteral("spelling"))));
        QVERIFY(!application->grammarWalkState().contains(QStringLiteral("walking")));
    }

    // SRT: Harper reads the text as spelling does there, with the <...> tags
    // left out and {...} as text; a suggestion keeps the tags.
    void srtTagsAreLeftOut()
    {
        start();
        grammarOn();
        const QString text = QStringLiteral(R"(<font face="tomorow">I has a apple.</font> {see you tomorow})");
        QVERIFY(application->openFile(writeSrt(home() + QStringLiteral("/tags.srt"), text)));
        QTRY_COMPARE(item("lineText")->property("text").toString(), text);
        const int article = text.indexOf(QStringLiteral("a apple"));
        QTRY_VERIFY_WITH_TIMEOUT(marks().contains(qMakePair(article, article + 1)), kHarperWait);
        const int braced = text.lastIndexOf(QStringLiteral("tomorow"));
        QTRY_VERIFY(marks().contains(qMakePair(braced, braced + 7)));
        for (const auto &[from, to] : marks())
            QVERIFY(from > text.indexOf(QLatin1Char('>'))); // nothing in the tag
        QVERIFY(application->replaceEditorGrammar(0, article, QStringLiteral("an")));
        QCOMPARE(lineText(), QStringLiteral(R"(<font face="tomorow">I has an apple.</font> {see you tomorow})"));
    }

    // The same text in a Document of another format is checked again: SRT
    // leaves <i> out, ASS reads it as text.
    void sameTextInAnotherFormatIsCheckedAgain()
    {
        start();
        grammarOn();
        const QString text = QStringLiteral("<i>I has a apple.</i>");
        QVERIFY(application->openFile(writeSrt(home() + QStringLiteral("/same.srt"), text)));
        QTRY_COMPARE(item("lineText")->property("text").toString(), text);
        const int article = text.indexOf(QStringLiteral("a apple"));
        QTRY_VERIFY_WITH_TIMEOUT(marks().contains(qMakePair(article, article + 1)), kHarperWait);
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/same.ass"), dialogue(text.toUtf8()))));
        application->selectTab(1);
        QTRY_VERIFY(target()->document().format() == core::SubtitleFormat::Ass);
        QTRY_VERIFY_WITH_TIMEOUT(marks().contains(qMakePair(article, article + 1)), kHarperWait);
        QVERIFY(application->replaceEditorGrammar(0, article, QStringLiteral("an")));
        QCOMPARE(lineText(), QStringLiteral("<i>I has an apple.</i>"));
    }

    // Skipped Lines (comments here) are read a turn of the event loop at a
    // time: the walk starts at once, and Cancel is answered while it reads.
    void walkReadsSkippedLinesInTurns()
    {
        start();
        grammarOn();
        QByteArray events;
        for (int i = 0; i < 3000; ++i)
            events += "Comment" + dialogue("I has a apple.").mid(8);
        events += dialogue("I has a apple.", 2);
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/comments.ass"), events)));
        QTRY_VERIFY(target());
        QVariantMap state = application->startGrammarWalk();
        QCOMPARE(state.value(QStringLiteral("total")).toInt(), 3001);
        QVERIFY(state.value(QStringLiteral("checked")).toInt() < 3000); // not every comment yet
        state = application->cancelGrammarWalk();
        QVERIFY(state.value(QStringLiteral("cancelled")).toBool());
        application->startGrammarWalk();
        QTRY_VERIFY_WITH_TIMEOUT(application->grammarWalkState().value(QStringLiteral("done")).toBool(), kHarperWait);
        QVERIFY(application->grammarWalkState().value(QStringLiteral("found")).toBool());
    }

    // A Line longer than the check's cap (4096 UTF-16 units) is never
    // checked, in the field or by the walk.
    void outsizedLinesAreNotChecked()
    {
        start();
        grammarOn();
        const QByteArray outsized = QByteArray("I has a apple. ").repeated(300);
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/long.ass"),
                                               dialogue(outsized) + dialogue("I has a apple.", 2))));
        QTRY_COMPARE(item("lineText")->property("text").toString(), QString::fromUtf8(outsized.trimmed()));
        QTest::qWait(1500); // the field's check would be back by now
        QVERIFY(marks().isEmpty());
        application->startGrammarWalk();
        QTRY_VERIFY_WITH_TIMEOUT(application->grammarWalkState().value(QStringLiteral("done")).toBool(), kHarperWait);
        const QVariantMap state = application->grammarWalkState();
        QVERIFY(state.value(QStringLiteral("found")).toBool());
        QCOMPARE(state.value(QStringLiteral("line")).toInt(), 2);
    }

    // Lines with nothing to check (tags only here, a million units of them)
    // keep nothing, so the walk never stops short of the Lines after them.
    void blankLinesDoNotHoldTheWalk()
    {
        start();
        grammarOn();
        // 4096 units each, the longest checked: 256 of them reach the kept
        // limit within one turn.
        const QByteArray tags = "{" + QByteArray("\\b1").repeated(1364) + "\\i}";
        QByteArray events;
        for (int i = 0; i < 300; ++i)
            events += dialogue(tags);
        events += dialogue("I has a apple.", 2);
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/tags.ass"), events)));
        QTRY_VERIFY(target());
        QTest::qWait(500); // the settings in (a change would start the walk over)
        application->startGrammarWalk();
        QTRY_VERIFY_WITH_TIMEOUT(application->grammarWalkState().value(QStringLiteral("done")).toBool(), kHarperWait);
        QCOMPARE(application->grammarWalkState().value(QStringLiteral("line")).toInt(), 301);
    }

    // A finding's Line becomes the only selected one, so Enter in the editor
    // commits there and nowhere else.
    void walkSelectsTheFindingsLine()
    {
        start();
        grammarOn();
        QByteArray events;
        for (int i = 0; i < 6; ++i)
            events += dialogue(i == 4 ? "I has a apple." : "This line is fine.", i + 1);
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/select.ass"), events)));
        QTRY_VERIFY(target());
        const auto lines = target()->document().lines();
        target()->setSelection(application::Selection{lines[0]->id, {lines[0]->id, lines[1]->id, lines[2]->id},
                                                      lines[0]->id, {}});
        application->startGrammarWalk();
        QTRY_VERIFY_WITH_TIMEOUT(application->grammarWalkState().value(QStringLiteral("found")).toBool(), kHarperWait);
        const auto &selection = target()->selection();
        QCOMPARE(selection.active, std::optional(lines[4]->id));
        QCOMPARE(selection.selected, (std::set<core::LineId>{lines[4]->id}));
        // Typed and committed in the field: only the finding's Line changes.
        application->editor().textEdited(QStringLiteral("I have an apple."), 16);
        QVERIFY(application->editor().commit()); // Enter
        QTRY_COMPARE(lineText(4), QStringLiteral("I have an apple."));
        QCOMPARE(lineText(0), QStringLiteral("This line is fine."));
        QCOMPARE(lineText(1), QStringLiteral("This line is fine."));
    }

    // A draft the editor cannot commit (its end before its start, under the
    // Block policy) keeps the editor and the selection where they are when a
    // finding comes in.
    void walkLeavesAnUncommittableDraft()
    {
        start();
        grammarOn();
        QByteArray events;
        for (int i = 0; i < 6; ++i)
            events += dialogue(i == 4 ? "I has a apple." : "This line is fine.", i + 1);
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/draft.ass"), events)));
        QTRY_VERIFY(target());
        const auto first = target()->document().lines()[0]->id;
        QVERIFY(target()->editDraft(first, application::DraftChange{.text = u8"Not yet sent",
                                                                    .start = core::DocumentTime(5000000)}));
        QVERIFY(target()->draftProblem());
        application->editor().reloadFromSession();
        QTRY_COMPARE(item("lineText")->property("text").toString(), QStringLiteral("Not yet sent"));
        application->startGrammarWalk();
        QTRY_VERIFY_WITH_TIMEOUT(application->grammarWalkState().value(QStringLiteral("found")).toBool(), kHarperWait);
        QCOMPARE(target()->draftLine(), std::optional(first));
        QCOMPARE(target()->selection().active, std::optional(first));
        QCOMPARE(item("lineText")->property("text").toString(), QStringLiteral("Not yet sent"));
    }

    // Replacing the walk's last finding: the editor and the Grid show the
    // correction though no finding is left to show.
    void replacingTheLastFindingShowsIt()
    {
        start();
        grammarOn();
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/last.ass"), dialogue("I has a apple."))));
        QTRY_VERIFY(target());
        application->startGrammarWalk();
        QTRY_VERIFY_WITH_TIMEOUT(application->grammarWalkState().value(QStringLiteral("done")).toBool(), kHarperWait);
        QVariantMap state = application->grammarWalkState();
        const int count = state.value(QStringLiteral("count")).toInt();
        for (int i = 0; i < count && state.value(QStringLiteral("rule")).toString() != QLatin1String("AnA"); ++i)
            state = application->grammarWalkIgnore();
        QCOMPARE(state.value(QStringLiteral("rule")).toString(), QStringLiteral("AnA")); // the Line's last
        state = application->grammarWalkReplace(QStringLiteral("an"));
        QVERIFY(!state.value(QStringLiteral("found")).toBool());
        QCOMPARE(lineText(), QStringLiteral("I has an apple."));
        QCOMPARE(item("lineText")->property("text").toString(), QStringLiteral("I has an apple."));
        QVERIFY(gridText().contains(QStringLiteral("an apple")));
    }

    // For review (HIKARI_SURFACE_SHOT_DIR; skipped without it), in the four
    // themes (gr1-<part>-<theme>.png): the Line editor's underlines with a
    // finding's menu, the toast after Ignore this rule, the Spellchecker
    // panel's Grammar walk and Settings › Spelling & grammar.
    void screenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        start();
        window->resize(1280, 800);
        grammarOn();
        const QString text = QStringLiteral(R"({\i1}Their{\i0} going to the store tomorow, I has a apple.)");
        QByteArray events = dialogue(text.toUtf8());
        for (int i = 0; i < 8; ++i)
            events += dialogue(i % 3 == 1 ? "She dont like the the rain." : "This line is fine.", i + 2);
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/shots.ass"), events)));
        auto *field = item("lineText");
        QTRY_COMPARE(field->property("text").toString(), text);
        const int article = text.indexOf(QStringLiteral("a apple"));
        QTRY_VERIFY_WITH_TIMEOUT(marks().contains(qMakePair(article, article + 1)), kHarperWait);
        auto &settings = *application->settingsStore();
        settings.setValue(QStringLiteral("appearance.followSystem"), false);
        const auto save = [&](QQuickWindow *w, const QString &name) {
            QTest::qWait(400);
            QImage image = QGuiApplication::platformName() == QLatin1String("xcb")
                               ? w->screen()->grabWindow(0, w->x(), w->y(), w->width(), w->height()).toImage()
                               : w->grabWindow();
            QVERIFY(image.save(out + QStringLiteral("/gr1-") + name + QStringLiteral(".png")));
        };
        auto *menu = field->findChild<QObject *>(QStringLiteral("lineTextMenu"));
        for (const QString &code : {QStringLiteral("light"), QStringLiteral("dark"), QStringLiteral("highContrastWhite"),
                                    QStringLiteral("highContrastBlack")}) {
            settings.setValue(QStringLiteral("appearance.theme"), code);
            QTest::qWait(300);
            rightClick(fieldPoint(field, article));
            QTRY_VERIFY(menu->property("visible").toBool());
            save(window, QStringLiteral("menu-") + code);
            QVERIFY(QMetaObject::invokeMethod(field->findChild<QQuickItem *>(QStringLiteral("lineTextIgnoreRule")),
                                              "triggered"));
            QVERIFY(QMetaObject::invokeMethod(menu, "close"));
            QTRY_VERIFY(toastButton(QStringLiteral("Undo")));
            // The other groups' marks, found again under the new rules.
            QTRY_VERIFY_WITH_TIMEOUT(marked(text, QStringLiteral("Their")), kHarperWait);
            QTRY_VERIFY(!item("lineTextGrammar")->property("rows").toList().isEmpty());
            QTest::qWait(1500); // the helper's restart slows the next frames
            save(window, QStringLiteral("toast-") + code);
            QVERIFY(QMetaObject::invokeMethod(toastButton(QStringLiteral("Undo")), "click"));
            QTRY_COMPARE(settings.boolean("grammar.group.miscellaneous"), true);
            QTRY_VERIFY_WITH_TIMEOUT(marks().contains(qMakePair(article, article + 1)), kHarperWait);

            auto *dock = named(QStringLiteral("spellDock"));
            QVERIFY(QMetaObject::invokeMethod(window, "showPanel", Q_ARG(QVariant, QVariant::fromValue(dock))));
            QQuickItem *panel = nullptr;
            QTRY_VERIFY((panel = item("spellCheckerPanel")));
            QVERIFY(QMetaObject::invokeMethod(panel, "setMode", Q_ARG(QVariant, QStringLiteral("grammar"))));
            QVERIFY(QMetaObject::invokeMethod(panel, "start"));
            QTRY_VERIFY_WITH_TIMEOUT(application->grammarWalkState().value(QStringLiteral("done")).toBool(), kHarperWait);
            QTRY_VERIFY(item("grammarMessage") && item("grammarMessage")->isVisible());
            save(window, QStringLiteral("panel-") + code);
            QVERIFY(QMetaObject::invokeMethod(panel, "setMode", Q_ARG(QVariant, QStringLiteral("spelling"))));

            QVERIFY(QMetaObject::invokeMethod(window, "openSettings", Q_ARG(QVariant, QStringLiteral("spelling")),
                                              Q_ARG(QVariant, QVariant())));
            auto *dialog = qobject_cast<QQuickWindow *>(named(QStringLiteral("settingsDialog")));
            QVERIFY(dialog);
            QTRY_VERIFY(dialog->isVisible());
            QTRY_VERIFY(item("setting_grammar.group.properNouns") && item("setting_grammar.group.properNouns")->isEnabled());
            save(dialog, QStringLiteral("settings-") + code);
            QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
            QTRY_VERIFY(!dialog->isVisible());
        }
        settings.setValue(QStringLiteral("appearance.theme"), QStringLiteral("dark"));
    }

    // Switched off, the helper ends at once.
    void turningItOffEndsTheHelper()
    {
        start();
        grammarOn();
        const QString text = QStringLiteral("I has a apple.");
        QVERIFY(application->openFile(writeAss(home() + QStringLiteral("/stop.ass"), dialogue(text.toUtf8()))));
        QTRY_VERIFY_WITH_TIMEOUT(!marks().isEmpty(), kHarperWait);
        if (harperChildren() >= 0)
            QCOMPARE(harperChildren(), 1);
        application->settingsStore()->set("grammar.language", 0);
        QTRY_VERIFY(marks().isEmpty());
        QTRY_COMPARE(std::max(0, harperChildren()), 0);
        QVERIFY(!application->grammarOn());
    }
};

QTEST_MAIN(GrammarUiTests)
#include "grammar_ui_tests.moc"
