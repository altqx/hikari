#include "hikari/app/plugin_popups.h"
#include "quick_pick_model.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
using api::Json;
using app::PluginPopup;
using app::PluginPopups;

namespace {

struct Delivered {
    std::string plugin, event;
    Json data;
    std::string coalesceKey;
};

struct Answer {
    std::optional<Json> json;
    void operator()(const std::string &text) { json = *Json::parse(text); }
    std::string error() const
    {
        const Json *e = json ? json->find("error") : nullptr;
        return e ? e->find("code")->asString() : std::string();
    }
    std::string reason() const
    {
        const Json *data = json->find("error")->find("data");
        return data ? data->find("reason")->asString() : std::string();
    }
    const Json &result() const { return *json->find("result"); }
};

Json params(const char *text)
{
    return *Json::parse(text);
}

std::vector<std::string> strings(const Json &array)
{
    std::vector<std::string> out;
    for (const Json &v : array.asArray())
        out.push_back(v.asString());
    return out;
}

}

class PluginPopupsTests : public QObject {
    Q_OBJECT

    std::vector<Delivered> m_delivered;
    bool m_userAsked = true;

    std::unique_ptr<PluginPopups> popups()
    {
        m_delivered.clear();
        m_userAsked = true;
        auto p = std::make_unique<PluginPopups>([this](const std::string &plugin, const std::string &event,
                                                       const std::string &json, const std::string &key) {
            m_delivered.push_back({plugin, event, *Json::parse(json), key});
            return true;
        });
        p->setUserAsked([this](const std::string &) { return m_userAsked; });
        return p;
    }
    static Answer *serve(PluginPopups &p, const char *operation, const char *spec, const std::string &plugin = "tidy")
    {
        auto *answer = new Answer;
        p.serve(plugin, QStringLiteral("Tidy"), operation, params(spec), [answer](std::string text) { (*answer)(text); });
        return answer;
    }

private slots:
    void anAskedPickAnswersThePickedIdsAndCloses()
    {
        auto p = popups();
        QSignalSpy shownChanged(p.get(), &PluginPopups::shownChanged);
        std::unique_ptr<Answer> answer(serve(*p, "quick_pick.ask", R"({"title": "Style", "value": "ma", "items": [
            {"id": "main", "label": "Main", "always_show": true},
            {"kind": "separator", "label": "Signs"},
            {"id": "sign", "label": "Sign", "buttons": [{"id": "edit", "icon": "edit", "tooltip": "Edit"}]}]})"));
        QVERIFY(!answer->json);
        PluginPopup *shown = p->shown();
        QVERIFY(shown);
        QCOMPARE(shownChanged.count(), 1);
        QCOMPARE(shown->kind(), QStringLiteral("pick"));
        QCOMPARE(shown->pluginName(), QStringLiteral("Tidy"));
        QVERIFY(!shown->live());
        QCOMPARE(shown->title(), QStringLiteral("Style"));
        QCOMPARE(shown->value(), QStringLiteral("ma"));
        QCOMPARE(shown->items().size(), 3);
        QVERIFY(shown->items().first().toMap().value(QStringLiteral("alwaysShow")).toBool());

        shown->edit(QStringLiteral("mai"));
        shown->activate(QStringLiteral("main"));
        QVERIFY(m_delivered.empty());
        shown->accept({QStringLiteral("main")});
        QVERIFY(!answer->json->find("error"));
        QCOMPARE(answer->result().find("cancelled")->asBool(), false);
        QCOMPARE(strings(*answer->result().find("items")), std::vector<std::string>{"main"});
        QVERIFY(!p->shown());
        QVERIFY(m_delivered.empty());
    }

    void anAskedPickAnswersBackAButtonOrACancel()
    {
        auto p = popups();
        std::unique_ptr<Answer> back(serve(*p, "quick_pick.ask", R"({"items": [], "back": true, "step": {"current": 2, "total": 3}})"));
        QCOMPARE(p->shown()->step(), QStringLiteral("2 / 3"));
        p->shown()->goBack();
        QCOMPARE(back->result().find("cancelled")->asBool(), true);
        QCOMPARE(back->result().find("back")->asBool(), true);

        std::unique_ptr<Answer> noBack(serve(*p, "quick_pick.ask", R"({"items": {}})"));
        p->shown()->goBack();
        QVERIFY(!noBack->json);
        p->shown()->press(QStringLiteral("refresh"));
        QCOMPARE(noBack->result().find("button")->asString(), std::string("refresh"));

        std::unique_ptr<Answer> dismissed(serve(*p, "input_box.ask", R"({})"));
        p->shown()->dismiss();
        QCOMPARE(dismissed->result().find("cancelled")->asBool(), true);
        QVERIFY(!dismissed->result().find("value"));
        QVERIFY(m_delivered.empty());
    }

    void badSpecsAreInvalidParams_data()
    {
        QTest::addColumn<QString>("operation");
        QTest::addColumn<QString>("spec");
        QTest::newRow("ask without items") << "quick_pick.ask" << R"({"title": "x"})";
        QTest::newRow("unknown field") << "quick_pick.open" << R"({"colour": "red"})";
        QTest::newRow("same id twice") << "quick_pick.ask" << R"({"items": [{"id": "a", "label": "A"}, {"id": "a", "label": "B"}]})";
        QTest::newRow("item without id") << "quick_pick.ask" << R"({"items": [{"label": "A"}]})";
        QTest::newRow("separator with id") << "quick_pick.ask" << R"({"items": [{"kind": "separator", "id": "s", "label": "S"}]})";
        QTest::newRow("bad pattern") << "input_box.ask" << R"({"validation": {"pattern": "a{2,1}", "message": "No"}})";
        QTest::newRow("selection backwards") << "input_box.ask" << R"({"value": "abc", "selection": {"from": 2, "to": 1}})";
        QTest::newRow("bad severity") << "input_box.update" << R"({"box": "b1", "message": {"severity": "loud", "text": "x"}})";
    }
    void badSpecsAreInvalidParams()
    {
        QFETCH(QString, operation);
        QFETCH(QString, spec);
        auto p = popups();
        std::unique_ptr<Answer> answer(serve(*p, operation.toUtf8().constData(), spec.toUtf8().constData()));
        QCOMPARE(answer->error(), std::string("invalid_params"));
        QVERIFY(!p->shown());
    }

    void onlyAPluginAnsweringTheUserMayOpenOne()
    {
        auto p = popups();
        m_userAsked = false;
        for (const char *operation : {"quick_pick.ask", "quick_pick.open", "input_box.ask", "input_box.open"}) {
            const bool pick = std::string_view(operation).starts_with("quick_pick");
            std::unique_ptr<Answer> answer(serve(*p, operation, pick ? R"({"items": []})" : R"({})"));
            QCOMPARE(answer->error(), std::string("refused"));
            QCOMPARE(answer->reason(), std::string("not_user_initiated"));
        }
        QVERIFY(!p->shown());
    }

    void aLivePickSendsEventsAndChangesUntilClosed()
    {
        auto p = popups();
        std::unique_ptr<Answer> opened(serve(*p, "quick_pick.open", R"({"filter": "none", "busy": true, "back": true})"));
        QCOMPARE(opened->result().find("pick")->asString(), std::string("q1"));
        PluginPopup *shown = p->shown();
        QVERIFY(shown->live());
        QVERIFY(!shown->filtering());
        QVERIFY(shown->busy());

        QSignalSpy activeSet(shown, &PluginPopup::activeSet);
        std::unique_ptr<Answer> updated(serve(*p, "quick_pick.update",
                                              R"({"pick": "q1", "items": [{"id": "a", "label": "A"}], "busy": false, "active": "a"})"));
        QCOMPARE(updated->result().find("pick")->asString(), std::string("q1"));
        QCOMPARE(shown->items().size(), 1);
        QVERIFY(!shown->busy());
        QCOMPARE(shown->active(), QStringLiteral("a"));
        QCOMPARE(activeSet.count(), 1);

        shown->edit(QStringLiteral("gl"));
        shown->activate(QStringLiteral("a"));
        shown->accept({QStringLiteral("a")});
        shown->press(QStringLiteral("edit"), QStringLiteral("a"));
        shown->goBack();
        QCOMPARE(m_delivered.size(), 5u);
        for (const Delivered &d : m_delivered) {
            QCOMPARE(d.plugin, std::string("tidy"));
            QCOMPARE(d.event, std::string("quick_pick:q1"));
            QCOMPARE(d.data.find("pick")->asString(), std::string("q1"));
        }
        QCOMPARE(m_delivered[0].data.find("kind")->asString(), std::string("value"));
        QCOMPARE(m_delivered[0].data.find("value")->asString(), std::string("gl"));
        QCOMPARE(m_delivered[0].coalesceKey, std::string("quick_pick:q1:value"));
        QCOMPARE(m_delivered[1].data.find("item")->asString(), std::string("a"));
        QCOMPARE(m_delivered[1].coalesceKey, std::string());
        QCOMPARE(strings(*m_delivered[2].data.find("items")), std::vector<std::string>{"a"});
        QCOMPARE(m_delivered[3].data.find("button")->asString(), std::string("edit"));
        QCOMPARE(m_delivered[3].data.find("item")->asString(), std::string("a"));
        QCOMPARE(m_delivered[4].data.find("kind")->asString(), std::string("back"));
        QCOMPARE(p->shown(), shown);

        std::unique_ptr<Answer> elsewhere(serve(*p, "quick_pick.close", R"({"pick": "q1"})", "other"));
        QCOMPARE(elsewhere->error(), std::string("not_found"));
        std::unique_ptr<Answer> closed(serve(*p, "quick_pick.close", R"({"pick": "q1"})"));
        QCOMPARE(closed->result().find("pick")->asString(), std::string("q1"));
        QVERIFY(!p->shown());
        QCOMPARE(m_delivered.back().data.find("kind")->asString(), std::string("hide"));
        std::unique_ptr<Answer> late(serve(*p, "quick_pick.update", R"({"pick": "q1", "busy": true})"));
        QCOMPARE(late->error(), std::string("not_found"));
    }

    void anInputBoxBlocksEnterOnAnErrorAndWaitsWhileBusy()
    {
        auto p = popups();
        std::unique_ptr<Answer> asked(serve(*p, "input_box.ask",
                                            R"({"value": "ab", "password": true, "validation": {"pattern": "[a-z]+", "message": "Letters, 3 or more", "min_length": 3}})"));
        PluginPopup *shown = p->shown();
        QVERIFY(shown->password());
        QCOMPARE(shown->selectionFrom(), -1);
        QCOMPARE(shown->severity(), QStringLiteral("error"));
        QCOMPARE(shown->message(), QStringLiteral("Letters, 3 or more"));
        shown->accept();
        QVERIFY(!asked->json);
        QSignalSpy messageChanged(shown, &PluginPopup::messageChanged);
        shown->edit(QStringLiteral("ab1"));
        QCOMPARE(shown->severity(), QStringLiteral("error"));
        shown->edit(QStringLiteral("abc"));
        QCOMPARE(messageChanged.count(), 2);
        QCOMPARE(shown->severity(), QString());
        shown->accept();
        QCOMPARE(asked->result().find("value")->asString(), std::string("abc"));
        QVERIFY(m_delivered.empty());

        std::unique_ptr<Answer> opened(serve(*p, "input_box.open", R"({"value": "Bo", "selection": {"from": 0, "to": 1}})"));
        QCOMPARE(opened->result().find("box")->asString(), std::string("b2"));
        shown = p->shown();
        QCOMPARE(shown->selectionTo(), 1);
        std::unique_ptr<Answer> warned(serve(*p, "input_box.update",
                                             R"({"box": "b2", "message": {"severity": "error", "text": "Taken"}, "busy": true})"));
        QCOMPARE(shown->message(), QStringLiteral("Taken"));
        shown->accept();
        QVERIFY(m_delivered.empty());
        std::unique_ptr<Answer> cleared(serve(*p, "input_box.update", R"({"box": "b2", "message": null})"));
        QCOMPARE(shown->severity(), QString());
        shown->accept();
        QVERIFY(m_delivered.empty());
        std::unique_ptr<Answer> done(serve(*p, "input_box.update", R"({"box": "b2", "busy": false})"));
        QCOMPARE(m_delivered.size(), 1u);
        QCOMPARE(m_delivered[0].event, std::string("input_box:b2"));
        QCOMPARE(m_delivered[0].data.find("kind")->asString(), std::string("accept"));
        QCOMPARE(m_delivered[0].data.find("value")->asString(), std::string("Bo"));
        QCOMPARE(p->shown(), shown);
    }

    void oneShowsAtATimeAndAStoppedPluginsCloses()
    {
        auto p = popups();
        std::unique_ptr<Answer> first(serve(*p, "quick_pick.ask", R"({"items": []})", "left"));
        std::unique_ptr<Answer> second(serve(*p, "quick_pick.open", R"({})", "right"));
        QCOMPARE(first->result().find("cancelled")->asBool(), true);
        QCOMPARE(p->shown()->pluginName(), QStringLiteral("Tidy"));

        std::unique_ptr<Answer> third(serve(*p, "input_box.open", R"({})", "left"));
        QCOMPARE(m_delivered.size(), 1u);
        QCOMPARE(m_delivered[0].plugin, std::string("right"));
        QCOMPARE(m_delivered[0].data.find("kind")->asString(), std::string("hide"));

        p->closeOf("right");
        QVERIFY(p->shown());
        p->closeOf("left");
        QVERIFY(!p->shown());
        QCOMPARE(m_delivered.size(), 1u);

        std::unique_ptr<Answer> asked(serve(*p, "input_box.ask", R"({})", "left"));
        p->closeOf("left");
        QCOMPARE(asked->result().find("cancelled")->asBool(), true);

        std::unique_ptr<Answer> live(serve(*p, "quick_pick.open", R"({})", "left"));
        p->dismissShown();
        QCOMPARE(m_delivered.back().data.find("kind")->asString(), std::string("hide"));
    }

    void theHostShowsThePopupAndPassesTheUsersKeysOn()
    {
        auto p = popups();
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Hikari.Ui
            Window {
                width: 800; height: 600; visible: true
                property alias host: host
                PluginQuickInput { id: host; anchors.fill: parent }
            })",
                          QUrl());
        std::unique_ptr<QObject> root(component.createWithInitialProperties({}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        auto *host = root->property("host").value<QObject *>();
        host->setProperty("popups", QVariant::fromValue(p.get()));
        auto *pick = host->property("pick").value<QObject *>();
        auto *box = host->property("box").value<QObject *>();

        std::unique_ptr<Answer> asked(serve(*p, "quick_pick.ask", R"({"title": "Style", "many": true, "items": [
            {"id": "a", "label": "Alpha", "picked": true}, {"id": "b", "label": "Beta"}]})"));
        QTRY_VERIFY(pick->property("visible").toBool());
        QCOMPARE(pick->property("owner").toString(), QStringLiteral("Tidy"));
        auto *model = pick->property("model").value<ui::QuickPickModel *>();
        QCOMPARE(model->count(), 2);
        QCOMPARE(model->pickedCount(), 1);
        QTest::keyClick(window, Qt::Key_Tab);
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(model->pickedCount(), 2);
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(strings(*asked->result().find("items")), (std::vector<std::string>{"a", "b"}));
        QTRY_VERIFY(!pick->property("visible").toBool());

        std::unique_ptr<Answer> named(serve(*p, "input_box.ask", R"({"prompt": "A name", "validation": {"min_length": 2, "message": "Too short"}})"));
        QTRY_VERIFY(box->property("visible").toBool());
        QCOMPARE(box->property("message").toString(), QStringLiteral("Too short"));
        QTest::keyClick(window, 'B');
        QTest::keyClick(window, Qt::Key_Return);
        QVERIFY(!named->json);
        QTest::keyClick(window, 'o');
        QCOMPARE(box->property("message").toString(), QString());
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(named->result().find("value")->asString(), std::string("Bo"));
        QTRY_VERIFY(!box->property("visible").toBool());

        std::unique_ptr<Answer> escaped(serve(*p, "quick_pick.ask", R"({"items": [], "back": true})"));
        QTRY_VERIFY(pick->property("visible").toBool());
        QTest::keyClick(window, Qt::Key_Left, Qt::AltModifier);
        QCOMPARE(escaped->result().find("back")->asBool(), true);
        std::unique_ptr<Answer> cancelled(serve(*p, "input_box.ask", R"({})"));
        QTRY_VERIFY(box->property("visible").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(cancelled->result().find("cancelled")->asBool(), true);
        QVERIFY(!p->shown());
    }
};

QTEST_MAIN(PluginPopupsTests)
#include "plugin_popups_tests.moc"
