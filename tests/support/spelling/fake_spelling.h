#pragma once

// F3: an in-memory stand-in for the spelling backend (legacy Hunspell). It
// reads a .dic file as a plain word list (the count line skipped, "/flags"
// dropped, no affix rules), checks words exactly, and suggests dictionary
// words within two edits, nearest first. It is a test double, not a
// Hunspell emulation: suggestions and accepted forms differ from Hunspell's.

#include "hikari/application/spell_checker.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <utility>
#include <string>
#include <vector>

namespace hikari::fakes {

class FakeSpelling : public application::SpellingBackend {
public:
    explicit FakeSpelling(std::set<std::u16string> words) : m_words(std::move(words)) {}

    bool spell(std::u16string_view word) override { return m_words.contains(std::u16string(word)); }
    std::vector<std::u16string> suggest(std::u16string_view word) override
    {
        std::vector<std::pair<std::size_t, std::u16string>> scored;
        for (const auto &w : m_words)
            if (const std::size_t d = distance(word, w); d <= 2)
                scored.emplace_back(d, w);
        std::ranges::sort(scored);
        std::vector<std::u16string> out;
        for (auto &s : scored)
            out.push_back(std::move(s.second));
        return out;
    }
    void add(std::u16string_view word) override
    {
        m_words.insert(std::u16string(word));
        added.emplace_back(word);
    }
    bool remove(std::u16string_view word) override
    {
        removed.emplace_back(word);
        return m_words.erase(std::u16string(word)) > 0;
    }

    std::vector<std::u16string> added;
    std::vector<std::u16string> removed;

    // A loader reading <language>.dic as a word list; `last` sees the backend.
    static application::SpellingBackendLoader loader(FakeSpelling **last = nullptr)
    {
        return [last](const std::filesystem::path &, const std::filesystem::path &dic)
                   -> std::unique_ptr<application::SpellingBackend> {
            std::ifstream in(dic, std::ios::binary);
            if (!in)
                return nullptr;
            const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::set<std::u16string> words;
            std::size_t from = 0;
            bool first = true;
            while (from < bytes.size()) {
                std::size_t at = bytes.find('\n', from);
                if (at == std::string::npos)
                    at = bytes.size();
                std::string line = bytes.substr(from, at - from);
                from = at + 1;
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (const auto slash = line.find('/'); slash != std::string::npos)
                    line.resize(slash);
                if (std::exchange(first, false) || line.empty())
                    continue;
                words.insert(core::toUtf16(std::u8string(line.begin(), line.end())));
            }
            auto backend = std::make_unique<FakeSpelling>(std::move(words));
            if (last)
                *last = backend.get();
            return backend;
        };
    }

private:
    static std::size_t distance(std::u16string_view a, std::u16string_view b)
    {
        std::vector<std::size_t> row(b.size() + 1);
        for (std::size_t j = 0; j <= b.size(); ++j)
            row[j] = j;
        for (std::size_t i = 1; i <= a.size(); ++i) {
            std::size_t previous = row[0];
            row[0] = i;
            for (std::size_t j = 1; j <= b.size(); ++j) {
                const std::size_t kept = row[j];
                row[j] = std::min({row[j] + 1, row[j - 1] + 1, previous + (a[i - 1] == b[j - 1] ? 0 : 1)});
                previous = kept;
            }
        }
        return row[b.size()];
    }

    std::set<std::u16string> m_words;
};

// A stand-in for boost::locale's word segmentation: runs of ASCII letters
// and digits are words (letters when one is a letter), every other character
// is a segment of its own; and ASCII case functions.
inline application::SpellingText asciiSpellingText()
{
    auto letter = [](char16_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); };
    auto digit = [](char16_t c) { return c >= u'0' && c <= u'9'; };
    application::SpellingText text;
    text.segment = [=](std::u16string_view s) {
        std::vector<core::legacy::WordSegment> out;
        std::size_t i = 0;
        while (i < s.size()) {
            if (!letter(s[i]) && !digit(s[i])) {
                out.push_back({i, 1, false, false});
                ++i;
                continue;
            }
            core::legacy::WordSegment w{i, 0, false, false};
            while (i < s.size() && (letter(s[i]) || digit(s[i]))) {
                w.letters = w.letters || letter(s[i]);
                ++i;
            }
            w.length = i - w.start;
            w.number = !w.letters;
            out.push_back(w);
        }
        return out;
    };
    text.cases.isUpper = [](char16_t c) { return c >= u'A' && c <= u'Z'; };
    text.cases.toUpper = [](char16_t c) { return c >= u'a' && c <= u'z' ? static_cast<char16_t>(c - 32) : c; };
    text.cases.toLower = [](char16_t c) { return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c + 32) : c; };
    return text;
}

} // namespace hikari::fakes
