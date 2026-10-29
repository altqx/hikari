#include "hikari/app/application.h"
#include "hikari/app/localisation.h"
#include "docking.h"

#include <QAccessible>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTranslator>
#include <QXmlStreamReader>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>

#include <memory>
#include <optional>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray withoutLayout(const QByteArray &saved)
{
    const qsizetype at = saved.lastIndexOf("[Hikari]");
    return at < 0 ? saved : saved.left(at);
}

QSet<QString> catalogSources()
{
    QSet<QString> out;
    QXmlStreamReader xml(readAll(QStringLiteral(HIKARI_SOURCE_DIR "/i18n/hikarisub_pl.ts")));
    QString source;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        if (xml.name() == u"source") {
            source = xml.readElementText();
        } else if (xml.name() == u"translation") {
            const auto type = xml.attributes().value(QStringLiteral("type"));
            if (type != u"vanished" && type != u"obsolete")
                out.insert(source);
        }
    }
    return out;
}

std::optional<std::pair<QString, QString>> tsEntry(const QString &file, const QString &context, const QString &source)
{
    QXmlStreamReader xml(readAll(file));
    QString name, text;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        if (xml.name() == u"name")
            name = xml.readElementText();
        else if (xml.name() == u"source")
            text = xml.readElementText();
        else if (xml.name() == u"translation" && name == context && text == source) {
            const QString type = xml.attributes().value(QStringLiteral("type")).toString();
            return std::pair{type, xml.readElementText()};
        }
    }
    return std::nullopt;
}

bool hasLetter(const QString &s)
{
    return std::any_of(s.begin(), s.end(), [](QChar c) { return c.isLetter(); });
}

}

class LanguageTest : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    QString episode;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;

    void start(const QString &settingsFile = {}, const QStringList &systemUiLanguages = {})
    {
        app::Application::Options options;
        options.settingsFile = settingsFile;
        options.systemUiLanguages = systemUiLanguages;
        application = std::make_unique<app::Application>(options);
        engine = std::make_unique<QQmlApplicationEngine>();
        ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
    }
    void stop()
    {
        engine.reset();
        application.reset();
        window = nullptr;
    }
    QObject *root() const { return engine->rootObjects().first(); }
    static QQuickItem *findItem(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findItem(child, name))
                return found;
        return nullptr;
    }
    QQuickItem *item(const char *name) const
    {
        if (auto *found = window->findChild<QQuickItem *>(QLatin1String(name)))
            return found;
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *quick = qobject_cast<QQuickWindow *>(w))
                if (QQuickItem *found = findItem(quick->contentItem(), QLatin1String(name)))
                    return found;
        return nullptr;
    }
    QString fileMenuTitle() const { return item("fileMenuBarItem")->property("text").toString(); }
    QObject *settingsDialog() const
    {
        QVariant made;
        QMetaObject::invokeMethod(root(), "settingsWindow", Q_RETURN_ARG(QVariant, made));
        return made.value<QObject *>();
    }
    QQuickItem *dialogItem(const char *name) const
    {
        auto *content = settingsDialog()->property("contentItem").value<QQuickItem *>();
        return content ? findItem(content, QLatin1String(name)) : nullptr;
    }
    QQuickItem *settingsButton(const char *name) const
    {
        return dialogItem(name);
    }
    void openSettings()
    {
        auto *menuItem = root()->findChild<QObject *>(QStringLiteral("settingsMenuItem"));
        QVERIFY(menuItem);
        QVERIFY(QMetaObject::invokeMethod(menuItem->property("action").value<QObject *>(), "trigger"));
        QTRY_VERIFY(settingsDialog()->property("visible").toBool());
    }
    void choose(const char *setting, int index)
    {
        auto *combo = dialogItem(setting);
        QVERIFY(combo);
        combo->setProperty("currentIndex", index);
        QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, index)));
    }
    QString tabTitle(int i) const { return application->tabs().value(i).toMap().value(QStringLiteral("title")).toString(); }
    QList<std::pair<QPointer<QObject>, QString>> shownTexts() const
    {
        QList<std::pair<QPointer<QObject>, QString>> out;
        QSet<QObject *> seen;
        const auto add = [&](QObject *o) {
            if (seen.contains(o))
                return;
            seen.insert(o);
            for (const char *p : {"text", "title", "placeholderText"}) {
                const QVariant v = o->property(p);
                if (v.typeId() == QMetaType::QString && hasLetter(v.toString()))
                    out.append({o, v.toString()});
            }
            if (auto *i = qobject_cast<QQuickItem *>(o); i && i->isVisible())
                if (QAccessibleInterface *a = QAccessible::queryAccessibleInterface(i))
                    if (const QString name = a->text(QAccessible::Name); hasLetter(name))
                        out.append({o, name});
        };
        for (QWindow *w : QGuiApplication::allWindows()) {
            auto *quick = qobject_cast<QQuickWindow *>(w);
            if (!quick)
                continue;
            add(quick);
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *i) {
                add(i);
                for (QQuickItem *c : i->childItems())
                    walk(c);
            };
            walk(quick->contentItem());
            for (QObject *o : quick->findChildren<QObject *>())
                add(o);
        }
        return out;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        episode = dir.filePath(QStringLiteral("episode.ass"));
        QFile f(episode);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("\xEF\xBB\xBF[Script Info]\r\nScriptType: v4.00+\r\nPlayResX: 1280\r\nPlayResY: 720\r\n\r\n"
                "[V4+ Styles]\r\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, "
                "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\r\n"
                "Style: Default,Arial,20.5,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100.25,100,0.5,1.5,1,"
                "2.25,0,2,10,10,10,1\r\n\r\n"
                "[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
                "Dialogue: 0,0:00:01.25,0:00:02.50,Default,,0,0,0,,first {\\fscx120.5}line\r\n"
                "Dialogue: 0,1:02:03.04,1:02:04.99,Default,,0,0,0,,\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 hello\r\n");
    }
    void cleanup()
    {
        stop();
        QCOMPARE(QGuiApplication::layoutDirection(), Qt::LeftToRight);
    }

    void catalogsAreTheEmbeddedQm()
    {
        app::Localisation localisation;
        QCOMPARE(localisation.catalogLanguages(),
                 (QStringList{QStringLiteral("ko_KR"), QStringLiteral("pl"), QStringLiteral("ta"), QStringLiteral("th_TH")}));
        QCOMPARE(localisation.language(), QString());
    }

    void languageResolution()
    {
        const QStringList shipped{QStringLiteral("ko_KR"), QStringLiteral("pl"), QStringLiteral("ta"), QStringLiteral("th_TH")};
        const auto resolve = [&](const char *tag, bool expectFound = true) {
            bool found = false;
            const QString out = app::Localisation::resolve(QLatin1String(tag), shipped, &found);
            if (found != expectFound)
                qWarning() << tag << "found" << found;
            return found == expectFound ? out : QStringLiteral("?");
        };
        QCOMPARE(resolve(""), QString());
        QCOMPARE(resolve("en"), QString());
        QCOMPARE(resolve("0"), QString());
        QCOMPARE(resolve("1"), QString());
        QCOMPARE(resolve("en_GB"), QString());
        QCOMPARE(resolve("pl"), QStringLiteral("pl"));
        QCOMPARE(resolve("ko_KR"), QStringLiteral("ko_KR"));
        QCOMPARE(resolve("ko"), QStringLiteral("ko_KR"));
        QCOMPARE(resolve("pl_PL"), QStringLiteral("pl"));
        QCOMPARE(resolve("th"), QStringLiteral("th_TH"));
        QCOMPARE(resolve("ta_IN"), QStringLiteral("ta"));
        QCOMPARE(resolve("pl-PL"), QStringLiteral("pl"));
        QCOMPARE(resolve("de", false), QString());
        QCOMPARE(resolve("zz_ZZ", false), QString());
        QCOMPARE(app::Localisation::firstStartLanguage({QStringLiteral("pl-PL"), QStringLiteral("en-US")}), QStringLiteral("pl"));
        QCOMPARE(app::Localisation::firstStartLanguage({QStringLiteral("en-US"), QStringLiteral("pl-PL")}), QString());
        QCOMPARE(app::Localisation::firstStartLanguage({}), QString());
    }

    void qtCatalogLoadsBelowHikariSubs()
    {
        app::Localisation localisation(QStringLiteral(HIKARI_LANGUAGE_FIXTURES));
        localisation.setQtCatalogDir(QStringLiteral(HIKARI_LANGUAGE_FIXTURES));
        QCOMPARE(localisation.catalogLanguages(), QStringList{QStringLiteral("pl")});
        QVERIFY(localisation.setLanguage(QStringLiteral("pl_PL")));
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Cancel"), QStringLiteral("Anuluj (HikariSub)"));
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Close"), QStringLiteral("Zamknij (Qt)"));
        QCOMPARE(QCoreApplication::translate("HikariSub.MainWindow", "&File"), QStringLiteral("&Plik (HikariSub)"));
        QVERIFY(localisation.setLanguage(QString()));
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Cancel"), QStringLiteral("Cancel"));
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Close"), QStringLiteral("Close"));
    }

    void englishCountsHaveTheirPlural()
    {
        const auto remove = [](int n) {
            return QCoreApplication::translate("HikariSub.Spelling", "Remove %n word(s)", nullptr, n);
        };
        {
            app::Localisation localisation;
            QCOMPARE(remove(1), QStringLiteral("Remove 1 word"));
            QCOMPARE(remove(3), QStringLiteral("Remove 3 words"));
            QVERIFY(localisation.setLanguage(QStringLiteral("pl")));
            QCOMPARE(remove(2), QStringLiteral("Usuń 2 słowa"));
            QCOMPARE(remove(5), QStringLiteral("Usuń 5 słów"));
            QVERIFY(!localisation.catalogLanguages().contains(QStringLiteral("en")));
        }
        QCOMPARE(remove(1), QStringLiteral("Remove 1 word(s)"));
    }

    void qtCatalogComesFromTheQtInstallation()
    {
        QTranslator qt;
        if (!qt.load(QLocale(QStringLiteral("pl")), QStringLiteral("qt"), QStringLiteral("_"),
                     QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            QSKIP("this Qt installation has no qt_pl catalog");
        const QString close = qt.translate("QPlatformTheme", "Close");
        QVERIFY(!close.isEmpty() && close != u"Close");
        app::Localisation localisation;
        QVERIFY(localisation.setLanguage(QStringLiteral("pl")));
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Close"), close);
        QVERIFY(localisation.setLanguage(QStringLiteral("en")));
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Close"), QStringLiteral("Close"));
    }

    void languageLabelKeyIsNew()
    {
        for (const char *language : {"ko_KR", "pl", "ta", "th_TH"}) {
            const QString ui = QStringLiteral(HIKARI_SOURCE_DIR "/i18n/hikarisub_%1.ts").arg(QLatin1String(language));
            const auto label = tsEntry(ui, QStringLiteral("HikariSub.Options"), QStringLiteral("Language"));
            QVERIFY2(label.has_value(), language);
            const auto old = tsEntry(ui, QStringLiteral("HikariSub.Options"), QStringLiteral("Language (program restart required)"));
            QVERIFY2(old.has_value(), language);
            QCOMPARE(old->first, QStringLiteral("vanished"));
            const QString gettext = QStringLiteral(HIKARI_SOURCE_DIR "/i18n/hikarisub_gettext_%1.ts").arg(QLatin1String(language));
            const auto script = tsEntry(gettext, QString::fromLatin1(app::Localisation::kGettextContext),
                                        QStringLiteral("Language (program restart required)"));
            QVERIFY2(script.has_value(), language);
            QCOMPARE(script->first, QString());
        }
    }

    void gettextResolvesThroughTheGettextCatalog()
    {
        app::Localisation localisation;
        QCOMPARE(localisation.gettext("Search bar"), std::string("Search bar"));
        QVERIFY(localisation.setLanguage(QStringLiteral("pl")));
        QCOMPARE(localisation.gettext("Search bar"), std::string("Pasek szukania"));
        QCOMPARE(localisation.gettext("no such key"), std::string("no such key"));
        QCOMPARE(localisation.gettext("%d element"), std::string("%d element"));
        QCOMPARE(localisation.gettext(""), std::string());
        QCOMPARE(localisation.gettext("\xff" "bad"), std::string());
        QCOMPARE(localisation.gettext("\xed\xa0\x80"), std::string());
        QCOMPARE(localisation.gettext(std::string("Search bar\0x", 12)), std::string("Search bar\0x", 12));
        QVERIFY(localisation.setLanguage(QStringLiteral("th")));
        QCOMPARE(localisation.language(), QStringLiteral("th_TH"));
        QTranslator thai;
        QVERIFY(thai.load(QStringLiteral(":/i18n/hikarisub_gettext_th_TH.qm")));
        const QString expected = thai.translate(app::Localisation::kGettextContext, "Search bar");
        QVERIFY(!expected.isEmpty());
        QCOMPARE(localisation.gettext("Search bar"), expected.toStdString());
        QVERIFY(localisation.setLanguage(QStringLiteral("pl")));
        QCOMPARE(QCoreApplication::translate("HikariSub.MainWindow", "&File"), QStringLiteral("&Plik"));
        QCOMPARE(localisation.gettext("&File"), std::string("&Plik"));
        QCOMPARE(localisation.gettext("Language (program restart required)"),
                 std::string("Język (wymaga restartu programu)"));
        QVERIFY(localisation.setLanguage(QStringLiteral("en")));
        QCOMPARE(localisation.gettext("Search bar"), std::string("Search bar"));
        QCOMPARE(QCoreApplication::translate("HikariSub.MainWindow", "&File"), QStringLiteral("&File"));
    }

    void settingsSwitchesTheLanguageLive()
    {
        start();
        QCOMPARE(fileMenuTitle(), QStringLiteral("&File"));
        QVERIFY(application->openFile(episode));
        application->addPage();
        QCOMPARE(tabTitle(1), QStringLiteral("Untitled"));
        openSettings();
        QCOMPARE(settingsDialog()->property("languages").toStringList(),
                 (QStringList{QStringLiteral("English"), QStringLiteral("한국어"), QStringLiteral("Polski"),
                              QStringLiteral("தமிழ்"), QStringLiteral("ไทย")}));
        QCOMPARE(dialogItem("setting_program.language")->property("currentIndex").toInt(), 0);
        QCOMPARE(settingsDialog()->property("title").toString(), QStringLiteral("Settings"));
        QCOMPARE(dialogItem("settingsLanguageGroup")->property("title").toString(), QStringLiteral("Language"));
        QAccessibleInterface *languageChoice = QAccessible::queryAccessibleInterface(dialogItem("setting_program.language"));
        QVERIFY(languageChoice);
        QCOMPARE(languageChoice->text(QAccessible::Name), QStringLiteral("Interface language"));
        choose("setting_program.language", 2);
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsApply"), "click"));
        QCOMPARE(application->settingsStore()->text("program.language"), QStringLiteral("pl"));
        QCOMPARE(application->localisation().language(), QStringLiteral("pl"));
        QTRY_COMPARE(fileMenuTitle(), QStringLiteral("&Plik"));
        QCOMPARE(settingsButton("settingsApply")->property("text").toString(), QStringLiteral("Zastosuj"));
        QCOMPARE(dialogItem("setting_program.language")->property("currentIndex").toInt(), 2);
        QAccessibleInterface *a = QAccessible::queryAccessibleInterface(dialogItem("setting_subtitles.openInNewTab"));
        QVERIFY(a);
        QCOMPARE(a->text(QAccessible::Name), QStringLiteral("Otwieraj napisy zawsze w nowej karcie"));
        QCOMPARE(settingsDialog()->property("title").toString(), QStringLiteral("Ustawienia"));
        QCOMPARE(dialogItem("settingsLanguageGroup")->property("title").toString(), QStringLiteral("Język"));
        QCOMPARE(languageChoice->text(QAccessible::Name), QStringLiteral("Język interfejsu"));
        QCOMPARE(tabTitle(0), QStringLiteral("episode.ass"));
        QCOMPARE(tabTitle(1), QStringLiteral("Bez nazwy"));
        QCOMPARE(settingsDialog()->property("dictionaries").toStringList().value(0),
                 QStringLiteral("Umieść pliki .dic i .aff do folderu \"Dictionary\""));
        choose("setting_program.language", 0);
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsOk"), "click"));
        QTRY_VERIFY(!settingsDialog()->property("visible").toBool());
        QCOMPARE(application->settingsStore()->text("program.language"), QStringLiteral("en"));
        QTRY_COMPARE(fileMenuTitle(), QStringLiteral("&File"));
        QCOMPARE(tabTitle(1), QStringLiteral("Untitled"));
        application->settingsStore()->set("program.language", QStringLiteral("ko_KR"));
        QCOMPARE(application->localisation().language(), QStringLiteral("ko_KR"));
        openSettings();
        QCOMPARE(dialogItem("setting_program.language")->property("currentIndex").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(settingsDialog(), "resetAll"));
        QObject *question = nullptr;
        QTRY_VERIFY((question = settingsDialog()->property("question").value<QObject *>()) &&
                    question->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(question, "answer", Q_ARG(QVariant, QStringLiteral("reset"))));
        QCOMPARE(application->localisation().language(), QStringLiteral("ko_KR"));
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsOk"), "click"));
        QCOMPARE(application->localisation().language(), QString());
        QTRY_COMPARE(fileMenuTitle(), QStringLiteral("&File"));
    }

    void coldStartReadsTheStoredLanguage()
    {
        const QString ini = dir.filePath(QStringLiteral("cold.ini"));
        {
            app::Application::Options options;
            options.settingsFile = ini;
            app::Application first(options);
            first.settingsStore()->set("program.language", QStringLiteral("pl_PL"));
            first.settingsStore()->sync();
        }
        start(ini);
        QCOMPARE(application->localisation().language(), QStringLiteral("pl"));
        QCOMPARE(fileMenuTitle(), QStringLiteral("&Plik"));
        application->addPage();
        QCOMPARE(tabTitle(application->tabs().size() - 1), QStringLiteral("Bez nazwy"));
        QCOMPARE(application->settingsStore()->text("program.language"), QStringLiteral("pl_PL"));
        stop();
        {
            app::Application::Options options;
            options.settingsFile = ini;
            app::Application first(options);
            first.settingsStore()->set("program.language", QStringLiteral("de"));
            first.settingsStore()->sync();
        }
        start(ini);
        QCOMPARE(application->localisation().language(), QString());
        QCOMPARE(fileMenuTitle(), QStringLiteral("&File"));
        QVERIFY(application->log().history().contains(QStringLiteral("Cannot find translation, language change failed")));
        stop();
        {
            app::Application::Options options;
            options.settingsFile = ini;
            app::Application first(options);
            first.settingsStore()->set("program.language", QStringLiteral("1"));
            first.settingsStore()->sync();
        }
        start(ini);
        QCOMPARE(application->settingsStore()->text("program.language"), QString());
        QVERIFY(!application->log().history().contains(QStringLiteral("Cannot find translation")));
    }

    void firstStartOnAPolishSystemTakesPolish()
    {
        const QStringList polish{QStringLiteral("pl-PL"), QStringLiteral("en-US")};
        const QString ini = dir.filePath(QStringLiteral("first-start/settings.toml"));
        QVERIFY(!QFile::exists(ini));
        start(ini, polish);
        QCOMPARE(application->settingsStore()->text("program.language"), QStringLiteral("pl"));
        QCOMPARE(application->dictionaryLanguage(), QStringLiteral("pl"));
        QCOMPARE(application->localisation().language(), QStringLiteral("pl"));
        QCOMPARE(fileMenuTitle(), QStringLiteral("&Plik"));
        application->settingsStore()->set("program.language", QString());
        application->settingsStore()->set("editor.dictionaryLanguage", QStringLiteral("en_US"));
        application->settingsStore()->sync();
        stop();
        QVERIFY(QFile::exists(ini));
        start(ini, polish);
        QCOMPARE(application->settingsStore()->text("program.language"), QString());
        QCOMPARE(application->dictionaryLanguage(), QStringLiteral("en_US"));
        QCOMPARE(fileMenuTitle(), QStringLiteral("&File"));
        stop();
        const QString other = dir.filePath(QStringLiteral("first-start/other.ini"));
        start(other, {QStringLiteral("en-US"), QStringLiteral("pl-PL")});
        QCOMPARE(application->settingsStore()->text("program.language"), QString());
        QVERIFY(application->dictionaryLanguage() != u"pl");
        QCOMPARE(fileMenuTitle(), QStringLiteral("&File"));
    }

    void cachedStringsFollowASwitch()
    {
        start();
        QVERIFY(application->openFile(episode));
        auto *grid = item("editingGrid");
        QVERIFY(grid);
        auto *model = grid->property("model").value<QAbstractItemModel *>();
        QVERIFY(model);
        application->shell().selectLine(application->files().session(*application->workspace().editingTarget())
                                            ->document().lines()[0]->id.value);
        QTRY_COMPARE(item("selectionStatus")->property("text").toString(), QStringLiteral("1 line selected"));
        const QString video = application->video().status();
        const QString audio = item("audioStatus")->property("text").toString();
        const QString heading = model->headerData(2, Qt::Horizontal).toString();
        QCOMPARE(heading, QStringLiteral("Start"));
        QAccessibleInterface *a = QAccessible::queryAccessibleInterface(grid);
        QVERIFY(a);
        const QString gridName = a->text(QAccessible::Name);
        QSignalSpy headers(model, &QAbstractItemModel::headerDataChanged);
        application->localisation().setPseudo(app::Localisation::Pseudo::Accents);
        QVERIFY(headers.count() >= 1);
        const auto pseudo = [](const QString &s) { return QStringLiteral("[") + s; };
        QCOMPARE(model->headerData(2, Qt::Horizontal).toString(), QStringLiteral("[Šţåŕţ~~]"));
        QTRY_VERIFY(item("selectionStatus")->property("text").toString().startsWith(pseudo(QStringLiteral("1 ļîñé"))));
        QVERIFY(application->video().status() != video);
        QVERIFY(application->video().status().startsWith(QLatin1Char('[')));
        QTRY_VERIFY(item("audioStatus")->property("text").toString() != audio);
        QVERIFY(a->text(QAccessible::Name) != gridName);
        QVERIFY(a->text(QAccessible::Name).startsWith(QLatin1Char('[')));
        application->localisation().setPseudo(app::Localisation::Pseudo::None);
        QTRY_COMPARE(item("selectionStatus")->property("text").toString(), QStringLiteral("1 line selected"));
        QCOMPARE(model->headerData(2, Qt::Horizontal).toString(), heading);
        QCOMPARE(application->video().status(), video);
        QTRY_COMPARE(item("audioStatus")->property("text").toString(), audio);
    }

    void hiddenSelectionStatusFollowsASwitch()
    {
        start();
        QVERIFY(application->openFile(episode));
        const auto &lines = application->files().session(*application->workspace().editingTarget())->document().lines();
        application->shell().selectLine(lines[1]->id.value);
        QVERIFY(QMetaObject::invokeMethod(root()->findChild<QObject *>(QStringLiteral("hideSelectedLines")), "triggered"));
        QTRY_VERIFY(application->shell().filtered());
        const QString english = application->shell().selectionStatus();
        QVERIFY2(english.endsWith(QStringLiteral(" (1 hidden)")), qPrintable(english));
        application->localisation().setPseudo(app::Localisation::Pseudo::Accents);
        const QString suffix = QCoreApplication::translate("HikariSub.StatusBar", " (%1 hidden)").arg(1);
        QVERIFY(suffix != QStringLiteral(" (1 hidden)"));
        QTRY_VERIFY2(application->shell().selectionStatus().endsWith(suffix),
                     qPrintable(application->shell().selectionStatus()));
        QCOMPARE(item("selectionStatus")->property("text").toString(), application->shell().selectionStatus());
        application->localisation().setPseudo(app::Localisation::Pseudo::None);
        QTRY_COMPARE(application->shell().selectionStatus(), english);
    }

    void dialogsSizeAfterASwitchAsAtStart()
    {
        const auto sizes = [this] {
            QList<QSizeF> out;
            for (const char *name : {"aboutDialog", "creditsDialog"}) {
                QObject *dialog = root()->findChild<QObject *>(QLatin1String(name));
                if (!dialog) {
                    out << QSizeF(-1, -1);
                    continue;
                }
                QMetaObject::invokeMethod(dialog, "open");
                const bool opened = QTest::qWaitFor([dialog] { return dialog->property("opened").toBool(); });
                out << (opened ? QSizeF(dialog->property("width").toReal(), dialog->property("height").toReal())
                               : QSizeF(-2, -2));
                QMetaObject::invokeMethod(dialog, "close");
                QTest::qWaitFor([dialog] { return !dialog->property("visible").toBool(); });
            }
            return out;
        };
        start();
        const QList<QSizeF> english = sizes();
        QVERIFY(!english.contains(QSizeF(-1, -1)) && !english.contains(QSizeF(-2, -2)));
        application->localisation().setPseudo(app::Localisation::Pseudo::Accents);
        QCoreApplication::processEvents();
        const QList<QSizeF> switched = sizes();
        stop();
        qputenv("HIKARI_PSEUDOLOCALE", "accents");
        start();
        qunsetenv("HIKARI_PSEUDOLOCALE");
        QCOMPARE(sizes(), switched);
        QVERIFY(switched != english);
        stop();
        start();
        application->settingsStore()->set("program.fontSize", 14);
        QCoreApplication::processEvents();
        const QList<QSizeF> larger = sizes();
        stop();
        const QString ini = dir.filePath(QStringLiteral("dialog-font.ini"));
        {
            app::Application::Options options;
            options.settingsFile = ini;
            app::Application first(options);
            first.settingsStore()->set("program.fontSize", 14);
            first.settingsStore()->sync();
        }
        start(ini);
        QCOMPARE(sizes(), larger);
        QVERIFY(larger != english);
    }

    void pseudolocalizedWindowLeavesNoSourceText()
    {
        start();
        QVERIFY(application->openFile(episode));
        const QSet<QString> sources = catalogSources();
        QVERIFY(sources.size() > 800);
        int before = 0;
        for (const auto &[o, text] : shownTexts())
            before += sources.contains(text);
        QVERIFY2(before > 100, qPrintable(QString::number(before)));
        application->localisation().setPseudo(app::Localisation::Pseudo::Accents);
        QCoreApplication::processEvents();
        // MUST KILL: flushes DeferredDelete so the replaced standard buttons are gone before the scan
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const QSet<QString> allowed{QStringLiteral("OK"), QStringLiteral("Default")};
        const auto unshown = [](QObject *o) {
            for (auto *i = qobject_cast<QQuickItem *>(o); i; i = i->parentItem())
                if (i->objectName() == u"historyList" || i->objectName() == u"visualToolMeasure")
                    return true;
            return false;
        };
        QStringList stale;
        for (const auto &[o, text] : shownTexts())
            if (sources.contains(text) && !allowed.contains(text) && o && o->objectName() != u"menuItemKeys" && !unshown(o))
            {
                QString chain;
                for (QObject *p = o; p; p = p->parent())
                    chain += QString::fromLatin1(p->metaObject()->className()) + QLatin1Char('/') + p->objectName() + QLatin1Char(' ');
                stale << QStringLiteral("%1: %2").arg(chain, text);
            }
        stale.removeDuplicates();
        QVERIFY2(stale.isEmpty(), qPrintable(stale.join(u'\n')));
    }

    void rightToLeftMirrorsTheShell()
    {
        start();
        QVERIFY(application->openFile(episode));
        auto *file = item("fileMenuBarItem");
        const qreal ltrX = file->mapToScene(QPointF()).x();
        QVERIFY(ltrX < window->width() / 2);
        application->localisation().setPseudo(app::Localisation::Pseudo::RightToLeft);
        QVERIFY(application->localisation().rightToLeft());
        QCOMPARE(QGuiApplication::layoutDirection(), Qt::RightToLeft);
        QCOMPARE(QQmlProperty(window->contentItem(), QStringLiteral("LayoutMirroring.enabled"), qmlContext(window)).read().toBool(), true);
        QTRY_VERIFY(file->mapToScene(QPointF(file->width(), 0)).x() > window->width() / 2);
        openSettings();
        auto *ok = settingsButton("settingsOk");
        auto *cancel = settingsButton("settingsCancel");
        QVERIFY(ok && cancel);
        const bool okLeftOfCancel = ok->mapToScene(QPointF()).x() < cancel->mapToScene(QPointF()).x();
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
        QTRY_VERIFY(!settingsDialog()->property("visible").toBool());
        application->localisation().setPseudo(app::Localisation::Pseudo::None);
        QCOMPARE(QGuiApplication::layoutDirection(), Qt::LeftToRight);
        QTRY_COMPARE(file->mapToScene(QPointF()).x(), ltrX);
        openSettings();
        QCOMPARE(settingsButton("settingsOk")->mapToScene(QPointF()).x() < settingsButton("settingsCancel")->mapToScene(QPointF()).x(),
                 !okLeftOfCancel);
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
    }

    void documentIgnoresTheLanguage()
    {
        start();
        QTRY_VERIFY(window->property("arrangementSettled").toBool());
        QVERIFY(application->openFile(episode));
        auto *session = application->files().session(*application->workspace().editingTarget());
        QVERIFY(session);
        const QLocale locale;
        const auto shown = [&] {
            auto *model = item("editingGrid")->property("model").value<QAbstractItemModel *>();
            QStringList out;
            for (int row = 0; row < model->rowCount(); ++row)
                for (int column = 0; column < model->columnCount(); ++column)
                    out << model->index(row, column).data().toString();
            return out << application->editor().startText() << application->editor().endText() << application->editor().text();
        };
        const QStringList before = shown();
        const QString english = dir.filePath(QStringLiteral("english.ass"));
        QVERIFY(application->saveAs(english));
        application->waitForWrites();
        for (const char *tag : {"pl", "th_TH", "ko_KR"}) {
            application->settingsStore()->set("program.language", QLatin1String(tag));
            QCOMPARE(application->localisation().language(), QLatin1String(tag));
            for (const auto pseudo : {app::Localisation::Pseudo::None, app::Localisation::Pseudo::RightToLeft}) {
                application->localisation().setPseudo(pseudo);
                QCoreApplication::processEvents();
                QCOMPARE(shown(), before);
                QCOMPARE(QLocale().name(), locale.name());
                QCOMPARE(QLocale().decimalPoint(), locale.decimalPoint());
                const QString path = dir.filePath(QStringLiteral("saved-%1-%2.ass").arg(QLatin1String(tag)).arg(int(pseudo)));
                QVERIFY(application->saveAs(path));
                application->waitForWrites();
                QVERIFY(readAll(path).contains("[Hikari]"));
                QCOMPARE(withoutLayout(readAll(path)), withoutLayout(readAll(english)));
            }
            application->localisation().setPseudo(app::Localisation::Pseudo::None);
        }
        QCOMPARE(readAll(english).mid(0, 3), QByteArray("\xEF\xBB\xBF"));
        QVERIFY(readAll(english).contains("Style: Default,Arial,20.5,"));
        QVERIFY(readAll(english).contains("1:02:03.04,1:02:04.99"));
    }

    void luaGettextFollowsTheLanguage()
    {
        start();
        QVERIFY(application->openFile(episode));
        application->settingsStore()->set("program.language", QStringLiteral("pl"));
        auto &automation = application->automation();
        const std::string script = HIKARI_LUA_FIXTURES "/gettext.lua";
        const auto status = [&] {
            for (const auto &s : automation.scripts())
                if (s.path == script)
                    return s;
            return application::ScriptStatus{};
        };
        automation.load(script);
        QTRY_VERIFY_WITH_TIMEOUT(status().state == application::ScriptStatus::State::Ready, 15000);
        QCOMPARE(status().info.name, std::string("Pasek szukania"));
        QCOMPARE(status().info.macros.at(0).name, std::string("Pasek szukania"));
        const auto runOnce = [&](QString &out) {
            if (!automation.run(script, 0) || !QTest::qWaitFor([&] { return !automation.running(); }, 15000))
                return false;
            out = automation.log();
            return true;
        };
        QString log;
        QVERIFY(runOnce(log));
        QVERIFY2(log.contains(QStringLiteral("Pasek szukania|Pasek szukania|no such key|%d element|12|")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("3 element")), qPrintable(log));
        QVERIFY2(log.endsWith(QStringLiteral("6|Search0")), qPrintable(log));
        application->settingsStore()->set("program.language", QStringLiteral("en"));
        QVERIFY(runOnce(log));
        QVERIFY2(log.contains(QStringLiteral("Pasek szukania|Search bar|no such key|%d element|12|")), qPrintable(log));
        QCOMPARE(status().info.name, std::string("Pasek szukania"));
        QVERIFY(automation.reload(script));
        QTRY_VERIFY_WITH_TIMEOUT(status().state == application::ScriptStatus::State::Ready, 15000);
        QCOMPARE(status().info.name, std::string("Search bar"));
    }

    void programFontAppliesLive()
    {
        const QFont before = QGuiApplication::font();
        start();
        QCOMPARE(QGuiApplication::font().families().value(0), QStringLiteral("Tahoma"));
        QCOMPARE(QGuiApplication::font().pointSize(), 10);
        const auto statusSize = [](qreal points) { return std::max(points * 10 / 13, 8.0); };
        auto *label = item("selectionStatus");
        QVERIFY(label);
        QTRY_COMPARE(label->property("font").value<QFont>().pointSizeF(), statusSize(10));
        QCOMPARE(label->property("font").value<QFont>().families().value(0), QStringLiteral("Tahoma"));
        application->settingsStore()->set("program.fontSize", 14);
        application->settingsStore()->set("program.font", QStringLiteral("DejaVu Sans"));
        QCOMPARE(QGuiApplication::font().pointSize(), 14);
        QCOMPARE(QGuiApplication::font().families().value(0), QStringLiteral("DejaVu Sans"));
        QTRY_COMPARE(label->property("font").value<QFont>().pointSizeF(), statusSize(14));
        QCOMPARE(item("historyGo")->property("font").value<QFont>().pointSize(), 14);
        QTRY_COMPARE(label->property("font").value<QFont>().families().value(0), QStringLiteral("DejaVu Sans"));
        application->settingsStore()->set("program.fontSize", 0);
        QCOMPARE(QGuiApplication::font().pointSize(), 10);
        stop();
        QCOMPARE(QGuiApplication::font(), before);
    }

    void semanticContextsShowTheCatalogOnSurfaces()
    {
        QHash<QString, QHash<QString, QString>> finished;
        {
            QXmlStreamReader xml(readAll(QStringLiteral(HIKARI_SOURCE_DIR "/i18n/hikarisub_pl.ts")));
            QString context, source;
            bool numerus = false;
            while (!xml.atEnd()) {
                if (xml.readNext() != QXmlStreamReader::StartElement)
                    continue;
                if (xml.name() == u"name") {
                    context = xml.readElementText();
                } else if (xml.name() == u"message") {
                    numerus = xml.attributes().value(QStringLiteral("numerus")) == u"yes";
                } else if (xml.name() == u"source") {
                    source = xml.readElementText();
                } else if (xml.name() == u"translation" && !numerus && !xml.attributes().hasAttribute(QStringLiteral("type"))) {
                    if (const QString text = xml.readElementText(); !text.isEmpty())
                        finished[source][context] = text;
                }
            }
        }
        QVERIFY(finished.size() > 500);
        QTranslator qm;
        QVERIFY(qm.load(QStringLiteral(":/i18n/hikarisub_pl.qm")));
        using Shown = QHash<std::pair<QObject *, QString>, QString>;
        const auto shown = [] {
            Shown out;
            for (QWindow *w : QGuiApplication::allWindows()) {
                auto *quick = qobject_cast<QQuickWindow *>(w);
                if (!quick)
                    continue;
                QList<QObject *> objects{quick};
                std::function<void(QQuickItem *)> walk = [&](QQuickItem *i) {
                    objects << i;
                    for (QQuickItem *c : i->childItems())
                        walk(c);
                };
                walk(quick->contentItem());
                objects << quick->findChildren<QObject *>();
                for (QObject *o : objects) {
                    for (const char *p : {"text", "title", "placeholderText"}) {
                        const QVariant v = o->property(p);
                        if (v.typeId() == QMetaType::QString && hasLetter(v.toString()))
                            out.insert({o, QLatin1String(p)}, v.toString());
                    }
                    if (auto *i = qobject_cast<QQuickItem *>(o); i && i->isVisible())
                        if (QAccessibleInterface *a = QAccessible::queryAccessibleInterface(i))
                            if (const QString name = a->text(QAccessible::Name); hasLetter(name))
                                out.insert({o, QStringLiteral("accessible")}, name);
                }
            }
            return out;
        };
        start();
        QVERIFY(application->openFile(episode));
        // MUST KILL: settingsDialog() is called for its side effect, making the Settings window
        QVERIFY(settingsDialog());
        QCoreApplication::processEvents();
        const Shown english = shown();
        application->settingsStore()->set("program.language", QStringLiteral("pl"));
        QCOMPARE(application->localisation().language(), QStringLiteral("pl"));
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const Shown polish = shown();
        QMap<QString, QSet<QString>> seen;
        int shownTranslations = 0;
        for (auto it = english.cbegin(); it != english.cend(); ++it) {
            const QString after = polish.value(it.key());
            if (after.isEmpty() || after == it.value())
                continue;
            QStringList contexts;
            for (const auto &[context, text] : finished.value(it.value()).asKeyValueRange()) {
                if (text != after)
                    continue;
                QVERIFY2(context.startsWith(QStringLiteral("HikariSub.")), qPrintable(context));
                QCOMPARE(qm.translate(context.toUtf8().constData(), it.value().toUtf8().constData()), after);
                contexts << context;
            }
            shownTranslations += !contexts.isEmpty();
            if (contexts.size() == 1 && finished.value(it.value()).size() == 1)
                seen[contexts.first()] << it.value();
        }
        QStringList found;
        for (const auto &[context, sources] : seen.asKeyValueRange())
            found << QStringLiteral("%1 (%2)").arg(context).arg(sources.size());
        qInfo().noquote() << shownTranslations << "shown translations; sources one context translates, by context:"
                          << found.join(QStringLiteral(", "));
        for (const char *context : {"HikariSub.MainWindow", "HikariSub.Audio", "HikariSub.Video", "HikariSub.Editor",
                                    "HikariSub.Grid", "HikariSub.TimeShift", "HikariSub.Options", "HikariSub.Styles",
                                    "HikariSub.Documents"})
            QVERIFY2(seen.contains(QLatin1String(context)), context);
        application->addPage();
        QCOMPARE(tabTitle(application->tabs().size() - 1), qm.translate("HikariSub.Documents", "Untitled"));
        QCOMPARE(tabTitle(application->tabs().size() - 1), QStringLiteral("Bez nazwy"));
        QCOMPARE(qm.translate("HikariSub.MainWindow", "&File"), QStringLiteral("&Plik"));
        QCOMPARE(qm.translate("Main", "&File"), QString());
        QCOMPARE(qm.translate("hikari::app::Application", "Untitled"), QString());
        application->settingsStore()->set("program.language", QStringLiteral("en"));
    }

    void wrapStylesShowTheirTranslation()
    {
        start();
        QVERIFY(application->openFile(episode));
        QVERIFY(QMetaObject::invokeMethod(root()->findChild<QObject *>(QStringLiteral("assProperties")), "triggered"));
        QTRY_VERIFY(item("propWrapStyle"));
        const auto choice = [this](int index) {
            QString text;
            QMetaObject::invokeMethod(item("propWrapStyle"), "textAt", Q_RETURN_ARG(QString, text), Q_ARG(int, index));
            return text;
        };
        QCOMPARE(choice(1), QStringLiteral("1: End-of-line wrapping, only \\N breaks"));
        QCOMPARE(choice(2), QStringLiteral("2: No wrapping, both \\n and \\N break"));
        for (const QString language : {QStringLiteral("pl"), QStringLiteral("ko_KR"), QStringLiteral("th_TH"), QStringLiteral("ta")}) {
            QTranslator qm;
            QVERIFY(qm.load(QStringLiteral(":/i18n/hikarisub_%1.qm").arg(language)));
            const QString one = qm.translate("HikariSub.ScriptProperties", "1: End-of-line wrapping, only %1 breaks");
            const QString two = qm.translate("HikariSub.ScriptProperties", "2: No wrapping, both %1 and %2 break");
            QVERIFY2(!one.isEmpty() && !two.isEmpty(), qPrintable(language));
            application->settingsStore()->set("program.language", language);
            QCOMPARE(application->localisation().language(), language);
            QCoreApplication::processEvents();
            QCOMPARE(choice(1), one.arg(QStringLiteral("\\N")));
            QCOMPARE(choice(2), two.arg(QStringLiteral("\\n")).arg(QStringLiteral("\\N")));
        }
        application->settingsStore()->set("program.language", QStringLiteral("en"));
    }

    void screenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_LANGUAGE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_LANGUAGE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        start();
        window->resize(1600, 900);
        QVERIFY(application->openFile(episode));
        const struct {
            const char *name, *language;
            app::Localisation::Pseudo pseudo;
        } cases[] = {{"polish", "pl", app::Localisation::Pseudo::None},
                     {"thai", "th_TH", app::Localisation::Pseudo::None},
                     {"pseudo-accents", "", app::Localisation::Pseudo::Accents},
                     {"pseudo-rtl", "", app::Localisation::Pseudo::RightToLeft}};
        for (const auto &c : cases) {
            application->settingsStore()->set("program.language", QLatin1String(c.language));
            application->localisation().setPseudo(c.pseudo);
            QTRY_VERIFY(fileMenuTitle() != QStringLiteral("&File"));
            QCOMPARE(QGuiApplication::layoutDirection(),
                     c.pseudo == app::Localisation::Pseudo::RightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
            QTest::qWait(300);
            QVERIFY(window->grabWindow().save(out + QStringLiteral("/main-window-%1.png").arg(QLatin1String(c.name))));
            auto *bar = item("fileMenuBarItem");
            auto *menu = bar->property("menu").value<QObject *>();
            QVERIFY(QMetaObject::invokeMethod(menu, "popup", Q_ARG(QQuickItem *, bar), Q_ARG(QPointF, QPointF(0, bar->height()))));
            QTRY_VERIFY(menu->property("opened").toBool());
            QTest::qWait(300);
            auto *content = menu->property("contentItem").value<QQuickItem *>();
            QVERIFY(content->window()->grabWindow().save(out + QStringLiteral("/file-menu-%1.png").arg(QLatin1String(c.name))));
            QMetaObject::invokeMethod(menu, "close");
            QTRY_VERIFY(!menu->property("visible").toBool());
            openSettings();
            QTest::qWait(300);
            QVERIFY(qobject_cast<QQuickWindow *>(settingsDialog())->grabWindow().save(
                out + QStringLiteral("/options-%1.png").arg(QLatin1String(c.name))));
            QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
            QTRY_VERIFY(!settingsDialog()->property("visible").toBool());
        }
        application->localisation().setPseudo(app::Localisation::Pseudo::None);
    }
};

QTEST_MAIN(LanguageTest)
#include "language_tests.moc"
