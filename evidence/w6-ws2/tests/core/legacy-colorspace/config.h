#pragma once

// Y7: what legacy colorspace.cpp takes from HikariSub/config.h (20d647c4),
// copied as written (config.h:551-561), so the legacy source compiles here.

#ifndef MIN
#define MIN(a,b) ((a)<(b))?(a):(b)
#endif

#ifndef MAX
#define MAX(a,b) ((a)>(b))?(a):(b)
#endif

#ifndef MID
#define MID(a,b,c) MAX((a),MIN((b),(c)))
#endif
