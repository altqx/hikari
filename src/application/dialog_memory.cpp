#include "hikari/application/dialog_memory.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace hikari::application {

bool sameDialog(const DialogRequest &a, const DialogRequest &b)
{
    const auto sameControl = [](const DialogControl &x, const DialogControl &y) {
        return x.kind == y.kind && x.name == y.name && x.x == y.x && x.y == y.y && x.width == y.width &&
               x.height == y.height && x.items == y.items && x.label == y.label && x.hint == y.hint;
    };
    return std::ranges::equal(a.controls, b.controls, sameControl) && a.buttons == b.buttons &&
           a.buttonIds == b.buttonIds;
}

bool isQuestion(const DialogRequest &request)
{
    return std::ranges::all_of(request.controls,
                               [](const DialogControl &c) { return dialogValueType(c.kind) == DialogValueType::None; });
}

bool cancelAnswer(const DialogRequest &request, int pressed)
{
    if (pressed < 0)
        return true;
    if (request.buttons.empty())
        return pressed == 1; // default buttons: OK, Cancel
    const auto at = static_cast<std::size_t>(pressed);
    return at < request.buttonIds.size() && request.buttonIds[at] == "cancel";
}

std::optional<DialogValue> dialogValueOf(const DialogControl &control, const DialogValue &value)
{
    switch (dialogValueType(control.kind)) {
    case DialogValueType::None:
        return std::nullopt;
    case DialogValueType::Text:
        if (const auto *s = std::get_if<std::string>(&value))
            return *s;
        return std::nullopt;
    case DialogValueType::Integer:
        if (const auto *i = std::get_if<int>(&value))
            return *i;
        if (const auto *d = std::get_if<double>(&value);
            d && std::trunc(*d) == *d && *d >= double(INT_MIN) && *d <= double(INT_MAX))
            return static_cast<int>(*d);
        return std::nullopt;
    case DialogValueType::Number:
        if (const auto *d = std::get_if<double>(&value); d && std::isfinite(*d))
            return *d;
        if (const auto *i = std::get_if<int>(&value))
            return double(*i);
        return std::nullopt;
    case DialogValueType::Boolean:
        if (const auto *b = std::get_if<bool>(&value))
            return *b;
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<DialogValue> acceptedDialogValue(const DialogControl &control, const DialogValue &value)
{
    auto v = dialogValueOf(control, value);
    if (!v)
        return std::nullopt;
    if (auto *i = std::get_if<int>(&*v))
        *i = std::clamp(*i, control.intMin, control.intMax);
    else if (auto *d = std::get_if<double>(&*v))
        *d = std::clamp(*d, control.numberMin, control.numberMax);
    else if (const auto *s = std::get_if<std::string>(&*v);
             s && control.kind == "dropdown" && std::ranges::find(control.items, *s) == control.items.end())
        return std::nullopt;
    return v;
}

DialogResult replayedAnswer(const DialogRequest &request, const DialogResult &recorded)
{
    DialogResult result = initialDialogResult(request);
    result.pressed = recorded.pressed;
    for (std::size_t i = 0; i < request.controls.size() && i < recorded.values.size(); ++i)
        if (const auto v = acceptedDialogValue(request.controls[i], recorded.values[i]))
            result.values[i] = *v;
    return result;
}

NamedDialogValues namedDialogValues(const DialogRequest &request, const DialogResult &result)
{
    NamedDialogValues named;
    for (std::size_t i = 0; i < request.controls.size() && i < result.values.size(); ++i) {
        const DialogControl &c = request.controls[i];
        if (c.name.empty() || dialogValueType(c.kind) == DialogValueType::None ||
            std::holds_alternative<std::monostate>(result.values[i]))
            continue;
        named.insert_or_assign(c.name, result.values[i]);
    }
    return named;
}

DialogResult filledByName(const DialogRequest &request, DialogResult base, const NamedDialogValues &values)
{
    base.values.resize(request.controls.size());
    for (std::size_t i = 0; i < request.controls.size(); ++i) {
        const DialogControl &c = request.controls[i];
        if (c.name.empty())
            continue;
        if (const auto it = values.find(c.name); it != values.end())
            if (const auto v = acceptedDialogValue(c, it->second))
                base.values[i] = *v;
    }
    return base;
}

LastDialogAnswer lastDialogAnswer(const DialogRequest &request, const DialogResult &answer)
{
    return {namedDialogValues(request, initialDialogResult(request)), namedDialogValues(request, answer),
            answer.pressed};
}

DialogResult safePrefill(const DialogRequest &request, const LastDialogAnswer &last)
{
    DialogResult result = initialDialogResult(request);
    for (std::size_t i = 0; i < request.controls.size(); ++i) {
        const DialogControl &c = request.controls[i];
        const auto sent = last.sent.find(c.name);
        const auto answered = last.answered.find(c.name);
        if (c.name.empty() || sent == last.sent.end() || answered == last.answered.end())
            continue;
        if (dialogValueOf(c, sent->second) != std::optional<DialogValue>(result.values[i]))
            continue;
        if (const auto v = acceptedDialogValue(c, answered->second))
            result.values[i] = *v;
    }
    return result;
}

} // namespace hikari::application
