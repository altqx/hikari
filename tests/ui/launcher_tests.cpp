#include "hikari/app/launcher.h"
#include "hikari/app/startup_look.h"
#include "theme.h"

#include <QtTest>

#include <string_view>

using namespace hikari;
namespace launcher = app::launcher;

namespace {

launcher::Look lookOf(ui::theme::Code code)
{
    const auto defaults = ui::theme::choiceFrom([](const char *) { return QVariant(); });
    const auto look = launcher::parseLook(app::startupLook(ui::theme::resolve(code, defaults)).toStdString());
    return look.value_or(launcher::Look{});
}

}

class LauncherTests : public QObject {
    Q_OBJECT

private slots:
    void firstStartLooksMatchTheDefaultThemes()
    {
        QVERIFY(lookOf(ui::theme::Code::Dark) == launcher::kDarkLook);
        QVERIFY(lookOf(ui::theme::Code::Light) == launcher::kLightLook);
    }

    void aDamagedLookIsRefused()
    {
        QVERIFY(!launcher::parseLook(""));
        QVERIFY(!launcher::parseLook("#171b20 #e8edf2 #a5b1bd"));
        QVERIFY(!launcher::parseLook("#171b20 #e8edf2 #a5b1bd #9cdbcg"));
        QVERIFY(!launcher::parseLook("171b20 #e8edf2 #a5b1bd #9cdbc9"));
        QVERIFY(launcher::parseLook("#171B20 #e8edf2\r\n#a5b1bd #9cdbc9\n"));
    }

    void onlyWindowStartsShowTheSplash()
    {
        using Args = std::initializer_list<std::string_view>;
        const auto starts = [](Args args) { return launcher::startsWindow(std::span(args.begin(), args.size())); };
        QVERIFY(starts(Args{}));
        QVERIFY(starts(Args{"a.ass", "b.mkv"}));
        QVERIFY(starts(Args{"--hikari-health=x", "a.ass"}));
        QVERIFY(starts(Args{"a.ass", "--hikari-apply-update"}));
        QVERIFY(!starts(Args{"--version"}));
        QVERIFY(!starts(Args{"--export-docs", "out"}));
        QVERIFY(!starts(Args{"--export-docs=out"}));
        QVERIFY(!starts(Args{"--hikari-apply-update", "1.0.0", "setup.exe", "42", "1"}));
    }
};

QTEST_GUILESS_MAIN(LauncherTests)
#include "launcher_tests.moc"
