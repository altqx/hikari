#include "hikari/application/settings.h"
#include "setting_labels.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <gtest/gtest.h>

using hikari::ui::SettingLabels;

namespace {

const QSet<QString> kComputedLabels{QStringLiteral("grammar.group."), QStringLiteral("scriptProperties.askForChange"),
                                    QStringLiteral("video.moveToActiveLine"), QStringLiteral("video.playAfterSelection")};

QSet<QString> rowsOnTheSettingsPages()
{
    QSet<QString> out;
    static const QRegularExpression setting(QStringLiteral(R"re(setting:\s*"([^"]+)")re"));
    for (const char *page : {"SettingsDialog.qml", "SettingsAiPage.qml"}) {
        QFile file(QStringLiteral(HIKARI_SOURCE_DIR "/src/ui/") + QLatin1String(page));
        if (!file.open(QIODevice::ReadOnly))
            ADD_FAILURE() << page;
        const QString text = QString::fromUtf8(file.readAll());
        for (auto it = setting.globalMatch(text); it.hasNext();)
            out.insert(it.next().captured(1));
    }
    return out;
}

}

TEST(SettingLabels, EveryRowOnTheSettingsPagesHasItsOwnLabel)
{
    const QSet<QString> rows = rowsOnTheSettingsPages();
    ASSERT_GT(rows.size(), 100);
    for (const QString &row : rows)
        EXPECT_NE(SettingLabels::of(row).isEmpty(), !kComputedLabels.contains(row)) << row.toStdString();
    EXPECT_EQ(SettingLabels::of(QStringLiteral("editor.spellchecker")), QStringLiteral("Check spelling"));
    EXPECT_EQ(SettingLabels::of(QStringLiteral("program.tabTextMaxChars")), QStringLiteral("Tab name length"));
    EXPECT_EQ(SettingLabels::of(QStringLiteral("ai.agent.dailyCapCents")), QStringLiteral("Cost cap per day"));
    EXPECT_EQ(SettingLabels::of(QStringLiteral("audio.karaokeThaiClusters")), QStringLiteral("Split Thai into character clusters"));
    EXPECT_EQ(SettingLabels::of(QStringLiteral("video.playbackPlayer")), QStringLiteral("Player"));
    EXPECT_EQ(SettingLabels::of(QStringLiteral("ai.mcp.http")), QStringLiteral("Also listen on http://127.0.0.1"));
}

TEST(SettingLabels, ASettingTheAgentCanChangeWithoutARowHasNoLabel)
{
    const QSet<QString> rows = rowsOnTheSettingsPages();
    int unlabelled = 0;
    for (const hikari::application::SettingDefinition &definition : hikari::application::settingDefinitions()) {
        const QString id = QString::fromUtf8(definition.id.data(), qsizetype(definition.id.size()));
        if (definition.disposition != hikari::application::SettingDisposition::Mapped || rows.contains(id))
            continue;
        EXPECT_TRUE(SettingLabels::of(id).isEmpty()) << definition.id;
        ++unlabelled;
    }
    EXPECT_GT(unlabelled, 0);
    EXPECT_TRUE(SettingLabels::of(QStringLiteral("audio.autoScroll")).isEmpty());
}

TEST(SettingLabels, EachLabelledSettingIsARowOnTheSettingsPages)
{
    const QSet<QString> rows = rowsOnTheSettingsPages();
    for (const QString &setting : SettingLabels::labelled())
        EXPECT_TRUE(rows.contains(setting)) << setting.toStdString();
}
