#include "time_field_mask.h"

#include "hikari/application/editor_fields.h"

#include <QClipboard>
#include <QGuiApplication>

#include <algorithm>

namespace hikari::ui {

namespace {

application::TimeFieldForm formOf(int form)
{
    return static_cast<application::TimeFieldForm>(std::clamp(form, 0, 3));
}

application::TimeFieldState stateOf(const QString &text, int start, int end)
{
    return application::TimeFieldState{text.toStdU16String(), static_cast<std::size_t>(std::max(start, 0)),
                                        static_cast<std::size_t>(std::max(end, 0))};
}

QVariantMap answer(const application::TimeFieldEdit &edit)
{
    return {{QStringLiteral("text"), QString::fromStdU16String(edit.state.text)},
            {QStringLiteral("selectionStart"), static_cast<int>(edit.state.selectionStart)},
            {QStringLiteral("selectionEnd"), static_cast<int>(edit.state.selectionEnd)},
            {QStringLiteral("changed"), edit.changed},
            {QStringLiteral("bell"), edit.bell}};
}

}

QVariantMap TimeFieldMask::type(int form, const QString &text, int start, int end, const QString &character) const
{
    const auto state = stateOf(text, start, end);
    if (character.size() != 1)
        return answer(application::TimeFieldEdit{state, false, true});
    return answer(application::typeIntoTimeField(formOf(form), state, character.front().unicode()));
}

QVariantMap TimeFieldMask::backspace(const QString &text, int start, int end) const
{
    return answer(application::backspaceTimeField(stateOf(text, start, end)));
}

QVariantMap TimeFieldMask::paste(int form, const QString &text, int start, int end) const
{
    const QString pasted = QGuiApplication::clipboard()->text();
    return answer(application::pasteIntoTimeField(formOf(form), stateOf(text, start, end), pasted.toStdU16String()));
}

void TimeFieldMask::copy(const QString &text) const
{
    QGuiApplication::clipboard()->setText(text);
}

bool TimeFieldMask::holds(int form, const QString &text) const
{
    if (formOf(form) == application::TimeFieldForm::Number)
        return text.size() <= static_cast<qsizetype>(application::kTimeFieldDigits) &&
               std::all_of(text.begin(), text.end(), [](QChar c) { return c >= u'0' && c <= u'9'; });
    return application::pasteIntoTimeField(formOf(form), {}, text.toStdU16String()).changed;
}

namespace {

application::TimeFieldMouse mouseOf(int form, bool frames)
{
    return {static_cast<application::TimeCtrlForm>(std::clamp(form, 0, 4)), frames};
}

QVariantMap dragMap(const application::TimeFieldDrag &drag)
{
    return {{QStringLiteral("value"), static_cast<double>(drag.value)},
            {QStringLiteral("step"), static_cast<double>(drag.step)},
            {QStringLiteral("x"), drag.x},
            {QStringLiteral("y"), drag.y}};
}

}

QString TimeFieldMask::wheel(int mouseForm, bool frames, const QString &text, int column, int notches) const
{
    const auto next = application::wheelTimeField(mouseOf(mouseForm, frames), text.toStdU16String(),
                                                  static_cast<std::size_t>(std::max(column, 0)), notches);
    return next ? QString::fromStdU16String(*next) : QString();
}

QVariantMap TimeFieldMask::dragBegin(int mouseForm, bool frames, const QString &text, int column, int x, int y) const
{
    return dragMap(application::beginTimeFieldDrag(mouseOf(mouseForm, frames), text.toStdU16String(),
                                                   static_cast<std::size_t>(std::max(column, 0)), x, y));
}

QVariantMap TimeFieldMask::dragMove(int mouseForm, bool frames, const QVariantMap &drag, int x, int y) const
{
    if (drag.isEmpty())
        return {};
    application::TimeFieldDrag state{static_cast<std::int64_t>(drag.value(QStringLiteral("value")).toDouble()),
                                     static_cast<std::int64_t>(drag.value(QStringLiteral("step")).toDouble()),
                                     drag.value(QStringLiteral("x")).toInt(), drag.value(QStringLiteral("y")).toInt()};
    const auto next = application::dragTimeField(mouseOf(mouseForm, frames), state, x, y);
    return {{QStringLiteral("drag"), dragMap(state)},
            {QStringLiteral("text"), next ? QString::fromStdU16String(*next) : QString()}};
}

}
