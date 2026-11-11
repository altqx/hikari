#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The karaoke templater's template language as Karaoke Templater 2.1.7 (Aegisub 3.2.2) reads and
// expands it, with HikariSub's KE1 problem placement. Lua stays behind callbacks.
namespace hikari::application::karaoke_templates {

// util.headtail: the text before the first run of whitespace, and the text after it.
std::pair<std::string_view, std::string_view> headtail(std::string_view s);
std::string lower(std::string_view s);

// A template or code Line: its subtitle index once earlier fx Lines are gone, its number in the Grid.
struct Where {
    int index = 0;
    int number = 0;
    std::string kind, effect, style;
    friend bool operator==(const Where &, const Where &) = default;
};

// Bytes from 1 in the Line's own text.
struct At {
    std::size_t start = 0, length = 0;
    friend bool operator==(const At &, const At &) = default;
};

struct Problem {
    int level = 0;
    std::optional<Where> where;
    std::optional<At> at;
    std::string message;
};

struct Part {
    std::string text;
    Where where;
};

struct Template {
    bool code = false;
    std::string text; // the code, or the main template text
    std::string pre;
    std::optional<std::string> style;
    double loops = 1;
    int layer = 0;
    bool addtext = true, keeptags = false, multi = false, isline = false, perchar = false, noblank = false;
    std::optional<std::string> fx, fxgroup, id;
    std::optional<Where> where, textWhere, preWhere;
    std::vector<Part> textParts, preParts;
};

// One template can sit in several lists, as a Lua table can.
using TemplateRef = std::shared_ptr<const Template>;

// Unnamed line templates come in file order, then named ones in the order they first appear.
struct Templates {
    std::vector<TemplateRef> once, line, syl, furi;
};

struct SubsEntry {
    bool dialogue = false, comment = false;
    int layer = 0;
    std::string effect, text, style;
};

struct Parsed {
    Templates templates;
    std::vector<int> removed; // the fx Lines, 1-based subtitle indices before any removal
    std::vector<Problem> problems;
};

using ToNumber = std::function<std::optional<double>(std::string_view)>;

// parse_templates, parse_code and parse_template; `keepFx` leaves the generated Lines in place.
Parsed parseTemplates(const std::vector<SubsEntry> &subs, bool keepFx, const ToNumber &toNumber);

using Value = std::variant<double, std::string>;
using Variables = std::map<std::string, Value, std::less<>>;

// As tostring writes the value.
std::string valueText(const Value &v);

struct Evaluated {
    enum class Kind { Text, Keep, ParseError, RuntimeError };
    Kind kind = Kind::Keep;
    std::string text; // the value's text, or the error
};

using Evaluate = std::function<Evaluated(const std::string &expression)>;
using Report = std::function<void(int level, const std::optional<Where> &, const std::optional<At> &,
                                  const std::string &message)>;

// run_text_template: $variables (when `variables` is given), then !expressions!.
std::string runTextTemplate(const std::string &text, const Variables *variables, const std::optional<Where> &where,
                            const std::vector<Part> *parts, const Evaluate &evaluate, const Report &report);

}
