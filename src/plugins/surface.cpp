#include "hikari/plugins/surface.h"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <vector>

namespace hikari::plugins {

namespace {

using api::Json;

enum class T { Line, Text, Bool, Number, Count, Id, Icon, Colour, Path, Enum, Children, Options, Rows, TreeRows, Tabs, Draw, Menu, Tip, Dash, Points, Buttons, Node };

struct Field {
    std::string_view name;
    T type;
    bool required = false;
    std::vector<std::string_view> choices = {};
};

using Fields = std::vector<Field>;

constexpr std::size_t kMaxLine = 500;
constexpr std::size_t kMaxText = 100'000;
constexpr std::int64_t kMaxCount = 100'000;

const Field kId{"id", T::Id, true};
const Field kLabel{"label", T::Line};

const std::map<std::string_view, Fields> &kinds()
{
    static const std::map<std::string_view, Fields> table = {
        {"label", {{"text", T::Text, true}, {"style", T::Enum, false, {"heading", "body", "muted", "small"}}, {"wrap", T::Bool}}},
        {"button", {kId, {"text", T::Line, true}, {"style", T::Enum, false, {"normal", "primary", "danger"}}, {"icon", T::Icon}}},
        {"icon_button", {kId, {"icon", T::Icon, true}, {"name", T::Line, true}, {"checked", T::Bool}}},
        {"text", {kId, {"value", T::Text}, {"placeholder", T::Line}, kLabel}},
        {"text_area", {kId, {"value", T::Text}, {"placeholder", T::Line}, kLabel, {"rows", T::Count}}},
        {"number",
         {kId, {"value", T::Number}, {"minimum", T::Number}, {"maximum", T::Number}, {"step", T::Number},
          {"decimals", T::Count}, {"unit", T::Line}, kLabel}},
        {"time", {kId, {"value", T::Number}, kLabel}},
        {"check", {kId, {"text", T::Line, true}, {"value", T::Bool}}},
        {"choice", {kId, {"options", T::Options, true}, {"value", T::Line}, kLabel}},
        {"radio", {kId, {"options", T::Options, true}, {"value", T::Line}, kLabel}},
        {"dropdown", {kId, {"options", T::Options, true}, {"value", T::Line}, kLabel}},
        {"colour", {kId, {"value", T::Colour}, {"alpha", T::Bool}, kLabel}},
        {"slider", {kId, {"value", T::Number}, {"minimum", T::Number}, {"maximum", T::Number}, {"step", T::Number}, kLabel}},
        {"progress", {{"value", T::Number}, {"text", T::Line}}},
        {"list", {kId, {"rows", T::Rows, true}, {"selected", T::Line}, {"height", T::Count}}},
        {"tree", {kId, {"rows", T::TreeRows, true}, {"selected", T::Line}, {"height", T::Count}}},
        {"tabs", {kId, {"tabs", T::Tabs, true}, {"selected", T::Count}}},
        {"group", {{"title", T::Line, true}, {"children", T::Children, true}}},
        {"link", {kId, {"text", T::Line, true}}},
        {"image", {{"src", T::Path, true}, {"width", T::Count}, {"height", T::Count}}},
        {"row", {{"children", T::Children, true}}},
        {"column", {{"children", T::Children, true}}},
        {"grid", {{"columns", T::Count, true}, {"children", T::Children, true}}},
        {"empty", {{"text", T::Line, true}}},
        {"canvas",
         {kId, {"height", T::Count, true}, {"width", T::Count}, {"draw", T::Draw, true}, {"name", T::Line, true},
          {"description", T::Text}, {"hover", T::Bool}, {"tip", T::Tip}}},
    };
    return table;
}

const Fields &commonFields()
{
    static const Fields fields = {{"kind", T::Line, true}, {"id", T::Id},          {"name", T::Line},
                                  {"tooltip", T::Line},    {"disabled", T::Bool}, {"grow", T::Bool}};
    return fields;
}

const std::map<std::string_view, Fields> &drawOps()
{
    const Field x{"x", T::Number, true}, y{"y", T::Number, true}, w{"w", T::Number, true}, h{"h", T::Number, true};
    const Field fill{"fill", T::Colour}, stroke{"stroke", T::Colour}, width{"width", T::Number};
    static const std::map<std::string_view, Fields> table = {
        {"rect", {x, y, w, h, fill, stroke, width, {"radius", T::Number}}},
        {"line",
         {{"x1", T::Number, true}, {"y1", T::Number, true}, {"x2", T::Number, true}, {"y2", T::Number, true},
          {"stroke", T::Colour, true}, width, {"dash", T::Dash}}},
        {"polyline", {{"points", T::Points, true}, stroke, fill, width, {"closed", T::Bool}, {"dash", T::Dash}}},
        {"path", {{"d", T::Text, true}, fill, stroke, width}},
        {"ellipse", {x, y, w, h, fill, stroke, width}},
        {"text",
         {x, y, {"text", T::Line, true}, fill, {"align", T::Enum, false, {"left", "center", "right"}},
          {"size", T::Enum, false, {"body", "small"}}}},
        {"image", {x, y, w, h, {"src", T::Path, true}}},
    };
    return table;
}

const std::set<std::string_view> kRoles = {"accent", "text", "muted", "line", "danger", "warning", "success", "panel", "field"};
const std::set<std::string_view> kValued = {"text",  "text_area", "number", "time",   "check", "choice",
                                            "radio", "dropdown",  "colour", "slider", "list",  "tree", "tabs"};

std::string inQuotes(std::string_view s)
{
    return "\xE2\x80\x9C" + std::string(s) + "\xE2\x80\x9D";
}

std::string withArticle(std::string_view noun)
{
    const bool vowel = !noun.empty() && std::string_view("aeiou").find(noun.front()) != std::string_view::npos;
    return (vowel ? "an " : "a ") + std::string(noun);
}

bool isHex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

bool isAssColour(std::string_view s)
{
    if (!s.starts_with("&H"))
        return false;
    s.remove_prefix(2);
    if (s.ends_with('&'))
        s.remove_suffix(1);
    return (s.size() == 6 || s.size() == 8) && std::ranges::all_of(s, isHex);
}

bool isIdText(std::string_view s)
{
    return !s.empty() && s.size() <= 64 && std::ranges::all_of(s, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
               c == '.' || c == ':';
    });
}

bool isIconRole(std::string_view s)
{
    return !s.empty() && s.size() <= 64 &&
           std::ranges::all_of(s, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

bool isPackageImage(std::string_view s)
{
    if (s.empty() || s.size() > 200)
        return false;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = s.find('/', start);
        const std::string_view part = s.substr(start, end == std::string_view::npos ? end : end - start);
        if (part.empty() || part.front() == '.' || part.front() == '-' ||
            !std::ranges::all_of(part, [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                       c == '-' || c == '.';
            }))
            return false;
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return s.ends_with(".png") || s.ends_with(".svg") || s.ends_with(".jpg");
}

class Checker {
public:
    std::optional<std::string> node(const Json &value, const std::string &at, int depth, bool root)
    {
        if (depth > kMaxDepth)
            return at + ": nests deeper than " + std::to_string(kMaxDepth) + " levels";
        if (++m_nodes > kMaxNodes)
            return at + ": the tree holds more than " + std::to_string(kMaxNodes) + " widgets";
        if (!value.isObject())
            return at + ": must be a widget, a table with a kind";
        const Json *kind = value.find("kind");
        if (!kind)
            return at + ": needs " + inQuotes("kind");
        if (!kind->isString())
            return at + ".kind: must be text";
        const auto spec = kinds().find(kind->asString());
        if (spec == kinds().end())
            return at + ": unknown kind " + inQuotes(kind->asString());
        m_depth = depth;
        Fields fields = commonFields();
        fields.insert(fields.end(), spec->second.begin(), spec->second.end());
        if (root) {
            fields.push_back({"menu", T::Menu});
            fields.push_back({"drop", T::Bool});
        }
        if (auto problem = object(value, fields, at, withArticle(spec->first)))
            return problem;
        if (const Json *id = value.find("id"); id && !m_ids.insert(id->asString()).second)
            return at + ".id: " + inQuotes(id->asString()) + " is used twice";
        if (spec->first == "choice")
            if (const auto count = value.find("options")->asArray().size(); count < 2 || count > 4)
                return at + ".options: a choice takes 2 to 4 options";
        return std::nullopt;
    }

    std::optional<std::string> object(const Json &value, const Fields &fields, const std::string &at, const std::string &what)
    {
        if (!value.isObject())
            return at + ": must be a table";
        for (const Json::Member &member : value.asObject()) {
            const auto field = std::ranges::find(fields, member.key, &Field::name);
            if (field == fields.end())
                return at + ": " + inQuotes(member.key) + " isn't a field of " + what;
            if (auto problem = check(member.value, *field, at + "." + member.key))
                return problem;
        }
        for (const Field &field : fields)
            if (field.required && !value.find(field.name))
                return at + ": needs " + inQuotes(field.name);
        return std::nullopt;
    }

    std::optional<std::string> check(const Json &v, const Field &field, const std::string &at)
    {
        switch (field.type) {
        case T::Line:
        case T::Text:
            if (!v.isString())
                return at + ": must be text";
            if (v.asString().size() > (field.type == T::Line ? kMaxLine : kMaxText))
                return at + ": is too long";
            return std::nullopt;
        case T::Bool:
            return v.isBool() ? std::nullopt : std::optional(at + ": must be true or false");
        case T::Number:
            return v.isNumber() ? std::nullopt : std::optional(at + ": must be a number");
        case T::Count:
            return v.isInt() && v.asInt() >= 0 && v.asInt() <= kMaxCount
                       ? std::nullopt
                       : std::optional(at + ": must be a whole number from 0 to " + std::to_string(kMaxCount));
        case T::Id:
            return v.isString() && isIdText(v.asString())
                       ? std::nullopt
                       : std::optional(at + ": must be an id of 1 to 64 letters, digits, _ - . or :");
        case T::Icon:
            return v.isString() && isIconRole(v.asString()) ? std::nullopt : std::optional(at + ": must be an icon name");
        case T::Colour:
            return v.isString() && (kRoles.contains(v.asString()) || isAssColour(v.asString()))
                       ? std::nullopt
                       : std::optional(at + ": must be a theme colour (accent, text, muted, line, danger, warning, "
                                            "success, panel, field) or an ASS colour such as &H0000FF&");
        case T::Path:
            return v.isString() && isPackageImage(v.asString())
                       ? std::nullopt
                       : std::optional(at + ": must be a .png, .svg or .jpg in the package");
        case T::Enum:
            if (v.isString() && std::ranges::find(field.choices, v.asString()) != field.choices.end())
                return std::nullopt;
            {
                std::string allowed;
                for (const auto choice : field.choices)
                    allowed += (allowed.empty() ? "" : ", ") + std::string(choice);
                return at + ": must be one of " + allowed;
            }
        case T::Node:
            return node(v, at, m_depth + 1, false);
        case T::Children:
            return list(v, at, kMaxNodes, [&](const Json &child, const std::string &place) {
                return node(child, place, m_depth + 1, false);
            });
        case T::Options:
            return list(v, at, 200, [&](const Json &option, const std::string &place) -> std::optional<std::string> {
                if (option.isString())
                    return check(option, {"", T::Line}, place);
                return object(option, {{"value", T::Line, true}, {"text", T::Line, true}}, place, "an option");
            });
        case T::Rows: {
            std::set<std::string> ids;
            return list(v, at, kMaxRows, [&](const Json &row, const std::string &place) {
                return this->row(row, place, ids, false, 0);
            });
        }
        case T::TreeRows: {
            std::set<std::string> ids;
            return list(v, at, kMaxRows, [&](const Json &row, const std::string &place) {
                return this->row(row, place, ids, true, 0);
            });
        }
        case T::Tabs:
            return list(v, at, 20, [&](const Json &tab, const std::string &place) {
                return object(tab, {{"text", T::Line, true}, {"body", T::Node, true}}, place, "a tab");
            });
        case T::Draw:
            return list(v, at, kMaxDrawOps, [&](const Json &op, const std::string &place) { return drawOp(op, place); });
        case T::Menu: {
            std::set<std::string> ids;
            return list(v, at, 30, [&](const Json &item, const std::string &place) -> std::optional<std::string> {
                if (auto problem = object(item,
                                          {{"id", T::Id, true}, {"text", T::Line, true}, {"checked", T::Bool},
                                           {"disabled", T::Bool}},
                                          place, "a menu row"))
                    return problem;
                if (!ids.insert(item.find("id")->asString()).second)
                    return place + ".id: " + inQuotes(item.find("id")->asString()) + " is used twice";
                return std::nullopt;
            });
        }
        case T::Tip:
            return object(v, {{"x", T::Number, true}, {"y", T::Number, true}, {"text", T::Line, true}}, at, "a tip");
        case T::Dash:
            if (!v.isArray() || v.asArray().size() != 2 ||
                !std::ranges::all_of(v.asArray(), [](const Json &n) { return n.isNumber() && n.asNumber() > 0; }))
                return at + ": must be two positive numbers, dash and gap";
            return std::nullopt;
        case T::Points: {
            if (!v.isArray())
                return at + ": must be a list of numbers";
            const auto &points = v.asArray();
            if (points.size() < 4 || points.size() % 2 != 0 || points.size() > 2 * kMaxDrawOps ||
                !std::ranges::all_of(points, &Json::isNumber))
                return at + ": must be x, y pairs, at least two points";
            return std::nullopt;
        }
        case T::Buttons:
            return buttons(v, at);
        }
        return std::nullopt;
    }

    std::optional<std::string> row(const Json &row, const std::string &at, std::set<std::string> &ids, bool nested, int depth)
    {
        if (depth > kMaxDepth)
            return at + ": nests deeper than " + std::to_string(kMaxDepth) + " levels";
        if (++m_rows > kMaxRows)
            return at + ": the list holds more than " + std::to_string(kMaxRows) + " rows";
        if (!row.isObject())
            return at + ": must be a row, a table with an id and text";
        Fields fields = {{"id", T::Id, true}, {"text", T::Line, true}, {"detail", T::Line}, {"icon", T::Icon}, {"muted", T::Bool}};
        if (nested)
            fields.push_back({"expanded", T::Bool});
        Json plain = Json::object();
        const Json *children = nullptr;
        for (const Json::Member &member : row.asObject()) {
            if (nested && member.key == "children")
                children = &member.value;
            else
                plain[member.key] = member.value;
        }
        if (auto problem = object(plain, fields, at, nested ? "a tree row" : "a row"))
            return problem;
        if (!ids.insert(plain.find("id")->asString()).second)
            return at + ".id: " + inQuotes(plain.find("id")->asString()) + " is used twice";
        if (!children)
            return std::nullopt;
        if (!children->isArray())
            return at + ".children: must be a list";
        for (std::size_t i = 0; i < children->asArray().size(); ++i)
            if (auto problem = this->row(children->asArray()[i], at + ".children[" + std::to_string(i + 1) + "]", ids,
                                         true, depth + 1))
                return problem;
        return std::nullopt;
    }

    std::optional<std::string> drawOp(const Json &op, const std::string &at)
    {
        if (!op.isObject())
            return at + ": must be a drawing operation, a table with an op";
        const Json *name = op.find("op");
        if (!name || !name->isString())
            return at + ": needs " + inQuotes("op");
        const auto spec = drawOps().find(name->asString());
        if (spec == drawOps().end())
            return at + ": unknown operation " + inQuotes(name->asString());
        Fields fields = spec->second;
        fields.push_back({"op", T::Line, true});
        return object(op, fields, at, withArticle(spec->first));
    }

    std::optional<std::string> buttons(const Json &v, const std::string &at)
    {
        int defaults = 0, cancels = 0;
        std::set<std::string> ids;
        auto problem = list(v, at, 4, [&](const Json &button, const std::string &place) -> std::optional<std::string> {
            if (auto refused = object(button,
                                      {{"id", T::Id, true}, {"text", T::Line, true},
                                       {"style", T::Enum, false, {"normal", "primary", "danger"}}, {"default", T::Bool},
                                       {"cancel", T::Bool}, {"stays_open", T::Bool}},
                                      place, "a button"))
                return refused;
            if (!ids.insert(button.find("id")->asString()).second)
                return place + ".id: " + inQuotes(button.find("id")->asString()) + " is used twice";
            const Json *isDefault = button.find("default");
            const Json *isCancel = button.find("cancel");
            defaults += isDefault && isDefault->asBool();
            cancels += isCancel && isCancel->asBool();
            return std::nullopt;
        });
        if (problem)
            return problem;
        if (v.asArray().empty())
            return at + ": needs at least one button";
        if (defaults > 1 || cancels > 1)
            return at + ": only one button may be the default, and only one may cancel";
        return std::nullopt;
    }

    template <typename Each>
    std::optional<std::string> list(const Json &v, const std::string &at, std::size_t limit, Each each)
    {
        if (!v.isArray())
            return at + ": must be a list";
        if (v.asArray().size() > limit)
            return at + ": holds more than " + std::to_string(limit) + " items";
        const int depth = m_depth;
        for (std::size_t i = 0; i < v.asArray().size(); ++i) {
            m_depth = depth;
            if (auto problem = each(v.asArray()[i], at + "[" + std::to_string(i + 1) + "]"))
                return problem;
        }
        m_depth = depth;
        return std::nullopt;
    }

    std::size_t m_nodes = 0;
    std::size_t m_rows = 0;
    int m_depth = 0;
    std::set<std::string> m_ids;
};

Json emptyValue(std::string_view kind)
{
    if (kind == "check")
        return false;
    if (kind == "text" || kind == "text_area")
        return "";
    if (kind == "tabs")
        return std::int64_t(0);
    return nullptr;
}

void collect(const Json &node, Json &values)
{
    if (!node.isObject())
        return;
    const Json *kind = node.find("kind");
    const Json *id = node.find("id");
    if (kind && kind->isString() && id && id->isString() && kValued.contains(kind->asString())) {
        const std::string_view k = kind->asString();
        const Json *value = node.find(k == "list" || k == "tree" || k == "tabs" ? "selected" : "value");
        values[id->asString()] = value ? *value : emptyValue(k);
    }
    if (const Json *children = node.find("children"); children && children->isArray())
        for (const Json &child : children->asArray())
            collect(child, values);
    if (const Json *tabs = node.find("tabs"); tabs && tabs->isArray())
        for (const Json &tab : tabs->asArray())
            if (const Json *body = tab.find("body"))
                collect(*body, values);
}

}

std::optional<std::string> panelTreeProblem(const Json &tree)
{
    return Checker().node(tree, "tree", 0, true);
}

std::optional<std::string> dialogProblem(const Json &spec)
{
    Checker checker;
    return checker.object(spec,
                          {{"title", T::Line, true}, {"body", T::Node, true}, {"buttons", T::Buttons, true},
                           {"width", T::Count}, {"height", T::Count}},
                          "spec", "a dialog");
}

Json inputValues(const Json &tree)
{
    Json values = Json::object();
    collect(tree, values);
    return values;
}

}
