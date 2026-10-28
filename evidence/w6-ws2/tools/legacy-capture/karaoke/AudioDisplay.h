// Stand-in for the legacy AudioDisplay.h: what Karaoke reads of the display
// (karaAuto, the Line, curStartMS/curEndMS, GetXAtMS and the label font's
// text extent). The probe's view is the rewrite's test view: 48 kHz audio
// at zoom 50 (1440 samples, 30 ms a column) from sample 0, with legacy's
// GetXAtMS arithmetic; D3DX's measure is 7 pixels a character
// (GetTextExtentPixel's +4 for a leading or trailing space kept).
#pragma once

#include "KaraokeSplitting.h"

class AudioDisplay {
public:
    bool karaAuto = false;
    Dialogue *dialogue = nullptr;
    int curStartMS = 0, curEndMS = 0;

    float GetXAtMS(long long ms) { return ((ms * 48000 / 1000.0) - 0) / (double)1440; }
    void GetTextExtentPixel(const wxString &text, int *x, int *y)
    {
        *x = int(text.length()) * 7;
        *y = 13;
        if (text.StartsWith(L" "))
            *x += 4;
        if (text.EndsWith(L" "))
            *x += 4;
    }

    // The probe reads Karaoke's private model through its friend.
    static wxArrayString &Syls(Karaoke &k) { return k.syls; }
    static wxArrayString &Tags(Karaoke &k) { return k.ktags; }
    static wxArrayInt &Times(Karaoke &k) { return k.syltimes; }
};
