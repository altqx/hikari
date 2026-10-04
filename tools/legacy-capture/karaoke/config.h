// Stand-in for the legacy config.h: the one option Karaoke::Split reads
// (AUDIO_MERGE_EVERY_N_WITH_SYLLABLE) and the log call it makes when its
// regex does not compile (it always compiles).
#pragma once

#include <wx/string.h>
#include <cstdio>

enum { AUDIO_MERGE_EVERY_N_WITH_SYLLABLE };

struct ProbeOptions {
    bool everyN = false;
    bool GetBool(int) const { return everyN; }
};
extern ProbeOptions Options;

static void HikariLogSilent(const wxString &text)
{
    std::fprintf(stderr, "legacy log: %s\n", static_cast<const char *>(text.utf8_str()));
}

#define MAX(a,b) ((a)>(b))?(a):(b)
#define MIN(a,b) ((a)<(b))?(a):(b)
#define MID(a,b,c) MAX((a),MIN((b),(c)))
