#include "hikari/application/karaoke_templates.h"

#include "hikari/application/edit_session.h"
#include "hikari/application/karaoke_layout.h"
#include "hikari/application/template_preview.h"
#include "hikari/core/document.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

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

constexpr std::array<std::string_view, 19> kKaraOKModifiers = {
    "line",     "lword", "lsyl",   "lchar", "word",     "syl",   "furi", "char",    "furichar", "all",
    "repeat",   "loop",  "notext", "keeptags", "noblank", "multi", "fx",  "fxgroup", "style"};

bool reserved(Mode mode, std::string_view id)
{
    const auto has = [&](const auto &list) { return std::ranges::find(list, id) != list.end(); };
    return mode == Mode::KaraOK ? has(kKaraOKModifiers) : has(kModifiers);
}

// A line class: whether its Lines write the pre part, and the unit its per-unit part runs before.
std::optional<std::pair<bool, Unit>> lineClass(Mode mode, std::string_view m)
{
    if (m == "lsyl")
        return std::pair{false, Unit::Syl};
    if (m == "lword")
        return std::pair{false, Unit::Word};
    if (m == "lchar")
        return std::pair{false, Unit::Char};
    if (m == "line")
        return mode == Mode::KaraOK ? std::pair{true, Unit::None} : std::pair{false, Unit::Syl};
    if (m == "pre-line" && mode == Mode::Stock)
        return std::pair{true, Unit::None};
    return std::nullopt;
}

bool unitClass(Mode mode, std::string_view m)
{
    return m == "syl" || m == "furi" || m == "word" || m == "furichar" || (m == "char" && mode == Mode::KaraOK);
}

using Building = std::shared_ptr<Template>;

struct Builder {
    std::vector<Building> once, line, named, syl, furi, word, chr, furichar;
    std::map<std::string, Building, std::less<>> byId;
    std::vector<Problem> problems;
    const ToNumber &toNumber;
    Mode mode = Mode::Stock;

    void problem(int level, const Where &where, std::string message)
    {
        problems.push_back({level, where, std::nullopt, std::move(message)});
    }

    std::vector<Building> &unitList(std::string_view m)
    {
        if (m == "syl")
            return syl;
        if (m == "furi")
            return furi;
        if (m == "word")
            return word;
        if (m == "furichar")
            return furichar;
        return chr;
    }

    bool loops(Template &t, std::string_view &rest, const SubsEntry &l, const Where &where, const char *what)
    {
        const auto [times, after] = headtail(rest);
        if (const auto n = toNumber(times)) {
            t.loops = *n;
            rest = after;
            return true;
        }
        problem(3, where,
                format("Failed reading this repeat-count to a number: %s\nIn %s: %s\nEffect field: %s\n",
                       {times, what, l.text, l.effect}));
        t.loops = 1;
        return false;
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
            if (m == "once" || m == "line") {
                (m == "once" ? once : line).push_back(t);
                inserted = true;
            } else if (m == "syl" || m == "furi" || m == "word" || m == "char" || m == "furichar") {
                unitList(m).push_back(t);
                inserted = true;
            } else if (m == "all") {
                t->style.reset();
            } else if (m == "noblank") {
                t->noblank = true;
            } else if (m == "multi" && mode == Mode::KaraOK) {
                t->multi = true;
            } else if (m == "repeat" || m == "loop") {
                loops(*t, rest, l, where, "template code line");
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
        std::optional<std::string> klass;
        std::string_view rest = mods;
        while (!rest.empty()) {
            const auto [head, tail] = headtail(rest);
            rest = tail;
            const std::string m = lower(head);
            const auto lc = lineClass(mode, m);
            if (lc && !inserted) {
                const auto [idText, after] = headtail(rest);
                std::optional<std::string> id = lower(idText);
                if (reserved(mode, *id) || id->empty())
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
                const auto [pre, unit] = *lc;
                if (pre) {
                    t->pre += l.text;
                    if (!t->preWhere)
                        t->preWhere = where;
                    t->preParts.push_back({l.text, where});
                } else if (t->unit != Unit::None && t->unit != unit) {
                    problem(2, where,
                            format("Unable to combine %s class templates with the %s class this template has\n",
                                   {m, unitName(t->unit)}));
                } else {
                    t->unit = unit;
                    t->text += l.text;
                    if (!t->textWhere)
                        t->textWhere = where;
                    t->textParts.push_back({l.text, where});
                }
            } else if (unitClass(mode, m) && !t->isline) {
                inserted = true;
                if (mode == Mode::Stock) {
                    unitList(m).push_back(t);
                } else if (!klass) {
                    unitList(m).push_back(t);
                    klass = m;
                } else if (*klass == "syl" && m == "char") {
                    std::erase(syl, t);
                    chr.push_back(t);
                    klass = m;
                } else if (*klass != m && !(*klass == "char" && m == "syl")) {
                    problem(2, where,
                            format("Unable to combine %s class templates with the %s class this template has\n",
                                   {m, *klass}));
                }
            } else if (lc && inserted) {
                problem(2, where, format("Unable to combine %s class templates with other template classes\n", {m}));
            } else if (unitClass(mode, m) && t->isline) {
                problem(2, where,
                        format(mode == Mode::KaraOK
                                   ? "Unable to combine %s class template lines with line type classes\n"
                                   : "Unable to combine %s class template lines with line or pre-line classes\n",
                               {m}));
            } else if (m == "all") {
                t->style.reset();
            } else if (m == "repeat" || m == "loop") {
                loops(*t, rest, l, where, "template line");
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
            } else if (m == "fx" || m == "fxgroup" || m == "style") {
                const auto [name, after] = headtail(rest);
                std::optional<std::string> &slot = m == "fx" ? t->fx : m == "fxgroup" ? t->fxgroup : t->stylegroup;
                if (!name.empty()) {
                    slot = std::string(name);
                    rest = after;
                } else {
                    // MUST KILL: "linee" is 2.1.7's own spelling
                    problem(3, where,
                            m == "fx" ? format("No fx name following fx modifier\nIn template line: %s\nEffect field: "
                                               "%s\n",
                                               {l.text, l.effect})
                            : m == "fxgroup"
                                ? format("No fxgroup name following fxgroup modifier\nIn template linee: %s\n"
                                         "Effect field: %s\n",
                                         {l.text, l.effect})
                                : format("No style matching string following style modifier\nIn template line: %s\n"
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

    static std::string_view unitName(Unit u)
    {
        return u == Unit::Word ? "lword" : u == Unit::Char ? "lchar" : "lsyl";
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

bool identifierChar(char c)
{
    return isVariableChar(c) || (c >= '0' && c <= '9');
}

// `name` in `text`, not followed by an identifier character.
bool mentions(std::string_view text, std::string_view name)
{
    for (std::size_t at = text.find(name); at != std::string_view::npos; at = text.find(name, at + 1))
        if (at + name.size() == text.size() || !identifierChar(text[at + name.size()]))
            return true;
    return false;
}

bool karaOKOnly(const SubsEntry &l)
{
    const auto [head, mods] = headtail(l.effect);
    const std::string fx = lower(head);
    if (fx != "template" && fx != "code")
        return false;
    for (std::string_view rest = mods; !rest.empty();) {
        const auto [word, tail] = headtail(rest);
        rest = tail;
        const std::string m = lower(word);
        if (fx == "template" ? (m == "lsyl" || m == "lword" || m == "lchar" || m == "furichar" || m == "word")
                             : (m == "word" || m == "char" || m == "furichar"))
            return true;
    }
    if (l.text.find("k_retime(") != std::string::npos)
        return true;
    for (const std::string_view name : {"tenv.word", "tenv.char", "tenv.syll", "tenv.basechar"})
        if (mentions(l.text, name))
            return true;
    if (fx == "template")
        for (std::size_t from = 0; const auto m = nextVariable(l.text, from); from = m->end) {
            const std::string name = lower(std::string_view(l.text).substr(m->at + 1, m->end - m->at - 1));
            if (name == "ci" || name == "slen" || name == "llen" || name == "len")
                return true;
        }
    return false;
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

std::optional<Mode> modeOfLine(std::string_view v)
{
    while (!v.empty() && isSpace(v.front()))
        v.remove_prefix(1);
    while (!v.empty() && isSpace(v.back()))
        v.remove_suffix(1);
    if (lower(v) == lower(kModeKaraOK))
        return Mode::KaraOK;
    if (lower(v) == lower(kModeStock))
        return Mode::Stock;
    return std::nullopt;
}

Mode modeOf(std::optional<std::string_view> modeLine, const std::vector<SubsEntry> &subs)
{
    if (const auto set = modeLine.and_then(modeOfLine))
        return *set;
    for (const SubsEntry &l : subs)
        if (l.dialogue && l.comment && karaOKOnly(l))
            return Mode::KaraOK;
    return Mode::Stock;
}

Mode documentMode(const core::Document &document, Mode withoutTemplates)
{
    const auto narrow = [](std::u8string_view s) { return std::string(s.begin(), s.end()); };
    const std::u8string key(kModeKey.begin(), kModeKey.end());
    const auto value = document.scriptInfo(key);
    const std::optional<std::string> modeLine = value ? std::optional(narrow(*value)) : std::nullopt;
    std::vector<SubsEntry> entries;
    for (const core::LineRecord *l : document.lines())
        entries.push_back({.dialogue = true,
                           .comment = l->comment,
                           .layer = static_cast<int>(l->layer.value),
                           .effect = narrow(l->effect),
                           .text = narrow(l->text),
                           .style = narrow(l->style)});
    if (!modeLine && std::ranges::none_of(document.lines(), [](const core::LineRecord *l) {
            return templateKindOf(*l).has_value();
        }))
        return withoutTemplates;
    return modeOf(modeLine, entries);
}

Mode documentMode(const EditSession &session)
{
    return documentMode(session.document(), session.isNewDocument() ? Mode::KaraOK : Mode::Stock);
}

namespace {

std::vector<std::string> effectWords(std::string_view effect)
{
    std::vector<std::string> words;
    std::size_t at = 0;
    while (at < effect.size()) {
        while (at < effect.size() && isSpace(effect[at]))
            ++at;
        const std::size_t from = at;
        while (at < effect.size() && !isSpace(effect[at]))
            ++at;
        if (at > from)
            words.emplace_back(effect.substr(from, at - from));
    }
    return words;
}

std::string joined(const std::vector<std::string> &words)
{
    std::string out;
    for (const auto &w : words)
        out += (out.empty() ? "" : " ") + w;
    return out;
}

}

std::string karaOKEffect(std::string_view stockEffect)
{
    const auto words = effectWords(stockEffect);
    if (words.empty() || lower(words.front()) != "template")
        return std::string(stockEffect);
    const bool chars = std::ranges::any_of(words, [](const std::string &w) { return lower(w) == "char"; });
    std::vector<std::string> out{words.front()};
    bool charPlaced = false;
    for (std::size_t k = 1; k < words.size(); ++k) {
        const std::string w = lower(words[k]);
        if (w == "pre-line")
            out.push_back("line");
        else if (w == "line")
            out.push_back("lsyl");
        else if (chars && (w == "syl" || w == "char")) {
            if (!std::exchange(charPlaced, true))
                out.push_back("char");
        } else
            out.push_back(words[k]);
    }
    return joined(out);
}

std::string stockEffect(std::string_view karaOKEffect)
{
    auto words = effectWords(karaOKEffect);
    if (words.empty() || lower(words.front()) != "template")
        return std::string(karaOKEffect);
    for (std::size_t k = 1; k < words.size(); ++k) {
        const std::string w = lower(words[k]);
        if (w == "line")
            words[k] = "pre-line";
        else if (w == "lsyl")
            words[k] = "line";
    }
    return joined(words);
}

Parsed parseTemplates(const std::vector<SubsEntry> &subs, bool keepFx, const ToNumber &toNumber, Mode mode)
{
    Builder b{.toNumber = toNumber, .mode = mode};
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
    out.templates.word = refs(b.word);
    out.templates.chr = refs(b.chr);
    out.templates.furichar = refs(b.furichar);
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
