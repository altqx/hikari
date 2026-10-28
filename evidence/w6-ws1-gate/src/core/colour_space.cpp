#include "hikari/core/colour_space.h"

#include <cstdint>

// Y7: a line-by-line port of legacy colorspace.cpp (20d647c4). The float and
// unsigned steps are kept as legacy wrote them (a double `1.` included), so
// the results are the legacy ones; tests/core/colour_space_tests.cpp checks
// every input against the legacy source itself.

namespace hikari::core::legacy {

namespace {

// The legacy outputs are unsigned char.
int byte(int value)
{
    return static_cast<unsigned char>(value);
}

// MAX and MIN (config.h:552-556) on the three channels.
float maxOf(float a, float b)
{
    return a > b ? a : b;
}
float minOf(float a, float b)
{
    return a < b ? a : b;
}

bool asciiSpace(char16_t c)
{
    // wxSafeIsspace: below 127 and isspace.
    return c == u' ' || (c >= u'\t' && c <= u'\r');
}

// wxString::ToLong(&value, 16) (wxStringToIntType over wcstol): true only
// when the whole, non-empty text is read.
bool hexToLong(std::u16string_view text, long *value)
{
    std::size_t i = 0;
    while (i < text.size() && asciiSpace(text[i]))
        ++i;
    bool negative = false;
    if (i < text.size() && (text[i] == u'+' || text[i] == u'-'))
        negative = text[i++] == u'-';
    auto digit = [](char16_t c) {
        if (c >= u'0' && c <= u'9')
            return c - u'0';
        if (c >= u'a' && c <= u'f')
            return c - u'a' + 10;
        if (c >= u'A' && c <= u'F')
            return c - u'A' + 10;
        return -1;
    };
    // strtol takes "0x" only before a hex digit; otherwise it reads the 0.
    if (i + 2 < text.size() && text[i] == u'0' && (text[i + 1] == u'x' || text[i + 1] == u'X') &&
        digit(text[i + 2]) >= 0)
        i += 2;
    const std::size_t digits = i;
    long result = 0;
    while (i < text.size() && digit(text[i]) >= 0)
        result = result * 16 + digit(text[i++]);
    if (i == digits || i != text.size())
        return false; // nothing read, or the scan stopped before the end
    *value = negative ? -result : result;
    return true;
}

} // namespace

int clipColourValue(int value)
{
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return value;
}

Channels hslToRgb(int H, int S, int L)
{
    if (S == 0)
        return {byte(L), byte(L), byte(L)};

    if (L == 128 && S == 255) {
        switch (H) {
        case 0:
        case 255:
            return {255, 0, 0};
        case 43:
            return {255, 255, 0};
        case 85:
            return {0, 255, 0};
        case 128:
            return {0, 255, 255};
        case 171:
            return {0, 0, 255};
        case 213:
            return {255, 0, 255};
        }
    }

    float h, s, l, r, g, b;
    h = H / 255.f;
    s = S / 255.f;
    l = L / 255.f;

    float temp2;
    if (l < .5) {
        temp2 = l * (1. + s);
    } else {
        temp2 = l + s - l * s;
    }

    float temp1 = 2.f * l - temp2;

    float temp3[3];
    temp3[0] = h + 1.f / 3.f;
    if (temp3[0] > 1.f)
        temp3[0] -= 1.f;
    temp3[1] = h;
    temp3[2] = h - 1.f / 3.f;
    if (temp3[2] < 0.f)
        temp3[2] += 1.f;

    if (6.f * temp3[0] < 1.f)
        r = temp1 + (temp2 - temp1) * 6.f * temp3[0];
    else if (2.f * temp3[0] < 1.f)
        r = temp2;
    else if (3.f * temp3[0] < 2.f)
        r = temp1 + (temp2 - temp1) * ((2.f / 3.f) - temp3[0]) * 6.f;
    else
        r = temp1;

    if (6.f * temp3[1] < 1.f)
        g = temp1 + (temp2 - temp1) * 6.f * temp3[1];
    else if (2.f * temp3[1] < 1.f)
        g = temp2;
    else if (3.f * temp3[1] < 2.f)
        g = temp1 + (temp2 - temp1) * ((2.f / 3.f) - temp3[1]) * 6.f;
    else
        g = temp1;

    if (6.f * temp3[2] < 1.f)
        b = temp1 + (temp2 - temp1) * 6.f * temp3[2];
    else if (2.f * temp3[2] < 1.f)
        b = temp2;
    else if (3.f * temp3[2] < 2.f)
        b = temp1 + (temp2 - temp1) * ((2.f / 3.f) - temp3[2]) * 6.f;
    else
        b = temp1;

    return {clipColourValue((int)(r * 255)), clipColourValue((int)(g * 255)), clipColourValue((int)(b * 255))};
}

Channels hsvToRgb(int H, int S, int V)
{
    if (S == 255) {
        switch (H) {
        case 0:
        case 255:
            return {byte(V), 0, 0};
        case 43:
            return {byte(V), byte(V), 0};
        case 85:
            return {0, byte(V), 0};
        case 128:
            return {0, byte(V), byte(V)};
        case 171:
            return {0, 0, byte(V)};
        case 213:
            return {byte(V), 0, byte(V)};
        }
    }

    std::uint32_t h = H * 360;
    std::uint32_t s = clipColourValue(S) * 256;
    std::uint32_t v = clipColourValue(V) * 256;

    if (S == 0)
        return {byte(V), byte(V), byte(V)};

    std::uint32_t r, g, b;
    std::uint32_t Hi = h / 60 / 256;
    std::uint32_t f = h / 60 - Hi * 256;
    std::uint32_t p = v * (65535 - s) / 65536;
    std::uint32_t q = v * (65535 - (f * s) / 256) / 65536;
    std::uint32_t t = v * (65535 - ((255 - f) * s) / 256) / 65536;
    switch (Hi) {
    case 0:
        r = v;
        g = t;
        b = p;
        break;
    case 1:
        r = q;
        g = v;
        b = p;
        break;
    case 2:
        r = p;
        g = v;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = v;
        break;
    case 4:
        r = t;
        g = p;
        b = v;
        break;
    case 5:
    default:
        r = v;
        g = p;
        b = q;
        break;
    }

    return {clipColourValue(int(r / 256)), clipColourValue(int(g / 256)), clipColourValue(int(b / 256))};
}

Channels rgbToHsl(int R, int G, int B)
{
    float r = R / 255.f, g = G / 255.f, b = B / 255.f;
    float h, s, l;

    float maxrgb = maxOf(r, maxOf(g, b)), minrgb = minOf(r, minOf(g, b));

    l = (minrgb + maxrgb) / 2;

    if (minrgb == maxrgb) {
        h = 0;
        s = 0;
    } else {
        if (l < 0.5) {
            s = (maxrgb - minrgb) / (maxrgb + minrgb);
        } else {
            s = (maxrgb - minrgb) / (2.f - maxrgb - minrgb);
        }
        if (r == maxrgb) {
            h = (g - b) / (maxrgb - minrgb) + 0;
        } else if (g == maxrgb) {
            h = (b - r) / (maxrgb - minrgb) + 2;
        } else {
            h = (r - g) / (maxrgb - minrgb) + 4;
        }
    }

    if (h < 0)
        h += 6;
    if (h >= 6)
        h -= 6;

    return {clipColourValue(int(h * 256 / 6)), clipColourValue(int(s * 255)), clipColourValue(int(l * 255))};
}

Channels rgbToHsv(int R, int G, int B)
{
    float r = R / 255.f, g = G / 255.f, b = B / 255.f;
    float h, s, v;

    float maxrgb = maxOf(r, maxOf(g, b)), minrgb = minOf(r, minOf(g, b));

    v = maxrgb;

    if (maxrgb < .001f) {
        s = 1;
    } else {
        s = (maxrgb - minrgb) / maxrgb;
    }

    if (minrgb == maxrgb) {
        h = 0;
    } else if (maxrgb == r) {
        h = (g - b) / (maxrgb - minrgb) + 0;
    } else if (maxrgb == g) {
        h = (b - r) / (maxrgb - minrgb) + 2;
    } else {
        h = (r - g) / (maxrgb - minrgb) + 4;
    }

    if (h < 0)
        h += 6;
    if (h >= 6)
        h -= 6;

    return {clipColourValue(int(h * 256 / 6)), clipColourValue(int(s * 255)), clipColourValue(int(v * 255))};
}

Channels hsvToHsl(int iH, int iS, int iV)
{
    int p = iV * (255 - iS);
    const int oH = byte(iH);
    const int oL = byte(clipColourValue((p + iV * 255) / 255 / 2));
    int oS;
    if (oL == 0) {
        oS = byte(iS);
    } else if (oL <= 128) {
        oS = byte(clipColourValue((iV * 255 - p) / (2 * oL)));
    } else {
        oS = byte(clipColourValue((iV * 255 - p) / (511 - 2 * oL)));
    }
    return {oH, oS, oL};
}

Channels hslToHsv(int iH, int iS, int iL)
{
    const int oH = byte(iH);

    if (iS == 0)
        return {oH, 0, byte(iL)};

    if (iL < 128)
        return {oH, byte(2 * 255 * iS / (255 + iS)), byte(iL * (255 + iS) / 255)};
    return {oH, byte(2 * 255 * iS * (255 - iL) / (iL * 255 + iS * 255 - iL * iS)),
            byte((iL * 255 + iS * 255 - iL * iS) / 255)};
}

std::u16string colourToHtml(int r, int g, int b)
{
    static constexpr char16_t hex[] = u"0123456789ABCDEF";
    std::u16string text = u"#";
    for (int c : {r, g, b}) {
        const unsigned value = static_cast<unsigned char>(c);
        text += hex[value >> 4];
        text += hex[value & 15];
    }
    return text;
}

Channels htmlToColour(std::u16string_view html)
{
    while (!html.empty() && asciiSpace(html.back()))
        html.remove_suffix(1);
    while (!html.empty() && asciiSpace(html.front()))
        html.remove_prefix(1);
    if (!html.empty() && html.front() == u'#')
        html.remove_prefix(1);
    long r, g, b;
    if (html.size() == 6) {
        if (hexToLong(html.substr(0, 2), &r) && hexToLong(html.substr(2, 2), &g) && hexToLong(html.substr(4, 2), &b))
            return {byte(int(r)), byte(int(g)), byte(int(b))};
        return {};
    }
    if (html.size() == 3) {
        if (hexToLong(html.substr(0, 1), &r) && hexToLong(html.substr(1, 1), &g) && hexToLong(html.substr(2, 1), &b))
            return {byte(int(r * 16 + r)), byte(int(g * 16 + g)), byte(int(b * 16 + b))};
        return {};
    }
    return {};
}

} // namespace hikari::core::legacy
