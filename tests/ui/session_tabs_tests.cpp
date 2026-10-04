// P6: tabs over the shared Workspace (GLOBAL_ADD_PAGE, GLOBAL_CLOSE_PAGE,
// GLOBAL_NEXT_TAB / GLOBAL_PREVIOUS_TAB) and session restore
// (GLOBAL_LOAD_LAST_SESSION, the .kls files) through the composition, against
// legacy Notebook, HikariSubFrame and hikarisubApp at 20d647c4: partial
// restore from fixtures with missing files, C05-audio-association, each tab's
// own media, and the startup choices.

#include "hikari/app/application.h"
#include "hikari/application/session_file.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
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

// A session file in the legacy format (CRLF, read the same on every
// platform under P6-session-crlf).
void writeSession(const QString &path, const QByteArray &text)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text);
}

application::Session readSession(const QString &path)
{
    const QByteArray bytes = readAll(path);
    auto text = application::decodeSessionBytes(std::string_view(bytes.constData(), bytes.size()));
    auto session = application::parseSession(text.value_or(std::string()));
    return session.value_or(application::Session{});
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
        o.settingsFile = home.filePath(QStringLiteral("hikari.ini"));
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
        QVERIFY(a.tabs().isEmpty()); // P1's zero-document state, not legacy's new empty tab
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
        app::Application a(options(home));
        writeSession(a.lastSessionPath(), text);
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

        // The next program run loads it: the last tab is active, the first
        // shows its video at the position again.
        app::Application b(options(home));
        QCOMPARE(b.startupSession(), QString()); // closed cleanly, not asked for
        // The session this program wrote (CRLF) restores on every platform,
        // Linux included (P6-session-crlf; legacy Linux kept each '\r').
        QVERIFY(loadSession(b));
        QCOMPARE(b.tabs().size(), qsizetype(2));
        QCOMPARE(b.currentTab(), 1);
        QVERIFY(b.unresolvedRestores().isEmpty());
        b.selectTab(0);
        QTRY_VERIFY_WITH_TIMEOUT(b.video().hasVideo(), 30000);
        QTRY_COMPARE_WITH_TIMEOUT(b.video().session().shownFrame().value_or(-1), 12, 10000);
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

    // A session file whose first line is not "[HikariSub..." is corrupt
    // (logged, nothing changes); no file or an empty one does nothing.
    void corruptAndMissingSessionsChangeNothing()
    {
        QTemporaryDir home;
        app::Application a(options(home));
        QVERIFY(!a.reviewSession({}).value(QStringLiteral("ok")).toBool()); // no LastSession.txt yet
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("keep.ass")), "k")));
        const QVariantMap corrupt =
            a.reviewSession(QUrl::fromLocalFile(QStringLiteral(HIKARI_SESSION_FIXTURES "/corrupt.txt")));
        QVERIFY(!corrupt.value(QStringLiteral("ok")).toBool());
        QCOMPARE(corrupt.value(QStringLiteral("problem")).toString(), QStringLiteral("Session file is corrupt"));
        QCOMPARE(titles(a), (QStringList{"keep.ass"}));
    }

    // GLOBAL_SAVE_EXTERNAL_SESSION and GLOBAL_LOAD_EXTERNAL_SESSION: the
    // same format; loading a .kls writes LastSession.txt, not the .kls.
    void externalSessionsRoundTrip()
    {
        QTemporaryDir home;
        app::Application a(options(home));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("k1.ass")), "k1")));
        QVERIFY(a.openFile(writeSubtitles(dir.filePath(QStringLiteral("k2.ass")), "k2")));
        a.selectLine(target(a)->document().lines()[2]->id.value);
        const QString kls = home.filePath(QStringLiteral("saved.kls"));
        QCOMPARE(a.saveSessionTo(QUrl::fromLocalFile(kls)), QString());
        const auto saved = readSession(kls);
        QVERIFY(!saved.closed);
        QCOMPARE(saved.tabs.size(), std::size_t(2));
        QCOMPARE(saved.tabs[0].active, 2);
        QCOMPARE(QString::fromStdString(saved.tabs[1].subtitles),
                 QDir::toNativeSeparators(dir.filePath(QStringLiteral("k2.ass"))));
        const QByteArray before = hash(kls);
        a.addPage();
        QVERIFY(loadSession(a, QUrl::fromLocalFile(kls)));
        QCOMPARE(hash(kls), before);
        QCOMPARE(QFile::exists(a.lastSessionPath()), true);
        // Written with CRLF, read back on every platform (P6-session-crlf).
        QCOMPARE(titles(a), (QStringList{"k1.ass", "k2.ass"}));
        QVERIFY(a.unresolvedRestores().isEmpty());
        a.selectTab(0);
        QCOMPARE(activeRow(a), 2);
    }

    // hikarisubApp::OnInit: LAST_SESSION_CONFIG 2 loads, 1 asks, otherwise a
    // session left without "[Close session]" asks about the crash; never
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
        } // an orderly end writes "[Close session]"
        app::Application b(options(home));
        QVERIFY(!b.lastSessionCrashed());
        QCOMPARE(b.startupSession(), QString());
        QCOMPARE(b.sessionRestore(), 0);
    }
};

QTEST_MAIN(SessionTabsTests)
#include "session_tabs_tests.moc"
