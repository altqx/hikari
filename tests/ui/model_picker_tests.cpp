#include "hikari/app/model_picker.h"

#include "quick_pick_model.h"

#include <gtest/gtest.h>

#include <QVariantMap>

namespace {

using hikari::app::ModelPickerGroup;
using hikari::app::ModelPickerModel;
using hikari::app::ModelPickerRequest;
using hikari::app::modelPickerItems;

ModelPickerRequest request()
{
    ModelPickerGroup claude{.name = QStringLiteral("Claude Code"), .key = QStringLiteral("claude")};
    claude.agentDefault = ModelPickerModel{QStringLiteral("agent:claude\t"), QStringLiteral("Claude Code"), {},
                                           QStringLiteral("Claude Code")};
    claude.models = {{QStringLiteral("agent:claude\topus"), QStringLiteral("Opus"), QStringLiteral("opus"),
                      QStringLiteral("Claude Code · Opus")}};
    ModelPickerGroup anthropic{.name = QStringLiteral("Anthropic"), .key = QStringLiteral("anthropic")};
    anthropic.models = {{QStringLiteral("anthropic\tsonnet-4"), QStringLiteral("Sonnet 4"), QStringLiteral("sonnet-4"),
                         QStringLiteral("Anthropic")},
                        {QStringLiteral("anthropic\tsame"), QStringLiteral("same"), QStringLiteral("same"),
                         QStringLiteral("Anthropic")}};
    ModelPickerGroup openRouter{.name = QStringLiteral("OpenRouter"), .key = QStringLiteral("or")};
    openRouter.models = {{QStringLiteral("or\tsonnet-4"), QStringLiteral("Sonnet 4"), QStringLiteral("anthropic/sonnet-4"),
                          QStringLiteral("OpenRouter")}};
    return {.groups = {claude, anthropic, openRouter}, .current = QStringLiteral("anthropic\tsonnet-4"), .settingsRow = true};
}

QStringList shown(const QVariantList &items, const QString &filter = {})
{
    hikari::ui::QuickPickModel model;
    model.setKeepOrder(true);
    model.setMatchOn({QStringLiteral("description")});
    model.setItems(items);
    model.setFilter(filter);
    QStringList out;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex at = model.index(row, 0);
        const bool heading = model.data(at, hikari::ui::QuickPickModel::KindRole).toString() == QLatin1String("heading");
        out << (heading ? QStringLiteral("# ") : QString()) + model.data(at, hikari::ui::QuickPickModel::LabelRole).toString();
    }
    return out;
}

QVariantMap byId(const QVariantList &items, const QString &id)
{
    for (const QVariant &item : items)
        if (item.toMap().value(QStringLiteral("id")).toString() == id)
            return item.toMap();
    return {};
}

TEST(ModelPicker, AgentsThenProvidersEachUnderItsNameWithTheIdWhenItDiffersAndSettingsLast)
{
    const QVariantList items = modelPickerItems(request());
    EXPECT_EQ(shown(items), (QStringList{"# Claude Code", "Agent default", "Opus", "# Anthropic", "Sonnet 4", "same",
                                         "# OpenRouter", "Sonnet 4", "Settings › AI…"}));
    EXPECT_EQ(byId(items, "anthropic\tsonnet-4").value("description").toString(), "sonnet-4");
    EXPECT_EQ(byId(items, "anthropic\tsame").value("description").toString(), "");
    EXPECT_EQ(byId(items, "settings").value("action").toString(), "settings");
    EXPECT_EQ(byId(items, "settings").value("icon").toString(), "settings");
}

TEST(ModelPicker, TheCurrentModelIsMarkedInRecentAndInItsGroup)
{
    ModelPickerRequest r = request();
    r.recent = {QStringLiteral("anthropic\tsonnet-4")};
    const QVariantList items = modelPickerItems(r);
    for (const char *id : {"recent\tanthropic\tsonnet-4", "anthropic\tsonnet-4"}) {
        EXPECT_EQ(byId(items, id).value("icon").toString(), "karaoke-check") << id;
        EXPECT_EQ(byId(items, id).value("tooltip").toString(), "current model") << id;
    }
    EXPECT_FALSE(byId(items, "or\tsonnet-4").contains("icon"));
}

TEST(ModelPicker, RecentShowsEachModelsSourceDropsOnesNoLongerOfferedAndHidesWhileTyping)
{
    ModelPickerRequest r = request();
    r.recent = {QStringLiteral("agent:claude\topus"), QStringLiteral("gone\tx"), QStringLiteral("or\tsonnet-4"),
                QStringLiteral("agent:claude\t")};
    const QVariantList items = modelPickerItems(r);
    EXPECT_EQ(shown(items).mid(0, 5), (QStringList{"# Recent", "Opus", "Sonnet 4", "Agent default", "# Claude Code"}));
    EXPECT_EQ(byId(items, "recent\tagent:claude\topus").value("description").toString(), "Claude Code · Opus");
    EXPECT_EQ(byId(items, "recent\tor\tsonnet-4").value("description").toString(), "OpenRouter");
    EXPECT_EQ(byId(items, "recent\tagent:claude\t").value("description").toString(), "Claude Code");
    EXPECT_TRUE(byId(items, "recent\tgone\tx").isEmpty());

    r.filter = QStringLiteral("opus");
    EXPECT_EQ(shown(modelPickerItems(r), r.filter), (QStringList{"# Claude Code", "Opus"}));
}

TEST(ModelPicker, TypingMatchesTheNameThenTheIdThenTheProvidersNameKeepingGroups)
{
    ModelPickerRequest r = request();
    r.filter = QStringLiteral("sonnet");
    EXPECT_EQ(shown(modelPickerItems(r), r.filter), (QStringList{"# Anthropic", "Sonnet 4", "# OpenRouter", "Sonnet 4"}));
    r.filter = QStringLiteral("anthropic/");
    EXPECT_EQ(shown(modelPickerItems(r), r.filter), (QStringList{"# OpenRouter", "Sonnet 4"}));
    r.filter = QStringLiteral("openrouter");
    EXPECT_EQ(shown(modelPickerItems(r), r.filter), (QStringList{"# OpenRouter", "Sonnet 4"}));
    r.filter = QStringLiteral("zzz");
    EXPECT_EQ(shown(modelPickerItems(r), r.filter), QStringList());
}

TEST(ModelPicker, AGroupThatCantBeUsedShowsOnlyItsRowWhichOpensSettings)
{
    ModelPickerRequest r = request();
    r.groups[1].blockedBy = QStringLiteral("Add a key for Anthropic…");
    const QVariantList items = modelPickerItems(r);
    EXPECT_EQ(shown(items).mid(3, 2), (QStringList{"# Anthropic", "Add a key for Anthropic…"}));
    EXPECT_EQ(byId(items, "blocked\tanthropic").value("action").toString(), "settings");
    EXPECT_EQ(byId(items, "blocked\tanthropic").value("key").toString(), "anthropic");
    EXPECT_TRUE(byId(items, "anthropic\tsonnet-4").isEmpty());
}

TEST(ModelPicker, AnAgentLoadingForTheFirstTimeAndOneThatFailedSayHowItWent)
{
    ModelPickerRequest r = request();
    r.groups[0].models.clear();
    r.groups[0].loading = true;
    QVariantList items = modelPickerItems(r);
    EXPECT_EQ(shown(items).mid(0, 3), (QStringList{"# Claude Code", "Agent default", "Loading models…"}));
    EXPECT_FALSE(byId(items, "loading\tclaude").value("disabled").toString().isEmpty());

    r = request();
    r.groups[0].failure = QStringLiteral("It took longer than 30 seconds.");
    items = modelPickerItems(r);
    EXPECT_EQ(shown(items).mid(0, 5), (QStringList{"# Claude Code", "Agent default", "Opus", "Couldn’t load models", "Try again"}));
    EXPECT_EQ(byId(items, "failed\tclaude").value("disabled").toString(), "It took longer than 30 seconds.");
    EXPECT_EQ(byId(items, "retry\tclaude").value("action").toString(), "retry");
    EXPECT_EQ(byId(items, "retry\tclaude").value("key").toString(), "claude");
    EXPECT_EQ(byId(items, "retry\tclaude").value("icon").toString(), "refresh");
}

TEST(ModelPicker, ARolePickerStartsWithItsEmptyChoiceAndHasNoSettingsRow)
{
    ModelPickerRequest r = request();
    r.groups.erase(r.groups.begin());
    r.settingsRow = false;
    r.emptyChoice = QStringLiteral("Same as chat");
    r.current = QString();
    const QVariantList items = modelPickerItems(r);
    EXPECT_EQ(shown(items), (QStringList{"Same as chat", "# Anthropic", "Sonnet 4", "same", "# OpenRouter", "Sonnet 4"}));
    EXPECT_EQ(byId(items, "empty").value("value").toString(), "");
    EXPECT_EQ(byId(items, "empty").value("icon").toString(), "karaoke-check");
}

}
