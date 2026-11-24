#pragma once

#include "hikari/app/plugin_runtime.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hikari::app {

// Offsets are UTF-16, into the field's text.
struct CompletionItem {
    QString label;
    QString insert;
    QString detail;
    QString source; // the plugin's name
    int from = 0;
    int to = 0;
    bool snippet = false;
    bool operator==(const CompletionItem &) const = default;
};

struct HoverText {
    QString text;
    QString source;
    int from = 0;
    int to = 0;
    bool operator==(const HoverText &) const = default;
};

inline constexpr std::size_t kMostCompletions = 200;

// A plugin's completion answer, without the items that break the contract.
std::vector<CompletionItem> completionItems(const api::Json &answer, int length, int offset, const QString &source);
std::optional<HoverText> hoverText(const api::Json &answer, int length, const QString &source);

// The plugins' contributes.providers for the line editor (#1227).
class LanguageProviders : public QObject {
    Q_OBJECT
public:
    struct Request {
        std::uint64_t document = 0;
        std::uint64_t line = 0;
        QString field; // "text" or "translation"
        QString text;
        int offset = 0;
        QString trigger; // the typed trigger character, empty for Ctrl+Space or a hover
    };
    using Completions = std::function<void(std::vector<CompletionItem>)>;
    using Hover = std::function<void(std::optional<HoverText>)>;

    explicit LanguageProviders(PluginRuntime &runtime, QObject *parent = nullptr);

    // The trigger characters of every plugin that completes `field`.
    QStringList triggers(const QString &field) const;
    // `done` gets every plugin's items in install order once each has answered, failed or run out of time; it isn't
    // called when a newer complete() came first.
    void complete(const Request &request, Completions done);
    // `done` gets the first plugin's hover, in install order; as complete for newer requests.
    void hover(const Request &request, Hover done);

private:
    PluginRuntime &m_runtime;
    std::uint64_t m_completion = 0;
    std::uint64_t m_hover = 0;
};

}
