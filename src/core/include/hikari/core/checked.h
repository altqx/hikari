#pragma once

#include <compare>
#include <cstdint>
#include <expected>

namespace hikari::core {

// Why a checked operation produced no value. Arithmetic never wraps.
enum class ArithmeticError {
    Overflow,           // the exact result does not fit the result type
    DivisionByZero,     // a zero divisor or denominator
    InvalidRate,        // a frame rate or timebase that is not positive
    OutOfRange,         // outside the known bounds of a timeline
};

template <class T>
using Checked = std::expected<T, ArithmeticError>;

enum class Rounding {
    Floor,              // toward negative infinity
    Ceil,               // toward positive infinity
    NearestTiesAway,    // nearest; exact halves move away from zero
};

Checked<std::int64_t> checkedAdd(std::int64_t a, std::int64_t b);
Checked<std::int64_t> checkedSub(std::int64_t a, std::int64_t b);
Checked<std::int64_t> checkedMul(std::int64_t a, std::int64_t b);

// Exact ordering of a*b against c*d, compared in 128 bits; never overflows.
std::strong_ordering compareProducts(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d);

// round(a * b / (c * d)) with an exact 128-bit intermediate. c and d must be
// positive. Fails with Overflow when the result does not fit in int64.
Checked<std::int64_t> mulDiv(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d, Rounding rounding);

namespace detail {
// The portable implementation behind mulDiv, used where the compiler has no
// native 128-bit integer (MSVC). Exposed so tests exercise it on every host.
Checked<std::int64_t> mulDivPortable(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d,
                                     Rounding rounding);
std::strong_ordering compareProductsPortable(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d);
} // namespace detail

} // namespace hikari::core
