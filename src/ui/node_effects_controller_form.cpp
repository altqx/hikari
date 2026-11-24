#include "node_catalogue_text.h"
#include "node_effects_controller.h"
#include "node_field_values.h"

#include "hikari/application/node_catalogue.h"
#include "hikari/application/node_graph_editing.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <algorithm>

namespace hikari::ui {

using application::FieldKind;
using application::FieldSpec;
using application::GraphNode;
using application::NodeGraph;
using json::Json;

namespace {

std::vector<const GraphNode *> chosenNodes(const NodeGraph &graph, const QStringList &selection)
{
    std::vector<const GraphNode *> out;
    for (const QString &id : selection) {
        const auto it = std::ranges::find(graph.nodes, id.toStdString(), &GraphNode::id);
        if (it != graph.nodes.end())
            out.push_back(&*it);
    }
    return out;
}

bool oneType(const std::vector<const GraphNode *> &nodes)
{
    return !nodes.empty() && std::ranges::all_of(nodes, [&](const GraphNode *n) { return n->type == nodes.front()->type; });
}

const FieldSpec *fieldOf(const std::vector<const GraphNode *> &nodes, const QString &field)
{
    const application::NodeSpec *spec = oneType(nodes) ? application::nodeSpec(nodes.front()->type) : nullptr;
    return spec ? application::fieldSpec(*spec, field.toStdString()) : nullptr;
}

bool sameValue(const Json &a, const Json &b)
{
    return a.isNumber() && b.isNumber() ? a.asNumber() == b.asNumber() : a == b;
}

bool changedOn(const GraphNode &node, const FieldSpec &f)
{
    const Json *stored = node.fields.isObject() ? node.fields.find(f.name) : nullptr;
    return stored && !stored->isNull() && (f.fallback.isNull() || !sameValue(*stored, f.fallback));
}

struct Range {
    std::optional<double> minimum, maximum;
};

Range rangeOf(const FieldSpec &f)
{
    if (f.kind == FieldKind::Alpha)
        return {0, 255};
    return {f.minimum, f.maximum};
}

QString atLeast(double minimum)
{
    //: A field's range in the Node panel, and the refusal of a smaller value: "At least 1"
    return QCoreApplication::translate("HikariSub.NodeEffects", "At least %1").arg(minimum);
}

QString atMost(double maximum)
{
    //: A field's range in the Node panel, and the refusal of a larger value: "At most 400"
    return QCoreApplication::translate("HikariSub.NodeEffects", "At most %1").arg(maximum);
}

QString rangeText(const Range &r)
{
    if (r.minimum && r.maximum)
        return QStringLiteral("%1 – %2").arg(*r.minimum).arg(*r.maximum);
    if (r.minimum)
        return atLeast(*r.minimum);
    if (r.maximum)
        return atMost(*r.maximum);
    return {};
}

QString outOfRange(const Range &r, double v)
{
    const bool below = r.minimum && v < *r.minimum, above = r.maximum && v > *r.maximum;
    if (!below && !above)
        return {};
    if (r.minimum && r.maximum)
        //: A refused value in the Node panel: "Between 10 and 400"
        return QCoreApplication::translate("HikariSub.NodeEffects", "Between %1 and %2").arg(*r.minimum).arg(*r.maximum);
    return r.minimum ? atLeast(*r.minimum) : atMost(*r.maximum);
}

constexpr const char *kUnits[] = {
    //: A unit after a number in the Node panel: pixels
    QT_TRANSLATE_NOOP("HikariSub.NodeEffects", "px"),
};

QString unitOf(const FieldSpec &f)
{
    if (f.kind == FieldKind::Time)
        //: The unit after a time in the Node panel: milliseconds
        return QCoreApplication::translate("HikariSub.NodeEffects", "ms");
    if (std::ranges::find(kUnits, f.unit) != std::end(kUnits))
        return QCoreApplication::translate("HikariSub.NodeEffects", f.unit.c_str());
    return QString::fromStdString(f.unit);
}

QVariantMap wireEnd(const std::optional<application::GraphEnd> &from)
{
    if (!from)
        return {{QStringLiteral("from"), QString()}};
    const QString node = QString::fromStdString(from->node), port = QString::fromStdString(from->port);
    return {{QStringLiteral("from"), QStringLiteral("%1 › %2").arg(node, port)},
            {QStringLiteral("fromNode"), node},
            {QStringLiteral("fromPort"), port}};
}

std::optional<application::GraphEnd> feedOf(const NodeGraph &graph, const std::string &node, const std::string &port)
{
    for (const application::GraphWire &w : graph.wires)
        if (w.to.node == node && w.to.port == port)
            return w.from;
    return std::nullopt;
}

QVariantMap fieldRow(const NodeGraph &graph, const std::vector<const GraphNode *> &nodes, const FieldSpec &f)
{
    const Json first = application::nodeField(*nodes.front(), f.name);
    const bool mixed = std::ranges::any_of(nodes, [&](const GraphNode *n) {
        return !sameValue(application::nodeField(*n, f.name), first);
    });
    QStringList choices;
    for (const std::string &c : f.choices)
        choices << QString::fromStdString(c);
    const Range range = rangeOf(f);
    QVariantMap row{{QStringLiteral("name"), QString::fromStdString(f.name)},
                    {QStringLiteral("kind"), node_fields::kindName(f.kind)},
                    {QStringLiteral("value"), mixed ? QVariant() : node_fields::variantOf(first)},
                    {QStringLiteral("mixed"), mixed},
                    {QStringLiteral("choices"), choices},
                    {QStringLiteral("minimum"), range.minimum ? QVariant(*range.minimum) : QVariant()},
                    {QStringLiteral("maximum"), range.maximum ? QVariant(*range.maximum) : QVariant()},
                    {QStringLiteral("range"), rangeText(range)},
                    {QStringLiteral("unit"), unitOf(f)},
                    {QStringLiteral("changed"), std::ranges::any_of(nodes, [&](const GraphNode *n) { return changedOn(*n, f); })},
                    {QStringLiteral("summary"), nodeFieldSummary(nodes.front()->type, f.name)}};
    if (nodes.size() == 1)
        row.insert(wireEnd(feedOf(graph, nodes.front()->id, f.name)));
    return row;
}

QVariantList wiredRows(const NodeGraph &graph, const GraphNode &node, const application::NodeSpec &spec)
{
    QVariantList out;
    const auto ports = application::nodePorts(node);
    if (!ports)
        return out;
    for (const application::PortSpec &p : ports->inputs) {
        if (application::fieldSpec(spec, p.name))
            continue;
        QVariantMap row = wireEnd(feedOf(graph, node.id, p.name));
        row.insert(QStringLiteral("name"), QString::fromStdString(p.name));
        row.insert(QStringLiteral("type"), QString::fromStdString(std::string(application::portTypeName(p.type))));
        out << row;
    }
    return out;
}

}

QVariantMap NodeEffectsController::form() const
{
    const std::optional<NodeGraph> v = view();
    const std::vector<const GraphNode *> nodes = v ? chosenNodes(*v, m_selection) : std::vector<const GraphNode *>{};
    if (nodes.empty())
        return {};
    QStringList ids;
    for (const GraphNode *n : nodes)
        ids << QString::fromStdString(n->id);
    const bool same = oneType(nodes);
    const std::string &type = nodes.front()->type;
    QVariantMap out{{QStringLiteral("ids"), ids},
                    {QStringLiteral("count"), int(nodes.size())},
                    {QStringLiteral("oneType"), same},
                    {QStringLiteral("type"), QString::fromStdString(type)},
                    {QStringLiteral("title"), nodeTitle(type)}};
    const application::NodeSpec *spec = same ? application::nodeSpec(type) : nullptr;
    if (!spec)
        return out;
    QVariantList fields;
    for (const FieldSpec &f : spec->fields) {
        if (f.kind == FieldKind::Ports || f.kind == FieldKind::Graph)
            continue;
        if (nodes.size() > 1 && (f.kind == FieldKind::List || (type == "lua.code" && f.name == "code")))
            continue;
        fields << fieldRow(*v, nodes, f);
    }
    out.insert(QStringLiteral("fields"), fields);
    if (nodes.size() == 1)
        out.insert(QStringLiteral("wired"), wiredRows(*v, *nodes.front(), *spec));
    return out;
}

QString NodeEffectsController::fieldRefusal(const QString &field, const QVariant &value) const
{
    const std::optional<NodeGraph> v = view();
    const FieldSpec *f = v ? fieldOf(chosenNodes(*v, m_selection), field) : nullptr;
    if (!f)
        return {};
    const Json j = node_fields::fieldJson(f, value);
    if (node_fields::numericKind(f->kind))
        return j.isNumber() ? outOfRange(rangeOf(*f), j.asNumber()) : QCoreApplication::translate("HikariSub.NodeEffects", "Enter a number");
    if (f->kind == FieldKind::Point) {
        const bool numbers = j.isArray() && j.asArray().size() == 2 &&
                             std::ranges::all_of(j.asArray(), [](const Json &e) { return e.isNumber(); });
        return numbers ? QString() : QCoreApplication::translate("HikariSub.NodeEffects", "Enter X and Y as numbers");
    }
    if (f->kind == FieldKind::Colour) {
        const QString text = value.toString();
        static const QRegularExpression hex(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
        return hex.match(text).hasMatch() ? QString() : QCoreApplication::translate("HikariSub.NodeEffects", "Enter a colour as #RRGGBB");
    }
    return {};
}

bool NodeEffectsController::setSelectedField(const QString &field, const QVariant &value)
{
    const std::optional<NodeGraph> v = view();
    const std::vector<const GraphNode *> nodes = v ? chosenNodes(*v, m_selection) : std::vector<const GraphNode *>{};
    const FieldSpec *f = fieldOf(nodes, field);
    if (!f || !fieldRefusal(field, value).isEmpty())
        return false;
    const Json j = node_fields::fieldJson(f, value);
    if (std::ranges::all_of(nodes, [&](const GraphNode *n) {
            const Json *had = n->fields.isObject() ? n->fields.find(f->name) : nullptr;
            return had && *had == j;
        }))
        return true;
    std::vector<std::string> ids;
    for (const GraphNode *n : nodes)
        ids.push_back(n->id);
    editView(application::withFieldOnNodes(*v, ids, f->name, j));
    return true;
}

void NodeEffectsController::resetSelectedField(const QString &field)
{
    const std::optional<NodeGraph> v = view();
    const std::vector<const GraphNode *> nodes = v ? chosenNodes(*v, m_selection) : std::vector<const GraphNode *>{};
    const FieldSpec *f = fieldOf(nodes, field);
    if (!f || std::ranges::none_of(nodes, [&](const GraphNode *n) { return n->fields.find(f->name) != nullptr; }))
        return;
    std::vector<std::string> ids;
    for (const GraphNode *n : nodes)
        ids.push_back(n->id);
    editView(application::withFieldOnNodes(*v, ids, f->name, std::nullopt));
}

QString NodeEffectsController::nodeNameRefusal(const QString &from, const QString &to) const
{
    if (from == to)
        return {};
    if (to.isEmpty())
        return QCoreApplication::translate("HikariSub.NodeEffects", "Enter a name");
    if (!application::isNodeId(to.toStdString()))
        return QCoreApplication::translate("HikariSub.NodeEffects", "Use only letters, digits, _ and -");
    const std::optional<NodeGraph> v = view();
    if (v && std::ranges::any_of(v->nodes, [&](const GraphNode &n) { return n.id == to.toStdString(); }))
        //: A refused node name: “fade” is the name another node has
        return QCoreApplication::translate("HikariSub.NodeEffects", "Another node is called “%1”.").arg(to);
    return {};
}

}
