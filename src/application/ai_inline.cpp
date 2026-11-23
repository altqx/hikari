#include "hikari/application/ai_inline.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

namespace hikari::application::ai_inline {

namespace {

constexpr std::u8string_view kOpen = u8"⟦";
constexpr std::u8string_view kClose = u8"⟧";

constexpr std::array<std::pair<Action, std::string_view>, 5> kActionIds{{
    {Action::Rewrite, "rewrite"},
    {Action::Shorten, "shorten"},
    {Action::Spelling, "spelling"},
    {Action::Translate, "translate"},
    {Action::Custom, "custom"},
}};

constexpr std::string_view kSystem =
    "You edit one subtitle Line for HikariSub, a subtitle editor. Answer with only the new text, nothing else: no "
    "quotes, no labels, no notes, no explanation.\n"
    "- Markers such as ⟦1⟧ stand for formatting tags. Keep every marker exactly once, in the same order, "
    "next to the words it belongs with.\n"
    "- \\N is a line break. Keep it, move it or drop it, as reads best.\n"
    "- The Lines before and after are context only. Never answer with them.\n"
    "- If the task asks for no change, answer with the text unchanged.";

std::string str(std::u8string_view s)
{
    return {reinterpret_cast<const char *>(s.data()), s.size()};
}

std::u8string u8(std::string_view s)
{
    return {reinterpret_cast<const char8_t *>(s.data()), s.size()};
}

std::string marker(std::size_t n)
{
    return str(kOpen) + std::to_string(n) + str(kClose);
}

std::string clock(std::int64_t ms)
{
    if (ms < 0)
        ms = 0;
    const std::int64_t cs = ms / 10;
    char out[32];
    std::snprintf(out, sizeof out, "%lld:%02lld:%02lld.%02lld", static_cast<long long>(cs / 360000),
                  static_cast<long long>(cs / 6000 % 60), static_cast<long long>(cs / 100 % 60),
                  static_cast<long long>(cs % 100));
    return out;
}

std::string seconds(std::int64_t ms)
{
    char out[32];
    std::snprintf(out, sizeof out, "%.2f", static_cast<double>(std::max<std::int64_t>(ms, 0)) / 1000.0);
    return out;
}

std::int64_t msOf(const core::TimeField &time)
{
    return time.value.microseconds() / 1000;
}

// The override blocks of `text` as [start, end) byte ranges, braces included; an unclosed brace runs to the end.
std::vector<std::pair<std::size_t, std::size_t>> blocks(std::u8string_view text)
{
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] != u8'{') {
            ++i;
            continue;
        }
        const std::size_t close = text.find(u8'}', i);
        const std::size_t end = close == std::u8string_view::npos ? text.size() : close + 1;
        out.emplace_back(i, end);
        i = end;
    }
    return out;
}

bool hasTag(std::u8string_view block, std::u8string_view name, bool numberMustBeNonZero)
{
    for (std::size_t at = block.find(u8'\\'); at != std::u8string_view::npos; at = block.find(u8'\\', at + 1)) {
        const std::u8string_view rest = block.substr(at + 1);
        if (!rest.starts_with(name))
            continue;
        const std::u8string_view after = rest.substr(name.size());
        if (after.empty() || after.front() < u8'0' || after.front() > u8'9')
            continue;
        if (!numberMustBeNonZero)
            return true;
        const auto end = std::ranges::find_if(after, [](char8_t c) { return c < u8'0' || c > u8'9'; });
        if (std::ranges::any_of(after.begin(), end, [](char8_t c) { return c != u8'0'; }))
            return true;
    }
    return false;
}

bool karaoke(std::u8string_view block)
{
    return hasTag(block, u8"kf", false) || hasTag(block, u8"ko", false) || hasTag(block, u8"k", false) ||
           hasTag(block, u8"K", false);
}

std::string trimmed(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    return std::string(s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1));
}

std::string unwrapped(std::string text, std::string_view open, std::string_view close)
{
    if (text.starts_with(open) && text.ends_with(close) && text.size() >= open.size() + close.size())
        return trimmed(std::string_view(text).substr(open.size(), text.size() - open.size() - close.size()));
    return text;
}

std::string fenceless(std::string text)
{
    if (!text.starts_with("```"))
        return text;
    const auto firstBreak = text.find('\n');
    const auto lastFence = text.rfind("```");
    if (firstBreak == std::string::npos || lastFence <= firstBreak)
        return text;
    return trimmed(std::string_view(text).substr(firstBreak + 1, lastFence - firstBreak - 1));
}

std::string withBreaks(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r')
            continue;
        if (text[i] == '\n') {
            while (!out.empty() && out.back() == ' ')
                out.pop_back();
            out += "\\N";
            while (i + 1 < text.size() && text[i + 1] == ' ')
                ++i;
            continue;
        }
        out += text[i];
    }
    return out;
}

std::string context(const core::LineRecord &line, const Neighbours &neighbours)
{
    std::string out;
    if (!line.style.empty())
        out += "Style: " + str(line.style) + "\n";
    if (!line.actor.empty())
        out += "Speaker: " + str(line.actor) + "\n";
    out += "Time: " + clock(msOf(line.start)) + " to " + clock(msOf(line.end)) + " (" +
           seconds(msOf(line.end) - msOf(line.start)) + " s)\n";
    if (!neighbours.before.empty()) {
        out += "\nLines before:\n";
        for (const std::u8string &n : neighbours.before)
            out += str(n) + "\n";
    }
    if (!neighbours.after.empty()) {
        out += "\nLines after:\n";
        for (const std::u8string &n : neighbours.after)
            out += str(n) + "\n";
    }
    return out;
}

std::string lowered(std::string_view s)
{
    std::string out(s);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string withoutListMark(std::string line)
{
    std::size_t at = 0;
    while (at < line.size() && std::isdigit(static_cast<unsigned char>(line[at])))
        ++at;
    if (at > 0 && at < line.size() && (line[at] == '.' || line[at] == ')'))
        line.erase(0, at + 1);
    else if (line.starts_with("- ") || line.starts_with("* "))
        line.erase(0, 2);
    else if (line.starts_with("\xE2\x80\xA2"))
        line.erase(0, 3);
    line = trimmed(line);
    line = unwrapped(std::move(line), "\"", "\"");
    if (line.size() >= 6)
        line = unwrapped(std::move(line), "\xE2\x80\x9C", "\xE2\x80\x9D");
    return trimmed(line);
}

constexpr std::string_view kSynonymsSystem =
    "You suggest replacements for one word of a subtitle Line for HikariSub, a subtitle editor. Answer with up to 8 "
    "replacements, best first, one per line, and nothing else: no numbers, no notes, no explanation.\n"
    "- Each replacement must fit the sentence as it stands: the same part of speech, tense, number, case and "
    "register, in the Line's language. A short phrase is fine when no single word fits.\n"
    "- The Lines before and after are context only.\n"
    "- If nothing fits, answer with nothing.";

std::string task(const Ask &ask, const core::LineRecord &line, bool part)
{
    const std::string what = part ? "the part inside <edit>" : "the Line";
    switch (ask.action) {
    case Action::Rewrite:
    case Action::Custom:
        return "Rewrite " + what + ": " + ask.instruction;
    case Action::Shorten: {
        const std::int64_t ms = msOf(line.end) - msOf(line.start);
        const std::int64_t chars = ms > 0 ? ms * ask.maxCps / 1000 : 0;
        return "Shorten " + what + " so the whole Line reads at " + std::to_string(ask.maxCps) +
               " characters per second or less: it is on screen for " + seconds(ms) + " s, so aim for at most " +
               std::to_string(chars) + " characters. Keep the meaning and the speaker's voice.";
    }
    case Action::Spelling:
        return "Fix the spelling, grammar and punctuation mistakes in " + what +
               ". Change nothing else. If there are none, answer with it unchanged.";
    case Action::Translate:
        return "Translate " + what + " into " + ask.language + ". Keep names as they are.";
    }
    return {};
}

}

std::string_view actionId(Action action)
{
    for (const auto &[a, id] : kActionIds)
        if (a == action)
            return id;
    return {};
}

std::optional<Action> actionFromId(std::string_view id)
{
    for (const auto &[a, name] : kActionIds)
        if (name == id)
            return a;
    return std::nullopt;
}

Skip skipOf(const core::LineRecord &line)
{
    if (line.comment)
        return Skip::Comment;
    const auto found = blocks(line.text);
    for (const auto &[start, end] : found) {
        const std::u8string_view block = std::u8string_view(line.text).substr(start, end - start);
        if (hasTag(block, u8"p", true))
            return Skip::Drawing;
        if (karaoke(block))
            return Skip::Karaoke;
    }
    const std::u8string rest = stripped(line.text);
    if (rest.find_first_not_of(u8" \t") == std::u8string::npos)
        return found.empty() ? Skip::Empty : Skip::OnlyTags;
    return Skip::None;
}

Masked mask(std::u8string_view text)
{
    Masked out;
    std::size_t at = 0;
    for (const auto &[start, end] : blocks(text)) {
        out.text += text.substr(at, start - at);
        out.tags.emplace_back(text.substr(start, end - start));
        out.text += u8(marker(out.tags.size()));
        at = end;
    }
    out.text += text.substr(at);
    return out;
}

std::optional<std::u8string> unmask(std::u8string_view answer, const std::vector<std::u8string> &tags)
{
    std::u8string out;
    std::size_t at = 0;
    std::size_t next = 1;
    for (std::size_t open = answer.find(kOpen); open != std::u8string_view::npos; open = answer.find(kOpen, at)) {
        const std::size_t close = answer.find(kClose, open + kOpen.size());
        if (close == std::u8string_view::npos)
            return std::nullopt;
        const std::u8string_view number = answer.substr(open + kOpen.size(), close - open - kOpen.size());
        if (str(number) != std::to_string(next) || next > tags.size())
            return std::nullopt;
        out += answer.substr(at, open - at);
        out += tags[next - 1];
        ++next;
        at = close + kClose.size();
    }
    if (next != tags.size() + 1)
        return std::nullopt;
    out += answer.substr(at);
    return out;
}

std::u8string stripped(std::u8string_view text)
{
    std::u8string out;
    std::size_t at = 0;
    for (const auto &[start, end] : blocks(text)) {
        out += text.substr(at, start - at);
        at = end;
    }
    out += text.substr(at);
    return out;
}

Prepared prepare(const Ask &ask, const core::LineRecord &line, const Neighbours &neighbours,
                 std::optional<std::pair<std::size_t, std::size_t>> span)
{
    Prepared out;
    const std::u8string_view text = line.text;
    std::size_t first = 0, last = text.size();
    if (span && span->first < span->second && span->second <= text.size()) {
        first = span->first;
        last = span->second;
    }
    const bool part = first > 0 || last < text.size();
    out.prefix = text.substr(0, first);
    out.suffix = text.substr(last);
    out.sent = text.substr(first, last - first);
    Masked masked = mask(out.sent);
    out.tags = std::move(masked.tags);
    out.system = std::string(kSystem);

    std::string user = "Task: " + task(ask, line, part) + "\n\n" + context(line, neighbours) + "\nThe Line:\n<line>";
    if (part)
        user += str(stripped(out.prefix)) + "<edit>" + str(masked.text) + "</edit>" + str(stripped(out.suffix));
    else
        user += str(masked.text);
    user += "</line>\n";
    if (part)
        user += "\nAnswer with only the new text of the part inside <edit>.";
    out.user = std::move(user);
    return out;
}

Prepared prepareSynonyms(const core::LineRecord &line, const Neighbours &neighbours,
                         std::pair<std::size_t, std::size_t> word)
{
    const std::u8string_view text = line.text;
    Prepared out;
    out.prefix = text.substr(0, word.first);
    out.sent = text.substr(word.first, word.second - word.first);
    out.suffix = text.substr(word.second);
    out.system = std::string(kSynonymsSystem);
    out.user = "Task: suggest replacements for the word inside <word>.\n\n" + context(line, neighbours) +
               "\nThe Line:\n<line>" + str(stripped(out.prefix)) + "<word>" + str(out.sent) + "</word>" +
               str(stripped(out.suffix)) + "</line>\n";
    return out;
}

std::vector<std::u8string> readSynonyms(std::string_view answer, std::u8string_view word)
{
    std::vector<std::u8string> out;
    std::vector<std::string> seen{lowered(str(word))};
    const std::string text = fenceless(trimmed(answer));
    std::size_t from = 0;
    while (from <= text.size() && out.size() < kMaxSynonyms) {
        const std::size_t end = std::min(text.find('\n', from), text.size());
        const std::string candidate = withoutListMark(text.substr(from, end - from));
        from = end + 1;
        if (candidate.empty() || candidate.size() > kMaxSynonymBytes ||
            candidate.find_first_of("{}\\<>") != std::string::npos || candidate.find(str(kOpen)) != std::string::npos)
            continue;
        std::string key = lowered(candidate);
        if (std::ranges::find(seen, key) != seen.end())
            continue;
        seen.push_back(std::move(key));
        out.push_back(u8(candidate));
    }
    return out;
}

Answer readAnswer(const Prepared &prepared, std::string_view answer)
{
    std::string text = fenceless(trimmed(answer));
    text = unwrapped(std::move(text), "<line>", "</line>");
    text = unwrapped(std::move(text), "<edit>", "</edit>");
    const std::string sent = str(prepared.sent);
    if (text.size() >= 2 && !sent.starts_with('"'))
        text = unwrapped(std::move(text), "\"", "\"");
    if (text.size() >= 6 && !sent.starts_with("“"))
        text = unwrapped(std::move(text), "“", "”");
    text = withBreaks(text);
    if (trimmed(text).empty() && !prepared.sent.empty())
        return {Outcome::Empty, {}};
    const auto restored = unmask(u8(text), prepared.tags);
    if (!restored)
        return {Outcome::DroppedTag, {}};
    if (*restored == prepared.sent)
        return {Outcome::Same, {}};
    return {Outcome::Changed, prepared.prefix + *restored + prepared.suffix};
}

}
