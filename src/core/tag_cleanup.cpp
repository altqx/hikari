#include "hikari/core/tag_cleanup.h"

#include "hikari/core/lua_pattern.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace hikari::core::tagcleanup {

namespace {

namespace lp = luapattern;

struct Tag {
    std::string name;
    std::string value;
};

struct Item {
    std::optional<Tag> tag;
    std::string raw;
};

struct Piece {
    bool block = false;
    std::string text;
    std::vector<Item> items;
    bool hadTags = false;
};

constexpr std::array<std::string_view, 53> kNames{
    "iclip", "alpha", "xbord", "ybord", "xshad", "yshad", "clip", "bord", "shad", "blur", "fscx", "fscy", "fade", "move",
    "fsp",   "frx",   "fry",   "frz",   "fax",   "fay",   "fad",  "pos",  "org",  "pbo",  "fs",   "fr",   "fn",   "fe",
    "an",    "be",    "kf",    "ko",    "kt",    "1c",    "2c",   "3c",   "4c",   "1a",   "2a",   "3a",   "4a",   "b",
    "i",     "u",     "s",     "a",     "q",     "p",     "r",    "t",    "k",    "K",    "c"};

std::string_view bytes(std::u8string_view text)
{
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

std::u8string utf8(std::string_view text)
{
    return {reinterpret_cast<const char8_t *>(text.data()), text.size()};
}

bool letter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

std::vector<Item> parseTags(std::string_view content)
{
    std::vector<Item> items;
    std::size_t i = content.find('\\');
    if (i != 0) {
        items.push_back(Item{std::nullopt, std::string(content.substr(0, i))});
        if (i == std::string_view::npos)
            return items;
    }
    while (i < content.size()) {
        const std::string_view rest = content.substr(i + 1);
        std::string name;
        for (const std::string_view known : kNames)
            if (rest.starts_with(known)) {
                name = known;
                break;
            }
        if (name.empty()) {
            std::size_t n = 0;
            while (n < rest.size() && letter(rest[n]))
                ++n;
            name = rest.substr(0, n);
        }
        std::size_t end = i + 1 + name.size();
        int depth = 0;
        for (; end < content.size(); ++end) {
            const char c = content[end];
            if (c == '(')
                ++depth;
            else if (c == ')' && depth > 0)
                --depth;
            else if (c == '\\' && depth == 0)
                break;
        }
        items.push_back(Item{Tag{name, std::string(content.substr(i + 1 + name.size(), end - i - 1 - name.size()))}, {}});
        i = end;
    }
    return items;
}

std::vector<Piece> parse(std::string_view text)
{
    std::vector<Piece> pieces;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t open = text.find('{', i);
        const std::size_t close = open == std::string_view::npos ? open : text.find('}', open);
        if (close == std::string_view::npos) {
            pieces.push_back(Piece{false, std::string(text.substr(i)), {}, false});
            break;
        }
        if (open > i)
            pieces.push_back(Piece{false, std::string(text.substr(i, open - i)), {}, false});
        auto items = parseTags(text.substr(open + 1, close - open - 1));
        const bool tags = std::ranges::any_of(items, [](const Item &item) { return item.tag.has_value(); });
        pieces.push_back(Piece{true, {}, std::move(items), tags});
        i = close + 1;
    }
    return pieces;
}

std::string serialize(const std::vector<Item> &items)
{
    std::string out;
    for (const auto &item : items)
        out += item.tag ? "\\" + item.tag->name + item.tag->value : item.raw;
    return out;
}

std::u8string serialize(const std::vector<Piece> &pieces)
{
    std::string out;
    for (const auto &p : pieces) {
        if (!p.block) {
            out += p.text;
            continue;
        }
        const bool tags = std::ranges::any_of(p.items, [](const Item &item) { return item.tag.has_value(); });
        const bool comment = std::ranges::any_of(p.items, [](const Item &item) { return !item.tag && !item.raw.empty(); });
        if (p.hadTags && !tags && !comment)
            continue;
        out += '{' + serialize(p.items) + '}';
    }
    return utf8(out);
}

std::string canonical(std::string_view name)
{
    if (name == "c")
        return "1c";
    if (name == "fr")
        return "frz";
    return std::string(name);
}

template <typename Keep>
void filterTransform(Tag &t, const Keep &keep, bool &emptied)
{
    if (t.value.empty() || t.value.front() != '(')
        return;
    int depth = 0;
    std::size_t close = 0;
    for (std::size_t i = 0; i < t.value.size() && !close; ++i) {
        if (t.value[i] == '(')
            ++depth;
        else if (t.value[i] == ')' && --depth == 0)
            close = i;
    }
    if (!close)
        return;
    const std::string_view inner = std::string_view(t.value).substr(1, close - 1);
    const std::size_t first = inner.find('\\');
    if (first == std::string_view::npos)
        return;
    auto items = parseTags(inner.substr(first));
    std::erase_if(items, [&](const Item &item) { return item.tag && !keep(*item.tag); });
    emptied = items.empty();
    t.value = '(' + std::string(inner.substr(0, first)) + serialize(items) + t.value.substr(close);
}

bool lastWins(TagFamily family)
{
    switch (family) {
    case TagFamily::Position:
    case TagFamily::Origin:
    case TagFamily::Alignment:
    case TagFamily::Clip:
    case TagFamily::Fade:
    case TagFamily::Transform:
    case TagFamily::Karaoke:
    case TagFamily::Reset:
    case TagFamily::WrapStyle:
    case TagFamily::Drawing:
    case TagFamily::Other: return false;
    default: return true;
    }
}

bool relative(const Tag &t)
{
    return t.name == "fs" && (t.value.starts_with('+') || t.value.starts_with('-'));
}

std::optional<std::string_view> lineWideKey(std::string_view name)
{
    switch (familyOf(name)) {
    case TagFamily::Position: return "pos";
    case TagFamily::Origin: return "org";
    case TagFamily::Alignment: return "an";
    case TagFamily::Fade: return "fad";
    default: return std::nullopt;
    }
}

}

TagFamily familyOf(std::string_view n)
{
    static const std::map<std::string_view, TagFamily> kFamilies{
        {"c", TagFamily::Colour},       {"1c", TagFamily::Colour},       {"2c", TagFamily::Colour},
        {"3c", TagFamily::Colour},      {"4c", TagFamily::Colour},       {"alpha", TagFamily::Alpha},
        {"1a", TagFamily::Alpha},       {"2a", TagFamily::Alpha},        {"3a", TagFamily::Alpha},
        {"4a", TagFamily::Alpha},       {"bord", TagFamily::Border},     {"xbord", TagFamily::Border},
        {"ybord", TagFamily::Border},   {"shad", TagFamily::Shadow},     {"xshad", TagFamily::Shadow},
        {"yshad", TagFamily::Shadow},   {"blur", TagFamily::Blur},       {"be", TagFamily::Blur},
        {"fn", TagFamily::Font},        {"fs", TagFamily::Font},         {"fe", TagFamily::Font},
        {"fscx", TagFamily::Scale},     {"fscy", TagFamily::Scale},      {"fsp", TagFamily::Spacing},
        {"frz", TagFamily::Rotation},   {"fr", TagFamily::Rotation},     {"frx", TagFamily::Rotation},
        {"fry", TagFamily::Rotation},   {"fax", TagFamily::Shear},       {"fay", TagFamily::Shear},
        {"b", TagFamily::FontStyle},    {"i", TagFamily::FontStyle},     {"u", TagFamily::FontStyle},
        {"s", TagFamily::FontStyle},    {"pos", TagFamily::Position},    {"move", TagFamily::Position},
        {"org", TagFamily::Origin},     {"an", TagFamily::Alignment},    {"a", TagFamily::Alignment},
        {"clip", TagFamily::Clip},      {"iclip", TagFamily::Clip},      {"fad", TagFamily::Fade},
        {"fade", TagFamily::Fade},      {"t", TagFamily::Transform},     {"k", TagFamily::Karaoke},
        {"K", TagFamily::Karaoke},      {"kf", TagFamily::Karaoke},      {"ko", TagFamily::Karaoke},
        {"kt", TagFamily::Karaoke},     {"r", TagFamily::Reset},         {"q", TagFamily::WrapStyle},
        {"p", TagFamily::Drawing},      {"pbo", TagFamily::Drawing},
    };
    const auto it = kFamilies.find(n);
    return it == kFamilies.end() ? TagFamily::Other : it->second;
}

std::u8string removeTags(std::u8string_view text, const Families &families, BlockScope scope)
{
    if (families.none())
        return std::u8string(text);
    auto pieces = parse(bytes(text));
    const auto keep = [&](const Tag &t) { return !families.test(static_cast<std::size_t>(familyOf(t.name))); };
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        auto &p = pieces[i];
        const bool start = i == 0;
        if (!p.block || (scope == BlockScope::Start && !start) || (scope == BlockScope::Inline && start))
            continue;
        std::erase_if(p.items, [&](Item &item) {
            if (!item.tag)
                return false;
            if (!keep(*item.tag))
                return true;
            bool emptied = false;
            if (item.tag->name == "t")
                filterTransform(*item.tag, keep, emptied);
            return emptied;
        });
    }
    return serialize(pieces);
}

std::u8string removeComments(std::u8string_view text)
{
    auto pieces = parse(bytes(text));
    std::erase_if(pieces, [](const Piece &p) {
        return p.block && std::ranges::all_of(p.items, [](const Item &item) { return !item.tag || item.tag->name == "N"; });
    });
    return serialize(pieces);
}

std::u8string mergeBlocks(std::u8string_view text)
{
    std::string out(bytes(text));
    for (int count = 1; count;) {
        auto r = lp::gsub(out, "({\\[^}]-)}{(\\[^}]-})", "%1%2");
        out = std::move(r.text);
        count = r.count;
    }
    return utf8(out);
}

std::u8string dropRedundant(std::u8string_view text)
{
    auto pieces = parse(bytes(text));
    std::set<std::string_view> seenLineWide;
    for (auto &p : pieces) {
        if (!p.block)
            continue;
        std::vector<unsigned char> drop(p.items.size(), 0);
        std::map<std::string, std::vector<std::size_t>> pending;
        for (std::size_t i = 0; i < p.items.size(); ++i) {
            const auto &tag = p.items[i].tag;
            if (!tag)
                continue;
            if (tag->name == "r") {
                pending.clear();
                continue;
            }
            if (const auto key = lineWideKey(tag->name)) {
                drop[i] = !seenLineWide.insert(*key).second;
                continue;
            }
            if (!lastWins(familyOf(tag->name)))
                continue;
            auto &earlier = pending[canonical(tag->name)];
            if (!relative(*tag)) {
                for (const auto e : earlier)
                    drop[e] = 1;
                earlier.clear();
            }
            earlier.push_back(i);
        }
        std::vector<Item> kept;
        for (std::size_t i = 0; i < p.items.size(); ++i)
            if (!drop[i])
                kept.push_back(std::move(p.items[i]));
        p.items = std::move(kept);
    }
    return serialize(pieces);
}

std::u8string cleanTags(std::u8string_view input)
{
    static constexpr std::string_view ktag = "\\[kK][fo]?%d+";
    std::string text(bytes(input));
    const auto has = [](std::string_view s, std::string_view pattern) { return lp::find(s, pattern).has_value(); };
    const auto repeatGsub = [&](std::string_view pattern, const auto &replacement) {
        for (int count = 1; count;) {
            auto r = lp::gsub(text, pattern, replacement);
            text = std::move(r.text);
            count = r.count;
        }
    };

    repeatGsub("{(.-)}{(.-)}", std::function([&](const lp::Captures &c) -> std::optional<std::string> {
                   if (has(c[0], ktag) && has(c[1], ktag))
                       return '{' + c[0] + "}\x01{" + c[1] + '}';
                   return '{' + c[0] + c[1] + '}';
               }));
    std::erase(text, '\x01');

    text = lp::gsub(text, "{([^{}]-)(" + std::string(ktag) + ")(.-)}", "{%2%1%3}").text;
    repeatGsub("{([^{}]-)(" + std::string(ktag) + ")(\\[^kK][^}]-)(" + std::string(ktag) + ")(.-)}", std::string_view("{%1%2%4%3%5}"));

    std::string linetags;
    const auto first = [&](std::string_view pattern) {
        if (const auto m = lp::find(text, pattern)) {
            text = lp::gsub(text, pattern, "").text;
            linetags += m->captures[0];
        }
    };
    const auto firstOfTwo = [&](std::string_view pattern1, std::string_view pattern2) {
        const auto m1 = lp::find(text, pattern1);
        const auto m2 = lp::find(text, pattern2);
        text = lp::gsub(text, pattern1, "").text;
        text = lp::gsub(text, pattern2, "").text;
        if (m1 && (!m2 || m1->start < m2->start))
            linetags += m1->captures[0];
        else if (m2)
            linetags += m2->captures[0];
    };
    first("(\\an?%d+)");
    first("(\\org%([^,%)]*,[^,%)]*%))");
    firstOfTwo("(\\move%([^,%)]*,[^,%)]*,[^,%)]*,[^,%)]*%))", "(\\pos%([^,%)]*,[^,%)]*%))");
    firstOfTwo("(\\fade%([^,%)]*,[^,%)]*,[^,%)]*,[^,%)]*,[^,%)]*,[^,%)]*,[^,%)]*%))", "(\\fad%([^,%)]*,[^,%)]*%))");
    if (!linetags.empty())
        text = text.starts_with('{') ? '{' + linetags + text.substr(1) : '{' + linetags + '}' + text;

    const auto oneOf = [](std::string_view chars, char c) { return chars.find(c) != std::string_view::npos; };
    repeatGsub("({[^}\\]*)([^}%s]*(\\[^%(}\\%s]*))%s*(%([^%s%)}]*)%s+([^}]*)",
               std::function([&](const lp::Captures &c) -> std::optional<std::string> {
                   const auto &a = c[0], &b = c[1], &tag = c[2], &d = c[3], &e = c[4];
                   if ((tag != "\\clip" && tag != "\\iclip") || oneOf(",({", d.back()) || (!e.empty() && oneOf(",)}", e.front())))
                       return a + b + d + e;
                   return a + b + d + '\x02' + e;
               }));
    std::ranges::replace(text, '\x02', ' ');

    text = lp::gsub(text, "{%s*}", "").text;
    return utf8(text);
}

std::u8string apply(std::u8string_view text, const Options &options)
{
    std::u8string out = removeTags(text, options.families, options.scope);
    const auto &c = options.cleanup;
    if (c.removeComments)
        out = removeComments(out);
    if (c.mergeBlocks)
        out = mergeBlocks(out);
    if (c.dropRedundant)
        out = dropRedundant(out);
    if (c.cleanTags)
        out = cleanTags(out);
    return out;
}

}
