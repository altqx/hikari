#include "hikari/application/keyframe_files.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace hikari::application {

std::vector<int> parseKeyframes(std::string_view text)
{
    // wxStringTokenizer(text, "\n", wxTOKEN_STRTOK); legacy reads the file in text mode (CRLF as LF).
    std::vector<std::string> lines;
    for (std::size_t p = 0; p < text.size();) {
        const auto nl = std::min(text.find('\n', p), text.size());
        std::string line(text.substr(p, nl - p));
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            lines.push_back(std::move(line));
        p = nl + 1;
    }
    std::vector<int> out;
    if (lines.empty())
        return out;
    const std::string &header = lines.front();
    enum { None, Aegisub, Xvid, Divx, X264 } type = None;
    if (header == "# keyframe format v1")
        type = Aegisub;
    else if (header.starts_with("# XviD 2pass stat file") || header.starts_with("# ffmpeg 2-pass log file, using xvid codec") ||
             header.starts_with("# avconv 2-pass log file, using xvid codec"))
        type = Xvid;
    else if (header.starts_with("##map version"))
        type = Divx;
    else if (header.starts_with("#options:"))
        type = X264;
    if (type == None)
        return out;
    if (type == Aegisub) {
        for (std::size_t i = 1; i < lines.size(); ++i)
            out.push_back(std::atoi(lines[i].c_str()));
        return out;
    }
    int frame = 0;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string &token = lines[i];
        char frameType = '#';
        if (type == Xvid) {
            frameType = token[0];
        } else if (type == Divx) {
            for (int k = 0; k < 3; ++k) {
                const auto at = token.find("IPB");
                if (at != std::string::npos) {
                    frameType = static_cast<char>(std::tolower(static_cast<unsigned char>(token[at])));
                    break;
                }
            }
        } else {
            const auto at = token.find("type:");
            if (at != std::string::npos && at + 5 < token.size())
                frameType = static_cast<char>(std::tolower(static_cast<unsigned char>(token[at + 5])));
        }
        if (frameType == 'i')
            out.push_back(frame++);
        else if (frameType == 'p' || frameType == 'b')
            ++frame;
    }
    return out;
}

} // namespace hikari::application
