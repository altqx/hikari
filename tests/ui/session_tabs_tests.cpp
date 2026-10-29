// P6: tabs over the shared Workspace (GLOBAL_ADD_PAGE, GLOBAL_CLOSE_PAGE,
// GLOBAL_NEXT_TAB / GLOBAL_PREVIOUS_TAB) and session restore
// (GLOBAL_LOAD_LAST_SESSION, the .hksession and legacy .kls files) through
// the composition, against legacy Notebook, HikariSubFrame and hikarisubApp
// at 20d647c4: partial restore from fixtures with missing files,
// C05-audio-association, each tab's own media, and the startup choices.
// CF3: last-session.json, converted once from LastSession.txt.

#include "hikari/app/application.h"
#include "hikari/app/session_json.h"
#include "hikari/application/session_file.h"

#include "legacy_session.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

// An ASS file with `lines` Dialogue Lines whose text is `text` and the row.
QString writeSubtitles(const QString &path, const char *text, int lines = 3)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
    for (int i = 0; i < lines; ++i)
        f.write(QStringLiteral("Dialogue: 0,0:00:0%1.00,0:00:0%2.00,Default,,0,0,0,,%3 %4\n")
                    .arg(i + 1)
                    .arg(i + 2)
                    .arg(QLatin1String(text))
                    .arg(i)
                    .toUtf8());
    return path;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray hash(const QString &path)
{
    return QCryptographicHash::hash(readAll(path), QCryptographicHash::Sha256);
}

// A session file's bytes: a legacy one (CRLF, read the same on every
// platform under P6-session-crlf) or JSON.
void writeSession(const QString &path, const QByteArray &text)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text);
}

// A legacy LastSession.txt with one closed tab.
QByteArray legacySession()
{
    application::SessionTab tab;
    tab.subtitles = "old.ass";
    return QByteArray::fromStdString(testing::writeLegacySession("HikariSub v0.0.2", true, {tab}));
}

application::Session readSession(const QString &path)
{
    const QByteArray bytes = readAll(path);
    return app::readSession(std::string_view(bytes.constData(), bytes.size())).value_or(application::Session{});
}

QString media(const char *name)
{
    return QDir::toNativeSeparators(QStringLiteral(HIKARI_MEDIA_FIXTURES "/") + QLatin1String(name));
}

QStringList titles(const app::Application &a)
{
    QStringList out;
    for (const QVariant &tab : a.tabs())
        out << tab.toMap().value(QStringLiteral("title")).toString();
    return out;
}

QVariantList choose(const QVariantList &rows, bool save)
{
    QVariantList out;
    for (const QVariant &r : rows)
        out << QVariantMap{{QStringLiteral("id"), r.toMap().value(QStringLiteral("id"))},
                           {QStringLiteral("save"), save},
                           {QStringLiteral("path"), QString()}};
    return out;
}

} // namespace

class SessionTabsTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;

    app::Application::Options options(const QTemporaryDir &home) const
    {
        app::Application::Options o;
        o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        return o;
    }
    application::EditSession *target(app::Application &a)
    {
        const auto t = a.workspace().editingTarget();
        return t ? a.files().session(*t) : nullptr;
    }
    int activeRow(app::Application &a)
    {
        auto *s = target(a);
        if (!s || !s->selection().active)
            return -1;
        const auto lines = s->document().lines();
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (lines[i]->id == *s->selection().active)
                return static_cast<int>(i);
        return -1;
    }
    void edit(app::Application &a, const char8_t *text)
    {
        auto *s = target(a);
        const auto line = s->document().lines().front()->id;
        QVERIFY(s->run(application::Command{"Set text", s->revision(), {line},
                                            [&](core::Document &d) { return d.setLineText(line, text); }}));
    }
    // reviewSession, then the review answered with Discard when it asks.
    bool loadSession(app::Application &a, const QUrl &file = {})
    {
        const QVariantMap review = a.reviewSession(file);
        if (!review.value(QStringLiteral("ok")).toBool())
            return false;
        const QVariantList rows = review.value(QStringLiteral("rows")).toList();
        if (rows.isEmpty())
            a.finishClose();
        else
            a.resolveClose(choose(rows, false));
        return true;
    }

private slots:
    // GLOBAL_NEXT_TAB / GLOBAL_PREVIOUS_TAB (HikariSubFrame::OnPageChange):
    // nothing with one tab, wrapping past either end, the protected
    // reference never a tab; a tab keeps its own selection.
    void tabTraversalWrapsAndSkipsTheReference()
    {
        app::Application a;
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("one.ass")), "one")));
        a.changeTab(1);
        QCOMPARE(a.currentTab(), 0);
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("two.ass")), "two")));
        QVERIFY(a.openReference(writeSubtitles(dir.filePath(QStringLiteral("ref.ass")), "ref")));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("three.ass")), "three")));
        QCOMPARE(titles(a), (QStringList{"one.ass", "two.ass", "three.ass"}));
        QCOMPARE(a.currentTab(), 0);
        QSignalSpy shown(&a, &app::Application::tabShown);
        a.changeTab(1);
        QCOMPARE(a.currentTab(), 1);
        QCOMPARE(a.shell().editingTitle(), QStringLiteral("two.ass"));
        a.selectLine(target(a)->document().lines()[2]->id.value);
        a.changeTab(1);
        QCOMPARE(a.currentTab(), 2);
        a.changeTab(1); // wraps to the first
        QCOMPARE(a.currentTab(), 0);
        a.changeTab(-1); // wraps to the last
        QCOMPARE(a.currentTab(), 2);
        a.changeTab(-1);
        QCOMPARE(a.currentTab(), 1);
        QCOMPARE(activeRow(a), 2); // its selection stayed with it
        QCOMPARE(shown.size(), 5);
        QVERIFY(a.workspace().reference()); // never shown as a tab, never the target
        a.selectTab(0);
        QCOMPARE(a.currentTab(), 0);
        a.selectTab(7);
        QCOMPARE(a.currentTab(), 0);
    }

    // Tab labels (HikariSubFrame::Label, Notebook::CalcSizes): "<step>*name"
    // while modified, at most 40 characters by default.
    void tabLabelsFollowLegacy()
    {
        app::Application a;
        const QString longName = QStringLiteral("a-very-long-subtitle-file-name-for-the-tab-bar.ass");
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(longName), "x")));
        auto row = a.tabs().first().toMap();
        QCOMPARE(row.value(QStringLiteral("label")).toString(), longName.left(40));
        QVERIFY(row.value(QStringLiteral("current")).toBool());
        QCOMPARE(row.value(QStringLiteral("tip")).toString(), longName + QLatin1Char('\n'));
        edit(a, u8"changed");
        a.selectLine(target(a)->document().lines()[1]->id.value); // views refresh
        row = a.tabs().first().toMap();
        QCOMPARE(row.value(QStringLiteral("label")).toString(), (QStringLiteral("1*") + longName).left(40));
    }

    // GLOBAL_ADD_PAGE (Ctrl+T, InsertTab): an Untitled tab after the last,
    // shown, without writing the session; the tab bar's "+" (AddPage(true,
    // true)) writes it.
    void addPageAppendsAnUntitledTab()
    {
        QTemporaryDir home;
        app::Application a(options(home));
        const QString last = a.lastSessionPath();
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("first.ass")), "first")));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("second.ass")), "second")));
        QFile::remove(last);
        a.addPage();
        QCOMPARE(titles(a), (QStringList{"first.ass", "second.ass", "Untitled"}));
        QCOMPARE(a.currentTab(), 2);
        QVERIFY(!QFile::exists(last));
        a.addPage(true);
        QCOMPARE(a.currentTab(), 3);
        QVERIFY(QFile::exists(last));
        QCOMPARE(readSession(last).tabs.size(), std::size_t(4));
    }

    // GLOBAL_CLOSE_PAGE (Notebook::DeletePage): the review first; the tab
    // taking the closed one's place becomes active, or the last one; the
    // session is written. A middle-clicked tab is reviewed and closed alone.
    void closingATabShowsTheNextOrTheLast()
    {
        QTemporaryDir home;
        app::Application a(options(home));
        for (const char *name : {"a.ass", "b.ass", "c.ass", "d.ass"})
            QVERIFY(a.openFile(writeSubtitles(dir.filePath(QLatin1String(name)), name)));
        a.selectTab(1);
        QVERIFY(a.reviewClose(QStringLiteral("close")).isEmpty());
        a.finishClose();
        QCOMPARE(titles(a), (QStringList{"a.ass", "c.ass", "d.ass"}));
        QCOMPARE(a.shell().editingTitle(), QStringLiteral("c.ass"));
        QCOMPARE(readSession(a.lastSessionPath()).tabs.size(), std::size_t(3));
        a.selectTab(2);
        QVERIFY(a.closeEditingTarget());
        QCOMPARE(a.shell().editingTitle(), QStringLiteral("c.ass")); // the last tab
        // A middle click on an inactive, modified tab reviews only that one.
        a.selectTab(0);
        edit(a, u8"unsaved");
        a.selectTab(1);
        const QVariantList rows = a.reviewCloseTab(0);
        QCOMPARE(rows.size(), qsizetype(1));
        QCOMPARE(rows.first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("a.ass"));
        a.cancelClose();
        QCOMPARE(titles(a), (QStringList{"a.ass", "c.ass"}));
        a.resolveClose(choose(a.reviewCloseTab(0), false));
        QCOMPARE(titles(a), (QStringList{"c.ass"}));
        QCOMPARE(a.shell().editingTitle(), QStringLiteral("c.ass"));
        QVERIFY(a.reviewCloseTab(0).isEmpty());
        a.finishClose();
        // SH2-last-tab-home: with no tab left the Home page shows, where
        // legacy Notebook::DeletePage made a new empty one. The session
        // written has no Document to offer.
        QVERIFY(titles(a).isEmpty());
        QVERIFY(!a.workspace().editingTarget());
        QVERIFY(a.tabs().isEmpty());
        QVERIFY(a.lastSessionSummary().value(QStringLiteral("names")).toStringList().isEmpty());
    }

    // Opening subtitles into the active tab (P2 review) keeps its place.
    void openingIntoATabKeepsItsPlace()
    {
        app::Application a;
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("p.ass")), "p")));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("q.ass")), "q")));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("r.ass")), "r")));
        a.selectTab(1);
        const QVariantMap review = a.reviewOpen(writeSubtitles(dir.filePath(QStringLiteral("s.ass")), "s"));
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        a.finishClose();
        QCOMPARE(titles(a), (QStringList{"p.ass", "s.ass", "r.ass"}));
        QCOMPARE(a.currentTab(), 1);
    }

    // Session fixture with missing files (tests/fixtures/sessions/
    // partial-restore.kls): the complete tabs restore, a tab whose
    // subtitles are missing stays as an Untitled tab, and every missing file
    // stays visible as an unresolved entry, written back to the session
    // until it is resolved or removed. The last tab is active (legacy
    // AddPage(false) made each loaded tab the active one).
    void partialRestoreKeepsMissingEntriesVisible()
    {
        QTemporaryDir home;
        QTemporaryDir files;
        const QString d = QDir::fromNativeSeparators(files.path());
        writeSubtitles(files.filePath(QStringLiteral("first.ass")), "first");
        writeSubtitles(files.filePath(QStringLiteral("second.ass")), "second", 10);
        QVERIFY(QFile::copy(media("audioonly.mkv"), files.filePath(QStringLiteral("first.wav"))));
        QByteArray text = readAll(QStringLiteral(HIKARI_SESSION_FIXTURES "/partial-restore.kls"));
        text.replace("@DIR@", d.toUtf8());
        const QString kls = files.filePath(QStringLiteral("partial.kls"));
        writeSession(kls, text);
        const QByteArray klsBefore = hash(kls);
        const QByteArray firstBefore = hash(files.filePath(QStringLiteral("first.ass")));

        app::Application a(options(home));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("open-before.ass")), "before")));
        QSignalSpy restored(&a, &app::Application::sessionRestored);
        QVERIFY(loadSession(a, QUrl::fromLocalFile(kls)));
        QCOMPARE(restored.size(), 1);
        QCOMPARE(restored.first().first().toInt(), 4);
        QCOMPARE(titles(a), (QStringList{"first.ass", "Untitled", "second.ass", "Untitled"}));
        QCOMPARE(a.currentTab(), 3);
        QVERIFY(!a.audio().hasAudio()); // the active tab has no audio of its own
        const QVariantList unresolved = a.unresolvedRestores();
        QCOMPARE(unresolved.size(), qsizetype(4));
        auto entry = [&](int i) { return unresolved[i].toMap(); };
        QCOMPARE(entry(0).value(QStringLiteral("kind")).toString(), QStringLiteral("subtitles"));
        QCOMPARE(entry(0).value(QStringLiteral("tab")).toInt(), 1);
        QCOMPARE(QDir::fromNativeSeparators(entry(0).value(QStringLiteral("path")).toString()), d + "/missing.ass");
        QCOMPARE(entry(1).value(QStringLiteral("kind")).toString(), QStringLiteral("video"));
        QCOMPARE(entry(2).value(QStringLiteral("kind")).toString(), QStringLiteral("audio"));
        QCOMPARE(entry(3).value(QStringLiteral("kind")).toString(), QStringLiteral("keyframes"));
        QCOMPARE(entry(3).value(QStringLiteral("tab")).toInt(), 2);

        // The session as written after loading (legacy SaveLastSession)
        // keeps the unresolved files; the .kls and the subtitles are unchanged.
        auto written = readSession(a.lastSessionPath());
        QCOMPARE(written.tabs.size(), std::size_t(4));
        QCOMPARE(QDir::fromNativeSeparators(QString::fromStdString(written.tabs[1].subtitles)), d + "/missing.ass");
        QCOMPARE(QDir::fromNativeSeparators(QString::fromStdString(written.tabs[1].video)), d + "/missing.mkv");
        QCOMPARE(written.tabs[1].position, 2002);
        QCOMPARE(QDir::fromNativeSeparators(QString::fromStdString(written.tabs[1].audio)), d + "/missing.wav");
        QCOMPARE(QDir::fromNativeSeparators(QString::fromStdString(written.tabs[2].keyframes)), d + "/missing_keyframes.txt");
        QCOMPARE(QDir::fromNativeSeparators(QString::fromStdString(written.tabs[0].audio)), d + "/first.wav");
        QCOMPARE(written.tabs[0].active, 1);
        QCOMPARE(written.tabs[2].active, 7);
        QCOMPARE(hash(kls), klsBefore);
        QCOMPARE(hash(files.filePath(QStringLiteral("first.ass"))), firstBefore);

        // Retry: the subtitles now exist and load into their tab, at its place.
        QVERIFY(!a.retryRestore(1)); // the video is still missing
        writeSubtitles(files.filePath(QStringLiteral("missing.ass")), "found");
        QVERIFY(a.retryRestore(0));
        QCOMPARE(titles(a), (QStringList{"first.ass", "missing.ass", "second.ass", "Untitled"}));
        QCOMPARE(a.currentTab(), 3);
        a.selectTab(1);
        QCOMPARE(activeRow(a), 2); // the session's Active
        // Remove forgets an entry; the session no longer names it.
        QCOMPARE(a.unresolvedRestores().size(), qsizetype(3));
        a.removeRestore(1); // the audio (rows renumber after the subtitles left)
        QCOMPARE(a.unresolvedRestores().size(), qsizetype(2));
        written = readSession(a.lastSessionPath());
        QCOMPARE(written.tabs[1].audio, std::string());
        QCOMPARE(QDir::fromNativeSeparators(QString::fromStdString(written.tabs[1].subtitles)), d + "/missing.ass");
        // Relink: another file for the keyframes entry.
        QFile k(files.filePath(QStringLiteral("other_keyframes.txt")));
        QVERIFY(k.open(QIODevice::WriteOnly));
        k.write("# keyframe format v1\nfps 0\n0\n24\n");
        k.close();
        QVERIFY(a.relinkRestore(1, QUrl::fromLocalFile(k.fileName())));
        QCOMPARE(a.unresolvedRestores().size(), qsizetype(1)); // the video stays
        written = readSession(a.lastSessionPath());
        QCOMPARE(QDir::fromNativeSeparators(QString::fromStdString(written.tabs[2].keyframes)),
                 QDir::fromNativeSeparators(k.fileName()));
    }

    // C05-audio-association: two tabs with Audio only on the first; the
    // second never shows the first's audio. An explicit association of a
    // later tab is its own; a missing audio file opens nothing.
    void aLaterTabNeverInheritsAudio()
    {
        QTemporaryDir home;
        QTemporaryDir files;
        const QString d = QDir::fromNativeSeparators(files.path());
        writeSubtitles(files.filePath(QStringLiteral("one.ass")), "one");
        writeSubtitles(files.filePath(QStringLiteral("two.ass")), "two");
        writeSubtitles(files.filePath(QStringLiteral("three.ass")), "three");
        writeSubtitles(files.filePath(QStringLiteral("four.ass")), "four");
        QVERIFY(QFile::copy(media("audioonly.mkv"), files.filePath(QStringLiteral("one.mka"))));
        QVERIFY(QFile::copy(media("audioonly.mkv"), files.filePath(QStringLiteral("three.mka"))));
        const QByteArray before = hash(files.filePath(QStringLiteral("one.mka")));
        const QByteArray text = "\xEF\xBB\xBF" + QStringLiteral(
            "[HikariSub v0.0.1]\r\n"
            "Tab: 0\r\nVideo: \r\nPosition: 0\r\nFFMS2: 1\r\nSubtitles: %1/one.ass\r\nActive: 0\r\nScroll: 0\r\nEditor: 1\r\n"
            "Audio: %1/one.mka\r\n"
            "Tab: 1\r\nVideo: \r\nPosition: 0\r\nFFMS2: 1\r\nSubtitles: %1/two.ass\r\nActive: 0\r\nScroll: 0\r\nEditor: 1\r\n"
            "Tab: 2\r\nVideo: \r\nPosition: 0\r\nFFMS2: 1\r\nSubtitles: %1/three.ass\r\nActive: 0\r\nScroll: 0\r\nEditor: 1\r\n"
            "Audio: %1/three.mka\r\n"
            "Tab: 3\r\nVideo: \r\nPosition: 0\r\nFFMS2: 1\r\nSubtitles: %1/four.ass\r\nActive: 0\r\nScroll: 0\r\nEditor: 1\r\n"
            "Audio: %1/gone.mka\r\n").arg(d).toUtf8();
        writeSession(home.filePath(QStringLiteral("LastSession.txt")), text); // CF3: converted at the start
        app::Application a(options(home));
        QVERIFY(loadSession(a));
        QCOMPARE(titles(a), (QStringList{"one.ass", "two.ass", "three.ass", "four.ass"}));
        QCOMPARE(a.currentTab(), 3);
        QVERIFY(!a.audio().hasAudio()); // its audio file is missing
        QCOMPARE(a.unresolvedRestores().size(), qsizetype(1));
        a.selectTab(0);
        QVERIFY(a.audio().hasAudio());
        QCOMPARE(QDir::fromNativeSeparators(a.audio().path()), d + "/one.mka");
        QTRY_VERIFY_WITH_TIMEOUT(a.audio().ready(), 20000);
        a.selectTab(1); // legacy: one.mka again; here none
        QVERIFY(!a.audio().hasAudio());
        a.selectTab(2);
        QCOMPARE(QDir::fromNativeSeparators(a.audio().path()), d + "/three.mka");
        a.selectTab(1);
        QVERIFY(!a.audio().hasAudio());
        const auto written = readSession(a.lastSessionPath());
        QCOMPARE(written.tabs[1].audio, std::string());
        QCOMPARE(QDir::fromNativeSeparators(QString::fromStdString(written.tabs[0].audio)), d + "/one.mka");
        QCOMPARE(QDir::fromNativeSeparators(QString::fromStdString(written.tabs[2].audio)), d + "/three.mka");
        QCOMPARE(hash(files.filePath(QStringLiteral("one.mka"))), before);
    }

    // Each tab keeps its own video and the position reached (legacy each
    // TabPanel had its VideoBox); the session writes them, and a restored
    // tab shows its video at the session's position.
    void eachTabKeepsItsVideoAndPosition()
    {
        QTemporaryDir home;
        auto a = std::make_unique<app::Application>(options(home));
        QVERIFY(a->openFile(writeSubtitles(dir.filePath(QStringLiteral("v1.ass")), "v1")));
        a->video().openVideo(media("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(a->video().hasVideo(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(a->video().session().shownFrame().has_value(), 10000);
        QVERIFY(a->video().showFrameAt(12));
        QTRY_COMPARE(a->video().session().shownFrame().value_or(-1), 12);
        const int ms = a->video().session().legacyTimebase().msAt(12);
        a->addPage();
        QVERIFY(!a->video().hasVideo()); // the new tab has none
        auto written = readSession(a->lastSessionPath());
        a->endSession();
        written = readSession(a->lastSessionPath());
        QVERIFY(written.closed);
        QCOMPARE(written.tabs.size(), std::size_t(2));
        QCOMPARE(QString::fromStdString(written.tabs[0].video), media("cfr.mkv"));
        QCOMPARE(written.tabs[0].position, ms);
        QCOMPARE(written.tabs[1].video, std::string());
        a->changeTab(-1);
        QTRY_VERIFY_WITH_TIMEOUT(a->video().hasVideo(), 30000);
        QTRY_COMPARE_WITH_TIMEOUT(a->video().session().shownFrame().value_or(-1), 12, 10000);
        a.reset();

        // The next program run loads it: the tab active at the end (CF3's
        // marker), the first, shows its video at the position again.
        app::Application b(options(home));
        QCOMPARE(b.startupSession(), QString()); // closed cleanly, not asked for
        QVERIFY(loadSession(b));
        QCOMPARE(b.tabs().size(), qsizetype(2));
        QCOMPARE(b.currentTab(), 0);
        QVERIFY(b.unresolvedRestores().isEmpty());
        b.selectTab(0);
        QTRY_VERIFY_WITH_TIMEOUT(b.video().hasVideo(), 30000);
        QTRY_COMPARE_WITH_TIMEOUT(b.video().session().shownFrame().value_or(-1), 12, 10000);
    }

    // AV3 (#240): a dummy video brings its own silent audio, as long as the
    // video. The session keeps the video alone (the silence is the video's,
    // as a file's own track is), and restoring it brings both back; a tab
    // switch leaves the silence with its tab. An older session's blank
    // 2 h 30 min audio still restores.
    void aDummyVideoRestoresWithItsSilence()
    {
        QTemporaryDir home;
        const QString dummy = QStringLiteral("?dummy:25.000000:50:320:240:47:163:254:");
        const QString silence = QStringLiteral("dummy-audio:silence?sr=44100&bd=16&ch=1&ln=88200");
        auto a = std::make_unique<app::Application>(options(home));
        QVERIFY(a->openFile(writeSubtitles(dir.filePath(QStringLiteral("d1.ass")), "d1")));
        a->video().openVideo(dummy);
        QTRY_VERIFY_WITH_TIMEOUT(a->video().hasVideo(), 10000);
        QTRY_VERIFY(a->audio().ready());
        QCOMPARE(a->audio().path(), silence);
        QCOMPARE(a->audio().box().audio()->sampleCount(), 88200);
        a->endSession();
        const auto written = readSession(a->lastSessionPath());
        QCOMPARE(written.tabs.size(), std::size_t(1));
        QCOMPARE(QString::fromStdString(written.tabs[0].video), dummy);
        QCOMPARE(written.tabs[0].audio, std::string());
        a.reset();

        app::Application b(options(home));
        QVERIFY(loadSession(b));
        QVERIFY(b.unresolvedRestores().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(b.video().hasVideo(), 10000);
        QVERIFY(b.video().dummy());
        QTRY_VERIFY(b.audio().ready());
        QCOMPARE(b.audio().path(), silence);
        b.addPage();
        QVERIFY(!b.audio().hasAudio());
        b.changeTab(-1);
        QTRY_VERIFY_WITH_TIMEOUT(b.video().hasVideo(), 10000);
        QTRY_VERIFY(b.audio().ready());
        QCOMPARE(b.audio().path(), silence);

        // An older session's "Open blank 2h30m audio".
        QTemporaryDir older;
        const QString d = QDir::fromNativeSeparators(older.path());
        writeSubtitles(older.filePath(QStringLiteral("old.ass")), "old");
        writeSession(older.filePath(QStringLiteral("LastSession.txt")),
                     "\xEF\xBB\xBF" + QStringLiteral("[HikariSub v0.0.1]\r\n"
                                                    "Tab: 0\r\nVideo: \r\nPosition: 0\r\nFFMS2: 1\r\nSubtitles: %1/old.ass\r\n"
                                                    "Active: 0\r\nScroll: 0\r\nEditor: 1\r\n"
                                                    "Audio: dummy-audio:silence?sr=44100&bd=16&ch=1&ln=396900000\r\n")
                                             .arg(d)
                                             .toUtf8());
        app::Application c(options(older));
        QVERIFY(loadSession(c));
        QVERIFY(c.unresolvedRestores().isEmpty());
        QTRY_VERIFY(c.audio().ready());
        QCOMPARE(c.audio().box().audio()->sampleCount(), 396'900'000);

        // A dummy entry that does not open is unresolved, as a missing file is.
        QTemporaryDir broken;
        writeSubtitles(broken.filePath(QStringLiteral("broken.ass")), "broken");
        writeSession(broken.filePath(QStringLiteral("LastSession.txt")),
                     "\xEF\xBB\xBF" + QStringLiteral("[HikariSub v0.0.1]\r\n"
                                                    "Tab: 0\r\nVideo: ?garbage\r\nPosition: 0\r\nFFMS2: 1\r\n"
                                                    "Subtitles: %1/broken.ass\r\nActive: 0\r\nScroll: 0\r\nEditor: 1\r\n")
                                             .arg(QDir::fromNativeSeparators(broken.path()))
                                             .toUtf8());
        app::Application e(options(broken));
        QVERIFY(loadSession(e));
        const auto unresolved = e.unresolvedRestores();
        QCOMPARE(unresolved.size(), 1);
        QCOMPARE(unresolved[0].toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("video"));
        QCOMPARE(unresolved[0].toMap().value(QStringLiteral("path")).toString(), QStringLiteral("?garbage"));
    }

    // Loading replaces every open tab only after their unsaved work is
    // reviewed as for Quit (approved P6-session-review; legacy destroyed the
    // tabs without asking). Cancel keeps everything.
    void loadingReviewsUnsavedWorkFirst()
    {
        QTemporaryDir home;
        app::Application a(options(home));
        const QString kls = home.filePath(QStringLiteral("review.kls"));
        writeSession(kls,
                     QStringLiteral("[HikariSub v0.0.1]\r\nTab: 0\r\nSubtitles: %1\r\n")
                         .arg(writeSubtitles(dir.filePath(QStringLiteral("session.ass")), "s"))
                         .toUtf8());
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("work.ass")), "w")));
        edit(a, u8"unsaved");
        a.addPage();
        QVariantMap review = a.reviewSession(QUrl::fromLocalFile(kls));
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        QCOMPARE(review.value(QStringLiteral("rows")).toList().size(), qsizetype(1));
        a.cancelClose();
        QCOMPARE(titles(a), (QStringList{"work.ass", "Untitled"}));
        QVERIFY(loadSession(a, QUrl::fromLocalFile(kls))); // Discard
        QCOMPARE(titles(a), (QStringList{"session.ass"}));
    }

    // L58-staged-replacement: subtitles staged before the review are read
    // again when the review has let the load go on and the file changed
    // meanwhile; a file gone meanwhile becomes an unresolved entry.
    void staleStagedFilesAreReadAgain()
    {
        QTemporaryDir home;
        QTemporaryDir files;
        const QString s1 = writeSubtitles(files.filePath(QStringLiteral("s1.ass")), "old", 1);
        const QString s2 = writeSubtitles(files.filePath(QStringLiteral("s2.ass")), "two", 1);
        const QString kls = files.filePath(QStringLiteral("stale.kls"));
        writeSession(kls, QStringLiteral("[HikariSub v0.0.1]\r\nTab: 0\r\nSubtitles: %1\r\nTab: 1\r\nSubtitles: %2\r\n")
                              .arg(s1, s2)
                              .toUtf8());
        app::Application a(options(home));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("busy.ass")), "busy")));
        edit(a, u8"unsaved");
        const QVariantMap review = a.reviewSession(QUrl::fromLocalFile(kls));
        QCOMPARE(review.value(QStringLiteral("rows")).toList().size(), qsizetype(1));
        // While the review waits: s1 changes, s2 goes.
        writeSubtitles(s1, "new and longer", 2);
        QVERIFY(QFile::remove(s2));
        a.resolveClose(choose(review.value(QStringLiteral("rows")).toList(), false));
        QCOMPARE(titles(a), (QStringList{"s1.ass", "Untitled"}));
        a.selectTab(0);
        QCOMPARE(target(a)->document().lines().size(), std::size_t(2));
        const std::u8string &text = target(a)->document().lines()[0]->text;
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(text.data()), qsizetype(text.size())),
                 QStringLiteral("new and longer 0"));
        QCOMPARE(a.unresolvedRestores().size(), qsizetype(1));
        QCOMPARE(a.unresolvedRestores().first().toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("subtitles"));
    }

    // The session written right after loading (legacy SaveLastSession at the
    // end of LoadLastSession) keeps the restored position while the video
    // opens and its seek has not landed; afterwards it is the shown frame's.
    void theSavedPositionWaitsForTheRestoredSeek()
    {
        QTemporaryDir home;
        QTemporaryDir files;
        const QString subs = writeSubtitles(files.filePath(QStringLiteral("pos.ass")), "pos"); // starts at 1.00
        const QString kls = files.filePath(QStringLiteral("pos.kls"));
        writeSession(kls, QStringLiteral("[HikariSub v0.0.1]\r\nTab: 0\r\nVideo: %1\r\nPosition: 2002\r\nSubtitles: %2\r\n")
                              .arg(media("cfr.mkv"), subs)
                              .toUtf8());
        app::Application a(options(home));
        QVERIFY(loadSession(a, QUrl::fromLocalFile(kls)));
        QCOMPARE(readSession(a.lastSessionPath()).tabs.at(0).position, 2002); // not 0 while opening
        const auto &video = a.video().session();
        // While the video indexes (its path set, not Ready): still the session's.
        QDeadlineTimer deadline(30000);
        while (video.state() == application::VideoSession::State::Closed && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (video.state() == application::VideoSession::State::Opening) {
            QVERIFY(a.saveLastSession());
            QCOMPARE(readSession(a.lastSessionPath()).tabs.at(0).position, 2002);
        } else {
            qWarning("the video was ready before the opening state could be observed");
        }
        QTRY_VERIFY_WITH_TIMEOUT(a.video().hasVideo(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(video.shownFrame() && std::abs(video.legacyTimebase().msAt(*video.shownFrame()) - 2002) < 60,
                                 10000);
        QVERIFY(a.saveLastSession());
        QCOMPARE(readSession(a.lastSessionPath()).tabs.at(0).position, video.legacyTimebase().msAt(*video.shownFrame()));
    }

    // The Grid's scroll reports are ignored from a tab change until the Grid
    // has restored that tab's scroll, so a model reset's transient 0 never
    // replaces it.
    void aTabsScrollSurvivesTheGridsReset()
    {
        QTemporaryDir home;
        app::Application a(options(home));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("scroll1.ass")), "a", 9)));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("scroll2.ass")), "b", 9)));
        a.selectTab(1);
        a.scrollRestored();
        a.setTargetScroll(2);
        QSignalSpy shown(&a, &app::Application::tabShown);
        a.selectTab(0);
        a.scrollRestored();
        a.setTargetScroll(5);
        a.selectTab(1);
        QCOMPARE(shown.last().first().toInt(), 2);
        a.setTargetScroll(0); // the reset model's contentY, before the restore
        a.scrollRestored();
        a.selectTab(0);
        QCOMPARE(shown.last().first().toInt(), 5);
        a.setTargetScroll(0);
        a.scrollRestored();
        QVERIFY(a.saveLastSession());
        const auto written = readSession(a.lastSessionPath());
        QCOMPARE(written.tabs.at(0).scroll, 5);
        QCOMPARE(written.tabs.at(1).scroll, 2);
    }

    // A session file whose first line is not "[HikariSub..." is corrupt
    // (logged, nothing changes); no file or an empty one does nothing.
    void corruptAndMissingSessionsChangeNothing()
    {
        QTemporaryDir home;
        app::Application a(options(home));
        QVERIFY(!a.reviewSession({}).value(QStringLiteral("ok")).toBool()); // no last-session.json yet
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("keep.ass")), "k")));
        const QVariantMap corrupt =
            a.reviewSession(QUrl::fromLocalFile(QStringLiteral(HIKARI_SESSION_FIXTURES "/corrupt.txt")));
        QVERIFY(!corrupt.value(QStringLiteral("ok")).toBool());
        QCOMPARE(corrupt.value(QStringLiteral("problem")).toString(), QStringLiteral("corrupt.txt: Session file is corrupt."));
        QCOMPARE(titles(a), (QStringList{"keep.ass"}));
    }

    // GLOBAL_SAVE_EXTERNAL_SESSION and GLOBAL_LOAD_EXTERNAL_SESSION (CF3):
    // a .hksession, JSON; loading it writes last-session.json, not the file.
    // A name ending in .kls is saved as a .hksession: a legacy file is never
    // written.
    void externalSessionsRoundTrip()
    {
        QTemporaryDir home;
        app::Application a(options(home));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("k1.ass")), "k1")));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("k2.ass")), "k2")));
        a.selectLine(target(a)->document().lines()[2]->id.value);
        const int current = a.currentTab();
        const QString file = home.filePath(QStringLiteral("saved.hksession"));
        QCOMPARE(a.saveSessionTo(QUrl::fromLocalFile(file)), QString());
        QVERIFY(readAll(file).startsWith("{"));
        const QJsonObject json = QJsonDocument::fromJson(readAll(file)).object();
        QCOMPARE(json.value(QStringLiteral("version")).toInt(), 1);
        QVERIFY(!json.contains(QStringLiteral("closed"))); // only last-session.json says it
        const auto saved = readSession(file);
        QCOMPARE(saved.tabs.size(), std::size_t(2));
        QCOMPARE(saved.activeTab, std::optional<std::size_t>(current));
        QCOMPARE(saved.tabs[current].active, 2);
        QCOMPARE(QString::fromStdString(saved.tabs[1].subtitles),
                 QDir::toNativeSeparators(dir.filePath(QStringLiteral("k2.ass"))));
        const QByteArray before = hash(file);
        a.addPage();
        QVERIFY(loadSession(a, QUrl::fromLocalFile(file)));
        QCOMPARE(hash(file), before);
        QCOMPARE(QFile::exists(a.lastSessionPath()), true);
        QCOMPARE(titles(a), (QStringList{"k1.ass", "k2.ass"}));
        QVERIFY(a.unresolvedRestores().isEmpty());
        QCOMPARE(a.currentTab(), current);
        QCOMPARE(activeRow(a), 2);

        const QString kls = home.filePath(QStringLiteral("typed.kls"));
        QCOMPARE(a.saveSessionTo(QUrl::fromLocalFile(kls)), QString());
        QVERIFY(!QFile::exists(kls));
        QCOMPARE(readSession(home.filePath(QStringLiteral("typed.hksession"))).tabs.size(), std::size_t(2));
        // The dialog asked about typed.kls: typed.hksession is replaced only
        // when the user says so.
        a.addPage();
        const QByteArray typed = hash(home.filePath(QStringLiteral("typed.hksession")));
        QCOMPARE(a.saveSessionTo(QUrl::fromLocalFile(kls)), QStringLiteral("exists"));
        QCOMPARE(hash(home.filePath(QStringLiteral("typed.hksession"))), typed);
        QCOMPARE(a.saveSessionTo(QUrl::fromLocalFile(kls), true), QString());
        QCOMPARE(readSession(home.filePath(QStringLiteral("typed.hksession"))).tabs.size(), std::size_t(3));
    }

    // CF3: a session file naming files on other computers is not looked into
    // (Windows would sign in to the share) until the user allows it; this
    // computer's own \\?\ paths do not count.
    void filesOnOtherComputersWaitForTheUser()
    {
        QTemporaryDir home;
        app::Application a(options(home));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("here.ass")), "here")));
        application::SessionTab share, media, local, device;
        share.subtitles = "\\\\nas\\subs\\x.ass";
        media.video = "//media-box/v/x.mkv";
        media.subtitles = "\\\\NAS\\subs\\y.ass";
        local.subtitles = "\\\\?\\C:\\subs\\z.ass";
        local.video = "\\\\.\\d:\\v.mkv";
        // Device paths that reach the network redirector count too.
        device.subtitles = "\\\\?\\UNC\\far\\s\\a.ass";
        device.audio = "\\\\.\\GLOBALROOT\\Device\\Mup\\mup\\s\\b.wav";
        device.keyframes = "\\\\\\odd\\k.txt";
        const QString file = home.filePath(QStringLiteral("shared.hksession"));
        writeSession(file, app::writeSessionJson(application::Session{"HikariSub", true, {share, media, local, device}, {}}));
        QVariantMap review = a.reviewSession(QUrl::fromLocalFile(file));
        QVERIFY(!review.value(QStringLiteral("ok")).toBool());
        QCOMPARE(review.value(QStringLiteral("network")).toStringList(),
                 (QStringList{"nas", "media-box", "far", QString::fromStdString(device.audio), QString::fromStdString(device.keyframes)}));
        QCOMPARE(titles(a), (QStringList{"here.ass"}));
        review = a.reviewSession(QUrl::fromLocalFile(file), true);
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        QVERIFY(review.value(QStringLiteral("rows")).toList().isEmpty());
        a.finishClose();
        QCOMPARE(a.tabs().size(), qsizetype(4));
        // The program's own last session is not asked about.
        QVERIFY(a.reviewSession({}).value(QStringLiteral("ok")).toBool());
    }

    // CF3: a legacy .kls still loads, as legacy wrote it (CRLF, a BOM): the
    // last tab is the active one, and the file is never written.
    void aLegacyKlsStillLoads()
    {
        QTemporaryDir home;
        app::Application a(options(home));
        application::SessionTab first, second;
        first.subtitles = writeSubtitles(dir.filePath(QStringLiteral("l1.ass")), "l1").toStdString();
        first.active = 1;
        second.subtitles = writeSubtitles(dir.filePath(QStringLiteral("l2.ass")), "l2").toStdString();
        const QString kls = home.filePath(QStringLiteral("legacy.kls"));
        writeSession(kls, QByteArray::fromStdString(testing::writeLegacySession("HikariSub v0.0.2", false, {first, second})));
        const QByteArray before = hash(kls);
        QVERIFY(loadSession(a, QUrl::fromLocalFile(kls)));
        QCOMPARE(titles(a), (QStringList{"l1.ass", "l2.ass"}));
        QCOMPARE(a.currentTab(), 1);
        a.selectTab(0);
        QCOMPARE(activeRow(a), 1);
        QCOMPARE(hash(kls), before);
        QVERIFY(readAll(a.lastSessionPath()).startsWith("{"));
        // Dropped alone it is a session for the shell's Load session; among
        // other files it is refused.
        const QVariantMap dropped = a.openDropped({QUrl::fromLocalFile(kls)});
        QCOMPARE(dropped.value(QStringLiteral("kind")).toString(), QStringLiteral("session"));
        QCOMPARE(dropped.value(QStringLiteral("path")).toString(), kls);
        a.openDropped({QUrl::fromLocalFile(kls), QUrl::fromLocalFile(dir.filePath(QStringLiteral("l1.ass")))});
        QCOMPARE(titles(a), (QStringList{"l1.ass", "l2.ass", "l1.ass"}));
    }

    // CF3: the first start without last-session.json converts LastSession.txt
    // into it once and keeps it as LastSession.txt.old; whether the program
    // closed comes along, so a crash is still offered. A corrupt one is kept
    // as .old too, with a notice, and restores nothing.
    void lastSessionTxtConvertsOnce()
    {
        QTemporaryDir home;
        application::SessionTab tab;
        tab.subtitles = writeSubtitles(dir.filePath(QStringLiteral("c1.ass")), "c1").toStdString();
        tab.scroll = 2;
        const QByteArray legacy = QByteArray::fromStdString(testing::writeLegacySession("HikariSub v0.0.2", false, {tab}));
        writeSession(home.filePath(QStringLiteral("LastSession.txt")), legacy);
        {
            app::Application a(options(home));
            QVERIFY(!QFile::exists(home.filePath(QStringLiteral("LastSession.txt"))));
            QCOMPARE(readAll(home.filePath(QStringLiteral("LastSession.txt.old"))), legacy);
            QCOMPARE(a.lastSessionPath(), home.filePath(QStringLiteral("last-session.json")));
            const auto converted = readSession(a.lastSessionPath());
            QVERIFY(!converted.closed);
            QCOMPARE(converted.tabs, std::vector<application::SessionTab>{tab});
            QCOMPARE(converted.activeTab, std::optional<std::size_t>(0));
            QCOMPARE(a.startupSession(), QStringLiteral("crash"));
            QCOMPARE(a.startupOfferNames(), QStringList{QStringLiteral("c1.ass")});
        }
        // Once: a LastSession.txt that turns up again is left alone.
        writeSession(home.filePath(QStringLiteral("LastSession.txt")), legacy);
        {
            app::Application b(options(home));
            QVERIFY(QFile::exists(home.filePath(QStringLiteral("LastSession.txt"))));
            QVERIFY(readSession(b.lastSessionPath()).closed); // the last run's orderly end
        }

        QTemporaryDir corrupt;
        writeSession(corrupt.filePath(QStringLiteral("LastSession.txt")), readAll(QStringLiteral(HIKARI_SESSION_FIXTURES "/corrupt.txt")));
        app::Application c(options(corrupt));
        QVERIFY(QFile::exists(corrupt.filePath(QStringLiteral("LastSession.txt.old"))));
        QVERIFY(!QFile::exists(c.lastSessionPath()));
        QVERIFY(c.log().history().contains(QStringLiteral("LastSession.txt: Session file is corrupt")));
        QCOMPARE(c.startupSession(), QString());
    }

    // CF3: a last-session.json that cannot be read is never overwritten: it
    // is kept as last-session.json.broken, the Log names the file and line,
    // and nothing is restored or offered. The next session written is a new
    // file.
    void aBrokenLastSessionIsKept()
    {
        QTemporaryDir home;
        const QByteArray broken = "{\n  \"version\": 1,\n  \"tabs\": [ oops ]\n}\n";
        writeSession(home.filePath(QStringLiteral("last-session.json")), broken);
        app::Application a(options(home));
        QVERIFY(!QFile::exists(a.lastSessionPath()));
        QCOMPARE(readAll(a.lastSessionPath() + QStringLiteral(".broken")), broken);
        QVERIFY(a.log().history().contains(QStringLiteral("last-session.json, line 3: ")));
        QVERIFY(a.log().history().contains(QStringLiteral("It was kept as last-session.json.broken; no tabs were restored.")));
        QCOMPARE(a.startupSession(), QString());
        QCOMPARE(a.startupOffer(), QString());
        QVERIFY(a.startupOfferNames().isEmpty());
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("after.ass")), "after")));
        QCOMPARE(readSession(a.lastSessionPath()).tabs.size(), std::size_t(1));
        QCOMPARE(readAll(a.lastSessionPath() + QStringLiteral(".broken")), broken);

        // A newer version's file, or a field of the wrong type, is broken too;
        // an older .broken stays.
        writeSession(a.lastSessionPath(), R"({"version": 2, "tabs": []})");
        QVERIFY(!a.reviewSession({}).value(QStringLiteral("ok")).toBool());
        // The numbering is the shared helper's (owned_file.h): .broken-N.
        QCOMPARE(QDir(home.path()).entryList({QStringLiteral("last-session.json.broken-*")}).size(), qsizetype(1));
        QVERIFY(a.log().history().contains(
            QStringLiteral("last-session.json: It is version 2, newer than this HikariSub reads (1). It was kept as "
                           "last-session.json.broken-2; no tabs were restored.")));
        writeSession(a.lastSessionPath(), R"({"version": 1, "tabs": [{"position": "soon"}]})");
        QVERIFY(!a.reviewSession({}).value(QStringLiteral("ok")).toBool());
        QVERIFY(a.log().history().contains(QStringLiteral("Tab 1: \"position\" has the wrong type.")));
        // A crash must not pass for an orderly end.
        writeSession(a.lastSessionPath(), R"({"version": 1, "closed": "false", "tabs": []})");
        QVERIFY(!a.reviewSession({}).value(QStringLiteral("ok")).toBool());
        QVERIFY(a.log().history().contains(QStringLiteral("\"closed\" is not true or false")));
    }

    // CF3, CF1's owned-file rules: a folder where last-session.json should be,
    // or a file that cannot be read, stays as it is and is not written over;
    // a LastSession.txt that cannot be read is not converted.
    void anUnreadableLastSessionStaysAsItIs()
    {
        QTemporaryDir folder;
        QVERIFY(QDir(folder.path()).mkdir(QStringLiteral("last-session.json")));
        {
            app::Application a(options(folder));
            QVERIFY(a.log().history().contains(QStringLiteral(
                "last-session.json: It could not be read. No tabs were restored, and HikariSub will not write over it.")));
            QCOMPARE(a.startupOffer(), QString());
            QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("folder.ass")), "folder")));
            QVERIFY(!a.saveLastSession());
        }
        QVERIFY(QFileInfo(folder.filePath(QStringLiteral("last-session.json"))).isDir());

        QTemporaryDir home;
        const QString file = home.filePath(QStringLiteral("last-session.json"));
        const QString legacy = home.filePath(QStringLiteral("LastSession.txt"));
        writeSession(file, R"({"version": 1, "tabs": []})");
        writeSession(legacy, legacySession());
        // Windows keeps read access whatever setPermissions asks.
        for (const QString &path : {file, legacy})
            if (QFile probe(path); !QFile::setPermissions(path, {}) || probe.open(QIODevice::ReadOnly))
                QSKIP("the file reads without permissions (Windows, root, or a file system without them)");
        {
            app::Application a(options(home));
            QVERIFY(a.log().history().contains(QStringLiteral("last-session.json: It could not be read.")));
            QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("unreadable.ass")), "unreadable")));
            QVERIFY(!a.saveLastSession());
        }
        QVERIFY(QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QCOMPARE(readAll(file), QByteArray(R"({"version": 1, "tabs": []})"));

        // With last-session.json gone, an unreadable LastSession.txt is not
        // converted; the session written as the program ends takes its place.
        QVERIFY(QFile::remove(file));
        {
            app::Application a(options(home));
            QVERIFY(!QFile::exists(file));
            QVERIFY(!QFile::exists(legacy + QStringLiteral(".old")));
        }
        QVERIFY(QFile::exists(file));
        QVERIFY(QFile::setPermissions(legacy, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QCOMPARE(readAll(legacy), legacySession());
    }

    // CF3: last-session.json keeps its permissions when it is written again,
    // and the converted one takes LastSession.txt's.
    void lastSessionKeepsItsPermissions()
    {
#ifdef Q_OS_WIN
        QSKIP("no POSIX permissions");
#endif
        const auto owner = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
        const auto ownerOnly = [&](const QString &path) {
            return (QFile::permissions(path) & ~(QFileDevice::ReadUser | QFileDevice::WriteUser)) == owner;
        };
        QTemporaryDir home;
        const QString legacy = home.filePath(QStringLiteral("LastSession.txt"));
        writeSession(legacy, legacySession());
        QVERIFY(QFile::setPermissions(legacy, owner));
        app::Application a(options(home));
        QVERIFY(QFile::exists(a.lastSessionPath()));
        QVERIFY(ownerOnly(a.lastSessionPath()));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("private.ass")), "private")));
        QVERIFY(a.saveLastSession());
        QCOMPARE(readSession(a.lastSessionPath()).tabs.size(), std::size_t(1));
        QVERIFY(ownerOnly(a.lastSessionPath()));
    }

    // CF3: two HikariSubs on one folder: while one holds the conversion's
    // lock the other leaves LastSession.txt to it, and both
    // writing last-session.json leave one whole session.
    void twoProgramsShareTheLastSession()
    {
        QTemporaryDir home;
        const QString legacy = home.filePath(QStringLiteral("LastSession.txt"));
        writeSession(legacy, legacySession());
        QLockFile other(home.filePath(QStringLiteral(".last-session.lock")));
        QVERIFY(other.tryLock(0));
        app::Application a(options(home));
        QVERIFY(QFile::exists(legacy));
        QVERIFY(!QFile::exists(a.lastSessionPath()));
        other.unlock();
        app::Application b(options(home));
        QVERIFY(QFile::exists(legacy + QStringLiteral(".old")));
        QCOMPARE(readSession(b.lastSessionPath()).tabs.size(), std::size_t(1));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("first.ass")), "first")));
        QVERIFY(b.openFile(writeSubtitles(dir.filePath(QStringLiteral("second.ass")), "second")));
        QVERIFY(a.saveLastSession());
        QVERIFY(b.saveLastSession());
        const auto session = readSession(a.lastSessionPath());
        QCOMPARE(session.tabs.size(), std::size_t(1));
        QVERIFY(QString::fromStdString(session.tabs.front().subtitles).endsWith(QStringLiteral("second.ass")));
        QVERIFY(QDir(home.path()).entryList({QStringLiteral("last-session.json?*")}).isEmpty());
    }

    // CF3: a broken .hksession the user opens is named in the Log with its
    // line, and stays as it is.
    void aBrokenSessionFileIsLeftAlone()
    {
        QTemporaryDir home;
        app::Application a(options(home));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("stay.ass")), "stay")));
        const QString file = home.filePath(QStringLiteral("broken.hksession"));
        writeSession(file, "{\"version\": 1,\n\"tabs\": [}");
        const QByteArray before = hash(file);
        const QVariantMap review = a.reviewSession(QUrl::fromLocalFile(file));
        QVERIFY(!review.value(QStringLiteral("ok")).toBool());
        QVERIFY(review.value(QStringLiteral("problem")).toString().startsWith(QStringLiteral("broken.hksession, line 2: ")));
        QCOMPARE(hash(file), before);
        QVERIFY(!QFile::exists(file + QStringLiteral(".broken")));
        QCOMPARE(titles(a), (QStringList{"stay.ass"}));
    }

    // WS1 without the shell: each tab's arrangement is a value the Session
    // carries. A TLMode file's tab is the Translation preset, a tab with no
    // arrangement yet writes no "layout", and a loaded .kls tab's "Layout: "
    // line is written back as it came (a valid one) or as the Editing preset.
    void eachTabsArrangementGoesWithTheSession()
    {
        QTemporaryDir home;
        app::Application a(options(home));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("plain.ass")), "plain")));
        const QString tl = dir.filePath(QStringLiteral("tl.ass"));
        {
            QFile f(tl);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Script Info]\nTLMode: Yes\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, "
                    "MarginV, Effect, Text\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,line\n");
        }
        a.addPage();
        QVERIFY(a.openFile(tl));
        auto saved = readSession(a.lastSessionPath());
        QCOMPARE(saved.tabs.size(), std::size_t(2));
        QCOMPARE(saved.tabs[0].layout, std::string()); // never shown by a shell
        const auto translation = ui::WorkspaceArrangement::parse(QByteArray::fromStdString(saved.tabs[1].layout));
        QVERIFY(translation);
        QCOMPARE(translation->preset, QStringLiteral("Translation"));
        QVERIFY(translation->payload.isEmpty());

        const QByteArray typesetting = ui::WorkspaceArrangement{QStringLiteral("Typesetting"), {}}.serialise();
        saved.tabs[0].layout = typesetting.toStdString();
        saved.tabs[1].layout = "{not a layout";
        const QString kls = home.filePath(QStringLiteral("layouts.kls"));
        writeSession(kls, QByteArray::fromStdString(testing::writeLegacySession("HikariSub v1", false, saved.tabs)));
        QVERIFY(loadSession(a, QUrl::fromLocalFile(kls)));
        QVERIFY(a.workspaceLayout().notice().contains(QStringLiteral("tl.ass")));
        const auto again = readSession(a.lastSessionPath());
        QCOMPARE(again.tabs.size(), std::size_t(2));
        QCOMPARE(QByteArray::fromStdString(again.tabs[0].layout), typesetting);
        const auto fallback = ui::WorkspaceArrangement::parse(QByteArray::fromStdString(again.tabs[1].layout));
        QVERIFY(fallback);
        QCOMPARE(fallback->preset, QStringLiteral("Editing"));
    }

    // hikarisubApp::OnInit: LAST_SESSION_CONFIG 2 loads, 1 asks, otherwise a
    // session not written as the program closed asks about the crash; never
    // when paths were given.
    void startupChoicesFollowLegacy()
    {
        QTemporaryDir home;
        {
            app::Application a(options(home));
            QCOMPARE(a.startupSession(), QString()); // no session yet
            QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("crash.ass")), "c")));
            QVERIFY(a.lastSessionCrashed()); // written while running, without the marker
            QCOMPARE(a.startupSession(), QStringLiteral("crash"));
            a.setSessionRestore(1);
            QCOMPARE(a.startupSession(), QStringLiteral("ask"));
            a.setSessionRestore(2);
            QCOMPARE(a.startupSession(), QStringLiteral("load"));
            a.setStartedWithPaths(true);
            QCOMPARE(a.startupSession(), QString());
            a.setSessionRestore(0);
        } // an orderly end writes "closed"
        app::Application b(options(home));
        QVERIFY(!b.lastSessionCrashed());
        QCOMPARE(b.startupSession(), QString());
        QCOMPARE(b.sessionRestore(), 0);
    }

    // SH2: the Home page offers the last session when it has a Document with
    // a file, named by its files (a video's for a tab without subtitles).
    void theLastSessionIsSummarisedForTheHomePage()
    {
        QTemporaryDir home;
        {
            app::Application a(options(home));
            QVERIFY(a.lastSessionSummary().value(QStringLiteral("names")).toStringList().isEmpty()); // none yet
            QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("s1.ass")), "s1")));
            a.addPage();
            a.video().openVideo(media("cfr.mkv"));
            QTRY_VERIFY_WITH_TIMEOUT(a.video().hasVideo(), 30000);
            a.addPage(); // an Untitled tab without media: not named
        }
        app::Application b(options(home));
        QCOMPARE(b.lastSessionSummary().value(QStringLiteral("names")).toStringList(),
                 (QStringList{QStringLiteral("s1.ass"), QStringLiteral("cfr.mkv")}));
        // Close all writes no session (legacy OnTabSel), so Home still
        // offers the closed tabs back.
        QVERIFY(loadSession(b));
        QVERIFY(b.reviewCloseAll().isEmpty());
        b.finishClose();
        QVERIFY(b.tabs().isEmpty());
        QCOMPARE(b.lastSessionSummary().value(QStringLiteral("names")).toStringList(),
                 (QStringList{QStringLiteral("s1.ass"), QStringLiteral("cfr.mkv")}));
        // A tab with only a dummy video is not offered: restoring reopens no
        // file for it.
        application::SessionTab dummy;
        dummy.video = "?dummy:23.976000:40000:1280:720:47:163:254:";
        QFile file(b.lastSessionPath());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(app::writeSessionJson(application::Session{"HikariSub", true, {dummy}, {}}, true));
        file.close();
        QVERIFY(b.lastSessionSummary().value(QStringLiteral("names")).toStringList().isEmpty());
    }

    // SH2-recent-opened-and-missing: the recent subtitles remember when each
    // was last opened, across runs; a missing file stays in the list for the
    // Home page (File > Recent leaves it out) until it is removed there.
    void recentFilesKeepTheirTimesAndMissingEntries()
    {
        QTemporaryDir home;
        const QString kept = writeSubtitles(dir.filePath(QStringLiteral("recent-kept.ass")), "k");
        const QString gone = writeSubtitles(dir.filePath(QStringLiteral("recent-gone.ass")), "g");
        const QDateTime before = QDateTime::currentDateTime().addSecs(-1);
        {
            app::Application a(options(home));
            QVERIFY(a.openFile(gone));
            QVERIFY(a.openFile(kept));
        }
        QVERIFY(QFile::remove(gone));
        app::Application b(options(home));
        const QVariantList rows = b.homeRecent();
        QCOMPARE(rows.size(), qsizetype(2));
        const QVariantMap first = rows[0].toMap(), second = rows[1].toMap();
        QCOMPARE(first.value(QStringLiteral("path")).toString(), QDir::toNativeSeparators(kept));
        QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("recent-kept.ass"));
        QCOMPARE(first.value(QStringLiteral("folder")).toString(), QDir::toNativeSeparators(dir.path()));
        QVERIFY(!first.value(QStringLiteral("missing")).toBool());
        QVERIFY(first.value(QStringLiteral("opened")).toDateTime() >= before);
        QVERIFY(first.value(QStringLiteral("opened")).toDateTime() <= QDateTime::currentDateTime());
        QVERIFY(second.value(QStringLiteral("missing")).toBool());
        QVERIFY(second.value(QStringLiteral("opened")).toDateTime().isValid());
        // The menu leaves the missing file out without dropping it.
        const QVariantList menu = b.recentSubtitles();
        QCOMPARE(menu.size(), qsizetype(1));
        QCOMPARE(menu[0].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("1 recent-kept.ass"));
        QCOMPARE(b.homeRecent().size(), qsizetype(2));
        // Remove missing files drops it; Remove drops any one entry.
        QSignalSpy changed(&b, &app::Application::recentChanged);
        b.removeMissingRecent();
        QCOMPARE(changed.size(), 1);
        QCOMPARE(b.homeRecent().size(), qsizetype(1));
        b.removeRecent(QDir::toNativeSeparators(kept));
        QVERIFY(b.homeRecent().isEmpty());
        QCOMPARE(changed.size(), 2);
    }

    // SH2: saving keeps the time the file was last opened (legacy SetRecent
    // after a save moves it to the top); a file first saved, as by Save As,
    // is stamped then.
    void savingKeepsTheRecentOpenedTime()
    {
        QTemporaryDir home;
        const QString file = writeSubtitles(dir.filePath(QStringLiteral("recent-saved.ass")), "s");
        const QString copy = dir.filePath(QStringLiteral("recent-saved-copy.ass"));
        app::Application a(options(home));
        QVERIFY(a.openFile(file));
        const QDateTime opened = a.homeRecent()[0].toMap().value(QStringLiteral("opened")).toDateTime();
        QVERIFY(opened.isValid());
        QTest::qWait(50);
        QSignalSpy changed(&a, &app::Application::recentChanged);
        QVERIFY(a.saveAs(file));
        a.waitForWrites();
        QTRY_COMPARE(changed.size(), 1);
        QCOMPARE(a.homeRecent()[0].toMap().value(QStringLiteral("opened")).toDateTime(), opened);
        QVERIFY(a.saveAs(copy));
        a.waitForWrites();
        QTRY_COMPARE(changed.size(), 2);
        const QVariantMap first = a.homeRecent()[0].toMap();
        QCOMPARE(first.value(QStringLiteral("path")).toString(), QDir::toNativeSeparators(copy));
        QVERIFY(first.value(QStringLiteral("opened")).toDateTime() > opened);
        QCOMPARE(a.homeRecent()[1].toMap().value(QStringLiteral("opened")).toDateTime(), opened);
    }

    // SH2: media opened while no Document is open (the Home page's Open
    // video…, File > Open video, Open audio) opens in a new Untitled
    // Document, as legacy's always-present tab held it.
    void mediaOpenedWithoutADocumentGetsOne()
    {
        QTemporaryDir home;
        app::Application a(options(home));
        QVERIFY(a.tabs().isEmpty());
        a.video().openVideo(media("cfr.mkv"));
        QCOMPARE(a.tabs().size(), qsizetype(1));
        QVERIFY(a.targetUntitled());
        QTRY_VERIFY_WITH_TIMEOUT(a.video().hasVideo(), 30000); // the new tab keeps it
        a.video().openVideo(media("cfr.mkv"));
        QCOMPARE(a.tabs().size(), qsizetype(1)); // with a Document open, none is added

        app::Application b(options(home));
        QVERIFY(b.tabs().isEmpty());
        b.audio().openDummy();
        QCOMPARE(b.tabs().size(), qsizetype(1));
        QVERIFY(b.targetUntitled());

        // A protected reference opened as no tab is a Document: no Home, and
        // media opens as before SH2, without an editing target.
        app::Application c(options(home));
        QVERIFY(c.home());
        QVERIFY(c.openReference(writeSubtitles(dir.filePath(QStringLiteral("media-reference.ass")), "r")));
        QVERIFY(c.tabs().isEmpty());
        QVERIFY(!c.home());
        c.video().openVideo(media("cfr.mkv"));
        QVERIFY(c.tabs().isEmpty());
        QVERIFY(!c.workspace().editingTarget());
    }
};

QTEST_MAIN(SessionTabsTests)
#include "session_tabs_tests.moc"
