// Spix clicks a real QML button and reads the resulting text back. Exits 0 only
// when the observed text equals HIKARI_UI_EXPECTED and Spix reported no errors;
// on failure it keeps a screenshot under HIKARI_TEST_ARTIFACT_DIR.
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <Spix/QtQmlBot.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>

using namespace std::chrono_literals;

static const char *kScene = R"(
import QtQuick
import QtQuick.Controls
ApplicationWindow {
    objectName: "mainWindow"
    width: 240; height: 120; visible: true
    Column {
        Button { objectName: "button"; text: "Press"; onClicked: result.text = "clicked" }
        Text { id: result; objectName: "result"; text: "idle" }
    }
}
)";

class Control : public spix::TestServer
{
public:
    std::atomic<bool> passed{false};

protected:
    void executeTest() override
    {
        if (waitForItem("mainWindow/button", 5s)) {
            mouseClick("mainWindow/button");
            wait(300ms);
            const std::string text = getStringProperty("mainWindow/result", "text");
            std::printf("observed=%s expected=%s\n", text.c_str(), HIKARI_UI_EXPECTED);
            passed = text == HIKARI_UI_EXPECTED;
        }
        for (const auto &e : getErrors()) {
            std::printf("spix error: %s\n", e.c_str());
            passed = false;
        }
        if (!passed) {
            QDir().mkpath(HIKARI_TEST_ARTIFACT_DIR);
            takeScreenshot("mainWindow", std::string(HIKARI_TEST_ARTIFACT_DIR) + "/ui-control-failure.png");
        }
        quit();
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.loadData(kScene);
    if (engine.rootObjects().isEmpty())
        return 2;
    Control test;
    spix::QtQmlBot bot;
    bot.runTestServer(test);
    app.exec();
    return test.passed ? 0 : 1;
}
