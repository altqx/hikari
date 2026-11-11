#pragma once

#include "hikari/application/automation.h"
#include "hikari/application/automation_services.h"
#include "hikari/application/extents_cache.h"
#include "hikari/application/resample.h"
#include "hikari/core/document.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Karaoke Line layout as karaskel's preproc_line and the karaoke templater compute it, shared
// by node effects, the native templater and native karaskel. Every number is the one the Lua
// code gives, byte for byte once written with numberText.
namespace hikari::application::karaoke_layout {

// As LuaJIT's tostring writes a number ("%.14g"), in any locale.
std::string numberText(double v);

struct StyleColour {
    int r = 0, g = 0, b = 0;
    friend bool operator==(const StyleColour &, const StyleColour &) = default;
};

struct StyleInfo {
    std::u8string name;
    std::vector<std::string> fields; // positional ASS v4+ fields, Name first
    double fontsize = 0, scaleX = 100, scaleY = 100, spacing = 0, angle = 0, outline = 0, shadow = 0;
    int align = 2, marginL = 0, marginR = 0, marginV = 0;
    StyleColour primary, secondary, outlineColour, back;
};

// A unit's $variables as the templater's set_ctx_syl (or, for the Line, apply_line) sets them.
struct Unit {
    std::string text, raw;
    std::string spaceStripped, prespace, postspace;
    double start = 0, end = 0, duration = 0, kdur = 0, mid = 0;
    double relStart = 0, relEnd = 0;
    double i = 0;
    double left = 0, center = 0, right = 0, width = 0, top = 0, middle = 0, bottom = 0, height = 0, x = 0, y = 0;
    double prespaceWidth = 0, postspaceWidth = 0;
    bool blank = false;
    std::u8string style;
};

enum class CharSplit { Graphemes, CodePoints };

// A char template's characters: grapheme clusters, or code points as unicode.chars reads them.
std::vector<std::string> charsOf(const std::string &text, CharSplit split);

struct LineOptions {
    bool chars = false;
    CharSplit split = CharSplit::Graphemes;
};

struct Highlight {
    double start = 0, end = 0, duration = 0;
};

// A syllable as karaskel's preproc_line leaves it in line.kara, before the templater rounds anything;
// left, center and right are from the Line's left.
struct KaraSyllable {
    std::string text, stripped, spaceStripped, prespace, postspace, tag, inlineFx;
    double start = 0, end = 0, duration = 0, kdur = 0;
    int i = 0;
    std::vector<Highlight> highlights;
    std::vector<int> furi; // into Line::karaFuri
    double width = 0, height = 0, prespaceWidth = 0, postspaceWidth = 0;
    double left = 0, center = 0, right = 0;
};

// A furigana part as line.furi holds it. Its syllable is kara[i]; its one highlight is that syllable's
// highlights[highlight]. Sizes and places are set only when the Line's Style has a "-furigana" Style.
struct KaraFuri {
    std::string text, tag, inlineFx;
    double start = 0, end = 0, duration = 0, kdur = 0;
    int i = 0;
    std::size_t highlight = 0;
    bool isBreak = false, spillback = false;
    double width = 0, height = 0, left = 0, center = 0, right = 0;
};

// The Line's own karaskel fields, unrounded.
struct KaraLine {
    std::string textStripped;
    double duration = 0;
    double width = 0, height = 0, descent = 0, extlead = 0;
    double left = 0, center = 0, right = 0, x = 0, top = 0, middle = 0, bottom = 0, y = 0;
    int effMarginL = 0, effMarginR = 0, effMarginT = 0, effMarginB = 0, effMarginV = 0;
    std::string halign, valign; // empty for an alignment outside 1 to 9
    bool hasFuriStyle = false;
};

struct Line {
    MacroDialogueLine macro;
    const StyleInfo *style = nullptr;
    Unit line;
    std::vector<Unit> syls;               // 0..n, 0 the text before the first karaoke tag
    std::vector<std::vector<Unit>> chars; // per syllable, when LineOptions::chars
    std::vector<Unit> furi;
    KaraLine kara;
    std::vector<KaraSyllable> karaSyllables; // 0..syln
    std::vector<KaraFuri> karaFuri;
    int syln = 0;
    double li = 0;
    std::string signature;
};

class Layout {
public:
    Layout(const core::Document &document, ExtentsCache &extents, std::optional<Resolution> video);
    Layout(const std::vector<MacroInfoLine> &info, const std::vector<MacroStyleLine> &styles, ExtentsCache &extents,
           std::optional<Resolution> video);

    // `li`: the Line's number as the templater's subs count it.
    Line line(const MacroDialogueLine &source, double li, const LineOptions &options,
              std::vector<std::string> &problems);
    // Every Style the templater adds (karaskel's "-furigana" ones), in its order; for $li.
    std::size_t furiganaStylesAdded() const { return m_added; }
    const StyleInfo *style(std::u8string_view name) const;
    std::optional<std::vector<std::u8string>> addedStyle(std::u8string_view name) const;

private:
    struct Extents {
        double width = 0, height = 0, descent = 0, extlead = 0;
    };
    void addStyles(const std::vector<std::pair<std::u8string, std::vector<std::string>>> &styles, Resolution written,
                   std::optional<Resolution> video);
    Extents measure(const StyleInfo &style, const std::string &text, std::vector<std::string> &problems);

    ExtentsCache &m_extents;
    std::map<std::u8string, StyleInfo, std::less<>> m_styles;
    std::map<std::u8string, StyleInfo, std::less<>> m_added_styles;
    const StyleInfo *m_first = nullptr;
    std::size_t m_added = 0;
    double m_resX = 0, m_resY = 0, m_factor = 1;
};

}
