#include "hikari/application/karaoke_templates.h"

#include "hikari/application/karaoke_layout.h"

#include <array>
#include <memory>

namespace hikari::application::karaoke_templates {

namespace {

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

bool isVariableChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

std::string format(std::string_view pattern, std::initializer_list<std::string_view> args)
{
    std::string out;
    auto arg = args.begin();
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '%' && i + 1 < pattern.size() && pattern[i + 1] == 's' && arg != args.end()) {
            out += *arg++;
            ++i;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

constexpr std::array<std::string_view, 14> kModifiers = {"pre-line", "line",    "syl",      "furi",  "char",
                                                         "all",      "repeat",  "loop",     "notext", "keeptags",
                                                         "noblank",  "multi",   "fx",       "fxgroup"};

using Building = std::shared_ptr<Template>;

struct Builder {
    std::vector<Building> once, line, named, syl, furi;
    std::map<std::string, Building, std::less<>> byId;
    std::vector<Problem> problems;
    const ToNumber &toNumber;

    void problem(int level, const Where &where, std::string message)
    {
        problems.push_back({level, where, std::nullopt, std::move(message)});
    }

    void code(const SubsEntry &l, std::string_view mods, const Where &where)
    {
        auto t = std::make_shared<Template>();
        t->code = true;
        t->text = l.text;
        t->style = l.style;
        t->where = where;
        bool inserted = false;
        std::string_view rest = mods;
        while (!rest.empty()) {
            const auto [head, tail] = headtail(rest);
            rest = tail;
            const std::string m = lower(head);
            if (m == "once" || m == "line" || m == "syl" || m == "furi") {
                (m == "once" ? once : m == "line" ? line : m == "syl" ? syl : furi).push_back(t);
                inserted = true;
            } else if (m == "all") {
                t->style.reset();
            } else if (m == "noblank") {
                t->noblank = true;
            } else if (m == "repeat" || m == "loop") {
                const auto [times, after] = headtail(rest);
                if (const auto n = toNumber(times)) {
                    t->loops = *n;
                    rest = after;
                } else {
                    problem(3, where,
                            format("Failed reading this repeat-count to a number: %s\nIn template code line: %s\n"
                                   "Effect field: %s\n",
                                   {times, l.text, l.effect}));
                    t->loops = 1;
                }
            } else {
                problem(3, where,
                        format("Unknown modifier in code template: %s\nIn template code line: %s\nEffect field: %s\n",
                               {m, l.text, l.effect}));
            }
        }
        if (!inserted)
            once.push_back(t);
    }

    void text(const SubsEntry &l, std::string_view mods, const Where &where)
    {
        auto t = std::make_shared<Template>();
        t->style = l.style;
        t->layer = l.layer;
        t->where = where;
        bool inserted = false;
        std::string_view rest = mods;
        while (!rest.empty()) {
            const auto [head, tail] = headtail(rest);
            rest = tail;
            const std::string m = lower(head);
            const bool lineClass = m == "pre-line" || m == "line";
            if (lineClass && !inserted) {
                const auto [idText, after] = headtail(rest);
                std::optional<std::string> id = lower(idText);
                for (const std::string_view kw : kModifiers)
                    if (*id == kw) {
                        id.reset();
                        break;
                    }
                if (id && id->empty())
                    id.reset();
                if (id)
                    rest = after;
                if (id && byId.contains(*id)) {
                    t = byId.find(*id)->second;
                } else if (id) {
                    t->id = id;
                    byId.emplace(*id, t);
                    named.push_back(t);
                } else {
                    line.push_back(t);
                }
                inserted = true;
                t->isline = true;
                if (m == "line") {
                    t->text += l.text;
                    if (!t->textWhere)
                        t->textWhere = where;
                    t->textParts.push_back({l.text, where});
                } else {
                    t->pre += l.text;
                    if (!t->preWhere)
                        t->preWhere = where;
                    t->preParts.push_back({l.text, where});
                }
            } else if ((m == "syl" || m == "furi") && !t->isline) {
                (m == "syl" ? syl : furi).push_back(t);
                inserted = true;
            } else if (lineClass && inserted) {
                problem(2, where, format("Unable to combine %s class templates with other template classes\n", {m}));
            } else if ((m == "syl" || m == "furi") && t->isline) {
                problem(2, where,
                        format("Unable to combine %s class template lines with line or pre-line classes\n", {m}));
            } else if (m == "all") {
                t->style.reset();
            } else if (m == "repeat" || m == "loop") {
                const auto [times, after] = headtail(rest);
                if (const auto n = toNumber(times)) {
                    t->loops = *n;
                    rest = after;
                } else {
                    problem(3, where,
                            format("Failed reading this repeat-count to a number: %s\nIn template line: %s\n"
                                   "Effect field: %s\n",
                                   {times, l.text, l.effect}));
                    t->loops = 1;
                }
            } else if (m == "notext") {
                t->addtext = false;
            } else if (m == "keeptags") {
                t->keeptags = true;
            } else if (m == "multi") {
                t->multi = true;
            } else if (m == "char") {
                t->perchar = true;
            } else if (m == "noblank") {
                t->noblank = true;
            } else if (m == "fx" || m == "fxgroup") {
                const auto [name, after] = headtail(rest);
                std::optional<std::string> &slot = m == "fx" ? t->fx : t->fxgroup;
                if (!name.empty()) {
                    slot = std::string(name);
                    rest = after;
                } else {
                    // MUST KILL: "linee" is 2.1.7's own spelling
                    problem(3, where,
                            m == "fx" ? format("No fx name following fx modifier\nIn template line: %s\nEffect field: "
                                               "%s\n",
                                               {l.text, l.effect})
                                      : format("No fxgroup name following fxgroup modifier\nIn template linee: %s\n"
                                               "Effect field: %s\n",
                                               {l.text, l.effect}));
                    slot.reset();
                }
            } else {
                problem(3, where,
                        format("Unknown modifier in template: %s\nIn template line: %s\nEffect field: %s\n",
                               {m, l.text, l.effect}));
            }
        }
        if (!inserted)
            syl.push_back(t);
        if (!t->isline)
            t->text = l.text;
    }
};

std::vector<TemplateRef> refs(const std::vector<Building> &list)
{
    return {list.begin(), list.end()};
}

struct Match {
    std::size_t at = 0, end = 0;
};

// The next "$name" at or after `from` (0-based), the name being [%a_]+.
std::optional<Match> nextVariable(const std::string &s, std::size_t from)
{
    for (std::size_t i = from; i + 1 < s.size(); ++i)
        if (s[i] == '$' && isVariableChar(s[i + 1])) {
            std::size_t end = i + 1;
            while (end < s.size() && isVariableChar(s[end]))
                ++end;
            return Match{i, end};
        }
    return std::nullopt;
}

const Value *lookup(const Variables &variables, const std::string &name)
{
    const auto it = variables.find(name);
    return it == variables.end() ? nullptr : &it->second;
}

std::string replaceVariables(const std::string &s, const Variables &variables)
{
    std::string out;
    std::size_t from = 0;
    while (const auto m = nextVariable(s, from)) {
        out.append(s, from, m->at - from);
        const std::string name = lower(std::string_view(s).substr(m->at + 1, m->end - m->at - 1));
        const Value *value = lookup(variables, name);
        out += value ? valueText(*value) : "$" + name;
        from = m->end;
    }
    out.append(s, from);
    return out;
}

std::optional<Where> partWhere(const std::vector<Part> *parts, const std::optional<Where> &where, std::size_t pos,
                               std::size_t length, const std::function<std::string(const std::string &)> &convert)
{
    if (!parts || parts->size() < 2)
        return where;
    std::optional<Where> found;
    std::size_t at = 0;
    for (const Part &part : *parts) {
        at += convert(part.text).size();
        if (!found && pos <= at)
            found = part.where;
    }
    if (at != length)
        return where;
    return found ? found : where;
}

std::optional<At> partAt(const std::vector<Part> *parts, std::size_t pos, std::size_t length, std::size_t total)
{
    if (!parts || parts->size() < 2)
        return At{pos, length};
    std::size_t at = 0;
    for (const Part &part : *parts)
        at += part.text.size();
    if (at != total)
        return std::nullopt;
    at = 0;
    for (const Part &part : *parts) {
        if (pos <= at + part.text.size()) {
            if (pos + length - 1 > at + part.text.size())
                return std::nullopt;
            return At{pos - at, length};
        }
        at += part.text.size();
    }
    return std::nullopt;
}

}

std::pair<std::string_view, std::string_view> headtail(std::string_view s)
{
    std::size_t space = 0;
    while (space < s.size() && !isSpace(s[space]))
        ++space;
    if (space == s.size())
        return {s, {}};
    std::size_t tail = space;
    while (tail < s.size() && isSpace(s[tail]))
        ++tail;
    return {s.substr(0, space), s.substr(tail)};
}

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char &c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return out;
}

Parsed parseTemplates(const std::vector<SubsEntry> &subs, bool keepFx, const ToNumber &toNumber)
{
    Builder b{.toNumber = toNumber};
    Parsed out;
    int number = 0;
    for (std::size_t k = 0; k < subs.size(); ++k) {
        const SubsEntry &l = subs[k];
        const int index = static_cast<int>(k + 1 - out.removed.size());
        const bool generated = l.dialogue && l.effect == "fx" && !l.comment;
        if (l.dialogue && (keepFx || !generated))
            ++number;
        if (l.dialogue && l.comment) {
            const auto [head, mods] = headtail(l.effect);
            const std::string fx = lower(head);
            const Where where{index, number, fx, l.effect, l.style};
            if (fx == "code")
                b.code(l, mods, where);
            else if (fx == "template")
                b.text(l, mods, where);
        } else if (generated && !keepFx) {
            out.removed.push_back(static_cast<int>(k + 1));
        }
    }
    out.templates.once = refs(b.once);
    out.templates.line = refs(b.line);
    for (const Building &t : b.named)
        out.templates.line.push_back(t);
    out.templates.syl = refs(b.syl);
    out.templates.furi = refs(b.furi);
    out.problems = std::move(b.problems);
    return out;
}

std::string valueText(const Value &v)
{
    if (const double *d = std::get_if<double>(&v))
        return karaoke_layout::numberText(*d);
    return std::get<std::string>(v);
}

std::string runTextTemplate(const std::string &text, const Variables *variables, const std::optional<Where> &where,
                            const std::vector<Part> *parts, const Evaluate &evaluate, const Report &report)
{
    const auto unchanged = [](const std::string &s) { return s; };
    std::function<std::string(const std::string &)> replaced = unchanged;
    std::string res = text;
    if (variables) {
        std::string out;
        std::size_t from = 0;
        while (const auto m = nextVariable(text, from)) {
            out.append(text, from, m->at - from);
            const std::string name = lower(std::string_view(text).substr(m->at + 1, m->end - m->at - 1));
            if (const Value *value = lookup(*variables, name)) {
                out += valueText(*value);
            } else {
                const std::size_t pos = m->at + 1;
                report(2, partWhere(parts, where, pos, text.size(), unchanged),
                       partAt(parts, pos, name.size() + 1, text.size()),
                       format("Unknown variable name: %s\nIn karaoke template: %s\n", {name, text}));
                out += "$" + name;
            }
            from = m->end;
        }
        out.append(text, from);
        res = std::move(out);
        replaced = [variables](const std::string &s) { return replaceVariables(s, *variables); };
    }
    const std::string expanded = res;
    const auto writtenAt = [&](std::size_t pos) -> std::optional<std::size_t> {
        if (!variables)
            return pos;
        std::size_t from = 1, at = 1;
        std::size_t scan = 0;
        while (const auto m = nextVariable(text, scan)) {
            const std::size_t start = m->at + 1, after = m->end + 1;
            if (pos < at + start - from)
                return from + pos - at;
            at += start - from;
            const Value *value = lookup(*variables, lower(std::string_view(text).substr(m->at + 1, m->end - m->at - 1)));
            const std::size_t length = value ? valueText(*value).size() : m->end - m->at;
            if (pos < at + length)
                return value ? std::nullopt : std::optional<std::size_t>(start + pos - at);
            at += length;
            from = after;
            scan = m->end;
        }
        return from + pos - at;
    };

    std::string out;
    std::size_t i = 0;
    while (i < expanded.size()) {
        const std::size_t close = expanded[i] == '!' ? expanded.find('!', i + 1) : std::string::npos;
        if (close == std::string::npos) {
            out += expanded[i++];
            continue;
        }
        const std::string expression = expanded.substr(i + 1, close - i - 1);
        const std::size_t pos = i + 1;
        const Evaluated e = evaluate(expression);
        if (e.kind == Evaluated::Kind::Text) {
            out += e.text;
        } else if (e.kind == Evaluated::Kind::Keep) {
            out += "!" + expression + "!";
        } else {
            std::optional<At> at;
            const auto open = writtenAt(pos), shut = writtenAt(pos + expression.size() + 1);
            if (open && shut)
                at = partAt(parts, *open, *shut - *open + 1, text.size());
            const bool parse = e.kind == Evaluated::Kind::ParseError;
            report(2, partWhere(parts, where, pos, expanded.size(), replaced), at,
                   format(parse ? "Error parsing expression: %s\nExpression producing error: %s\nTemplate with "
                                  "expression: %s\n"
                                : "Runtime error in template expression: %s\nExpression producing error: %s\n"
                                  "Template with expression: %s\n",
                          {e.text, expression, text}));
            out += "!" + expression + "!";
        }
        i = close + 1;
    }
    return out;
}

}
