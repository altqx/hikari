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

struct Head {
    std::optional<application::Resolution> video;
    int firstStyle = 0;                      // the subs index the added "-furigana" Styles start at
    std::vector<std::string> furiganaAdded; // those Styles' names, in subs order
};

// karaskel.collect_head(subs, generate_furigana): pushes meta, then styles. With `generateFurigana` the
// "-furigana" Styles go into subs before the first Style, as karaskel inserts them. `rows`, when set, is a list
// of subs' rows read already; `cancelCheck`, when set, is called in place of the cancel check.
Head pushKaraskelHead(lua_State *L, int subs, bool generateFurigana, int rows = 0, int cancelCheck = 0);

// Takes `head`'s added "-furigana" Styles out of the styles table at `styles`; subs keeps them.
void dropAddedFurigana(lua_State *L, int styles, const Head &head);

// karaskel.preproc_line(subs, meta, styles, line): fills the line table at `line` from `layout` and pushes it.
// `layout` is of lua_helper::karaokeSource's line, which parse_karaoke_data reads.
void pushKaraskelLine(lua_State *L, int styles, int line, const application::karaoke_layout::Line &layout);

// The staged Lines' layout. It measures through the run's extents, with each Style as aegisub.text_extents
// sends it, so the templater, karaskel and scripts share one memo.
application::karaoke_layout::Layout stagedLayout(std::optional<application::Resolution> video);

// layout.line() for a dialogue line; a host that could not answer raises aegisub.text_extents' error.
application::karaoke_layout::Line layLine(lua_State *L, application::karaoke_layout::Layout &layout,
                                          const application::MacroDialogueLine &dialogue, double li,
                                          std::vector<std::string> &problems);

// aegisub.__karaskel_impl on the aegisub table at the stack top: collect_head(subs, generate_furigana) and
// preproc_line(subs, meta, styles, line) as karaskel's.
void install(lua_State *L);

}
