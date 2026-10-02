#include "hikari/core/checked.h"

#include <limits>

namespace hikari::core {

namespace {

// Unsigned 128-bit magnitude for the portable path.
struct U128 {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
};

U128 mul64(std::uint64_t a, std::uint64_t b)
{
    const std::uint64_t aL = a & 0xffffffffu, aH = a >> 32;
    const std::uint64_t bL = b & 0xffffffffu, bH = b >> 32;
    const std::uint64_t ll = aL * bL, lh = aL * bH, hl = aH * bL, hh = aH * bH;
    const std::uint64_t mid = (ll >> 32) + (lh & 0xffffffffu) + (hl & 0xffffffffu);
    return {hh + (lh >> 32) + (hl >> 32) + (mid >> 32), (mid << 32) | (ll & 0xffffffffu)};
}

bool less(const U128 &x, const U128 &y)
{
    return x.hi != y.hi ? x.hi < y.hi : x.lo < y.lo;
}

U128 sub(const U128 &x, const U128 &y)
{
    return {x.hi - y.hi - (x.lo < y.lo ? 1u : 0u), x.lo - y.lo};
}

bool isZero(const U128 &x)
{
    return x.hi == 0 && x.lo == 0;
}

// Restoring long division: quotient and remainder of n / d, d != 0.
void divMod(U128 n, const U128 &d, U128 &q, U128 &r)
{
    q = {};
    r = {};
    for (int bit = 127; bit >= 0; --bit) {
        r = {(r.hi << 1) | (r.lo >> 63), r.lo << 1};
        const std::uint64_t nbit = bit >= 64 ? (n.hi >> (bit - 64)) & 1u : (n.lo >> bit) & 1u;
        r.lo |= nbit;
        if (!less(r, d)) {
            r = sub(r, d);
            if (bit >= 64)
                q.hi |= std::uint64_t{1} << (bit - 64);
            else
                q.lo |= std::uint64_t{1} << bit;
        }
    }
}

std::uint64_t magnitude(std::int64_t v)
{
    return v < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
}

// Applies the rounding to a truncated quotient magnitude q of a result with the
// given sign, then converts to int64, reporting overflow.
Checked<std::int64_t> finish(U128 q, const U128 &rem, const U128 &den, bool negative, Rounding rounding)
{
    bool bump = false;
    if (!isZero(rem)) {
        switch (rounding) {
        case Rounding::Floor:
            bump = negative;
            break;
        case Rounding::Ceil:
            bump = !negative;
            break;
        case Rounding::NearestTiesAway: {
            // Compare 2*rem with den: equal is a tie, which moves away from zero.
            const U128 twice{(rem.hi << 1) | (rem.lo >> 63), rem.lo << 1};
            bump = (rem.hi >> 63) != 0 || !less(twice, den);
            break;
        }
        }
    }
    if (bump) {
        q.lo += 1;
        if (q.lo == 0)
            q.hi += 1;
    }
    constexpr std::uint64_t maxPositive = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (q.hi != 0)
        return std::unexpected(ArithmeticError::Overflow);
    if (!negative) {
        if (q.lo > maxPositive)
            return std::unexpected(ArithmeticError::Overflow);
        return static_cast<std::int64_t>(q.lo);
    }
    if (q.lo > maxPositive + 1)
        return std::unexpected(ArithmeticError::Overflow);
    if (q.lo == maxPositive + 1)
        return std::numeric_limits<std::int64_t>::min();
    return -static_cast<std::int64_t>(q.lo);
}

} // namespace

namespace detail {

std::strong_ordering compareProductsPortable(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d)
{
    // Sign of each product, then magnitudes compared as unsigned 128-bit values.
    auto sign = [](std::int64_t x, std::int64_t y) {
        if (x == 0 || y == 0)
            return 0;
        return (x < 0) == (y < 0) ? 1 : -1;
    };
    const int sl = sign(a, b), sr = sign(c, d);
    if (sl != sr)
        return sl <=> sr;
    if (sl == 0)
        return std::strong_ordering::equal;
    const U128 l = mul64(magnitude(a), magnitude(b));
    const U128 r = mul64(magnitude(c), magnitude(d));
    if (!less(l, r) && !less(r, l))
        return std::strong_ordering::equal;
    // Larger magnitude is greater for positive products and smaller for negative.
    const bool lBigger = less(r, l);
    return (lBigger == (sl > 0)) ? std::strong_ordering::greater : std::strong_ordering::less;
}

Checked<std::int64_t> mulDivPortable(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d,
                                     Rounding rounding)
{
    if (c == 0 || d == 0)
        return std::unexpected(ArithmeticError::DivisionByZero);
    if (c < 0 || d < 0)
        return std::unexpected(ArithmeticError::InvalidRate);
    const bool negative = (a < 0) != (b < 0) && a != 0 && b != 0;
    const U128 num = mul64(magnitude(a), magnitude(b));
    const U128 den = mul64(magnitude(c), magnitude(d));
    U128 q, r;
    divMod(num, den, q, r);
    return finish(q, r, den, negative, rounding);
}

} // namespace detail

Checked<std::int64_t> checkedAdd(std::int64_t a, std::int64_t b)
{
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    constexpr auto min = std::numeric_limits<std::int64_t>::min();
    if ((b > 0 && a > max - b) || (b < 0 && a < min - b))
        return std::unexpected(ArithmeticError::Overflow);
    return a + b;
}

Checked<std::int64_t> checkedSub(std::int64_t a, std::int64_t b)
{
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    constexpr auto min = std::numeric_limits<std::int64_t>::min();
    if ((b < 0 && a > max + b) || (b > 0 && a < min + b))
        return std::unexpected(ArithmeticError::Overflow);
    return a - b;
}

Checked<std::int64_t> checkedMul(std::int64_t a, std::int64_t b)
{
    return mulDiv(a, b, 1, 1, Rounding::Floor);
}

std::strong_ordering compareProducts(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d)
{
#if defined(__SIZEOF_INT128__)
    return static_cast<__int128>(a) * b <=> static_cast<__int128>(c) * d;
#else
    return detail::compareProductsPortable(a, b, c, d);
#endif
}

Checked<std::int64_t> mulDiv(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d, Rounding rounding)
{
#if defined(__SIZEOF_INT128__)
    if (c == 0 || d == 0)
        return std::unexpected(ArithmeticError::DivisionByZero);
    if (c < 0 || d < 0)
        return std::unexpected(ArithmeticError::InvalidRate);
    // |a*b| < 2^126 and 0 < c*d < 2^126: both fit a signed 128-bit integer.
    const __int128 num = static_cast<__int128>(a) * b;
    const __int128 den = static_cast<__int128>(c) * d;
    __int128 q = num / den;     // truncates toward zero
    const __int128 r = num % den; // same sign as num
    if (r != 0) {
        switch (rounding) {
        case Rounding::Floor:
            if (num < 0)
                --q;
            break;
        case Rounding::Ceil:
            if (num > 0)
                ++q;
            break;
        case Rounding::NearestTiesAway: {
            const __int128 twice = (r < 0 ? -r : r) * 2;
            if (twice >= den)
                q += num < 0 ? -1 : 1;
            break;
        }
        }
    }
    if (q > std::numeric_limits<std::int64_t>::max() || q < std::numeric_limits<std::int64_t>::min())
        return std::unexpected(ArithmeticError::Overflow);
    return static_cast<std::int64_t>(q);
#else
    return detail::mulDivPortable(a, b, c, d, rounding);
#endif
}

} // namespace hikari::core
