#include "command_names.h"

#include "hikari/application/edit_session.h"
#include "hikari/application/hotkeys.h"
#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_view.h"
#include "hikari/core/ass_load.h"

#include <QCoreApplication>
#include <QTranslator>
#include <QtTest>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <set>
#include <span>

namespace {

bool extracted(std::span<const char *const> sources, std::string_view name)
{
    return std::ranges::any_of(sources, [&](const char *source) { return name == source; });
}

namespace visual = hikari::application::visual;

class Host : public visual::VisualHost {
public:
    Host()
    {
        constexpr std::string_view script = "[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n"
                                            "[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,line\n";
        const auto bytes = std::as_bytes(std::span(script));
        m_session = std::make_unique<hikari::application::EditSession>(hikari::core::loadAss(bytes).document);
        const hikari::core::LineId id = m_session->document().lines().front()->id;
        m_session->setSelection({id, {id}, id, std::nullopt});
        video.setClient(1920, 1080, 0);
        video.setScript(1920, 1080);
        video.open({1920, 1080, 1, 1});
    }

    const visual::VideoView &view() const override { return video; }
    const hikari::application::EditSession *session() const override { return m_session.get(); }
    std::optional<hikari::core::LineId> activeLine() const override { return m_session->selection().active; }
    std::vector<hikari::core::LineId> batchTargets() const override { return {*activeLine()}; }
    std::expected<visual::Gesture *, hikari::application::CommandRefusal> beginGesture(std::vector<hikari::core::LineId>,
                                                                                     std::string) override
    {
        return std::unexpected(hikari::application::CommandRefusal::Invalid);
    }
    visual::Gesture *gesture() override { return nullptr; }
    std::expected<void, hikari::application::CommandRefusal> commitGesture() override { return {}; }
    void cancelGesture() override {}
    std::pair<int, int> measureLabel(std::u16string_view text) const override { return {7 * int(text.size()), 14}; }
    void toolChanged() override {}

    visual::VideoView video;

private:
    std::unique_ptr<hikari::application::EditSession> m_session;
};

bool spellsAWord(std::u16string_view text)
{
    return std::ranges::count_if(text, [](char16_t c) { return QChar(c).isLetter(); }) >= 2;
}

void collect(const visual::VisualTool &tool, const visual::VisualHost &host, std::set<std::u16string> &out)
{
    const auto add = [&](const std::u16string &text) {
        if (spellsAWord(text))
            out.insert(text);
    };
    for (const auto &o : tool.options(host)) {
        add(o.tooltip);
        std::ranges::for_each(o.sectionNames, add);
        if (o.searchable || o.choices.empty())
            continue;
        if (o.listEnds) {
            add(o.choices.front());
            add(o.choices.back());
        } else {
            std::ranges::for_each(o.choices, add);
        }
    }
    for (const auto &v : tool.values(host)) {
        add(v.label);
        add(v.unit);
    }
}

std::set<std::u16string> toolTextsThroughEveryChoice()
{
    std::set<std::u16string> out;
    Host host;
    for (const auto &family : visual::families()) {
        auto tool = visual::makeVisualTool(family.family);
        if (!tool)
            continue;
        tool->selected(host);
        collect(*tool, host, out);
        for (const auto &o : tool->options(host)) {
            if (o.kind != visual::ToolOption::Kind::Choice || o.searchable || o.listEnds)
                continue;
            for (int i = 0; i < int(o.choices.size()); ++i) {
                tool->setOption(o.name, i, host);
                collect(*tool, host, out);
            }
            tool->setOption(o.name, o.index, host);
        }
    }
    for (const auto warning : {visual::LineWarning::NotVisible, visual::LineWarning::Comment})
        out.insert(std::u16string(visual::warningText(warning)));
    return out;
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

    void everyHotkeyRefusalIsExtracted()
    {
        using hikari::application::HotkeyPress;
        namespace wxk = hikari::application::wxk;
        const std::pair<HotkeyPress, int> refused[] = {
            {{'K', false, false, false, false}, hikari::application::GlobalHotkey},
            {{'V', false, true, false, true}, hikari::application::VideoHotkey},
            {{wxk::F4, true, false, false, false}, hikari::application::VideoHotkey},
        };
        for (const auto &[press, window] : refused) {
            const auto capture = hikari::application::captureHotkey(press, window);
            QCOMPARE(capture.result, hikari::application::HotkeyCapture::Result::Refused);
            QVERIFY2(extracted(hikari::ui::hotkeyRefusalSources(), capture.message), capture.message.c_str());
        }
    }

    void everyVisualToolNameIsExtracted()
    {
        for (const auto &family : hikari::application::visual::families())
            QVERIFY2(extracted(hikari::ui::visualToolSources(), family.tooltip), family.tooltip.data());
    }

    void everyVisualToolTextIsExtracted()
    {
        QStringList missing;
        for (const std::u16string &text : toolTextsThroughEveryChoice()) {
            const QByteArray utf8 = QString::fromStdU16String(text).toUtf8();
            if (!extracted(hikari::ui::visualToolTextSources(), utf8.toStdString()))
                missing << QString::fromStdU16String(text);
        }
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join(QStringLiteral("\n---\n"))));
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
        QCOMPARE(hikari::ui::visualToolText(u"Show grid"), QStringLiteral("[Show grid]"));
        QCOMPARE(hikari::ui::hotkeyCommandName("Play / Pause"), QStringLiteral("Play / Pause"));
        QCoreApplication::removeTranslator(&tools);
    }
};

QTEST_GUILESS_MAIN(CommandNamesTests)
#include "command_names_tests.moc"
