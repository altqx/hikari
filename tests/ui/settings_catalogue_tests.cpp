#include "hikari/application/settings.h"
#include "settings_catalogue.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <gtest/gtest.h>

using hikari::ui::SettingsCatalogue;

namespace {

QSet<QString> rowsOnTheSettingsPages()
{
    QSet<QString> out;
    static const QRegularExpression setting(QStringLiteral(R"re(setting:\s*"([^"]+)"(?!\s*\+))re"));
    for (const char *page : {"SettingsDialog.qml", "SettingsAiPage.qml", "ProjectSettingsPages.qml"}) {
        QFile file(QStringLiteral(HIKARI_SOURCE_DIR "/src/ui/") + QLatin1String(page));
        if (!file.open(QIODevice::ReadOnly))
            ADD_FAILURE() << page;
        const QString text = QString::fromUtf8(file.readAll());
        for (auto it = setting.globalMatch(text); it.hasNext();)
            out.insert(it.next().captured(1));
    }
    return out;
}

QStringList ids(const QVariantMap &found)
{
    return found.value(QStringLiteral("settings")).toMap().keys();
}

}

TEST(SettingsCatalogue, EveryRowOnTheSettingsPagesIsCatalogued)
{
    const QSet<QString> rows = rowsOnTheSettingsPages();
    ASSERT_GT(rows.size(), 100);
    for (const QString &row : rows)
        if (!row.startsWith(QLatin1String("grammar.group.")))
            EXPECT_FALSE(SettingsCatalogue::label(row).isEmpty()) << row.toStdString();
    EXPECT_EQ(SettingsCatalogue::label(QStringLiteral("editor.spellchecker")), QStringLiteral("Check spelling"));
    EXPECT_EQ(SettingsCatalogue::label(QStringLiteral("program.tabTextMaxChars")), QStringLiteral("Tab name length"));
    EXPECT_EQ(SettingsCatalogue::label(QStringLiteral("ai.agent.dailyCapCents")), QStringLiteral("Cost cap per day"));
    EXPECT_EQ(SettingsCatalogue::label(QStringLiteral("video.playbackPlayer")), QStringLiteral("Player"));
    EXPECT_EQ(SettingsCatalogue::description(QStringLiteral("notices.toasts")), QStringLiteral("Off: notices go to the Log only."));
    EXPECT_EQ(SettingsCatalogue::sectionOf(QStringLiteral("grid.autoHideColumns")), QStringLiteral("grid.columns"));
    EXPECT_EQ(SettingsCatalogue::pageOf(QStringLiteral("grid.autoHideColumns")), QStringLiteral("grid"));
    EXPECT_TRUE(SettingsCatalogue::label(QStringLiteral("audio.autoScroll")).isEmpty());
}

TEST(SettingsCatalogue, EachCataloguedSettingIsOneTheAppKeepsInOneKnownSection)
{
    QSet<QString> sections;
    for (const QVariant &page : SettingsCatalogue::pages())
        for (const QVariant &section : page.toMap().value(QStringLiteral("sections")).toList())
            sections.insert(section.toMap().value(QStringLiteral("id")).toString());
    QSet<QString> seen;
    for (const QString &setting : SettingsCatalogue::settings()) {
        const auto *definition = hikari::application::findSetting(setting.toStdString());
        ASSERT_NE(definition, nullptr) << setting.toStdString();
        EXPECT_NE(definition->disposition, hikari::application::SettingDisposition::Excluded) << setting.toStdString();
        EXPECT_TRUE(sections.contains(SettingsCatalogue::sectionOf(setting))) << setting.toStdString();
        EXPECT_FALSE(seen.contains(setting)) << setting.toStdString();
        seen.insert(setting);
    }
}

TEST(SettingsCatalogue, ThePagesKeepTheirOrderAndTheProjectTabShowsOnlyProjectSections)
{
    const QVariantList pages = SettingsCatalogue::pages();
    ASSERT_EQ(pages.size(), 14);
    EXPECT_EQ(pages.front().toMap().value(QStringLiteral("id")), QStringLiteral("general"));
    EXPECT_EQ(pages.back().toMap().value(QStringLiteral("id")), QStringLiteral("updates"));
    QStringList project;
    for (const QVariant &page : pages)
        for (const QVariant &section : page.toMap().value(QStringLiteral("sections")).toList())
            if (section.toMap().value(QStringLiteral("project")).toBool())
                project << section.toMap().value(QStringLiteral("id")).toString();
    EXPECT_EQ(project, (QStringList{QStringLiteral("lineEditor.translationMode"), QStringLiteral("spelling.spelling"),
                                    QStringLiteral("spelling.grammar"), QStringLiteral("spelling.ruleGroups"),
                                    QStringLiteral("conversion.export"), QStringLiteral("subtitleProperties.credits"),
                                    QStringLiteral("ai.models"), QStringLiteral("ai.privacy"), QStringLiteral("ai.keys"),
                                    QStringLiteral("updates.discord")}));
}

TEST(SettingsCatalogue, SearchMatchesLabelsDescriptionsIdsAndSectionNames)
{
    EXPECT_TRUE(ids(SettingsCatalogue::search(QStringLiteral("spelling"))).contains(QStringLiteral("editor.spellchecker")));
    EXPECT_EQ(ids(SettingsCatalogue::search(QStringLiteral("grid.autoHideColumns"))), QStringList{QStringLiteral("grid.autoHideColumns")});
    EXPECT_TRUE(ids(SettingsCatalogue::search(QStringLiteral("log only"))).contains(QStringLiteral("notices.toasts")));
    const QVariantMap cache = SettingsCatalogue::search(QStringLiteral("CACHE files"));
    EXPECT_TRUE(ids(cache).contains(QStringLiteral("audio.cacheFilesLimit")));
    EXPECT_EQ(cache.value(QStringLiteral("pages")).toMap().value(QStringLiteral("audio")).toInt(), 1);
    const QVariantMap accent = SettingsCatalogue::search(QStringLiteral("accent"));
    EXPECT_EQ(accent.value(QStringLiteral("sections")).toMap().value(QStringLiteral("appearance.accentColour")).toInt(), 1);
    EXPECT_EQ(SettingsCatalogue::search(QStringLiteral("kara zzz")).value(QStringLiteral("count")).toInt(), 0);
}

TEST(SettingsCatalogue, ModifiedAndPluginTokensAndTheProjectTabNarrowTheResults)
{
    const QStringList changed{QStringLiteral("grid.autoHideColumns"), QStringLiteral("editor.spellchecker")};
    EXPECT_EQ(ids(SettingsCatalogue::search(QStringLiteral("@modified"), {}, changed)), changed.mid(1) + changed.mid(0, 1));
    EXPECT_EQ(ids(SettingsCatalogue::search(QStringLiteral("@modified grid"), {}, changed)),
              QStringList{QStringLiteral("grid.autoHideColumns")});
    EXPECT_EQ(ids(SettingsCatalogue::search(QStringLiteral("@modified"), {}, changed, true)),
              QStringList{QStringLiteral("editor.spellchecker")});
    EXPECT_FALSE(ids(SettingsCatalogue::search(QStringLiteral("show"), {}, {}, true)).contains(QStringLiteral("audio.drawKeyframes")));

    const QVariantList plugins{
        QVariantMap{{QStringLiteral("setting"), QStringLiteral("plugin.line-tools.width")}, {QStringLiteral("plugin"), QStringLiteral("line-tools")},
                    {QStringLiteral("pluginName"), QStringLiteral("Line tools")}, {QStringLiteral("label"), QStringLiteral("Width")},
                    {QStringLiteral("keywords"), QStringList{QStringLiteral("columns")}}, {QStringLiteral("projectable"), true}},
        QVariantMap{{QStringLiteral("setting"), QStringLiteral("plugin.other.width")}, {QStringLiteral("plugin"), QStringLiteral("other")},
                    {QStringLiteral("pluginName"), QStringLiteral("Other")}, {QStringLiteral("label"), QStringLiteral("Width")}}};
    const QVariantMap one = SettingsCatalogue::search(QStringLiteral("@plugin:line-tools"), plugins);
    EXPECT_EQ(ids(one), QStringList{QStringLiteral("plugin.line-tools.width")});
    EXPECT_EQ(one.value(QStringLiteral("plugin")), QStringLiteral("line-tools"));
    EXPECT_EQ(one.value(QStringLiteral("sections")).toMap().keys(), QStringList{QStringLiteral("plugins.line-tools")});
    EXPECT_TRUE(ids(SettingsCatalogue::search(QStringLiteral("columns"), plugins)).contains(QStringLiteral("plugin.line-tools.width")));
    EXPECT_EQ(ids(SettingsCatalogue::search(QStringLiteral("width"), plugins, {}, true)), QStringList{QStringLiteral("plugin.line-tools.width")});
}
