// O5 (#197): the interface language over the O4 catalogs (docs/qt/localisation.md).
// Settings lists English and the embedded catalogs as legacy OptionsDialog
// does (OptionsDialog.cpp:322-341); the choice is stored in PROGRAM_LANGUAGE
// and read at start as hikarisubApp.cpp:327-369 reads it, and it switches
// live (the card's departure from "program restart required"): QML, dialogs,
// menus, models, cached controller strings and accessible names follow.
// aegisub.gettext resolves through the HikariSub.Automation.Gettext QM
// (Automation.cpp:83-88). Pseudolocalized and right-to-left layouts, the
// Document's bytes, times and numbers across a switch, and the program font
// applied live (OptionsDialog.cpp:1105-1112).

#include "hikari/app/application.h"
#include "hikari/app/localisation.h"
#include "docking.h"

#include <QAccessible>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
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

#include <memory>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Every source text the rewrite's catalogs key (i18n/migration/keys.ts, the
// keys lupdate extracts from the sources).
QSet<QString> catalogSources()
{
    QSet<QString> out;
    QXmlStreamReader xml(readAll(QStringLiteral(HIKARI_SOURCE_DIR "/i18n/migration/keys.ts")));
    while (!xml.atEnd())
        if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == u"source")
            out.insert(xml.readElementText());
    return out;
}

bool hasLetter(const QString &s)
{
    return std::any_of(s.begin(), s.end(), [](QChar c) { return c.isLetter(); });
}

} // namespace

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
    QObject *settingsDialog() const { return root()->findChild<QObject *>(QStringLiteral("settingsDialog")); }
    QQuickItem *dialogItem(const char *name) const
    {
        auto *content = settingsDialog()->property("contentItem").value<QQuickItem *>();
        return content ? findItem(content, QLatin1String(name)) : nullptr;
    }
    QQuickItem *settingsButton(const char *name) const
    {
        auto *footer = settingsDialog()->property("footer").value<QQuickItem *>();
        return footer ? findItem(footer, QLatin1String(name)) : nullptr;
    }
    void openSettings()
    {
        auto *menuItem = root()->findChild<QObject *>(QStringLiteral("settingsMenuItem"));
        QVERIFY(menuItem);
        QVERIFY(QMetaObject::invokeMethod(menuItem->property("action").value<QObject *>(), "trigger"));
        QTRY_VERIFY(settingsDialog()->property("visible").toBool());
    }
    // A choice of the dialog, as the user makes it (ComboBox activated).
    void choose(const char *setting, int index)
    {
        auto *combo = dialogItem(setting);
        QVERIFY(combo);
        combo->setProperty("currentIndex", index);
        QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, index)));
    }
    QString tabTitle(int i) const { return application->tabs().value(i).toMap().value(QStringLiteral("title")).toString(); }
    // Every text an item or a menu object shows, with its accessible name.
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
        // A style with decimal values and Lines with times: what a language
        // must not change.
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

    // The QM the build embeds (hikari_translations), sorted as
    // wxArrayString::Sort sorts GetAvailableTranslations' tags.
    void catalogsAreTheEmbeddedQm()
    {
        app::Localisation localisation;
        QCOMPARE(localisation.catalogLanguages(),
                 (QStringList{QStringLiteral("ko_KR"), QStringLiteral("pl"), QStringLiteral("ta"), QStringLiteral("th_TH")}));
        QCOMPARE(localisation.language(), QString());
    }

    // Locale selection and fallback, the same on every platform.
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
        // English: the source language, no catalog (legacy "" and "en";
        // "0" and "1" are old values legacy turns into "").
        QCOMPARE(resolve(""), QString());
        QCOMPARE(resolve("en"), QString());
        QCOMPARE(resolve("0"), QString());
        QCOMPARE(resolve("1"), QString());
        QCOMPARE(resolve("en_GB"), QString());
        // A catalog's tag, or a tag of its language (wxLocale's canonical
        // names: ko -> ko_KR, pl_PL -> pl, th -> th_TH, ta_IN -> ta).
        QCOMPARE(resolve("pl"), QStringLiteral("pl"));
        QCOMPARE(resolve("ko_KR"), QStringLiteral("ko_KR"));
        QCOMPARE(resolve("ko"), QStringLiteral("ko_KR"));
        QCOMPARE(resolve("pl_PL"), QStringLiteral("pl"));
        QCOMPARE(resolve("th"), QStringLiteral("th_TH"));
        QCOMPARE(resolve("ta_IN"), QStringLiteral("ta"));
        QCOMPARE(resolve("pl-PL"), QStringLiteral("pl"));
        // No catalog of the language: English, reported.
        QCOMPARE(resolve("de", false), QString());
        QCOMPARE(resolve("zz_ZZ", false), QString());
        // Legacy's first start on a Polish system (hikarisubApp.cpp:319-325).
        QCOMPARE(app::Localisation::firstStartLanguage({QStringLiteral("pl-PL"), QStringLiteral("en-US")}), QStringLiteral("pl"));
        QCOMPARE(app::Localisation::firstStartLanguage({QStringLiteral("en-US"), QStringLiteral("pl-PL")}), QString());
        QCOMPARE(app::Localisation::firstStartLanguage({}), QString());
    }

    // aegisub.gettext's lookup: the legacy MO's answers (O4's qm_tests
    // compare the whole catalogs), missing keys, conversion edge cases and
    // the catalog after a switch.
    void gettextResolvesThroughTheGettextCatalog()
    {
        app::Localisation localisation;
        // English: no catalog, every source comes back.
        QCOMPARE(localisation.gettext("Search bar"), std::string("Search bar"));
        QVERIFY(localisation.setLanguage(QStringLiteral("pl")));
        QCOMPARE(localisation.gettext("Search bar"), std::string("Pasek szukania"));
        QCOMPARE(localisation.gettext("no such key"), std::string("no such key"));
        // A plural entry's msgstr[0] under its singular id, printf kept (a
        // script formats it itself).
        QCOMPARE(localisation.gettext("%d element"), std::string("%d element"));
        // wxString(str, wxConvUTF8, len) is empty for bytes that are not
        // UTF-8, and the empty string is never translated.
        QCOMPARE(localisation.gettext(""), std::string());
        QCOMPARE(localisation.gettext("\xff" "bad"), std::string());
        QCOMPARE(localisation.gettext("\xed\xa0\x80"), std::string()); // an encoded surrogate
        // A NUL never matches an entry ("Search" alone is not looked up).
        QCOMPARE(localisation.gettext(std::string("Search bar\0x", 12)), std::string("Search bar\0x", 12));
        // UTF-8 out: a Thai translation, after a live switch.
        QVERIFY(localisation.setLanguage(QStringLiteral("th")));
        QCOMPARE(localisation.language(), QStringLiteral("th_TH"));
        QTranslator thai;
        QVERIFY(thai.load(QStringLiteral(":/i18n/hikarisub_gettext_th_TH.qm")));
        const QString expected = thai.translate(app::Localisation::kGettextContext, "Search bar");
        QVERIFY(!expected.isEmpty());
        QCOMPARE(localisation.gettext("Search bar"), expected.toStdString());
        // The rewrite's interface contexts never answer for scripts.
        QVERIFY(localisation.setLanguage(QStringLiteral("pl")));
        QCOMPARE(QCoreApplication::translate("Main", "&File"), QStringLiteral("&Plik"));
        QCOMPARE(localisation.gettext("&File"), std::string("&Plik")); // the legacy id, from the gettext catalog
        QCOMPARE(localisation.gettext("Language (program restart required)"),
                 std::string("Język (wymaga restartu programu)"));
        QVERIFY(localisation.setLanguage(QStringLiteral("en")));
        QCOMPARE(localisation.gettext("Search bar"), std::string("Search bar"));
        QCOMPARE(QCoreApplication::translate("Main", "&File"), QStringLiteral("&File"));
    }

    // Settings: English and the catalogs; Apply switches the open window,
    // its dialogs, menus and accessible names at once and stores the tag.
    void settingsSwitchesTheLanguageLive()
    {
        start();
        QCOMPARE(fileMenuTitle(), QStringLiteral("&File"));
        QVERIFY(application->openFile(episode));
        application->addPage(); // an Untitled tab (legacy SubsName = _("Untitled"))
        QCOMPARE(tabTitle(1), QStringLiteral("Untitled"));
        openSettings();
        QCOMPARE(settingsDialog()->property("languages").toStringList(),
                 (QStringList{QStringLiteral("English"), QStringLiteral("한국어"), QStringLiteral("Polski"),
                              QStringLiteral("தமிழ்"), QStringLiteral("ไทย")}));
        QCOMPARE(dialogItem("setting_program.language")->property("currentIndex").toInt(), 0);
        QCOMPARE(settingsDialog()->property("title").toString(), QStringLiteral("Options"));
        choose("setting_program.language", 2);
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsApply"), "click"));
        QCOMPARE(application->settingsStore()->text("program.language"), QStringLiteral("pl"));
        QCOMPARE(application->localisation().language(), QStringLiteral("pl"));
        // The window and the open dialog, retranslated.
        QTRY_COMPARE(fileMenuTitle(), QStringLiteral("&Plik"));
        QCOMPARE(settingsDialog()->property("title").toString(), QStringLiteral("Opcje"));
        QCOMPARE(settingsButton("settingsApply")->property("text").toString(), QStringLiteral("Zastosuj"));
        QCOMPARE(dialogItem("setting_program.language")->property("currentIndex").toInt(), 2);
        // An accessible name (Accessible.name: qsTr(...)).
        QAccessibleInterface *a = QAccessible::queryAccessibleInterface(dialogItem("setting_program.language"));
        QVERIFY(a);
        QCOMPARE(a->text(QAccessible::Name), QStringLiteral("Język (wymaga restartu programu)"));
        // A string the application keeps: the Untitled tab's name; a tab
        // named after its file keeps it.
        QCOMPARE(tabTitle(0), QStringLiteral("episode.ass"));
        QCOMPARE(tabTitle(1), QStringLiteral("Bez nazwy"));
        // The dictionary choice's entry for an empty Dictionary folder.
        QCOMPARE(settingsDialog()->property("dictionaries").toStringList().value(0),
                 QStringLiteral("Umieść pliki .dic i .aff do folderu \"Dictionary\""));
        // OK with English again.
        choose("setting_program.language", 0);
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsOk"), "click"));
        QTRY_VERIFY(!settingsDialog()->property("visible").toBool());
        QCOMPARE(application->settingsStore()->text("program.language"), QStringLiteral("en"));
        QTRY_COMPARE(fileMenuTitle(), QStringLiteral("&File"));
        QCOMPARE(tabTitle(1), QStringLiteral("Untitled"));
        // "Set default" resets PROGRAM_LANGUAGE at once (legacy
        // ResetDefault), so the interface is English then too.
        application->settingsStore()->set("program.language", QStringLiteral("ko_KR"));
        QCOMPARE(application->localisation().language(), QStringLiteral("ko_KR"));
        openSettings();
        QCOMPARE(dialogItem("setting_program.language")->property("currentIndex").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsDefault"), "click"));
        QCOMPARE(application->localisation().language(), QString());
        QTRY_COMPARE(fileMenuTitle(), QStringLiteral("&File"));
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
    }

    // Cold start: the stored language before the window exists, also from
    // a tag of the language only; one without a catalog is English, logged.
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
        QCOMPARE(application->settingsStore()->text("program.language"), QStringLiteral("pl_PL")); // not rewritten
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
        // Legacy "0" and "1" are English, written back as "".
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

    // Legacy's first start: LoadOptions() finds no settings file (2) and the
    // system's interface language is Polish, so PROGRAM_LANGUAGE and
    // DICTIONARY_LANGUAGE become "pl" before either is read
    // (hikarisubApp.cpp:319-325). Only then: a settings file that exists, or
    // another first system language, leaves both as they are.
    void firstStartOnAPolishSystemTakesPolish()
    {
        const QStringList polish{QStringLiteral("pl-PL"), QStringLiteral("en-US")};
        const QString ini = dir.filePath(QStringLiteral("first-start/hikari.ini"));
        QVERIFY(!QFile::exists(ini));
        start(ini, polish);
        QCOMPARE(application->settingsStore()->text("program.language"), QStringLiteral("pl"));
        QCOMPARE(application->dictionaryLanguage(), QStringLiteral("pl"));
        // Read at start: the window opens in Polish.
        QCOMPARE(application->localisation().language(), QStringLiteral("pl"));
        QCOMPARE(fileMenuTitle(), QStringLiteral("&Plik"));
        application->settingsStore()->set("program.language", QString());
        application->settingsStore()->set("editor.dictionaryLanguage", QStringLiteral("en_US"));
        application->settingsStore()->sync();
        stop();
        QVERIFY(QFile::exists(ini));
        // The second start keeps what the settings say.
        start(ini, polish);
        QCOMPARE(application->settingsStore()->text("program.language"), QString());
        QCOMPARE(application->dictionaryLanguage(), QStringLiteral("en_US"));
        QCOMPARE(fileMenuTitle(), QStringLiteral("&File"));
        stop();
        // A first start whose first system language is not Polish (legacy
        // GetSystemDefaultUILanguage() is one language, the first here).
        const QString other = dir.filePath(QStringLiteral("first-start/other.ini"));
        start(other, {QStringLiteral("en-US"), QStringLiteral("pl-PL")});
        QCOMPARE(application->settingsStore()->text("program.language"), QString());
        QVERIFY(application->dictionaryLanguage() != u"pl");
        QCOMPARE(fileMenuTitle(), QStringLiteral("&File"));
    }

    // What controllers and models keep is made again after a switch: the
    // video and audio status, the selection status, the Grid's headings and
    // its accessible name. The pseudolocale translates every string, so a
    // stale one stays English.
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
        QTRY_COMPARE(item("selectionStatus")->property("text").toString(), QStringLiteral("1 Line selected"));
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
        QTRY_VERIFY(item("selectionStatus")->property("text").toString().startsWith(pseudo(QStringLiteral("1 Ļîñé"))));
        QVERIFY(application->video().status() != video);
        QVERIFY(application->video().status().startsWith(QLatin1Char('[')));
        QTRY_VERIFY(item("audioStatus")->property("text").toString() != audio);
        QVERIFY(a->text(QAccessible::Name) != gridName);
        QVERIFY(a->text(QAccessible::Name).startsWith(QLatin1Char('[')));
        // And back.
        application->localisation().setPseudo(app::Localisation::Pseudo::None);
        QTRY_COMPARE(item("selectionStatus")->property("text").toString(), QStringLiteral("1 Line selected"));
        QCOMPARE(model->headerData(2, Qt::Horizontal).toString(), heading);
        QCOMPARE(application->video().status(), video);
        QTRY_COMPARE(item("audioStatus")->property("text").toString(), audio);
    }

    // The selection status with hidden selected Lines (G8): the " (%1
    // hidden)" part is made again after a switch too, with the same count.
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
        const QString suffix =
            QCoreApplication::translate("hikari::ui::ShellController", " (%1 hidden)").arg(1);
        QVERIFY(suffix != QStringLiteral(" (1 hidden)"));
        QTRY_VERIFY2(application->shell().selectionStatus().endsWith(suffix),
                     qPrintable(application->shell().selectionStatus()));
        QCOMPARE(item("selectionStatus")->property("text").toString(), application->shell().selectionStatus());
        application->localisation().setPseudo(app::Localisation::Pseudo::None);
        QTRY_COMPARE(application->shell().selectionStatus(), english);
    }

    // A live switch re-evaluates the closed Dialogs' implicit sizes, where
    // the Fusion Dialog and DialogButtonBox bindings report "Binding loop
    // detected for property implicitWidth" (as a text change in a closed
    // Dialog does without a switch: the Style Manager's catalog question in
    // the shell tests). Opened afterwards, each Dialog has the size it has
    // when the program starts in that language or with that font.
    void dialogsSizeAfterASwitchAsAtStart()
    {
        // One button (Ok) and two (Yes, No), in Main.qml and StyleManager.qml.
        const auto sizes = [this] {
            QList<QSizeF> out;
            for (const char *name : {"aboutDialog", "creditsDialog", "shiftConfirm", "styleMultiQuestion"}) {
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
        // The program font, live and at start.
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

    // The pseudolocalized window: after the switch no item or menu shows a
    // catalog source text any more, so every translatable string on it was
    // refreshed (strings never translated stay, as the allowance says).
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
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); // standard buttons made anew
        // Text legacy never translates either: the OK buttons' literal label
        // (OptionsDialog.cpp:1016, MappedButton(this, wxID_OK, L"OK")).
        const QSet<QString> allowed{QStringLiteral("OK")};
        QStringList stale;
        for (const auto &[o, text] : shownTexts())
            if (sources.contains(text) && !allowed.contains(text))
            {
                QString chain;
                for (QObject *p = o; p; p = p->parent())
                    chain += QString::fromLatin1(p->metaObject()->className()) + QLatin1Char('/') + p->objectName() + QLatin1Char(' ');
                stale << QStringLiteral("%1: %2").arg(chain, text);
            }
        stale.removeDuplicates();
        QVERIFY2(stale.isEmpty(), qPrintable(stale.join(u'\n')));
    }

    // Right to left: the catalogs' QT_LAYOUT_DIRECTION, the application's
    // direction and LayoutMirroring on each window's root item, inherited by
    // the menu bar, the panels and the popups.
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
        // The menu bar starts at the right.
        QTRY_VERIFY(file->mapToScene(QPointF(file->width(), 0)).x() > window->width() / 2);
        // A popup opened now is mirrored too (the overlay inherits).
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

    // The Document never follows the interface: its saved bytes, the
    // times and numbers the views show, the locale numbers are read in and
    // a right-to-left line's text are the same before and after switches.
    void documentIgnoresTheLanguage()
    {
        start();
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
                QCOMPARE(readAll(path), readAll(english));
            }
            application->localisation().setPseudo(app::Localisation::Pseudo::None);
        }
        QCOMPARE(readAll(english).mid(0, 3), QByteArray("\xEF\xBB\xBF"));
        QVERIFY(readAll(english).contains("Style: Default,Arial,20.5,"));
        QVERIFY(readAll(english).contains("1:02:03.04,1:02:04.99"));
    }

    // aegisub.gettext through the application: a script loaded in Polish
    // registers Polish names; after a switch its next lookups read the new
    // catalog and what it already holds stays until it is reloaded.
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
            out = automation.log(); // the run's log
            return true;
        };
        QString log;
        QVERIFY(runOnce(log));
        QVERIFY2(log.contains(QStringLiteral("Pasek szukania|Pasek szukania|no such key|%d element|12|")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("3 element")), qPrintable(log));
        // (aegisub.log adds no line break) the text to its NUL, and bytes
        // that are not UTF-8 give the empty string.
        QVERIFY2(log.endsWith(QStringLiteral("6|Search0")), qPrintable(log));
        application->settingsStore()->set("program.language", QStringLiteral("en"));
        QVERIFY(runOnce(log));
        QVERIFY2(log.contains(QStringLiteral("Pasek szukania|Search bar|no such key|%d element|12|")), qPrintable(log));
        QCOMPARE(status().info.name, std::string("Pasek szukania")); // registered while Polish
        QVERIFY(automation.reload(script));
        QTRY_VERIFY_WITH_TIMEOUT(status().state == application::ScriptStatus::State::Ready, 15000);
        QCOMPARE(status().info.name, std::string("Search bar"));
    }

    // PROGRAM_FONT and PROGRAM_FONT_SIZE are the application's font from the
    // start (Options.GetFont(): Tahoma, 10 points by default) and follow a
    // change at once; the font before the application comes back after it.
    void programFontAppliesLive()
    {
        const QFont before = QGuiApplication::font();
        start();
        QCOMPARE(QGuiApplication::font().families().value(0), QStringLiteral("Tahoma"));
        QCOMPARE(QGuiApplication::font().pointSize(), 10);
        auto *label = item("selectionStatus");
        QVERIFY(label);
        QTRY_COMPARE(label->property("font").value<QFont>().pointSize(), 10);
        application->settingsStore()->set("program.fontSize", 14);
        application->settingsStore()->set("program.font", QStringLiteral("DejaVu Sans"));
        QCOMPARE(QGuiApplication::font().pointSize(), 14);
        QCOMPARE(QGuiApplication::font().families().value(0), QStringLiteral("DejaVu Sans"));
        QTRY_COMPARE(label->property("font").value<QFont>().pointSize(), 14);
        // A control in a window made with the main one (the History window).
        QCOMPARE(item("historyOk")->property("font").value<QFont>().pointSize(), 14);
        QTRY_COMPARE(label->property("font").value<QFont>().families().value(0), QStringLiteral("DejaVu Sans"));
        application->settingsStore()->set("program.fontSize", 0); // legacy GetFont: 10
        QCOMPARE(QGuiApplication::font().pointSize(), 10);
        stop();
        QCOMPARE(QGuiApplication::font(), before);
    }

    // Screenshots for review (HIKARI_LANGUAGE_SHOT_DIR; skipped without it):
    // the main window with the File menu and the Options dialog in Polish,
    // the accented pseudolocale and the right-to-left one.
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
            QVERIFY(window->grabWindow().save(out + QStringLiteral("/options-%1.png").arg(QLatin1String(c.name))));
            QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
            QTRY_VERIFY(!settingsDialog()->property("visible").toBool());
        }
        application->localisation().setPseudo(app::Localisation::Pseudo::None);
    }
};

QTEST_MAIN(LanguageTest)
#include "language_tests.moc"
