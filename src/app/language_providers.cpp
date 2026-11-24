#include "hikari/app/language_providers.h"

#include <QPointer>

#include <cmath>
#include <memory>

namespace hikari::app {

namespace {

std::optional<int> offsetIn(const api::Json *value, int length)
{
    if (!value || !value->isNumber())
        return std::nullopt;
    const double at = value->asNumber();
    if (at != std::floor(at) || at < 0 || at > length)
        return std::nullopt;
    return static_cast<int>(at);
}

QString textOf(const api::Json *value)
{
    return value && value->isString() ? QString::fromStdString(value->asString()) : QString();
}

struct Range {
    int from = 0;
    int to = 0;
};

// Both or neither of from and to, with from <= to <= length.
std::optional<Range> rangeOf(const api::Json &object, int length, Range fallback)
{
    const api::Json *from = object.find("from");
    const api::Json *to = object.find("to");
    if (!from && !to)
        return fallback;
    const auto start = offsetIn(from, length);
    const auto end = offsetIn(to, length);
    if (!start || !end || *start > *end)
        return std::nullopt;
    return Range{*start, *end};
}

api::Json requestJson(const LanguageProviders::Request &request)
{
    return api::Json::object({{"document", api::Json(request.document)},
                              {"line", api::Json(request.line)},
                              {"field", request.field.toStdString()},
                              {"text", request.text.toStdString()},
                              {"offset", request.offset},
                              {"trigger", request.trigger.isEmpty() ? api::Json() : api::Json(request.trigger.toStdString())}});
}

std::optional<api::Json> answerOf(const std::expected<std::string, QString> &answer)
{
    if (!answer)
        return std::nullopt;
    auto json = api::Json::parse(*answer);
    return json ? std::optional<api::Json>(std::move(*json)) : std::nullopt;
}

}

std::vector<CompletionItem> completionItems(const api::Json &answer, int length, int offset, const QString &source)
{
    std::vector<CompletionItem> out;
    if (!answer.isArray())
        return out;
    for (const api::Json &item : answer.asArray()) {
        if (out.size() == kMostCompletions)
            break;
        if (!item.isObject())
            continue;
        const QString label = textOf(item.find("label"));
        const api::Json *insert = item.find("insert");
        const api::Json *snippet = item.find("snippet");
        const auto range = rangeOf(item, length, {offset, offset});
        if (label.isEmpty() || !range || (insert && !insert->isString()) || (snippet && !snippet->isBool()))
            continue;
        out.push_back({label, insert ? textOf(insert) : label, textOf(item.find("detail")), source, range->from,
                       range->to, snippet && snippet->asBool()});
    }
    return out;
}

std::optional<HoverText> hoverText(const api::Json &answer, int length, const QString &source)
{
    if (!answer.isObject())
        return std::nullopt;
    const QString text = textOf(answer.find("text"));
    const auto range = rangeOf(answer, length, {0, 0});
    if (text.isEmpty() || !range)
        return std::nullopt;
    return HoverText{text, source, range->from, range->to};
}

LanguageProviders::LanguageProviders(PluginRuntime &runtime, QObject *parent)
    : QObject(parent)
    , m_runtime(runtime)
{
}

QStringList LanguageProviders::triggers(const QString &field) const
{
    QStringList out;
    for (const auto &provider : m_runtime.providers("completion", field.toStdString()))
        for (const std::string &trigger : provider.triggers)
            if (const QString t = QString::fromStdString(trigger); !out.contains(t))
                out.push_back(t);
    return out;
}

void LanguageProviders::complete(const Request &request, Completions done)
{
    struct Gather {
        std::vector<std::vector<CompletionItem>> answers;
        std::size_t waiting = 0;
        Completions done;
    };
    const std::uint64_t generation = ++m_completion;
    const auto providers = m_runtime.providers("completion", request.field.toStdString());
    auto gather = std::make_shared<Gather>(Gather{std::vector<std::vector<CompletionItem>>(providers.size()), 0, std::move(done)});
    const auto finish = [this, generation, gather] {
        if (generation != m_completion)
            return;
        std::vector<CompletionItem> items;
        for (auto &answer : gather->answers)
            for (auto &item : answer)
                if (items.size() < kMostCompletions)
                    items.push_back(std::move(item));
        gather->done(std::move(items));
    };
    const api::Json json = requestJson(request);
    const int length = static_cast<int>(request.text.size());
    for (std::size_t i = 0; i < providers.size(); ++i) {
        const QString source = providers[i].pluginName;
        auto answered = [self = QPointer(this), gather, finish, i, length, offset = request.offset,
                         source](std::expected<std::string, QString> answer) {
            if (const auto json = answerOf(answer))
                gather->answers[i] = completionItems(*json, length, offset, source);
            if (--gather->waiting == 0 && self)
                finish();
        };
        ++gather->waiting;
        if (!m_runtime.provide(providers[i].plugin, "completion", json, answered))
            --gather->waiting;
    }
    if (gather->waiting == 0)
        finish();
}

void LanguageProviders::hover(const Request &request, Hover done)
{
    struct Gather {
        std::vector<std::optional<HoverText>> answers;
        std::size_t waiting = 0;
        Hover done;
    };
    const std::uint64_t generation = ++m_hover;
    const auto providers = m_runtime.providers("hover", request.field.toStdString());
    auto gather = std::make_shared<Gather>(Gather{std::vector<std::optional<HoverText>>(providers.size()), 0, std::move(done)});
    const auto finish = [this, generation, gather] {
        if (generation != m_hover)
            return;
        for (auto &answer : gather->answers)
            if (answer)
                return gather->done(std::move(answer));
        gather->done(std::nullopt);
    };
    const api::Json json = requestJson(request);
    const int length = static_cast<int>(request.text.size());
    for (std::size_t i = 0; i < providers.size(); ++i) {
        const QString source = providers[i].pluginName;
        auto answered = [self = QPointer(this), gather, finish, i, length, source](std::expected<std::string, QString> answer) {
            if (const auto json = answerOf(answer))
                gather->answers[i] = hoverText(*json, length, source);
            if (--gather->waiting == 0 && self)
                finish();
        };
        ++gather->waiting;
        if (!m_runtime.provide(providers[i].plugin, "hover", json, answered))
            --gather->waiting;
    }
    if (gather->waiting == 0)
        finish();
}

}
