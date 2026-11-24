#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>

#include <optional>
#include <vector>

namespace hikari::app {

struct ModelPickerModel {
    QString value; // "<provider id>\t<model id>"
    QString name;
    QString id;
    /// Recent's description: the provider's name, or for an agent model the model button's text.
    QString source;
};

/// An agent's or a provider's group in the model picker.
struct ModelPickerGroup {
    QString name;
    /// The agent ("claude", "acp:x") or the provider's id.
    QString key;
    /// An agent's "Agent default" row, first in its group; empty for a provider.
    std::optional<ModelPickerModel> agentDefault;
    std::vector<ModelPickerModel> models;
    /// Set: the group's one row instead of its models, such as "Add a key for Anthropic…".
    QString blockedBy;
    bool loading = false;
    /// Set: the last load failed, for this reason.
    std::optional<QString> failure;
};

struct ModelPickerRequest {
    std::vector<ModelPickerGroup> groups;
    /// Model values, newest first.
    QStringList recent;
    QString current;
    QString filter;
    /// Set: a first row choosing "", with this label.
    std::optional<QString> emptyChoice;
    bool settingsRow = false;
};

/// The quick pick's items, in order. A model row's `value` is what to choose; an `action` row is "settings", for
/// a blocked group's row and the last row, or "retry" with the agent in `key`.
QVariantList modelPickerItems(const ModelPickerRequest &request);

}
