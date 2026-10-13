// Runs legacy Demux.cpp (copied unchanged; demux/standins.h stands in for the
// grid, the chooser, the progress dialog and the message box) on Matroska
// files and prints one JSON object per case. FFMS2 is the legacy Linux
// build's: the distribution's libffms2 with the legacy extension
// (Thirdparty/Build/FFMS2/indexing_additional.cpp) compiled against the
// ffms2 fork's headers, as the legacy root CMakeLists links it. The packet
// callback's arguments are recorded on their way in: the probe is linked
// with --wrap=FFMS_GetSubtitles, so Demux's call reaches
// __wrap_FFMS_GetSubtitles, which hands the real one a callback that records
// (Start, Duration, Total, Line) and then calls Demux's.
// Each Style and Line Demux builds is also read by legacy's own
// Styles::parseStyle and Dialogue::SetRaw (demux/legacy_parse.h), and the
// op carries their GetRaw as "parsed".
//
// Usage: legacy_demux_capture <fixtures dir> < cases
// The probe works in <fixtures dir>, so the files are named relative to it
// (FFMS2's error text names the file as it was given).
//
// Case lines (fields separated by spaces):
//   subs <name> <file> [choose <index>|choose cancel] [cancel <packet>]
//       SubsGrid::OnMkvSubs' part: Demux::Open, then Demux::GetSubtitles on
//       a grid. choose answers the track chooser (default 0); cancel
//       presses the progress dialog's Cancel during that packet (from 1).
//   fonts <name> <file>
//       FontCollector::CopyMKVFonts' part: Demux::Open, GetFontList, then
//       SaveFont of every font to a scratch folder.
// '#' starts a comment line.
#include "Demux.h"
#include "standins.h"

#include <wx/init.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

// LogHandler.h's HikariLog writes through LogHandler::Get(); a handler that
// is never constructed (LogMessage touches none of its members) makes every
// call reach the record.
alignas(LogHandler) static unsigned char logStorage[sizeof(LogHandler)];
LogHandler *LogHandler::sthis = reinterpret_cast<LogHandler *>(logStorage);
void LogHandler::LogMessage(const wxString &format, bool)
{
    probe::record("log", format);
}

namespace probe {
std::vector<Op> ops;
int choice = 0;
int cancelAfter = 0;
int progressCalls = 0;
void record(const char *kind, const wxString &text, int number, const wxString &parsed)
{
    const auto utf8 = text.utf8_str();
    const auto parsedUtf8 = parsed.utf8_str();
    ops.push_back({kind, std::string(utf8.data(), utf8.length()), number,
                   std::string(parsedUtf8.data(), parsedUtf8.length())});
}
} // namespace probe

// SubsFile::GetSInfo, GetSInfoP and AddSInfo, unchanged.
#include "subs_file.inc"

namespace {

struct Packet {
    std::int64_t start, duration, total;
    std::string line;
};
std::vector<Packet> packets;
GetSubtitlesCallback demuxCallback = nullptr;

int FFMS_CC recordPacket(int64_t Start, int64_t Duration, int64_t Total, const char *Line, void *ICPrivate)
{
    packets.push_back({Start, Duration, Total, Line ? std::string(Line) : std::string()});
    return demuxCallback(Start, Duration, Total, Line, ICPrivate);
}

} // namespace

extern "C" {
void __real_FFMS_GetSubtitles(FFMS_Indexer *Indexer, int Track, GetSubtitlesCallback IC, void *ICPrivate);
void __wrap_FFMS_GetSubtitles(FFMS_Indexer *Indexer, int Track, GetSubtitlesCallback IC, void *ICPrivate)
{
    probe::record("read-track", emptyString, Track);
    demuxCallback = IC;
    __real_FFMS_GetSubtitles(Indexer, Track, &recordPacket, ICPrivate);
}
}

namespace {

bool validUtf8(const std::string &s)
{
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const std::size_t n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : 4;
        if (n == 4 || i + n >= s.size())
            return false;
        for (std::size_t k = 1; k <= n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) >> 6) != 2)
                return false;
        i += n + 1;
    }
    return true;
}

std::string json(const std::string &s)
{
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += char(c);
            }
        }
    }
    return out + "\"";
}

// Bytes that are not UTF-8 are given as hex.
std::string bytesField(const char *key, const std::string &s)
{
    if (validUtf8(s))
        return "\"" + std::string(key) + "\":" + json(s);
    std::string hex;
    for (unsigned char c : s) {
        char buf[3];
        std::snprintf(buf, sizeof buf, "%02x", c);
        hex += buf;
    }
    return "\"" + std::string(key) + "_hex\":\"" + hex + "\"";
}

std::string str(const wxString &s)
{
    const auto utf8 = s.utf8_str();
    return std::string(utf8.data(), utf8.length());
}

// FNV-1a, 64 bits: the saved font's bytes.
std::string fnv1a(const std::string &bytes)
{
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

std::string opsJson()
{
    std::string out = "[";
    bool first = true;
    for (const auto &op : probe::ops) {
        out += first ? "" : ",";
        first = false;
        out += "{\"op\":" + json(op.kind);
        if (!op.text.empty())
            out += "," + bytesField("text", op.text);
        if (op.number || op.kind == "progress" || op.kind == "read-track")
            out += ",\"n\":" + std::to_string(op.number);
        if (!op.parsed.empty())
            out += "," + bytesField("parsed", op.parsed);
        out += "}";
    }
    return out + "]";
}

void reset()
{
    probe::ops.clear();
    probe::choice = 0;
    probe::cancelAfter = 0;
    probe::progressCalls = 0;
    packets.clear();
}

void subs(const std::string &name, const std::string &file, std::istringstream &rest)
{
    reset();
    std::string word;
    while (rest >> word) {
        std::string value;
        rest >> value;
        if (word == "choose")
            probe::choice = value == "cancel" ? -1 : std::stoi(value);
        else if (word == "cancel")
            probe::cancelAfter = std::stoi(value);
    }
    SubsFile subsFile;
    SubsGrid grid;
    grid.file = &subsFile;
    bool opened = false, result = false;
    {
        Demux dmx;
        opened = dmx.Open(wxString::FromUTF8(file.c_str()));
        if (opened) {
            result = dmx.GetSubtitles(&grid);
            dmx.Close();
        }
    }
    std::string out = "{\"case\":" + json(name) + ",\"kind\":\"subs\",\"file\":" + json(file) +
                      ",\"open\":" + (opened ? "true" : "false") + ",\"result\":" + (result ? "true" : "false") +
                      ",\"originalFormat\":" + std::to_string(grid.originalFormat) + ",\"packets\":[";
    for (std::size_t i = 0; i < packets.size(); ++i) {
        const auto &p = packets[i];
        out += (i ? "," : "") + std::string("{\"start\":") + std::to_string(p.start) +
               ",\"duration\":" + std::to_string(p.duration) + ",\"total\":" + std::to_string(p.total) + "," +
               bytesField("line", p.line) + "}";
    }
    out += "],\"ops\":" + opsJson() + ",\"sinfo\":[";
    for (std::size_t i = 0; i < subsFile.subs->sinfo.size(); ++i) {
        const SInfo *info = subsFile.subs->sinfo[i];
        out += (i ? "," : "") + std::string("[") + json(str(info->Name)) + "," + json(str(info->Val)) + "]";
    }
    std::cout << out << "]}" << std::endl;
}

void fonts(const std::string &name, const std::string &file)
{
    reset();
    const auto dir = std::filesystem::temp_directory_path() / ("legacy-demux-fonts-" + std::to_string(getpid()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string out = "{\"case\":" + json(name) + ",\"kind\":\"fonts\",\"file\":" + json(file) + ",\"open\":";
    Demux dmx;
    const bool opened = dmx.Open(wxString::FromUTF8(file.c_str()));
    out += opened ? "true" : "false";
    out += ",\"fonts\":[";
    if (opened) {
        wxArrayString list;
        dmx.GetFontList(&list);
        for (std::size_t i = 0; i < list.size(); ++i) {
            const wxString path = wxString::FromUTF8(dir.string().c_str()) + L"/" + list[i];
            const bool saved = dmx.SaveFont(int(i), path);
            std::ifstream in(std::string(path.utf8_str()), std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            out += (i ? "," : "") + std::string("{") + bytesField("name", str(list[i])) +
                   ",\"saved\":" + (saved ? "true" : "false") + ",\"size\":" + std::to_string(bytes.size()) +
                   ",\"fnv1a64\":\"" + fnv1a(bytes) + "\"}";
        }
        dmx.Close();
    }
    out += "],\"ops\":" + opsJson() + "}";
    std::filesystem::remove_all(dir);
    std::cout << out << std::endl;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2 || chdir(argv[1]) != 0) {
        std::cerr << "usage: legacy_demux_capture <fixtures dir> < cases\n";
        return 2;
    }
    wxInitializer init;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream in(line);
        std::string kind, name, file;
        in >> kind >> name >> file;
        if (kind == "subs")
            subs(name, file, in);
        else if (kind == "fonts")
            fonts(name, file);
        else {
            std::cerr << "unknown case line: " << line << "\n";
            return 2;
        }
    }
    return 0;
}
