// Runs the legacy Karaoke class (HikariSub/KaraokeSplitting.cpp, compiled as
// it is against the stand-in headers in karaoke/) on the cases in
// inputs/karaoke-cases.txt and prints one JSON object per case: the syllables,
// tags and times after Split, the text GetText gives, each syllable's stripped
// text, and the same after every operation of the case.
//
// Case file lines (UTF-8):
//   case <name>
//   text <the Line's text, to the end of the line>
//   tl <the Line's translation>          (optional)
//   times <start ms> <end ms>
//   auto 0|1                             (karaAuto, AUDIO_KARAOKE_SPLIT_MODE)
//   everyn 0|1                           (AUDIO_MERGE_EVERY_N_WITH_SYLLABLE)
//   join <syllable>                      (Karaoke::Join)
//   splitsyl <syllable> <letters>        (Karaoke::SplitSyl)
//   settime <syllable> <ms>              (a boundary dragged: syltimes[i] = ms)
//   curstart <ms>                        (the display's curStartMS)
//   letters <syllable> <letters>         (Karaoke::GetLetters, nothing changed)
//   sylat <x> / over <x> / letterat <x>  (GetSylAtX, CheckIfOver, GetLetterAtX)
//   end
// The first operation (or the end) runs Split first.
#include "KaraokeSplitting.h"
#include "AudioDisplay.h"
#include "config.h"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

ProbeOptions Options;

namespace {

std::string json(const wxString &s)
{
    const std::string utf8(s.utf8_str());
    std::string out = "\"";
    for (const unsigned char c : utf8) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                out += buffer;
            } else {
                out += char(c);
            }
        }
    }
    return out + "\"";
}

std::string state(Karaoke &k, AudioDisplay &ad)
{
    std::string out = "{\"syls\":[";
    for (size_t i = 0; i < AudioDisplay::Syls(k).size(); i++)
        out += (i ? "," : "") + json(AudioDisplay::Syls(k)[i]);
    out += "],\"tags\":[";
    for (size_t i = 0; i < AudioDisplay::Tags(k).size(); i++)
        out += (i ? "," : "") + json(AudioDisplay::Tags(k)[i]);
    out += "],\"times\":[";
    for (size_t i = 0; i < AudioDisplay::Times(k).size(); i++)
        out += (i ? "," : "") + std::to_string(AudioDisplay::Times(k)[i]);
    out += "],\"stripped\":[";
    for (size_t i = 0; i < AudioDisplay::Syls(k).size(); i++) {
        wxString stripped;
        k.GetTextStripped(int(i), stripped);
        out += (i ? "," : "") + json(stripped);
    }
    out += "]";
    // GetText reads a time per syllable; with fewer times it would read past them
    if (AudioDisplay::Times(k).size() >= AudioDisplay::Syls(k).size())
        out += ",\"text\":" + json(k.GetText());
    (void)ad;
    return out + "}";
}

} // namespace

int main()
{
    std::string line;
    Dialogue dial;
    AudioDisplay ad;
    ad.dialogue = &dial;
    Karaoke *k = nullptr;
    std::string name, ops;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto space = line.find(' ');
        const std::string op = line.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string() : line.substr(space + 1);
        std::istringstream in(rest);
        if (op == "case") {
            name = rest;
            dial = Dialogue();
            ad.karaAuto = false;
            Options.everyN = false;
            ops.clear();
            delete k;
            k = nullptr;
        } else if (op == "text") {
            dial.Text = wxString::FromUTF8(rest);
        } else if (op == "tl") {
            dial.TextTl = wxString::FromUTF8(rest);
        } else if (op == "times") {
            in >> dial.Start.mstime >> dial.End.mstime;
            ad.curStartMS = dial.Start.mstime;
            ad.curEndMS = dial.End.mstime;
        } else if (op == "auto") {
            int v = 0;
            in >> v;
            ad.karaAuto = v != 0;
        } else if (op == "everyn") {
            int v = 0;
            in >> v;
            Options.everyN = v != 0;
        } else if (op == "end") {
            if (!k) {
                k = new Karaoke(&ad);
                k->Split();
                ops += "{\"op\":\"split\",\"state\":" + state(*k, ad) + "}";
            }
            std::printf("{\"case\":%s,\"ops\":[%s]}\n", json(wxString::FromUTF8(name)).c_str(), ops.c_str());
            std::fflush(stdout);
        } else {
            if (!k) {
                k = new Karaoke(&ad);
                k->Split();
                ops += "{\"op\":\"split\",\"state\":" + state(*k, ad) + "}";
            }
            std::string result;
            if (op == "join") {
                int i = 0;
                in >> i;
                k->Join(i);
            } else if (op == "splitsyl") {
                int i = 0, n = 0;
                in >> i >> n;
                result = k->SplitSyl(i, n) ? "true" : "false";
            } else if (op == "settime") {
                int i = 0, ms = 0;
                in >> i >> ms;
                AudioDisplay::Times(*k)[i] = ms;
            } else if (op == "curstart") {
                in >> ad.curStartMS;
            } else if (op == "letters") {
                int i = 0, n = 0;
                in >> i >> n;
                wxString first, second;
                k->GetLetters(i, n, first, second);
                result = "[" + json(first) + "," + json(second) + "]";
            } else if (op == "sylat" || op == "over" || op == "letterat") {
                int x = 0, r = -1, syl = -1;
                in >> x;
                bool found = false;
                if (op == "sylat")
                    found = k->GetSylAtX(x, &r);
                else if (op == "over")
                    found = k->CheckIfOver(x, &r);
                else
                    found = k->GetLetterAtX(x, &syl, &r);
                result = "[" + std::string(found ? "true" : "false") + "," + std::to_string(syl) + "," +
                         std::to_string(r) + "]";
            } else {
                std::fprintf(stderr, "unknown op %s\n", op.c_str());
                return 2;
            }
            ops += ",{\"op\":" + json(wxString::FromUTF8(line)) + (result.empty() ? "" : ",\"result\":" + result) +
                   ",\"state\":" + state(*k, ad) + "}";
        }
    }
    delete k;
    return 0;
}
