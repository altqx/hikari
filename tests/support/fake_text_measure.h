#pragma once

#include "hikari/application/automation.h"

#include <QString>

#include <string>
#include <vector>

namespace hikari::test {

inline void appendFakeExtents(application::HostServiceReply &out, const std::vector<std::string> &style,
                              const std::string &text)
{
    const auto number = [&](std::size_t i) { return i < style.size() ? std::stod(style[i]) : 0.0; };
    const double size = number(2), scaleX = number(11) / 100, scaleY = number(12) / 100, spacing = number(13);
    double ems = 0;
    int chars = 0;
    for (const char32_t c : QString::fromStdString(text).toUcs4()) {
        ems += c >= 0x2E80 ? 1.0 : 0.55;
        ++chars;
    }
    out.numbers.insert(out.numbers.end(), {ems * size * scaleX + spacing * chars, size * scaleY, size * 0.21, 0});
}

inline application::HostServiceReply fakeMeasureReply(const application::HostServiceRequest &r)
{
    using application::HostService;
    application::HostServiceReply out;
    if (r.service == HostService::TextExtents) {
        appendFakeExtents(out, r.style, r.strings.at(0));
    } else if (r.service == HostService::TextExtentsMany) {
        for (const auto &text : r.strings) {
            appendFakeExtents(out, r.style, text);
            out.integers.push_back(1);
        }
    } else {
        return application::HostServiceReply::unavailable();
    }
    return out;
}

}
