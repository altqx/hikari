#include "command_names.h"

#include "hikari/application/hotkeys.h"
#include "hikari/application/visual_tools.h"

#include <QCoreApplication>
#include <QTranslator>
#include <QtTest>

#include <algorithm>
#include <cstring>
#include <span>

namespace {

bool extracted(std::span<const char *const> sources, std::string_view name)
{
    return std::ranges::any_of(sources, [&](const char *source) { return name == source; });
}

class BracketingTranslator : public QTranslator {
public:
    explicit BracketingTranslator(const char *context) : m_context(context) {}

    QString translate(const char *context, const char *sourceText, const char *, int) const override
    {
        return std::strcmp(context, m_context) == 0 ? QStringLiteral("[%1]").arg(QString::fromUtf8(sourceText)) : QString();
    }
    bool isEmpty() const override { return false; }

private:
    const char *m_context;
};

}

class CommandNamesTests : public QObject {
    Q_OBJECT

private slots:
    void everyHotkeyCommandNameIsExtracted()
    {
        for (const auto &[id, name] : hikari::application::hotkeyNames())
            QVERIFY2(extracted(hikari::ui::hotkeyCommandSources(), name), qPrintable(QString::fromStdString(name)));
        for (int type = 0; type < hikari::application::kHotkeyWindows; ++type) {
            const std::string_view window = hikari::application::hotkeyWindowName(type);
            QVERIFY2(extracted(hikari::ui::hotkeyWindowSources(), window), window.data());
        }
    }

    void everyVisualToolNameIsExtracted()
    {
        for (const auto &family : hikari::application::visual::families())
            QVERIFY2(extracted(hikari::ui::visualToolSources(), family.tooltip), family.tooltip.data());
    }

    void namesAreTranslatedInTheirContexts()
    {
        BracketingTranslator hotkeys("HikariSub.Hotkeys");
        QCoreApplication::installTranslator(&hotkeys);
        QCOMPARE(hikari::ui::hotkeyCommandName("Play / Pause"), QStringLiteral("[Play / Pause]"));
        QCOMPARE(hikari::ui::hotkeyWindowName(hikari::application::VideoHotkey), QStringLiteral("[Video]"));
        QCOMPARE(hikari::ui::visualToolName("Position pointer"), QStringLiteral("Position pointer"));
        QCoreApplication::removeTranslator(&hotkeys);

        BracketingTranslator tools("HikariSub.VisualTools");
        QCoreApplication::installTranslator(&tools);
        QCOMPARE(hikari::ui::visualToolName("Position pointer"), QStringLiteral("[Position pointer]"));
        QCOMPARE(hikari::ui::hotkeyCommandName("Play / Pause"), QStringLiteral("Play / Pause"));
        QCoreApplication::removeTranslator(&tools);
    }
};

QTEST_GUILESS_MAIN(CommandNamesTests)
#include "command_names_tests.moc"
