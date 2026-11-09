#pragma once

#include "hikari/application/karaoke_layout.h"
#include "hikari/application/resample.h"

extern "C" {
#include <lua.h>
}

#include <optional>
#include <string>
#include <vector>

// karaskel-auto4.lua's tables built in C++: what collect_head and preproc_line give a script, field for field
// and with the same shared tables.
namespace hikari::karaskel_tables {

// aegisub.text_extents for the layout, with the Style canonicalised as a style table would send it.
class HostMeasure final : public application::TextMeasurePort {
public:
    explicit HostMeasure(lua_State *L) : L(L) {}
    std::optional<application::TextExtents> measure(const std::vector<std::string> &style,
                                                    const std::string &text) override;
    std::vector<std::string> failures; // a problem message for each text the host could not measure

private:
    lua_State *L;
};

struct Head {
    std::optional<application::Resolution> video;
};

// karaskel.collect_head(subs, generate_furigana): pushes meta, then styles. With `generateFurigana` the
// "-furigana" Styles go into subs before the first Style, as karaskel inserts them.
Head pushKaraskelHead(lua_State *L, int subs, bool generateFurigana);

// karaskel.preproc_line(subs, meta, styles, line): fills the line table at `line` from `layout` and pushes it.
// `layout` is of lua_helper::karaokeSource's line, which parse_karaoke_data reads.
void pushKaraskelLine(lua_State *L, int styles, int line, const application::karaoke_layout::Line &layout);

// aegisub.__karaskel_impl on the aegisub table at the stack top: collect_head(subs, generate_furigana) and
// preproc_line(subs, meta, styles, line) as karaskel's.
void install(lua_State *L);

}
