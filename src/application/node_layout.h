#pragma once

#include "hikari/application/automation.h"
#include "hikari/application/automation_services.h"
#include "hikari/application/node_effects.h"
#include "hikari/application/resample.h"
#include "hikari/core/document.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application::node_layout {

// As LuaJIT's tostring writes a number ("%.14g"), in any locale.
std::string numberText(double v);

struct StyleInfo {
    std::u8string name;
    std::vector<std::string> fields;
    double fontsize = 0, scaleX = 100, scaleY = 100, spacing = 0, angle = 0, outline = 0, shadow = 0;
    int align = 2, marginL = 0, marginR = 0, marginV = 0;
    NodeColour primary, secondary, outlineColour, back;
};

// A unit's $variables as the templater's set_ctx_syl (or, for the Line, apply_line) sets them.
struct Unit {
    std::string text, raw;
    double start = 0, end = 0, duration = 0, kdur = 0, mid = 0;
    double relStart = 0, relEnd = 0;
    double i = 0;
    double left = 0, center = 0, right = 0, width = 0, top = 0, middle = 0, bottom = 0, height = 0, x = 0, y = 0;
    bool blank = false;
    std::u8string style;
};

struct Line {
    const core::LineRecord *source = nullptr;
    MacroDialogueLine macro;
    const StyleInfo *style = nullptr;
    Unit line;
    std::vector<Unit> syls;               // 0..n, 0 the text before the first karaoke tag
    std::vector<std::vector<Unit>> chars;
    std::vector<Unit> furi;
    int syln = 0;
    double li = 0;
    std::string signature;
};

class Layout {
public:
    Layout(const core::Document &document, TextMeasurePort &measure, std::optional<Resolution> video);
    // `li`: the Line's number as the templater's subs count it.
    Line line(const core::LineRecord &source, double li, bool chars, std::vector<std::string> &problems);
    // Every Style the templater adds (karaskel's "-furigana" ones), in its order; for $li.
    std::size_t furiganaStylesAdded() const { return m_added; }
    const StyleInfo *style(std::u8string_view name) const;
    std::optional<std::vector<std::u8string>> addedStyle(std::u8string_view name) const;

private:
    struct Extents {
        double width = 0, height = 0;
    };
    Extents measure(const StyleInfo &style, const std::string &text, std::vector<std::string> &problems);

    TextMeasurePort &m_measure;
    std::map<std::u8string, StyleInfo, std::less<>> m_styles;
    std::map<std::u8string, StyleInfo, std::less<>> m_added_styles;
    const StyleInfo *m_first = nullptr;
    std::size_t m_added = 0;
    double m_resX = 0, m_resY = 0, m_factor = 1;
    std::map<std::pair<std::string, std::string>, Extents> m_extents;
};

}
