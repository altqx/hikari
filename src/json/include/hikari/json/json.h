#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace hikari::json {

// A JSON value. Objects keep their keys in insertion order, each key once.
// Integers without a fraction or exponent stay exact as int64.
class Json {
public:
    struct Member;
    using Array = std::vector<Json>;
    using Object = std::vector<Member>;
    enum class Type { Null, Bool, Int, Double, String, Array, Object };

    Json();
    Json(std::nullptr_t);
    Json(bool value);
    Json(int value);
    Json(std::int64_t value);
    Json(std::uint64_t value);
    Json(double value);
    Json(const char *value);
    Json(std::string_view value);
    Json(std::string value);
    Json(Array value);
    Json(Object value);
    Json(const Json &);
    Json(Json &&) noexcept;
    Json &operator=(const Json &);
    Json &operator=(Json &&) noexcept;
    ~Json();

    static Json array(std::initializer_list<Json> items = {});
    static Json object(std::initializer_list<Member> members = {});

    Type type() const { return static_cast<Type>(m_value.index()); }
    bool isNull() const { return type() == Type::Null; }
    bool isBool() const { return type() == Type::Bool; }
    bool isInt() const { return type() == Type::Int; }
    bool isNumber() const { return type() == Type::Int || type() == Type::Double; }
    bool isString() const { return type() == Type::String; }
    bool isArray() const { return type() == Type::Array; }
    bool isObject() const { return type() == Type::Object; }

    // Each accessor needs the matching type; asNumber also reads an Int.
    bool asBool() const { return std::get<bool>(m_value); }
    std::int64_t asInt() const { return std::get<std::int64_t>(m_value); }
    double asNumber() const;
    const std::string &asString() const { return std::get<std::string>(m_value); }
    const Array &asArray() const { return std::get<Array>(m_value); }
    Array &asArray() { return std::get<Array>(m_value); }
    const Object &asObject() const { return std::get<Object>(m_value); }
    Object &asObject() { return std::get<Object>(m_value); }

    // Null for a missing key or a value that is not an object.
    const Json *find(std::string_view key) const;
    // Inserts null for a new key; a null value becomes an empty object first.
    Json &operator[](std::string_view key);
    // A null value becomes an empty array first.
    void push(Json value);

    // Members compare in order; 1 and 1.0 differ.
    friend bool operator==(const Json &a, const Json &b);

    // Compact for indent < 0, else that many spaces per level.
    std::string dump(int indent = -1) const;

    struct ParseError {
        std::size_t offset = 0;
        std::string message;
    };
    // The whole text must be one value; nesting deeper than kMaxDepth is refused.
    static constexpr int kMaxDepth = 256;
    static std::expected<Json, ParseError> parse(std::string_view text);

private:
    std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object> m_value;
};

struct Json::Member {
    std::string key;
    Json value;
    friend bool operator==(const Member &, const Member &) = default;
};

bool isUtf8(std::string_view text);

}
