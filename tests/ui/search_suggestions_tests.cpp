#include "search_suggestions_view.h"
#include "theme.h"

#include <QFile>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

QObject *findNamed(QQuickItem *root, const QString &name)
{
    if (!root)
        return nullptr;
    if (root->objectName() == name)
        return root;
    for (QQuickItem *child : root->childItems())
        if (QObject *found = findNamed(child, name))
            return found;
    return nullptr;
}

std::string googleSample()
{
    QFile file(QStringLiteral(HIKARI_SEARCH_SUGGESTIONS_FIXTURE));
    return file.open(QIODevice::ReadOnly) ? file.readAll().toStdString() : std::string();
}

std::string twoChips()
{
    return "<svg class=\"logo-light\" width=\"18\" height=\"18\" viewBox=\"0 0 18 18\"><circle cx=\"9\" cy=\"9\" r=\"8\" "
           "fill=\"#4285F4\"/></svg><a class=\"chip\" href=\"https://www.google.com/search?q=one&amp;x=1\">one</a>"
           "<a class=\"chip\" href=\"https://www.google.com/search?q=two\">two</a>";
}

}

class SearchSuggestionsTests : public QObject {
    Q_OBJECT

    std::unique_ptr<QQuickView> view;

    QObject *item(const QString &name) { return findNamed(view->rootObject(), name); }

    void show(const std::string &html)
    {
        view = std::make_unique<QQuickView>();
        view->setInitialProperties(ui::searchSuggestionsView(html));
        view->loadFromModule("Hikari.Ui", "SearchSuggestions");
        QVERIFY(view->rootObject());
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->resize(420, 48);
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));
    }

    void useTheme(const char *code)
    {
        ui::theme::preview({{QStringLiteral("appearance.theme"), QString::fromLatin1(code)},
                            {QStringLiteral("appearance.followSystem"), false}});
    }

private slots:
    void initTestCase() { ui::theme::chooseControlsStyle(); }
    void cleanup()
    {
        view.reset();
        ui::theme::endPreview();
    }

    void googlesSnippetShowsItsChipAndLogo()
    {
        useTheme("light");
        show(googleSample());
        auto *chip = item(QStringLiteral("searchSuggestion_0"));
        QVERIFY(chip);
        QCOMPARE(chip->property("text").toString(), QStringLiteral("current Google stock price"));
        QVERIFY(!item(QStringLiteral("searchSuggestion_1")));
        auto *logo = item(QStringLiteral("searchSuggestionsLogo"));
        QTRY_COMPARE(logo->property("status").toInt(), 1);
        QVERIFY(logo->property("source").toUrl().toString().startsWith(QStringLiteral("data:image/svg+xml;base64,")));
    }

    void clickingAChipGivesItsLinkAsSent()
    {
        show(twoChips());
        QSignalSpy activated(view->rootObject(), SIGNAL(chipActivated(QUrl)));
        auto *chip = qobject_cast<QQuickItem *>(item(QStringLiteral("searchSuggestion_1")));
        QVERIFY(chip);
        QTest::mouseClick(view.get(), Qt::LeftButton, {}, chip->mapToScene(chip->boundingRect().center()).toPoint());
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.at(0).at(0).toUrl(), QUrl(QStringLiteral("https://www.google.com/search?q=two")));
        QMetaObject::invokeMethod(item(QStringLiteral("searchSuggestion_0")), "click");
        QCOMPARE(activated.at(1).at(0).toUrl(), QUrl(QStringLiteral("https://www.google.com/search?q=one&x=1")));
    }

    void lightAndDarkThemesUseGooglesOwnColours()
    {
        useTheme("light");
        show(twoChips());
        QCOMPARE(view->rootObject()->property("color").value<QColor>(), QColor(0xfa, 0xfa, 0xfa));
        useTheme("highContrastBlack");
        QTRY_COMPARE(view->rootObject()->property("color").value<QColor>(), QColor(0x1f, 0x1f, 0x1f));
        useTheme("highContrastWhite");
        QTRY_COMPARE(view->rootObject()->property("color").value<QColor>(), QColor(0xfa, 0xfa, 0xfa));
    }

    void aSnippetWithoutChipsShowsNoChip()
    {
        QVERIFY(ui::searchSuggestionsView("<div>chips</div>").isEmpty());
        show("<div>chips</div>");
        QVERIFY(!item(QStringLiteral("searchSuggestion_0")));
    }
};

QTEST_MAIN(SearchSuggestionsTests)
#include "search_suggestions_tests.moc"
