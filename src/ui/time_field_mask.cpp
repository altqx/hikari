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

} // namespace

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

QString TimeFieldMask::wheel(int form, bool frameNumber, const QString &text, int column, int notches) const
{
    const auto next = application::wheelTimeField(formOf(form), frameNumber, text.toStdU16String(),
                                                  static_cast<std::size_t>(std::max(column, 0)), notches);
    return next ? QString::fromStdU16String(*next) : QString();
}

namespace {

QVariantMap dragMap(const application::TimeFieldDrag &drag)
{
    return {{QStringLiteral("ms"), static_cast<qlonglong>(drag.ms)},
            {QStringLiteral("step"), static_cast<qlonglong>(drag.step)},
            {QStringLiteral("x"), drag.x},
            {QStringLiteral("y"), drag.y}};
}

} // namespace

QVariantMap TimeFieldMask::dragBegin(int form, const QString &text, int column, int x, int y) const
{
    const auto drag = application::beginTimeFieldDrag(formOf(form), text.toStdU16String(),
                                                      static_cast<std::size_t>(std::max(column, 0)), x, y);
    return drag ? dragMap(*drag) : QVariantMap();
}

QVariantMap TimeFieldMask::dragMove(int form, const QVariantMap &drag, int x, int y) const
{
    if (drag.isEmpty())
        return {};
    application::TimeFieldDrag state{drag.value(QStringLiteral("ms")).toLongLong(),
                                     drag.value(QStringLiteral("step")).toLongLong(),
                                     drag.value(QStringLiteral("x")).toInt(), drag.value(QStringLiteral("y")).toInt()};
    const auto next = application::dragTimeField(formOf(form), state, x, y);
    return {{QStringLiteral("drag"), dragMap(state)},
            {QStringLiteral("text"), next ? QString::fromStdU16String(*next) : QString()}};
}

} // namespace hikari::ui
