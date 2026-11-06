#pragma once

#include "hikari/core/export.h"
#include "hikari/core/style.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::core::detail {

using u8sv = std::u8string_view;
using StyleMap = std::map<std::u8string, StyleValues, std::less<>>;

constexpr std::uint32_t bit(ExportLoss loss)
{
    return static_cast<std::uint32_t>(loss);
}

inline constexpr std::uint32_t kWhite = 0xFFFFFF;

struct Look {
    bool italic = false, bold = false, underline = false, strike = false;
    std::uint32_t colour = kWhite; // 0xRRGGBB
    bool operator==(const Look &) const = default;
};

struct Piece {
    std::u8string text;
    Look look;
    std::int64_t syllableUs = -1; // a karaoke syllable starts here, from the Line's start
};

using Row = std::vector<Piece>;

// A Line as the export formats see it: rows of styled text, placement and what no format carries.
struct Flat {
    const LineRecord *line = nullptr;
    std::size_t order = 0;
    std::int64_t startUs = 0, endUs = 0;
    std::vector<Row> rows;
    const StyleValues *style = nullptr;
    int alignment = 2;
    std::optional<std::pair<double, double>> pos;
    int marginL = 0, marginR = 0, marginV = 0;
    std::uint32_t dropped = 0; // no format carries these
    bool colourTag = false, formattingTag = false, strikeTag = false, karaoke = false;
};

class Flattener {
public:
    Flattener(const StyleMap &styles, bool softBreaks) : m_styles(styles), m_softBreaks(softBreaks) {}
    Flat flatten(const LineRecord &line, std::size_t order);

private:
    const StyleValues *styleNamed(u8sv name) const;
    void read(u8sv text);
    void put(std::u8string text);
    void block(u8sv block);
    void tag(u8sv token);
    std::vector<Row> finishRows();

    const StyleMap &m_styles;
    bool m_softBreaks = false;
    Flat *m_flat = nullptr;
    Look m_base, m_look;
    bool m_drawing = false;
    std::int64_t m_karaokeUs = 0;
    std::int64_t m_syllable = -1;
    bool m_alignmentSet = false, m_posSet = false;
    std::vector<Row> m_rows;
};

struct PlayRes {
    double x = 384, y = 288;
};

std::u8string ascii(std::string_view s);
// Like atoi: spaces, a sign, digits; 0 without digits.
std::int64_t leadingInt(u8sv s);
double leadingDouble(u8sv s);
std::uint32_t rgbOf(const Colour &c);
Look lookOf(const StyleValues *style);
PlayRes playRes(const Document &document);

bool positioned(const Flat &f);
bool uniform(const Row &row, bool Look::*property);
bool uniformColour(const Row &row);

std::int64_t roundedDiv(std::int64_t value, std::int64_t by);
std::u8string padded(std::int64_t v, int width);
// Hours with `hourDigits` digits, then minutes, seconds and milliseconds.
std::u8string clock(std::int64_t us, int hourDigits, char8_t fraction);
// Two decimals at most, without trailing zeros.
std::u8string decimal(double v);
std::u8string hex(std::uint32_t rgb, bool upper);
// Letters, digits, '-' and '_' (a CSS class name and an XML NCName), never starting with a digit or '-'.
std::u8string identifierFor(u8sv name);

std::u8string plainRow(const Row &row);
std::u8string joinedRows(const Flat &f, u8sv separator, const std::function<std::u8string(const Row &)> &text);

std::uint32_t ttmlLosses(const ExportOptions &o, const Flat &f);
// IMSC1 text profile when `imsc`, else DFXP. Adds Position to `losses` where the placement is approximated.
// YTSubConverter 1.6.5's conversion of the Lines as the video renderer sees them, without attachments.
ExportResult exportSrv3(const Document &document, const ExportOptions &options);

void writeTtml(std::u8string &out, const std::vector<Flat> &cues, const ExportOptions &o, PlayRes res, bool imsc,
               std::map<std::uint64_t, std::uint32_t> &losses);

}
