#include "style_colours.h"

#include <QtQml/qqmlengine.h>

#include <algorithm>
#include <vector>

namespace hikari::ui::style {

namespace {

// One per engine (the singleton), all showing the same colours; the GUI
// thread's only.
QColor g_focus;
QColor g_control;
std::vector<StyleColours *> g_instances;

} // namespace

StyleColours::StyleColours(QObject *parent) : QObject(parent)
{
    g_instances.push_back(this);
}

StyleColours::~StyleColours()
{
    g_instances.erase(std::remove(g_instances.begin(), g_instances.end(), this), g_instances.end());
}

StyleColours *StyleColours::create(QQmlEngine *engine, QJSEngine *)
{
    return new StyleColours(engine);
}

bool StyleColours::themed() const
{
    return g_focus.isValid();
}

QColor StyleColours::focus() const
{
    return g_focus;
}

QColor StyleColours::control() const
{
    return g_control;
}

void StyleColours::setControl(const QColor &control)
{
    if (control == g_control)
        return;
    g_control = control;
    for (auto *instance : g_instances)
        emit instance->changed();
}

void StyleColours::setFocus(const QColor &focus)
{
    if (focus == g_focus)
        return;
    g_focus = focus;
    for (auto *instance : g_instances)
        emit instance->changed();
}

} // namespace hikari::ui::style
