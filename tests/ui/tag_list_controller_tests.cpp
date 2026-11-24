#include "tag_list_controller.h"

#include <QtTest>

using hikari::ui::ProviderRow;
using hikari::ui::TagListController;
namespace taglist = hikari::core::taglist;

namespace {

struct Asked {
    int role = 0;
    QString text;
    int caret = 0;
    QString trigger;
    TagListController::Rows answer;
};

struct Fake {
    QStringList triggers;
    std::vector<Asked> asked;
    void wire(TagListController &c)
    {
        c.setProviders([this](int) { return triggers; },
                       [this](int role, const QString &text, int caret, const QString &trigger, TagListController::Rows answer) {
                           asked.push_back({role, text, caret, trigger, std::move(answer)});
                       });
    }
};

QStringList coreRows(const taglist::Completion &completion)
{
    QStringList out;
    if (const auto *list = completion.list())
        for (const auto entry : list->shownEntries())
            out << QString::fromStdU16String(std::u16string(taglist::entries()[entry].tag));
    return out;
}

}

class TagListControllerTests : public QObject {
    Q_OBJECT

private slots:
    void withoutAnswersTheTagListIsTheCoresOwn()
    {
        for (const bool wired : {false, true}) {
            TagListController c;
            Fake fake;
            fake.triggers = {QStringLiteral("\\")};
            if (wired)
                fake.wire(c);
            taglist::Completion core;
            const QString text = QStringLiteral("{\\b");
            QVERIFY(c.typed(text.left(2), 2, QStringLiteral("\\")));
            QVERIFY(core.typed(u"{\\", 2, u'\\', 0));
            QVERIFY(c.typed(text, 3, QStringLiteral("b")));
            QVERIFY(core.typed(u"{\\b", 3, u'b', 0));
            for (auto &a : fake.asked)
                a.answer({});
            QCOMPARE(c.rows(), coreRows(core));
            QCOMPARE(c.tagRows(), int(core.list()->count()));
            QVERIFY(c.move(-1));
            QVERIFY(core.move(-1));
            QCOMPARE(c.selection(), core.list()->selection());
            QCOMPARE(c.scrollPosition(), core.list()->scrollPosition());
            const QVariantMap put = c.put(text, 3);
            const auto corePut = core.put(u"{\\b", 3);
            QVERIFY(corePut);
            QCOMPARE(put.value(QStringLiteral("text")).toString(), QString::fromStdU16String(corePut->text));
            QCOMPARE(put.value(QStringLiteral("caret")).toInt(), int(corePut->caret));
            QVERIFY(!c.open());
            QCOMPARE(fake.asked.size(), std::size_t(wired ? 1 : 0));
        }
    }

    void providerRowsFollowTheTagRowsAndReplaceTheirRange()
    {
        TagListController c;
        Fake fake;
        fake.triggers = {QStringLiteral("\\")};
        fake.wire(c);
        QVERIFY(c.typed(QStringLiteral("{\\"), 2, QStringLiteral("\\"), 1));
        QCOMPARE(fake.asked.size(), std::size_t(1));
        QCOMPARE(fake.asked[0].role, 1);
        QCOMPARE(fake.asked[0].trigger, QStringLiteral("\\"));
        const int tags = c.tagRows();
        QVERIFY(tags > 0);
        fake.asked[0].answer({{QStringLiteral("\\fad"), QStringLiteral("\\fad(${1:200},${2:200})"), QStringLiteral("Fade"),
                               QStringLiteral("Tags"), 1, 2, true},
                              {QStringLiteral("\\fn"), {}, {}, QStringLiteral("Tags"), 1, 2, false}});
        QCOMPARE(c.rows().size(), tags + 2);
        QCOMPARE(c.rows().at(tags), QStringLiteral("\\fad"));
        QCOMPARE(c.providerDetails(), (QStringList{QStringLiteral("Fade · Tags"), QStringLiteral("Tags")}));
        QCOMPARE(c.selection(), 0);
        QVERIFY(c.move(-1));
        QCOMPARE(c.selection(), tags + 1);
        QVERIFY(c.move(-1));
        QCOMPARE(c.selection(), tags);
        QVERIFY(c.scrollPosition() <= tags && tags < c.scrollPosition() + taglist::kMaxVisible);
        QVERIFY(c.selectedAnnouncement().startsWith(QStringLiteral("\\fad, Fade · Tags, ")));
        const QVariantMap put = c.put(QStringLiteral("{\\}"), 2);
        QCOMPARE(put.value(QStringLiteral("text")).toString(), QStringLiteral("{\\fad(200,200)}"));
        QCOMPARE(put.value(QStringLiteral("caret")).toInt(), 6);
        QVERIFY(!c.open());
    }

    void providerRowsAloneOpenThePopupWithTheFirstChosen()
    {
        TagListController c;
        Fake fake;
        fake.triggers = {QStringLiteral(":")};
        fake.wire(c);
        QVERIFY(!c.typed(QStringLiteral("a:"), 2, QStringLiteral(":")));
        QVERIFY(!c.popupShown());
        fake.asked[0].answer({{QStringLiteral("smile"), QStringLiteral("🙂"), {}, QStringLiteral("Emoji"), 0, 2, false}});
        QVERIFY(c.open());
        QVERIFY(c.popupShown());
        QCOMPARE(c.tagRows(), 0);
        QCOMPARE(c.selection(), 0);
        QVERIFY(c.pointerAt(0));
        const QVariantMap put = c.put(QStringLiteral("a:"), 2);
        QCOMPARE(put.value(QStringLiteral("text")).toString(), QStringLiteral("🙂"));
        QCOMPARE(put.value(QStringLiteral("caret")).toInt(), 2);
    }

    void typingWhileProviderRowsShowAsksAgainAndALateAnswerIsDropped()
    {
        TagListController c;
        Fake fake;
        fake.triggers = {QStringLiteral(":")};
        fake.wire(c);
        c.typed(QStringLiteral(":"), 1, QStringLiteral(":"));
        fake.asked[0].answer({{QStringLiteral("smile"), {}, {}, {}, 0, 1, false}});
        c.typed(QStringLiteral(":s"), 2, QStringLiteral("s"));
        QCOMPARE(fake.asked.size(), std::size_t(2));
        QCOMPARE(fake.asked[1].trigger, QString());
        c.close();
        fake.asked[1].answer({{QStringLiteral("late"), {}, {}, {}, 0, 2, false}});
        QVERIFY(!c.open());
        QVERIFY(c.rows().isEmpty());
    }

    void triggerOpensTheTagListOnATagWordAndAsksEveryProvider()
    {
        TagListController c;
        Fake fake;
        fake.wire(c);
        QVERIFY(c.trigger(QStringLiteral("{\\fa"), 4));
        QVERIFY(c.popupShown());
        for (const QString &row : c.rows())
            QVERIFY2(row.startsWith(QStringLiteral("fa")), qPrintable(row));
        QCOMPARE(fake.asked.size(), std::size_t(1));
        QCOMPARE(fake.asked[0].trigger, QString());
        c.close();
        QVERIFY(c.trigger(QStringLiteral("word"), 4));
        QCOMPARE(c.tagRows(), 0);
        QCOMPARE(fake.asked.size(), std::size_t(2));
    }

    void snippetsWithoutAFirstStopPutTheCaretAtTheEndOrTheirZeroStop()
    {
        TagListController c;
        Fake fake;
        fake.triggers = {QStringLiteral("(")};
        fake.wire(c);
        c.typed(QStringLiteral("("), 1, QStringLiteral("("));
        fake.asked[0].answer({{QStringLiteral("a"), QStringLiteral("x$0y\\$1"), {}, {}, 0, 1, true}});
        QVariantMap put = c.put(QStringLiteral("("), 1);
        QCOMPARE(put.value(QStringLiteral("text")).toString(), QStringLiteral("xy$1"));
        QCOMPARE(put.value(QStringLiteral("caret")).toInt(), 1);
        c.typed(QStringLiteral("("), 1, QStringLiteral("("));
        fake.asked[1].answer({{QStringLiteral("b"), QStringLiteral("plain"), {}, {}, 0, 1, true}});
        put = c.put(QStringLiteral("("), 1);
        QCOMPARE(put.value(QStringLiteral("caret")).toInt(), 5);
    }

    void aHoverAnswersTheLatestRequestAndClears()
    {
        TagListController c;
        bool hovers = false;
        std::vector<std::pair<int, TagListController::HoverAnswer>> asked;
        c.setHover([&](int) { return hovers; },
                   [&](int, const QString &, int position, TagListController::HoverAnswer answer) {
                       asked.emplace_back(position, std::move(answer));
                   });
        c.hoverAt(0, QStringLiteral("{\\fad}"), 3);
        QVERIFY(asked.empty());
        hovers = true;
        c.hoverAt(1, QStringLiteral("{\\fad}"), 3);
        c.hoverAt(1, QStringLiteral("{\\fad}"), 4);
        QCOMPARE(asked.size(), std::size_t(2));
        asked[0].second(hikari::ui::ProviderHover{QStringLiteral("old"), {}, 0, 0});
        QVERIFY(c.hover().isEmpty());
        asked[1].second(hikari::ui::ProviderHover{QStringLiteral("Fade"), QStringLiteral("Tags"), 1, 5});
        QCOMPARE(c.hover(), (QVariantMap{{QStringLiteral("role"), 1}, {QStringLiteral("at"), 4}, {QStringLiteral("text"), QStringLiteral("Fade")},
                                         {QStringLiteral("source"), QStringLiteral("Tags")}, {QStringLiteral("from"), 1}, {QStringLiteral("to"), 5}}));
        c.clearHover();
        QVERIFY(c.hover().isEmpty());
        c.hoverAt(1, QStringLiteral("x"), 0);
        asked.back().second(std::nullopt);
        QVERIFY(c.hover().isEmpty());
    }
};

QTEST_MAIN(TagListControllerTests)
#include "tag_list_controller_tests.moc"
