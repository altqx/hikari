#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The .NET behaviour YTSubConverter 1.6.5 relies on, so the port's output matches it byte for byte.
namespace hikari::core::srv3 {

using u16 = std::u16string;
using u16v = std::u16string_view;

// DateTime and TimeSpan ticks: 100 ns.
using Ticks = std::int64_t;
inline constexpr Ticks kTicksPerMs = 10'000;

struct ConversionError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// System.Drawing.Color: Empty is not the same as ARGB 0.
class Color {
public:
    constexpr Color() = default;
    static constexpr Color fromArgb(std::uint32_t argb) { return Color(argb); }
    static Color fromArgb(int a, int r, int g, int b);
    static Color fromArgb(int a, Color rgb) { return fromArgb(a, rgb.r(), rgb.g(), rgb.b()); }
    static Color fromRgb(int r, int g, int b) { return fromArgb(255, r, g, b); }

    constexpr bool isEmpty() const { return m_empty; }
    constexpr std::uint32_t toArgb() const { return m_argb; }
    constexpr int a() const { return static_cast<int>(m_argb >> 24); }
    constexpr int r() const { return static_cast<int>((m_argb >> 16) & 0xFF); }
    constexpr int g() const { return static_cast<int>((m_argb >> 8) & 0xFF); }
    constexpr int b() const { return static_cast<int>(m_argb & 0xFF); }
    constexpr bool operator==(const Color &) const = default;

private:
    constexpr explicit Color(std::uint32_t argb) : m_empty(false), m_argb(argb) {}
    bool m_empty = true;
    std::uint32_t m_argb = 0;
};

Color changeAlpha(Color color, int alpha);
bool isDark(Color color);
Color brighten(Color color);
u16 toHtml(Color color);

struct PointF {
    float x = 0, y = 0;
    bool operator==(const PointF &) const = default;
};

// System.Collections.Generic.Dictionary's enumeration order: a removed entry's slot is reused by the next add.
template<typename K, typename V>
class NetDict {
public:
    std::size_t size() const { return m_count; }
    bool empty() const { return m_count == 0; }

    V *find(const K &key)
    {
        for (auto &slot : m_slots)
            if (slot && slot->first == key)
                return &slot->second;
        return nullptr;
    }
    const V *find(const K &key) const { return const_cast<NetDict *>(this)->find(key); }
    bool contains(const K &key) const { return find(key) != nullptr; }
    const V &at(const K &key) const
    {
        if (const V *v = find(key))
            return *v;
        throw ConversionError("The given key was not present in the dictionary.");
    }
    V getOrDefault(const K &key) const
    {
        const V *v = find(key);
        return v ? *v : V{};
    }

    void set(const K &key, V value)
    {
        if (V *v = find(key)) {
            *v = std::move(value);
            return;
        }
        add(key, std::move(value));
    }
    void add(const K &key, V value)
    {
        if (contains(key))
            throw ConversionError("An item with the same key has already been added.");
        ++m_count;
        if (!m_free.empty()) {
            const std::size_t i = m_free.back();
            m_free.pop_back();
            m_slots[i].emplace(key, std::move(value));
            return;
        }
        m_slots.emplace_back(std::in_place, key, std::move(value));
    }
    bool remove(const K &key)
    {
        for (std::size_t i = 0; i < m_slots.size(); ++i)
            if (m_slots[i] && m_slots[i]->first == key) {
                m_slots[i].reset();
                m_free.push_back(i);
                --m_count;
                return true;
            }
        return false;
    }
    template<typename Pred>
    void removeIf(Pred pred)
    {
        for (const auto &[k, v] : entries())
            if (pred(k, v))
                remove(k);
    }
    void clear()
    {
        m_slots.clear();
        m_free.clear();
        m_count = 0;
    }

    std::vector<std::pair<K, V>> entries() const
    {
        std::vector<std::pair<K, V>> out;
        for (const auto &slot : m_slots)
            if (slot)
                out.push_back(*slot);
        return out;
    }
    std::vector<K> keys() const
    {
        std::vector<K> out;
        for (const auto &slot : m_slots)
            if (slot)
                out.push_back(slot->first);
        return out;
    }

private:
    std::vector<std::optional<std::pair<K, V>>> m_slots;
    std::vector<std::size_t> m_free;
    std::size_t m_count = 0;
};

// System.Random(int seed): the subtractive generator .NET keeps for seeded instances.
class NetRandom {
public:
    explicit NetRandom(int seed);
    double nextDouble() { return sample(); }
    int next(int maxValue) { return static_cast<int>(sample() * maxValue); }

private:
    double sample();
    int m_seeds[56] = {};
    int m_next = 0, m_nextP = 21;
};

// int.TryParse with NumberStyles.Integer.
std::optional<int> parseInt(u16v s);
// NumberStyles.Float, or NumberStyles.Any with `any`.
std::optional<float> parseFloat(u16v s, bool any = false);
// int.TryParse / uint.TryParse with AllowHexSpecifier (and surrounding white space when `white`).
std::optional<std::uint32_t> parseHex(u16v s, bool white);

bool isWhiteSpace(char16_t c);
u16v trim(u16v s);
u16 replaceAll(u16v s, u16v from, u16v to);
// String.Split(string, maxItems) from YTSubConverter's StringExtensions.
std::vector<u16> splitMax(u16v s, u16v separator, std::optional<std::size_t> maxItems);
std::vector<u16> split(u16v s, char16_t separator);
// Culture-sensitive String.StartsWith / EndsWith under ICU, which skip ignorable code points such as U+200B.
bool startsWithLinguistic(u16v s, u16v prefix);
bool endsWithLinguistic(u16v s, u16v suffix);
u16 toLowerInvariant(u16v s);
u16 toU16(std::string_view ascii);
u16 decimalString(std::int64_t v);

// System.Text.RegularExpressions over PCRE2.
class NetRegex {
public:
    struct Match {
        std::size_t index = 0, length = 0;
        std::vector<std::optional<std::pair<std::size_t, std::size_t>>> groups;
        u16v subject;
        u16v group(std::size_t i) const
        {
            if (i >= groups.size() || !groups[i])
                return {};
            return subject.substr(groups[i]->first, groups[i]->second);
        }
        u16v value() const { return subject.substr(index, length); }
    };

    explicit NetRegex(u16v pattern);
    ~NetRegex();
    NetRegex(const NetRegex &) = delete;
    NetRegex &operator=(const NetRegex &) = delete;

    std::optional<Match> match(u16v subject, std::size_t from = 0) const;
    std::vector<Match> matches(u16v subject) const;
    bool isMatch(u16v subject) const { return match(subject).has_value(); }
    std::size_t groupNumber(std::string_view name) const;
    u16 replace(u16v subject, const std::function<u16(const Match &)> &evaluator) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
