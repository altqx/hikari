#include "style_colours.h"

#include <QtQml/qqmlengine.h>

#include <algorithm>
#include <vector>

namespace hikari::ui::style {

namespace {

StyleColours::Colours g_colours;
std::vector<StyleColours *> g_instances;

}

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
    return g_colours.focus.isValid();
}

bool StyleColours::highContrast() const
{
    return g_colours.highContrast;
}

QColor StyleColours::focus() const
{
    return g_colours.focus;
}

QColor StyleColours::control() const
{
    return g_colours.control;
}

QColor StyleColours::text() const
{
    return g_colours.text;
}

QColor StyleColours::muted() const
{
    return g_colours.muted;
}

QColor StyleColours::line() const
{
    return g_colours.line;
}

QColor StyleColours::panel() const
{
    return g_colours.panel;
}

QColor StyleColours::raised() const
{
    return g_colours.raised;
}

QColor StyleColours::field() const
{
    return g_colours.field;
}

QColor StyleColours::accent() const
{
    return g_colours.accent;
}

QColor StyleColours::onAccent() const
{
    return g_colours.onAccent;
}

QColor StyleColours::select() const
{
    return g_colours.select;
}

QColor StyleColours::hover() const
{
    return g_colours.hover;
}

QColor StyleColours::pressed() const
{
    return g_colours.pressed;
}

QColor StyleColours::disabled() const
{
    return g_colours.disabled;
}

QColor StyleColours::danger() const
{
    return g_colours.danger;
}

void StyleColours::set(const Colours &colours)
{
    if (colours == g_colours)
        return;
    g_colours = colours;
    for (auto *instance : g_instances)
        emit instance->changed();
}

}
