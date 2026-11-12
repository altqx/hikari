#include "node_catalogue_text.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <algorithm>

namespace hikari::ui {

namespace {

struct NodeText {
    const char *type, *title, *summary;
};

struct FieldText {
    const char *type, *field, *summary;
};

struct CategoryText {
    const char *category, *title;
};

constexpr NodeText kNodes[] = {
    {"units.for_each", QT_TRANSLATE_NOOP("HikariSub.Nodes", "For each"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "Each unit of every karaoke line the graph applies to: the line itself, its syllables, their characters or the furigana. Blank units (no duration or only spaces) can be skipped.")},
    {"units.repeat", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Repeat"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "Each unit `count` times; j runs from 1 to count.")},
    {"units.frames", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Frames"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "Each unit once per step of time from `start` to `end` (absolute ms); without them, the unit’s own span.")},
    {"units.clip_gradient", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Clip gradient"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "Each unit once per strip of its box, side by side across or stacked down: the strip’s \\clip rectangle (whole pixels, the edges shared) and its colour along the gradient.")},
    {"units.array", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Array"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "Each unit once per copy: on a grid centred on it, on a circle around it, or spread evenly along a path drawn from its position. angle is in degrees, as \\frz takes it.")},
    {"units.particles", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Particles"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "Each unit once per particle, spread at random over a rectangle or an ellipse centred on it. The same graph seed, node seed and line give the same particles on every run and platform.")},
    {"units.highlights", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Highlights"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "Each syllable or character once per highlight, as the templater’s multi gives it: the highlight’s start, end and duration. A furigana or line unit has one, its own.")},
    {"units.inline_fx", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Inline-fx"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "Only the units whose inline-fx ({\\-name}, kept until the next one) is `fx`, as the templater’s fx modifier keeps them. Characters have their syllable’s; a line unit has none.")},
    {"units.info", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Unit info"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "What the karaoke templater’s $variables give for the current unit and its line, rounded as the templater rounds them. Times of a syllable are from its line’s start. line_alignment is the line’s first \\an, or its Style’s alignment.")},
    {"values.number", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Number"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "A number.")},
    {"values.text", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Text"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "A text.")},
    {"values.colour", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Colour"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "A colour.")},
    {"values.point", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Point"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "A point from its coordinates.")},
    {"values.choose", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Choose"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "One of a list of values by its place (from 1); places past the end wrap around.")},
    {"math.calculate", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Calculate"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "Arithmetic in doubles, as Lua does it. modulo is a - floor(a/b)*b; round is floor(a + 0.5); sine and cosine take a in degrees and round to millionths. One-input operations use a.")},
    {"math.compare", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Compare"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "Whether a stands in that relation to b.")},
    {"math.progress", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Progress"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "How far index is through count, from 0 at the first to 1 at the last, eased.")},
    {"math.random", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Random"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "A seeded random number from min to max, in multiples of step from min when step is above 0. The same graph seed, node and scope give the same number on every run and platform.")},
    {"time.retime", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Retime"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "A line’s start and end as the templater’s retime(mode, start_offset, end_offset) sets them.")},
    {"time.stagger", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Stagger"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "step times the place: index - 1 from the start, count - index from the end, or the distance from the middle.")},
    {"tags.tag", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Tag"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "One override tag after `before`. pos and org take point, clip and iclip take point and point2 (or drawing), colours take colour, alpha tags take alpha, fn and r take text, raw writes text as it is, and the others take value.")},
    {"tags.move", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Move"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "\\move after `before`, with its times when timed.")},
    {"tags.fade", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Fade"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "\\fad after `before`.")},
    {"tags.transform", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Transform"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "\\t over `tags` after `before`; t1 and t2 when timed, accel when it is not 1.")},
    {"colours.gradient", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Gradient"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "The colour at progress (0 to 1) along evenly spaced stops.")},
    {"audio.peaks", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Audio peaks"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "The audio’s level over the unit’s span, or from start to end (absolute ms), read as the Hikari API’s audio.peaks reads it. level is the loudest sample from 0 to 1; low and high are the lowest and highest from -1 to 1. Without audio it gives 0 and the graph shows a problem.")},
    {"drawings.shape", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Shape"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "A vector drawing.")},
    {"drawings.text", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Text to drawing"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "The text (unwired, the unit’s own) drawn in its line’s Style font, at \\p1 from the text’s top left, as Yutils’ text_to_shape draws it. width and height are the text’s.")},
    {"drawings.combine", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Combine drawings"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "a and b combined, each filled as libass fills it: unite, subtract (b taken away from a), intersect, or exclude (what only one of them covers). Each one’s \\p scale is kept; the result is at \\p1.")},
    {"text.join", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Join"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "The units of the current line joined: each one’s tags as an override block, then its text (unwired, the unit’s own).")},
    {"output.emit", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Emit"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "One line per unit: the source line with these times, Layer, tags and text (or drawing), Effect “fx <graph name>”. Unwired times are the source line’s. Lines come in the templater’s order: Line units first, then by syllable, then by `order`.")},
    {"groups.group", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Group"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "A graph inside a node. Its inputs are its Group input’s ports, its outputs its Group output’s.")},
    {"groups.input", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Group input"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "Inside a group: its inputs, as outputs.")},
    {"groups.output", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Group output"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "Inside a group: its outputs, as inputs.")},
    {"lua.code", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Lua"),
     QT_TRANSLATE_NOOP("HikariSub.Nodes", "Lua code run in the automation helper once per line for all its units, only once the user trusts the document’s Lua. Its inputs are globals and it sets its outputs as globals; it gets no random numbers.")},
};

constexpr FieldText kFields[] = {
    {"units.for_each", "unit", QT_TRANSLATE_NOOP("HikariSub.Nodes", "What one unit is.")},
    {"units.for_each", "skip_blank", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Leave out units with no duration or only spaces.")},
    {"units.repeat", "count", QT_TRANSLATE_NOOP("HikariSub.Nodes", "How many times.")},
    {"units.frames", "start", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The first step’s start; null: the unit’s start.")},
    {"units.frames", "end", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The last step’s end; null: the unit’s end.")},
    {"units.frames", "step", QT_TRANSLATE_NOOP("HikariSub.Nodes", "One step, in ms.")},
    {"units.clip_gradient", "strips", QT_TRANSLATE_NOOP("HikariSub.Nodes", "How many strips.")},
    {"units.clip_gradient", "direction", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Strips side by side from left to right, or stacked from top to bottom.")},
    {"units.clip_gradient", "colours", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The gradient’s stops, as #RRGGBB, at least one.")},
    {"units.clip_gradient", "padding", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Added to the box on every side, in pixels.")},
    {"units.array", "shape", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Where the copies go.")},
    {"units.array", "columns", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Grid: copies in a row.")},
    {"units.array", "rows", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Grid: rows.")},
    {"units.array", "spacing", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Grid: from one copy to the next, as [across, down].")},
    {"units.array", "count", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Circle and path: how many copies.")},
    {"units.array", "radius", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Circle: its radius.")},
    {"units.array", "start_angle", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Circle: the first copy’s angle, in degrees counter-clockwise from the right.")},
    {"units.particles", "count", QT_TRANSLATE_NOOP("HikariSub.Nodes", "How many particles.")},
    {"units.particles", "area", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The area they spread over.")},
    {"units.particles", "width", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The area’s width.")},
    {"units.particles", "height", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The area’s height.")},
    {"units.particles", "seed", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Added to the graph’s seed.")},
    {"audio.peaks", "start", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The span’s start; null: the unit’s start.")},
    {"audio.peaks", "end", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The span’s end; null: the unit’s end.")},
    {"units.highlights", "skip_blank", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Leave out highlights with no duration.")},
    {"units.inline_fx", "fx", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The inline-fx name; empty: units without one.")},
    {"values.number", "value", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The number.")},
    {"values.text", "value", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The text.")},
    {"values.colour", "value", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The colour, as #RRGGBB.")},
    {"values.point", "x", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Across.")},
    {"values.point", "y", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Down.")},
    {"values.choose", "type", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The values’ type.")},
    {"values.choose", "values", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The values, at least one.")},
    {"values.choose", "index", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The place.")},
    {"math.calculate", "op", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The operation.")},
    {"math.calculate", "a", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The first operand.")},
    {"math.calculate", "b", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The second operand.")},
    {"math.compare", "op", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The relation.")},
    {"math.compare", "a", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The first operand.")},
    {"math.compare", "b", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The second operand.")},
    {"math.progress", "ease", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The easing (quadratic).")},
    {"math.progress", "index", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The place, from 1.")},
    {"math.progress", "count", QT_TRANSLATE_NOOP("HikariSub.Nodes", "How many places.")},
    {"math.random", "min", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The smallest value.")},
    {"math.random", "max", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The largest value.")},
    {"math.random", "step", QT_TRANSLATE_NOOP("HikariSub.Nodes", "0: any value.")},
    {"math.random", "scope", QT_TRANSLATE_NOOP("HikariSub.Nodes", "One number per output line, per unit (the same across repeats and frames) or per line.")},
    {"math.random", "seed", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Added to the graph’s seed.")},
    {"time.retime", "mode", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The templater’s retime mode.")},
    {"time.retime", "start_offset", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Added to the start (sylpct: a percentage).")},
    {"time.retime", "end_offset", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Added to the end (sylpct: a percentage).")},
    {"time.stagger", "from", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Where the first unit is.")},
    {"time.stagger", "index", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The place, from 1.")},
    {"time.stagger", "count", QT_TRANSLATE_NOOP("HikariSub.Nodes", "How many places.")},
    {"time.stagger", "step", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The time between places.")},
    {"tags.tag", "tag", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The tag, without its backslash.")},
    {"tags.tag", "value", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Its number.")},
    {"tags.tag", "point", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Its point, as [x, y].")},
    {"tags.tag", "point2", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Its second point.")},
    {"tags.tag", "colour", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Its colour, as #RRGGBB.")},
    {"tags.tag", "alpha", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Its alpha, 0 (opaque) to 255.")},
    {"tags.tag", "text", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Its text.")},
    {"tags.move", "from", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Where it starts, as [x, y].")},
    {"tags.move", "to", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Where it ends.")},
    {"tags.move", "timed", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Write t1 and t2.")},
    {"tags.move", "t1", QT_TRANSLATE_NOOP("HikariSub.Nodes", "When it starts, from the line’s start.")},
    {"tags.move", "t2", QT_TRANSLATE_NOOP("HikariSub.Nodes", "When it ends.")},
    {"tags.fade", "fade_in", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The fade in, in ms.")},
    {"tags.fade", "fade_out", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The fade out, in ms.")},
    {"tags.transform", "timed", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Write t1 and t2.")},
    {"tags.transform", "t1", QT_TRANSLATE_NOOP("HikariSub.Nodes", "When it starts, from the line’s start.")},
    {"tags.transform", "t2", QT_TRANSLATE_NOOP("HikariSub.Nodes", "When it ends.")},
    {"tags.transform", "accel", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The acceleration.")},
    {"colours.gradient", "colours", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The stops, as #RRGGBB, at least one.")},
    {"colours.gradient", "progress", QT_TRANSLATE_NOOP("HikariSub.Nodes", "How far along.")},
    {"drawings.shape", "shape", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The drawing commands.")},
    {"drawings.shape", "scale", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The \\p scale.")},
    {"drawings.combine", "op", QT_TRANSLATE_NOOP("HikariSub.Nodes", "How to combine them.")},
    {"text.join", "unit_text", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The unit’s text while no text is wired: without tags, with them, or none.")},
    {"output.emit", "when", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Write the line.")},
    {"output.emit", "layer", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Its layer.")},
    {"output.emit", "order", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Its place among the emits for one unit; ties keep graph order.")},
    {"output.emit", "unit_text", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The unit’s text while no text or drawing is wired: without tags, with them, or none.")},
    {"groups.group", "graph", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The inner nodes and wires.")},
    {"groups.input", "ports", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The ports, each a name and a port type.")},
    {"groups.output", "ports", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The ports, each a name and a port type.")},
    {"lua.code", "code", QT_TRANSLATE_NOOP("HikariSub.Nodes", "The code.")},
    {"lua.code", "inputs", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Its inputs, each a name and a port type.")},
    {"lua.code", "outputs", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Its outputs.")},
};

constexpr CategoryText kCategories[] = {
    {"units", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Units")},
    {"values", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Values")},
    {"math", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Math")},
    {"time", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Time")},
    {"tags", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Tags")},
    {"colours", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Colours")},
    {"audio", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Audio")},
    {"drawings", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Drawings")},
    {"text", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Text")},
    {"output", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Output")},
    {"groups", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Groups")},
    {"lua", QT_TRANSLATE_NOOP("HikariSub.Nodes", "Lua")},
};

QString translated(const char *text)
{
    return QCoreApplication::translate("HikariSub.Nodes", text);
}

const NodeText *nodeText(std::string_view type)
{
    const auto it = std::ranges::find_if(kNodes, [&](const NodeText &n) { return std::string_view(n.type) == type; });
    return it == std::end(kNodes) ? nullptr : &*it;
}

const FieldText *fieldText(std::string_view type, std::string_view field)
{
    const auto it = std::ranges::find_if(kFields, [&](const FieldText &f) {
        return std::string_view(f.type) == type && std::string_view(f.field) == field;
    });
    return it == std::end(kFields) ? nullptr : &*it;
}

const CategoryText *categoryText(std::string_view category)
{
    const auto it = std::ranges::find_if(kCategories, [&](const CategoryText &c) {
        return std::string_view(c.category) == category;
    });
    return it == std::end(kCategories) ? nullptr : &*it;
}

QString utf8(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

// The catalogue's glossary terms (Line, Document) as interface copy writes them (docs/ux/text-style.md).
QString interfaceCopy(std::string_view summary)
{
    static const QRegularExpression term(QStringLiteral("\\b(Line|Document)"));
    static const QRegularExpression apostrophe(QStringLiteral("(?<=\\w)'|'(?=\\w)"));
    QString out = utf8(summary);
    for (auto it = term.globalMatch(out); it.hasNext();) {
        const auto m = it.next();
        const QString before = out.left(m.capturedStart()).trimmed();
        if (!before.isEmpty() && !QStringLiteral(".!?:").contains(before.back()))
            out[m.capturedStart()] = out[m.capturedStart()].toLower();
    }
    bool opening = true;
    for (QChar &c : out)
        if (c == u'"') {
            c = opening ? QChar(0x201C) : QChar(0x201D);
            opening = !opening;
        }
    return out.replace(apostrophe, QStringLiteral("\u2019"));
}

}

QString nodeTitle(std::string_view type)
{
    if (const application::PluginNode *plugin = application::pluginNode(type))
        return utf8(plugin->title);
    const NodeText *text = nodeText(type);
    return text ? translated(text->title) : utf8(type);
}

QString nodeSummary(std::string_view type)
{
    if (const application::PluginNode *plugin = application::pluginNode(type))
        return utf8(plugin->spec.summary);
    const NodeText *text = nodeText(type);
    return text ? translated(text->summary) : QString();
}

QString nodeFieldSummary(std::string_view type, std::string_view field)
{
    if (const application::PluginNode *plugin = application::pluginNode(type)) {
        const application::FieldSpec *f = application::fieldSpec(plugin->spec, field);
        return f ? utf8(f->summary) : QString();
    }
    const FieldText *text = fieldText(type, field);
    return text ? translated(text->summary) : QString();
}

QString nodeCategoryTitle(std::string_view category)
{
    for (const application::PluginNode &plugin : application::pluginNodes())
        if (plugin.spec.category == category)
            return utf8(plugin.pluginName);
    const CategoryText *text = categoryText(category);
    return text ? translated(text->title) : utf8(category);
}

std::vector<std::string> untranslatedCatalogueText(const std::vector<application::NodeSpec> &catalogue)
{
    std::vector<std::string> missing;
    for (const application::NodeSpec &spec : catalogue) {
        if (const NodeText *node = nodeText(spec.id); !node || QString::fromUtf8(node->summary) != interfaceCopy(spec.summary))
            missing.push_back(spec.id);
        if (!categoryText(spec.category))
            missing.push_back(spec.category);
        for (const application::FieldSpec &field : spec.fields)
            if (const FieldText *text = fieldText(spec.id, field.name); !text || QString::fromUtf8(text->summary) != interfaceCopy(field.summary))
                missing.push_back(spec.id + "." + field.name);
    }
    return missing;
}

}
