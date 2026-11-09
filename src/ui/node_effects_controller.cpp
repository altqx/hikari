#include "node_effects_controller.h"

#include "node_catalogue_text.h"
#include "template_editor_controller.h"

#include "hikari/application/node_graph_editing.h"
#include "hikari/application/node_graph_library.h"
#include "hikari/application/template_preview.h"

#include <QCoreApplication>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace hikari::ui {

using application::GraphEnd;
using application::GraphNode;
using application::GraphWire;
using application::NodeGraph;
using json::Json;

namespace {

QString qstr(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

QString qstr(std::u8string_view s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), qsizetype(s.size()));
}

std::string utf8(const QString &s)
{
    return s.toStdString();
}

std::u8string wide(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return std::u8string(b.begin(), b.end());
}

QString number(double v)
{
    char buffer[32];
    const auto [end, ec] = std::to_chars(buffer, buffer + sizeof buffer, v, std::chars_format::general, 6);
    return ec == std::errc() ? QString::fromLatin1(buffer, end - buffer) : QString();
}

QString hexColour(const application::NodeColour &c)
{
    return QStringLiteral("#%1%2%3")
        .arg(c.r, 2, 16, QLatin1Char('0'))
        .arg(c.g, 2, 16, QLatin1Char('0'))
        .arg(c.b, 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString valueText(const application::NodeValue &v)
{
    return std::visit(
        [](const auto &value) -> QString {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, double>)
                return number(value);
            else if constexpr (std::is_same_v<T, bool>)
                return value ? QCoreApplication::translate("HikariSub.NodeEffects", "yes") : QCoreApplication::translate("HikariSub.NodeEffects", "no");
            else if constexpr (std::is_same_v<T, std::string>)
                return qstr(value);
            else if constexpr (std::is_same_v<T, application::NodeColour>)
                return hexColour(value);
            else if constexpr (std::is_same_v<T, application::NodePoint>)
                return QStringLiteral("(%1, %2)").arg(number(value.x), number(value.y));
            else if constexpr (std::is_same_v<T, application::NodeTags>)
                return qstr(value.text);
            else
                return qstr(value.commands);
        },
        v);
}

QVariant variantOf(const Json &j)
{
    switch (j.type()) {
    case Json::Type::Bool:
        return j.asBool();
    case Json::Type::Int:
    case Json::Type::Double:
        return j.asNumber();
    case Json::Type::String:
        return qstr(j.asString());
    case Json::Type::Array: {
        QVariantList out;
        for (const Json &e : j.asArray())
            out << variantOf(e);
        return out;
    }
    case Json::Type::Object: {
        QVariantMap out;
        for (const Json::Member &m : j.asObject())
            out.insert(qstr(m.key), variantOf(m.value));
        return out;
    }
    default:
        return {};
    }
}

Json jsonOf(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Bool:
        return Json(v.toBool());
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        return Json(v.toDouble());
    case QMetaType::QString:
        return Json(utf8(v.toString()));
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        Json out = Json::array();
        for (const QVariant &e : v.toList())
            out.push(jsonOf(e));
        return out;
    }
    case QMetaType::QVariantMap: {
        Json out = Json::object();
        const QVariantMap map = v.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            out[utf8(it.key())] = jsonOf(it.value());
        return out;
    }
    default:
        return v.isNull() ? Json() : Json(utf8(v.toString()));
    }
}

bool numericKind(application::FieldKind kind)
{
    using enum application::FieldKind;
    return kind == Number || kind == Integer || kind == Alpha || kind == Time;
}

// A field as the user typed it, in the type its kind takes.
Json fieldJson(const application::FieldSpec *spec, const QVariant &value)
{
    if (spec && numericKind(spec->kind) && value.typeId() == QMetaType::QString) {
        bool ok = false;
        const double d = value.toString().trimmed().toDouble(&ok);
        if (!ok)
            return value.toString().trimmed().isEmpty() ? Json() : Json(utf8(value.toString()));
        return spec->kind == application::FieldKind::Integer ? Json(static_cast<std::int64_t>(std::llround(d))) : Json(d);
    }
    if (spec && spec->kind == application::FieldKind::Integer && value.canConvert<double>() &&
        value.typeId() != QMetaType::QString)
        return Json(static_cast<std::int64_t>(std::llround(value.toDouble())));
    return jsonOf(value);
}

QString constantText(const Json &j)
{
    if (j.isNumber())
        return number(j.asNumber());
    if (j.isBool())
        return j.asBool() ? QCoreApplication::translate("HikariSub.NodeEffects", "yes") : QCoreApplication::translate("HikariSub.NodeEffects", "no");
    if (j.isString())
        return qstr(j.asString());
    if (j.isArray()) {
        QStringList parts;
        for (const Json &e : j.asArray())
            parts << constantText(e);
        return QStringLiteral("(%1)").arg(parts.join(QStringLiteral(", ")));
    }
    return {};
}

QString kindName(application::FieldKind kind)
{
    using enum application::FieldKind;
    switch (kind) {
    case Number: return QStringLiteral("Number");
    case Integer: return QStringLiteral("Integer");
    case Text: return QStringLiteral("Text");
    case Bool: return QStringLiteral("Bool");
    case Colour: return QStringLiteral("Colour");
    case Alpha: return QStringLiteral("Alpha");
    case Time: return QStringLiteral("Time");
    case Point: return QStringLiteral("Point");
    case Choice: return QStringLiteral("Choice");
    case List: return QStringLiteral("List");
    case Ports: return QStringLiteral("Ports");
    case Graph: return QStringLiteral("Graph");
    }
    return {};
}

QString portTypeTitle(application::PortType type)
{
    using enum application::PortType;
    switch (type) {
    case Number: return QCoreApplication::translate("HikariSub.NodeEffects", "Number");
    case Text: return QCoreApplication::translate("HikariSub.NodeEffects", "Text");
    case Colour: return QCoreApplication::translate("HikariSub.NodeEffects", "Colour");
    case Alpha: return QCoreApplication::translate("HikariSub.NodeEffects", "Alpha");
    case Time: return QCoreApplication::translate("HikariSub.NodeEffects", "Time");
    case Point: return QCoreApplication::translate("HikariSub.NodeEffects", "Point");
    case Tags: return QCoreApplication::translate("HikariSub.NodeEffects", "Tags");
    case Drawing: return QCoreApplication::translate("HikariSub.NodeEffects", "Drawing");
    case Bool: return QCoreApplication::translate("HikariSub.NodeEffects", "Yes or no");
    case Units: return QCoreApplication::translate("HikariSub.NodeEffects", "Units");
    case Any: return QCoreApplication::translate("HikariSub.NodeEffects", "Value");
    }
    return {};
}

QVariantList portsList(const std::vector<application::PortSpec> &ports)
{
    QVariantList out;
    for (const application::PortSpec &p : ports)
        out << QVariantMap{{QStringLiteral("name"), qstr(p.name)},
                           {QStringLiteral("type"), qstr(application::portTypeName(p.type))}};
    return out;
}

const GraphNode *nodeOf(const NodeGraph &graph, const std::string &id)
{
    const auto it = std::ranges::find(graph.nodes, id, &GraphNode::id);
    return it == graph.nodes.end() ? nullptr : &*it;
}

std::optional<GraphEnd> feeding(const NodeGraph &graph, const std::string &node, const std::string &port)
{
    for (const GraphWire &w : graph.wires)
        if (w.to.node == node && w.to.port == port)
            return w.from;
    return std::nullopt;
}

bool feedsOut(const NodeGraph &graph, const std::string &node, const std::string &port)
{
    return std::ranges::any_of(graph.wires, [&](const GraphWire &w) { return w.from.node == node && w.from.port == port; });
}

QString stripped(std::u8string_view text)
{
    QString out;
    int depth = 0;
    for (const QChar c : qstr(text)) {
        if (c == QLatin1Char('{'))
            ++depth;
        else if (c == QLatin1Char('}') && depth > 0)
            --depth;
        else if (depth == 0)
            out += c;
    }
    return out;
}

constexpr const char *kRandom = "math.random";
constexpr const char *kLua = "lua.code";
constexpr const char *kGroup = "groups.group";
constexpr int kRecentCount = 5;

}

NodeEffectsController::NodeEffectsController(QObject *parent) : QObject(parent)
{
    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(150);
    connect(&m_previewTimer, &QTimer::timeout, this, &NodeEffectsController::runPreview);
    m_commitTimer.setSingleShot(true);
    m_commitTimer.setInterval(800);
    connect(&m_commitTimer, &QTimer::timeout, this, &NodeEffectsController::commit);
}

NodeEffectsController::~NodeEffectsController() = default;

void NodeEffectsController::setSessionSource(SessionSource source)
{
    m_sessionSource = std::move(source);
}

void NodeEffectsController::retranslate()
{
    emit textsChanged();
    emit graphChanged();
    emit inspectorChanged();
    emit recentChanged();
}

void NodeEffectsController::setLuaNodes(LuaNodesSource source)
{
    m_luaNodes = std::move(source);
}

void NodeEffectsController::shareTrustWith(TemplateEditorController *editor)
{
    m_trustOwner = editor;
    connect(editor, &TemplateEditorController::documentTrusted, this, [this] {
        emit changed();
        schedulePreview();
    });
}

void NodeEffectsController::setLibraryFolder(std::filesystem::path folder)
{
    m_libraryFolder = std::move(folder);
    emit libraryChanged();
}

void NodeEffectsController::setEditable(bool editable)
{
    if (m_editable == editable)
        return;
    m_editable = editable;
    emit changed();
}

void NodeEffectsController::setDelays(int previewMs, int commitMs)
{
    m_previewTimer.setInterval(previewMs);
    m_commitTimer.setInterval(commitMs);
}

application::EditSession *NodeEffectsController::session() const
{
    return m_sessionSource ? m_sessionSource() : nullptr;
}

const core::Document *NodeEffectsController::document() const
{
    const auto *s = session();
    return s ? &s->document() : nullptr;
}

const application::DocumentGraph *NodeEffectsController::currentGraph() const
{
    const auto it = std::ranges::find_if(m_graphs, [&](const application::DocumentGraph &g) { return g.line == m_current; });
    return it == m_graphs.end() ? nullptr : &*it;
}

std::optional<NodeGraph> NodeEffectsController::view() const
{
    return m_graph ? application::graphAt(*m_graph, m_path) : std::nullopt;
}

void NodeEffectsController::setShown(bool shown)
{
    if (m_shown == shown)
        return;
    m_shown = shown;
    if (!shown)
        commit();
    else
        refresh();
    emit changed();
    schedulePreview();
}

void NodeEffectsController::refresh()
{
    const auto *s = session();
    if (s != m_seenSession) {
        commit();
        m_seenSession = s;
        m_seenRevision = 0;
        m_current.reset();
        m_graph.reset();
        m_path.clear();
        m_selection.clear();
        m_source.reset();
        m_sourceSelection.clear();
        m_preview.reset();
        m_cache.clear();
        emit selectionChanged();
    }
    if (s && s->revision() == m_seenRevision && !m_graphs.empty() == m_current.has_value())
        return;
    m_seenRevision = s ? s->revision() : 0;
    reload();
}

void NodeEffectsController::reload()
{
    const core::Document *doc = document();
    m_graphs = doc ? application::documentGraphs(*doc) : std::vector<application::DocumentGraph>{};
    if (!currentGraph())
        m_current = m_graphs.empty() ? std::nullopt : std::optional(m_graphs.front().line);
    m_unreadable.clear();
    m_newerFormat = false;
    if (!m_pending) {
        const application::DocumentGraph *g = currentGraph();
        if (g && g->graph) {
            m_graph = application::laidOutGraph(*g->graph);
        } else {
            m_graph.reset();
            if (g) {
                m_unreadable = qstr(g->graph.error().message);
                const core::LineRecord *line = nullptr;
                for (const core::LineRecord *l : doc->lines())
                    if (l->id == g->line)
                        line = l;
                const std::u8string &text = line->text;
                if (const auto j = Json::parse(std::string_view(reinterpret_cast<const char *>(text.data()), text.size()));
                    j && j->isObject())
                    if (const Json *format = j->find("format"); format && format->isNumber())
                        m_newerFormat = format->asNumber() > application::kNodeGraphFormat;
            }
        }
    }
    if (!view())
        m_path.clear();
    const std::optional<NodeGraph> v = view();
    const qsizetype before = m_selection.size();
    m_selection.removeIf([&](const QString &id) { return !v || !nodeOf(*v, utf8(id)); });
    if (m_selection.size() != before)
        emit selectionChanged();
    updateSources();
    emit changed();
    emit graphChanged();
    emit inspectorChanged();
    schedulePreview();
}

void NodeEffectsController::updateSources()
{
    m_sources.clear();
    const core::Document *doc = document();
    const application::DocumentGraph *g = currentGraph();
    if (doc && g)
        for (const core::LineRecord *l : doc->lines())
            if (application::isKaraokeLine(*l) && ((m_graph && m_graph->allStyles) || l->style == g->style))
                m_sources.push_back(l->id);
    if (!m_source || std::ranges::find(m_sources, *m_source) == m_sources.end())
        m_source = m_sources.empty() ? std::nullopt : std::optional(m_sources.front());
    std::erase_if(m_sourceSelection, [&](core::LineId id) { return std::ranges::find(m_sources, id) == m_sources.end(); });
}

QVariantList NodeEffectsController::graphs() const
{
    QVariantList out;
    for (const application::DocumentGraph &g : m_graphs)
        out << QVariantMap{{QStringLiteral("line"), qulonglong(g.line.value)},
                           {QStringLiteral("name"), qstr(g.name)},
                           {QStringLiteral("style"), qstr(g.style)},
                           {QStringLiteral("readable"), g.graph.has_value()}};
    return out;
}

void NodeEffectsController::setCurrent(qulonglong id)
{
    if (current() == id)
        return;
    commit();
    if (std::ranges::none_of(m_graphs, [&](const application::DocumentGraph &g) { return g.line.value == id; }))
        return;
    m_current = core::LineId{id};
    m_path.clear();
    m_selection.clear();
    emit selectionChanged();
    reload();
}

QString NodeEffectsController::name() const
{
    const application::DocumentGraph *g = currentGraph();
    return g ? qstr(g->name) : QString();
}

QString NodeEffectsController::style() const
{
    const application::DocumentGraph *g = currentGraph();
    return g ? qstr(g->style) : QString();
}

QStringList NodeEffectsController::styles() const
{
    QStringList out;
    if (const core::Document *doc = document())
        for (const auto &section : doc->sections())
            for (const auto &record : section.records)
                if (const auto *style = std::get_if<core::StyleRecord>(&record); style && !style->fields.empty())
                    out << qstr(style->fields.front());
    return out;
}

QStringList NodeEffectsController::path() const
{
    QStringList out;
    for (const std::string &id : m_path)
        out << qstr(id);
    return out;
}

QVariantList NodeEffectsController::nodes() const
{
    QVariantList out;
    const std::optional<NodeGraph> v = view();
    if (!v)
        return out;
    QSet<QString> broken;
    for (const QVariant &p : problems())
        broken.insert(p.toMap().value(QStringLiteral("viewNode")).toString());
    for (const GraphNode &n : v->nodes) {
        const application::NodeSpec *spec = application::nodeSpec(n.type);
        const auto ports = application::nodePorts(n);
        QVariantList inputs, outputs;
        if (ports) {
            for (const application::PortSpec &p : ports->inputs) {
                const bool wired = feeding(*v, n.id, p.name).has_value();
                const bool hasField = spec && application::fieldSpec(*spec, p.name);
                inputs << QVariantMap{{QStringLiteral("name"), qstr(p.name)},
                                      {QStringLiteral("type"), qstr(application::portTypeName(p.type))},
                                      {QStringLiteral("wired"), wired},
                                      {QStringLiteral("constant"),
                                       wired || !hasField ? QString() : constantText(application::nodeField(n, p.name))}};
            }
            for (const application::PortSpec &p : ports->outputs)
                outputs << QVariantMap{{QStringLiteral("name"), qstr(p.name)},
                                       {QStringLiteral("type"), qstr(application::portTypeName(p.type))},
                                       {QStringLiteral("wired"), feedsOut(*v, n.id, p.name)}};
        } else {
            QStringList seenIn, seenOut;
            for (const GraphWire &w : v->wires) {
                if (w.to.node == n.id && !seenIn.contains(qstr(w.to.port))) {
                    seenIn << qstr(w.to.port);
                    inputs << QVariantMap{{QStringLiteral("name"), qstr(w.to.port)}, {QStringLiteral("type"), QStringLiteral("Any")},
                                          {QStringLiteral("wired"), true}, {QStringLiteral("constant"), QString()}};
                }
                if (w.from.node == n.id && !seenOut.contains(qstr(w.from.port))) {
                    seenOut << qstr(w.from.port);
                    outputs << QVariantMap{{QStringLiteral("name"), qstr(w.from.port)},
                                           {QStringLiteral("type"), QStringLiteral("Any")}, {QStringLiteral("wired"), true}};
                }
            }
        }
        QVariantList fieldRows;
        if (spec)
            for (const application::FieldSpec &f : spec->fields) {
                using enum application::FieldKind;
                const bool isPort = ports && std::ranges::any_of(ports->inputs, [&](const application::PortSpec &p) {
                    return p.name == f.name;
                });
                if (isPort || f.kind == Graph || f.kind == Ports || (n.type == kLua && f.name == "code") ||
                    (n.type == "tags.tag" && f.name == "tag"))
                    continue;
                const Json value = application::nodeField(n, f.name);
                fieldRows << QVariantMap{{QStringLiteral("name"), qstr(f.name)},
                                         {QStringLiteral("kind"), kindName(f.kind)},
                                         {QStringLiteral("text"), constantText(value)},
                                         {QStringLiteral("value"), variantOf(value)}};
            }
        QString title = nodeTitle(n.type);
        if (n.type == "tags.tag")
            title += QStringLiteral(" \\") + constantText(application::nodeField(n, "tag"));
        const auto [x, y] = n.at.value_or(std::pair(0.0, 0.0));
        out << QVariantMap{{QStringLiteral("id"), qstr(n.id)},
                           {QStringLiteral("type"), qstr(n.type)},
                           {QStringLiteral("title"), title},
                           {QStringLiteral("fields"), fieldRows},
                           {QStringLiteral("code"), n.type == kLua ? constantText(application::nodeField(n, "code")) : QString()},
                           {QStringLiteral("known"), spec != nullptr},
                           {QStringLiteral("group"), n.type == kGroup},
                           {QStringLiteral("lua"), n.type == kLua},
                           {QStringLiteral("folds"), application::foldsUnwiredOutputs(n)},
                           {QStringLiteral("x"), x},
                           {QStringLiteral("y"), y},
                           {QStringLiteral("problem"), broken.contains(qstr(n.id))},
                           {QStringLiteral("inputs"), inputs},
                           {QStringLiteral("outputs"), outputs}};
    }
    return out;
}

QVariantList NodeEffectsController::wires() const
{
    QVariantList out;
    const std::optional<NodeGraph> v = view();
    if (!v)
        return out;
    for (const GraphWire &w : v->wires) {
        QString type = QStringLiteral("Any");
        if (const GraphNode *from = nodeOf(*v, w.from.node))
            if (const auto ports = application::nodePorts(*from))
                for (const application::PortSpec &p : ports->outputs)
                    if (p.name == w.from.port)
                        type = qstr(application::portTypeName(p.type));
        out << QVariantMap{{QStringLiteral("fromNode"), qstr(w.from.node)}, {QStringLiteral("fromPort"), qstr(w.from.port)},
                           {QStringLiteral("toNode"), qstr(w.to.node)},     {QStringLiteral("toPort"), qstr(w.to.port)},
                           {QStringLiteral("type"), type}};
    }
    return out;
}

QVariantList NodeEffectsController::problems() const
{
    QVariantList out;
    if (!m_graph)
        return out;
    std::string prefix;
    for (const std::string &id : m_path)
        prefix += id + "/";
    const auto add = [&](const std::string &node, const std::string &port, const std::string &message) {
        QString viewNode;
        if (node.starts_with(prefix) && node.size() > prefix.size()) {
            const std::string rest = node.substr(prefix.size());
            viewNode = qstr(rest.substr(0, rest.find('/')));
        }
        out << QVariantMap{{QStringLiteral("node"), qstr(node)},
                           {QStringLiteral("viewNode"), viewNode},
                           {QStringLiteral("port"), qstr(port)},
                           {QStringLiteral("message"), qstr(message)}};
    };
    const std::vector<application::GraphProblem> found = application::validateGraph(*m_graph);
    for (const application::GraphProblem &p : found)
        add(p.node, p.port, p.message);
    if (found.empty())
        for (const application::NodeEffectProblem &p : m_runProblems)
            add(p.node, {}, p.message);
    return out;
}

void NodeEffectsController::setSelection(const QStringList &nodes)
{
    if (m_selection == nodes)
        return;
    m_selection = nodes;
    emit selectionChanged();
    emit inspectorChanged();
}

QVariantMap NodeEffectsController::inspector() const
{
    const std::optional<NodeGraph> v = view();
    const GraphNode *n = v && !m_selection.isEmpty() ? nodeOf(*v, utf8(m_selection.front())) : nullptr;
    if (!n)
        return {};
    const application::NodeSpec *spec = application::nodeSpec(n->type);
    const auto ports = application::nodePorts(*n);
    const auto fromText = [&](const std::string &port) -> QString {
        const auto from = feeding(*v, n->id, port);
        return from ? QStringLiteral("%1 › %2").arg(qstr(from->node), qstr(from->port)) : QString();
    };
    QVariantList fields;
    QStringList fieldNames;
    if (spec)
        for (const application::FieldSpec &f : spec->fields) {
            fieldNames << qstr(f.name);
            QStringList choices;
            for (const std::string &c : f.choices)
                choices << qstr(c);
            fields << QVariantMap{{QStringLiteral("name"), qstr(f.name)},
                                  {QStringLiteral("kind"), kindName(f.kind)},
                                  {QStringLiteral("value"), variantOf(application::nodeField(*n, f.name))},
                                  {QStringLiteral("choices"), choices},
                                  {QStringLiteral("minimum"), f.minimum ? QVariant(*f.minimum) : QVariant()},
                                  {QStringLiteral("maximum"), f.maximum ? QVariant(*f.maximum) : QVariant()},
                                  {QStringLiteral("summary"), nodeFieldSummary(n->type, f.name)},
                                  {QStringLiteral("from"), fromText(f.name)}};
        }
    QVariantList inputs, outputs, values;
    if (ports) {
        for (const application::PortSpec &p : ports->inputs)
            if (!fieldNames.contains(qstr(p.name)))
                inputs << QVariantMap{{QStringLiteral("name"), qstr(p.name)},
                                      {QStringLiteral("type"), qstr(application::portTypeName(p.type))},
                                      {QStringLiteral("typeTitle"), portTypeTitle(p.type)},
                                      {QStringLiteral("from"), fromText(p.name)}};
        std::string full;
        for (const std::string &id : m_path)
            full += id + "/";
        full += n->id;
        const auto traced = m_trace.values.find(full);
        for (const application::PortSpec &p : ports->outputs) {
            outputs << QVariantMap{{QStringLiteral("name"), qstr(p.name)},
                                   {QStringLiteral("type"), qstr(application::portTypeName(p.type))},
                                   {QStringLiteral("typeTitle"), portTypeTitle(p.type)}};
            if (traced != m_trace.values.end())
                if (const auto value = traced->second.find(p.name); value != traced->second.end())
                    values << QVariantMap{{QStringLiteral("port"), qstr(p.name)},
                                          {QStringLiteral("text"), valueText(value->second)}};
        }
    }
    QVariantList warnings;
    for (const QVariant &p : problems())
        if (p.toMap().value(QStringLiteral("viewNode")).toString() == qstr(n->id))
            warnings << p;
    QVariantMap emitted;
    if (m_preview)
        for (const application::EmitReport &e : m_preview->emits)
            if (e.node == n->id && m_path.empty())
                emitted = {{QStringLiteral("lines"), qulonglong(e.generated)}, {QStringLiteral("ms"), e.seconds * 1000}};
    return {{QStringLiteral("id"), qstr(n->id)},
            {QStringLiteral("type"), qstr(n->type)},
            {QStringLiteral("title"), nodeTitle(n->type)},
            {QStringLiteral("summary"), nodeSummary(n->type)},
            {QStringLiteral("known"), spec != nullptr},
            {QStringLiteral("category"), spec ? nodeCategoryTitle(spec->category) : QString()},
            {QStringLiteral("group"), n->type == kGroup},
            {QStringLiteral("lua"), n->type == kLua},
            {QStringLiteral("random"), n->type == kRandom},
            {QStringLiteral("graphSeed"), m_graph ? qint64(m_graph->seed) : 0},
            {QStringLiteral("fields"), fields},
            {QStringLiteral("inputs"), inputs},
            {QStringLiteral("outputs"), outputs},
            {QStringLiteral("values"), values},
            {QStringLiteral("valuesLine"), sourceNumber()},
            {QStringLiteral("warnings"), warnings},
            {QStringLiteral("emitted"), emitted}};
}

QVariantList NodeEffectsController::catalogue()
{
    QVariantList out;
    for (const application::NodeSpec &spec : application::nodeCatalogue())
        out << QVariantMap{{QStringLiteral("type"), qstr(spec.id)},
                           {QStringLiteral("title"), nodeTitle(spec.id)},
                           {QStringLiteral("category"), qstr(spec.category)},
                           {QStringLiteral("categoryTitle"), nodeCategoryTitle(spec.category)},
                           {QStringLiteral("summary"), nodeSummary(spec.id)},
                           {QStringLiteral("inputs"), portsList(spec.inputs)},
                           {QStringLiteral("outputs"), portsList(spec.outputs)},
                           {QStringLiteral("insideGroups"), spec.id == "groups.input" || spec.id == "groups.output"},
                           {QStringLiteral("lua"), spec.id == kLua}};
    return out;
}

QVariantList NodeEffectsController::recent() const
{
    QVariantList out;
    for (const QString &type : m_recent)
        out << QVariantMap{{QStringLiteral("type"), type}, {QStringLiteral("title"), nodeTitle(utf8(type))}};
    return out;
}

void NodeEffectsController::addRecent(const QString &type)
{
    m_recent.removeAll(type);
    m_recent.prepend(type);
    while (m_recent.size() > kRecentCount)
        m_recent.removeLast();
    emit recentChanged();
}

QVariantList NodeEffectsController::presets()
{
    QVariantList out;
    const QVariantList templates = TemplateEditorController::presets();
    const auto graphs = application::nodeGraphPresets();
    for (int i = 0; i < int(graphs.size()); ++i) {
        const QString file = qstr(graphs[std::size_t(i)].templateFile);
        for (const QVariant &t : templates)
            if (t.toMap().value(QStringLiteral("file")).toString() == file)
                out << QVariantMap{{QStringLiteral("index"), i},
                                   {QStringLiteral("title"), t.toMap().value(QStringLiteral("title"))},
                                   {QStringLiteral("description"), t.toMap().value(QStringLiteral("description"))}};
    }
    return out;
}

QStringList NodeEffectsController::library() const
{
    QStringList out;
    if (!m_libraryFolder.empty())
        for (const std::u8string &name : application::NodeGraphLibrary(m_libraryFolder).names())
            out << qstr(name);
    return out;
}

QVariantList NodeEffectsController::sources() const
{
    QVariantList out;
    const core::Document *doc = document();
    if (!doc)
        return out;
    const auto lines = doc->lines();
    for (std::size_t row = 0; row < lines.size(); ++row) {
        const core::LineRecord *l = lines[row];
        if (std::ranges::find(m_sources, l->id) == m_sources.end())
            continue;
        out << QVariantMap{{QStringLiteral("line"), qulonglong(l->id.value)},
                           {QStringLiteral("number"), int(row) + 1},
                           {QStringLiteral("text"), stripped(l->text)},
                           {QStringLiteral("style"), qstr(l->style)},
                           {QStringLiteral("startMs"), qint64(l->start.value.microseconds() / 1000)},
                           {QStringLiteral("endMs"), qint64(l->end.value.microseconds() / 1000)},
                           {QStringLiteral("selected"), m_sourceSelection.contains(l->id)}};
    }
    return out;
}

void NodeEffectsController::setSource(qulonglong id)
{
    if (source() == id || std::ranges::none_of(m_sources, [&](core::LineId l) { return l.value == id; }))
        return;
    m_source = core::LineId{id};
    emit changed();
    schedulePreview();
}

QVariantList NodeEffectsController::sourceSelection() const
{
    QVariantList out;
    for (const core::LineId id : m_sourceSelection)
        out << qulonglong(id.value);
    return out;
}

void NodeEffectsController::setSourceSelection(const QVariantList &ids)
{
    std::set<core::LineId> chosen;
    for (const QVariant &v : ids)
        if (const core::LineId id{v.toULongLong()}; std::ranges::find(m_sources, id) != m_sources.end())
            chosen.insert(id);
    if (chosen == m_sourceSelection)
        return;
    m_sourceSelection = std::move(chosen);
    emit changed();
}

int NodeEffectsController::sourceNumber() const
{
    const core::Document *doc = document();
    if (!doc || !m_source)
        return 0;
    const auto lines = doc->lines();
    const auto it = std::ranges::find_if(lines, [&](const core::LineRecord *l) { return l->id == *m_source; });
    return it == lines.end() ? 0 : int(it - lines.begin()) + 1;
}

int NodeEffectsController::sourceStartMs() const
{
    const core::Document *doc = document();
    if (doc && m_source)
        for (const core::LineRecord *l : doc->lines())
            if (l->id == *m_source)
                return int(l->start.value.microseconds() / 1000);
    return 0;
}

int NodeEffectsController::sourceEndMs() const
{
    const core::Document *doc = document();
    if (doc && m_source)
        for (const core::LineRecord *l : doc->lines())
            if (l->id == *m_source)
                return int(l->end.value.microseconds() / 1000);
    return 0;
}

QVariantList NodeEffectsController::previewLines() const
{
    QVariantList out;
    if (!m_preview)
        return out;
    for (const core::LineRecord &l : m_preview->lines)
        out << QVariantMap{{QStringLiteral("layer"), qint64(l.layer.value)},
                           {QStringLiteral("startMs"), qint64(l.start.value.microseconds() / 1000)},
                           {QStringLiteral("endMs"), qint64(l.end.value.microseconds() / 1000)},
                           {QStringLiteral("style"), qstr(l.style)},
                           {QStringLiteral("text"), qstr(l.text)}};
    return out;
}

void NodeEffectsController::setPreviewOnVideo(bool on)
{
    if (m_previewOnVideo == on)
        return;
    m_previewOnVideo = on;
    emit changed();
    emit previewChanged();
    emit videoChanged();
}

bool NodeEffectsController::videoPreview() const
{
    return m_shown && m_previewOnVideo && m_preview && m_source;
}

std::optional<core::Document> NodeEffectsController::videoDocument(const core::Document &doc) const
{
    if (!videoPreview())
        return std::nullopt;
    core::Document out = doc;
    std::int64_t from = std::numeric_limits<std::int64_t>::max(), to = std::numeric_limits<std::int64_t>::min();
    for (const core::LineRecord &l : m_preview->lines) {
        from = std::min<std::int64_t>(from, l.start.value.microseconds() / 1000);
        to = std::max<std::int64_t>(to, l.end.value.microseconds() / 1000);
    }
    const std::u8string &name = m_preview->name;
    out.removeLinesIf([&](const core::LineRecord &l) {
        if (!application::isGeneratedBy(l, name))
            return false;
        if (l.generatedFrom.value != 0)
            return l.generatedFrom == *m_source;
        return l.start.value.microseconds() / 1000 < to && l.end.value.microseconds() / 1000 > from;
    });
    for (const auto &fields : m_preview->styles)
        out.appendStyle(fields);
    for (core::LineRecord l : m_preview->lines)
        out.appendLine(std::move(l));
    return out;
}

bool NodeEffectsController::hasLua() const
{
    if (!m_graph)
        return false;
    const auto flat = application::flattenGraph(*m_graph);
    return flat && std::ranges::any_of(flat->nodes, [](const GraphNode &n) { return n.type == kLua; });
}

bool NodeEffectsController::trusted() const
{
    return !hasLua() || (m_trustOwner && m_trustOwner->trusted());
}

void NodeEffectsController::trustDocument()
{
    if (m_trustOwner && !trusted())
        m_trustOwner->trustDocument();
}

bool NodeEffectsController::editKeepingTrust(const std::function<bool()> &edit)
{
    return m_trustOwner ? m_trustOwner->editKeepingTrust(edit) : edit();
}

bool NodeEffectsController::canApply() const
{
    return m_editable && m_graph && !m_newerFormat && !m_sources.empty() && trusted() &&
           application::validateGraph(*m_graph).empty();
}

bool NodeEffectsController::canApplySelected() const
{
    return canApply() && !m_sourceSelection.empty();
}

application::NodeEffectOptions NodeEffectsController::options() const
{
    application::NodeEffectOptions o;
    o.measure = m_measure;
    o.trustLua = trusted();
    if (m_luaNodes && o.trustLua && hasLua())
        o.lua = m_luaNodes();
    o.cache = const_cast<application::NodeEffectCache *>(&m_cache);
    return o;
}

void NodeEffectsController::schedulePreview()
{
    if (m_shown)
        m_previewTimer.start();
}

void NodeEffectsController::runPreview()
{
    const core::Document *doc = document();
    const application::DocumentGraph *g = currentGraph();
    if (!m_shown || !doc || !g || !m_graph || !m_source) {
        const bool had = m_preview.has_value();
        m_preview.reset();
        m_trace = {};
        m_runProblems.clear();
        if (had) {
            emit previewChanged();
            emit videoChanged();
        }
        return;
    }
    application::NodeEffectOptions o = options();
    o.sources = std::set<core::LineId>{*m_source};
    o.threads = 1;
    application::NodeEffectRun run = application::runNodeEffect(*doc, g->name, g->style, *m_graph, o);
    m_runProblems = run.problems;
    if (run.ran && run.ok()) {
        o.cache = nullptr;
        m_trace = {};
        o.trace = &m_trace;
        application::runNodeEffect(*doc, g->name, g->style, *m_graph, o);
        m_previewSeconds = run.seconds;
        m_preview = std::move(run);
        m_previewKept = false;
    } else {
        m_previewKept = m_preview.has_value();
    }
    emit previewChanged();
    emit graphChanged();
    emit inspectorChanged();
    emit videoChanged();
}

void NodeEffectsController::edit(const NodeGraph &root)
{
    if (!m_editable || m_newerFormat)
        return;
    m_graph = root;
    m_pending = true;
    m_commitTimer.start();
    emit graphChanged();
    emit inspectorChanged();
    emit changed();
    schedulePreview();
}

void NodeEffectsController::editView(const NodeGraph &v)
{
    if (!m_graph)
        return;
    if (const auto root = application::withGraphAt(*m_graph, m_path, v))
        edit(*root);
}

void NodeEffectsController::commit()
{
    m_commitTimer.stop();
    if (!std::exchange(m_pending, false))
        return;
    auto *s = session();
    if (!s || !m_current || !m_graph)
        return;
    if (editKeepingTrust([&] { return application::editGraph(*s, *m_current, *m_graph).has_value(); }))
        emit documentEdited();
    m_seenRevision = 0;
    refresh();
}

QString NodeEffectsController::addNode(const QString &type, double x, double y, const QString &fromNode,
                                       const QString &fromPort)
{
    std::optional<NodeGraph> v = view();
    if (!v || !application::nodeSpec(utf8(type)))
        return {};
    const std::string id = application::freeNodeId(*v, utf8(type));
    GraphNode node{id, utf8(type)};
    node.at = std::pair(x, y);
    v->nodes.push_back(node);
    if (!fromNode.isEmpty())
        if (const auto ports = application::nodePorts(node))
            for (const application::PortSpec &p : ports->inputs) {
                const GraphWire wire{{utf8(fromNode), utf8(fromPort)}, {id, p.name}};
                if (!application::wireRefusal(*v, wire)) {
                    *v = application::withWire(std::move(*v), wire);
                    break;
                }
            }
    editView(*v);
    addRecent(type);
    setSelection({qstr(id)});
    return qstr(id);
}

void NodeEffectsController::moveNode(const QString &node, double x, double y)
{
    std::optional<NodeGraph> v = view();
    if (!v)
        return;
    for (GraphNode &n : v->nodes)
        if (n.id == utf8(node)) {
            if (n.at == std::pair(x, y))
                return;
            n.at = std::pair(x, y);
            editView(*v);
            return;
        }
}

void NodeEffectsController::removeNode(const QString &node)
{
    std::optional<NodeGraph> v = view();
    if (!v || !nodeOf(*v, utf8(node)))
        return;
    editView(application::withoutNode(std::move(*v), utf8(node)));
    if (m_selection.removeAll(node)) {
        emit selectionChanged();
        emit inspectorChanged();
    }
}

void NodeEffectsController::removeSelected()
{
    std::optional<NodeGraph> v = view();
    if (!v || m_selection.isEmpty())
        return;
    for (const QString &id : std::as_const(m_selection))
        *v = application::withoutNode(std::move(*v), utf8(id));
    m_selection.clear();
    editView(*v);
    emit selectionChanged();
}

QString NodeEffectsController::wireProblem(const QString &fromNode, const QString &fromPort, const QString &toNode,
                                           const QString &toPort) const
{
    const std::optional<NodeGraph> v = view();
    if (!v)
        return QCoreApplication::translate("HikariSub.NodeEffects", "There is no graph to wire.");
    const auto refused = application::wireRefusal(*v, {{utf8(fromNode), utf8(fromPort)}, {utf8(toNode), utf8(toPort)}});
    if (!refused)
        return {};
    using application::WireRefusal;
    switch (refused->reason) {
    case WireRefusal::Types:
        return QCoreApplication::translate("HikariSub.NodeEffects", "A %1 can’t connect to a %2 input.").arg(portTypeTitle(refused->from), portTypeTitle(refused->to));
    case WireRefusal::Loop:
        return QCoreApplication::translate("HikariSub.NodeEffects", "That wire would make a loop.");
    case WireRefusal::SameNode:
        return QCoreApplication::translate("HikariSub.NodeEffects", "A node can’t take its own output.");
    case WireRefusal::UnknownNode:
    case WireRefusal::NotAnOutput:
    case WireRefusal::NotAnInput:
        break;
    }
    return QCoreApplication::translate("HikariSub.NodeEffects", "That port doesn’t exist.");
}

QString NodeEffectsController::connectPorts(const QString &fromNode, const QString &fromPort, const QString &toNode,
                                            const QString &toPort)
{
    const QString problem = wireProblem(fromNode, fromPort, toNode, toPort);
    if (problem.isEmpty())
        editView(application::withWire(*view(), {{utf8(fromNode), utf8(fromPort)}, {utf8(toNode), utf8(toPort)}}));
    return problem;
}

void NodeEffectsController::disconnectInput(const QString &node, const QString &port)
{
    std::optional<NodeGraph> v = view();
    if (!v)
        return;
    const std::size_t before = v->wires.size();
    std::erase_if(v->wires, [&](const GraphWire &w) { return w.to.node == utf8(node) && w.to.port == utf8(port); });
    if (v->wires.size() != before)
        editView(*v);
}

QStringList NodeEffectsController::nodeTypesTaking(const QString &portType) const
{
    QStringList out;
    const std::optional<application::PortType> from = application::portTypeNamed(utf8(portType));
    if (!from)
        return out;
    for (const application::NodeSpec &spec : application::nodeCatalogue())
        if (std::ranges::any_of(spec.inputs, [&](const application::PortSpec &p) {
                return application::portTypesConnect(*from, p.type);
            }))
            out << qstr(spec.id);
    return out;
}

void NodeEffectsController::setField(const QString &node, const QString &field, const QVariant &value)
{
    std::optional<NodeGraph> v = view();
    if (!v)
        return;
    for (GraphNode &n : v->nodes) {
        if (n.id != utf8(node))
            continue;
        const application::NodeSpec *spec = application::nodeSpec(n.type);
        const application::FieldSpec *f = spec ? application::fieldSpec(*spec, utf8(field)) : nullptr;
        const Json j = fieldJson(f, value);
        if (const Json *had = n.fields.find(utf8(field)); had && had->dump() == j.dump())
            return;
        n.fields[utf8(field)] = j;
        editView(*v);
        return;
    }
}

bool NodeEffectsController::renameNode(const QString &from, const QString &to)
{
    const std::optional<NodeGraph> v = view();
    const auto renamed = v ? application::withNodeRenamed(*v, utf8(from), utf8(to)) : std::nullopt;
    if (!renamed)
        return false;
    editView(*renamed);
    m_selection.replaceInStrings(QRegularExpression(QStringLiteral("^%1$").arg(QRegularExpression::escape(from))), to);
    emit selectionChanged();
    emit inspectorChanged();
    return true;
}

bool NodeEffectsController::groupSelection()
{
    const std::optional<NodeGraph> v = view();
    if (!v || m_selection.isEmpty())
        return false;
    std::vector<std::string> ids;
    for (const QString &id : std::as_const(m_selection))
        ids.push_back(utf8(id));
    const std::string groupId = application::freeNodeId(*v, kGroup);
    const auto grouped = application::groupedNodes(*v, ids, groupId);
    if (!grouped)
        return false;
    editView(*grouped);
    setSelection({qstr(groupId)});
    return true;
}

bool NodeEffectsController::enterGroup(const QString &node)
{
    const std::optional<NodeGraph> v = view();
    const GraphNode *n = v ? nodeOf(*v, utf8(node)) : nullptr;
    if (!n || n->type != kGroup)
        return false;
    m_path.push_back(n->id);
    if (!view()) {
        m_path.pop_back();
        return false;
    }
    m_selection.clear();
    emit selectionChanged();
    emit graphChanged();
    emit inspectorChanged();
    return true;
}

bool NodeEffectsController::leaveGroup()
{
    if (m_path.empty())
        return false;
    const QString group = qstr(m_path.back());
    m_path.pop_back();
    emit graphChanged();
    setSelection({group});
    return true;
}

void NodeEffectsController::setSeed(qint64 seed)
{
    if (!m_graph || m_graph->seed == seed)
        return;
    NodeGraph root = *m_graph;
    root.seed = seed;
    edit(root);
}

void NodeEffectsController::newSeed()
{
    setSeed(QRandomGenerator::global()->bounded(1, 1000000));
}

void NodeEffectsController::setAllStyles(bool all)
{
    if (!m_graph || m_graph->allStyles == all)
        return;
    NodeGraph root = *m_graph;
    root.allStyles = all;
    edit(root);
    updateSources();
    emit changed();
}

bool NodeEffectsController::runGraphCommand(const std::function<bool(application::EditSession &)> &command)
{
    commit();
    auto *s = session();
    if (!s || !m_editable || !editKeepingTrust([&] { return command(*s); }))
        return false;
    emit documentEdited();
    m_seenRevision = 0;
    refresh();
    return true;
}

void NodeEffectsController::setStyle(const QString &style)
{
    if (!m_current || style == this->style())
        return;
    const core::LineId line = *m_current;
    runGraphCommand([&](application::EditSession &s) { return application::setGraphStyle(s, line, wide(style)).has_value(); });
}

namespace {

std::u8string defaultStyle(const core::Document &doc, const std::vector<core::LineId> &sources,
                           std::optional<core::LineId> source)
{
    for (const core::LineRecord *l : doc.lines())
        if ((source && l->id == *source) || (!source && application::isKaraokeLine(*l)))
            return l->style;
    (void)sources;
    for (const auto &section : doc.sections())
        for (const auto &record : section.records)
            if (const auto *style = std::get_if<core::StyleRecord>(&record); style && !style->fields.empty())
                return style->fields.front();
    return u8"Default";
}

}

bool NodeEffectsController::newGraph()
{
    const core::Document *doc = document();
    if (!doc)
        return false;
    NodeGraph graph;
    graph.nodes.push_back({"syl", "units.for_each"});
    graph.nodes.push_back({"emit", "output.emit"});
    graph.wires.push_back({{"syl", "units"}, {"emit", "units"}});
    graph = application::laidOutGraph(std::move(graph));
    const std::u8string name = application::freeGraphName(*doc, wide(QCoreApplication::translate("HikariSub.NodeEffects", "New graph")));
    const std::u8string style = defaultStyle(*doc, m_sources, m_source);
    std::optional<core::LineId> added;
    const bool done = runGraphCommand([&](application::EditSession &s) {
        const auto line = application::addGraph(s, name, style, graph);
        added = line ? std::optional(*line) : std::nullopt;
        return added.has_value();
    });
    if (done)
        setCurrent(added->value);
    return done;
}

bool NodeEffectsController::addPreset(int index)
{
    const core::Document *doc = document();
    const auto all = application::nodeGraphPresets();
    if (!doc || index < 0 || index >= int(all.size()))
        return false;
    const auto graph = application::parseGraph(all[std::size_t(index)].graph);
    if (!graph)
        return false;
    QString title;
    for (const QVariant &p : presets())
        if (p.toMap().value(QStringLiteral("index")).toInt() == index)
            title = p.toMap().value(QStringLiteral("title")).toString();
    const std::u8string name = application::freeGraphName(*doc, wide(title));
    const std::u8string style = defaultStyle(*doc, m_sources, m_source);
    std::optional<core::LineId> added;
    const bool done = runGraphCommand([&](application::EditSession &s) {
        const auto line = application::addGraph(s, name, style, application::laidOutGraph(*graph));
        added = line ? std::optional(*line) : std::nullopt;
        return added.has_value();
    });
    if (done)
        setCurrent(added->value);
    return done;
}

bool NodeEffectsController::addFromLibrary(const QString &libraryName)
{
    const core::Document *doc = document();
    if (!doc || m_libraryFolder.empty())
        return false;
    const auto graph = application::NodeGraphLibrary(m_libraryFolder).load(wide(libraryName));
    if (!graph)
        return false;
    const std::u8string name = application::freeGraphName(*doc, wide(libraryName));
    const std::u8string style = defaultStyle(*doc, m_sources, m_source);
    std::optional<core::LineId> added;
    const bool done = runGraphCommand([&](application::EditSession &s) {
        const auto line = application::addGraph(s, name, style, application::laidOutGraph(*graph));
        added = line ? std::optional(*line) : std::nullopt;
        return added.has_value();
    });
    if (done)
        setCurrent(added->value);
    return done;
}

bool NodeEffectsController::saveToLibrary()
{
    commit();
    const application::DocumentGraph *g = currentGraph();
    if (!g || !m_graph || m_libraryFolder.empty())
        return false;
    const bool saved = application::NodeGraphLibrary(m_libraryFolder).save(g->name, *m_graph);
    emit libraryChanged();
    return saved;
}

QString NodeEffectsController::graphNameProblem(const QString &name) const
{
    const std::u8string wanted = wide(name);
    if (!application::isGraphName(wanted))
        return QCoreApplication::translate("HikariSub.NodeEffects", "A graph name can’t be empty, start or end with a space, end with a dot, or hold “karaoke”, a "
                  "comma, a double quote, a control character or any of / \\ : * ? < > |.");
    if (std::ranges::any_of(m_graphs, [&](const application::DocumentGraph &g) { return g.name == wanted && g.line != m_current; }))
        return QCoreApplication::translate("HikariSub.NodeEffects", "Another graph in this file has that name.");
    return {};
}

bool NodeEffectsController::renameGraph(const QString &name)
{
    if (!m_current || !graphNameProblem(name).isEmpty())
        return false;
    const core::LineId line = *m_current;
    return runGraphCommand([&](application::EditSession &s) { return application::renameGraph(s, line, wide(name)).has_value(); });
}

bool NodeEffectsController::duplicateGraph()
{
    commit();
    const core::Document *doc = document();
    const application::DocumentGraph *g = currentGraph();
    if (!doc || !g || !m_graph)
        return false;
    const std::u8string name = application::freeGraphName(*doc, wide(QCoreApplication::translate("HikariSub.NodeEffects", "%1 copy").arg(qstr(g->name))));
    const std::u8string style = g->style;
    const NodeGraph graph = *m_graph;
    std::optional<core::LineId> added;
    const bool done = runGraphCommand([&](application::EditSession &s) {
        const auto line = application::addGraph(s, name, style, graph, "Duplicate graph");
        added = line ? std::optional(*line) : std::nullopt;
        return added.has_value();
    });
    if (done)
        setCurrent(added->value);
    return done;
}

bool NodeEffectsController::deleteGraph()
{
    if (!m_current)
        return false;
    const core::LineId line = *m_current;
    m_pending = false;
    m_commitTimer.stop();
    return runGraphCommand([&](application::EditSession &s) { return application::deleteGraph(s, line).has_value(); });
}

bool NodeEffectsController::apply(bool selectedOnly)
{
    commit();
    const application::DocumentGraph *g = currentGraph();
    auto *s = session();
    if (!s || !g || !m_graph || !(selectedOnly ? canApplySelected() : canApply()))
        return false;
    application::NodeEffectOptions o = options();
    std::optional<application::NodeEffectRun> owners;
    if (selectedOnly) {
        owners = application::runNodeEffect(s->document(), g->name, g->style, *m_graph, o);
        o.sources = m_sourceSelection;
    }
    const application::NodeEffectRun run = application::runNodeEffect(s->document(), g->name, g->style, *m_graph, o);
    if (!run.ran || !run.ok()) {
        m_lastApply = {{QStringLiteral("failed"), true}};
        emit changed();
        return false;
    }
    if (!application::applyNodeEffect(*s, run, owners ? &*owners : nullptr))
        return false;
    m_lastApply = {{QStringLiteral("lines"), qulonglong(run.lines.size())},
                   {QStringLiteral("sources"), qulonglong(run.applied.size())},
                   {QStringLiteral("ms"), run.seconds * 1000}};
    emit documentEdited();
    m_seenRevision = 0;
    refresh();
    return true;
}

}
