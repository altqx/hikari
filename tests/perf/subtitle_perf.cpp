#include "perf_harness.h"

#include "hikari/backends/libass_renderer.h"

#include <QFile>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace hikari;

namespace {

std::vector<std::byte> heavyScript()
{
    std::string s = "[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\nScaledBorderAndShadow: yes\n\n"
                    "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                    "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, "
                    "Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                    "Style: Sign,Titillium Web,64,&H00F0F0F0,&H000000FF,&H00202020,&H80000000,1,0,0,0,100,100,0,0,1,"
                    "4,3,5,40,40,40,1\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, "
                    "Effect, Text\n";
    char buf[512];
    for (int i = 0; i < 24; ++i) {
        const int x = 160 + (i % 6) * 320, y = 140 + (i / 6) * 260;
        std::snprintf(buf, sizeof buf,
                      "Dialogue: 0,0:00:00.00,0:01:00.00,Sign,,0,0,0,,{\\move(%d,%d,%d,%d)\\blur%d\\frz%d\\fad(300,300)"
                      "\\clip(m %d %d l %d %d %d %d %d %d)}Sign %d the heavy one\n"
                      "Dialogue: 1,0:00:00.00,0:01:00.00,Sign,,0,0,0,,{\\pos(%d,%d)\\blur2\\bord2\\p1}m 0 0 b 40 -30 "
                      "120 -30 160 0 b 120 30 40 30 0 0{\\p0}\n",
                      x, y, x + 40, y + 20, 2 + i % 4, (i * 7) % 30 - 15, x - 200, y - 120, x + 200, y - 100, x + 180,
                      y + 120, x - 180, y + 100, i, x, y + 60);
        s += buf;
    }
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

std::shared_ptr<const std::vector<std::byte>> testFont()
{
    QFile f(QStringLiteral(HIKARI_TEST_FONT));
    if (!f.open(QIODevice::ReadOnly))
        std::abort();
    const QByteArray d = f.readAll();
    auto bytes = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(d.size()));
    std::memcpy(bytes->data(), d.constData(), bytes->size());
    return bytes;
}

}

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: hikari_subtitle_perf <report.json> <reference-hosts.json>\n";
        return 2;
    }
    backends::LibassRenderer renderer;
    if (!renderer.prepare({heavyScript(), {{"Titillium Web", testFont()}}, "Titillium Web", false}))
        return 1;
    std::vector<perf::BenchmarkResult> results;
    struct Size {
        const char *name;
        int width, height, videoWidth, videoHeight;
    };
    for (const Size size : {Size{"video_1080p", 1920, 1080, 1920, 1080}, Size{"screen_1080p.video_720p", 1920, 1080, 1280, 720},
                            Size{"screen_4k.video_1080p", 3840, 2160, 1920, 1080}}) {
        std::int64_t us = 0;
        results.push_back(perf::run({std::string("subtitles.heavy.") + size.name, 5, 200, perf::kWarmWarmup, [&] {
            us = (us + 41'708) % 60'000'000;
            const auto frame = renderer.renderScaled(core::DocumentTime(us), size.width, size.height, size.videoWidth,
                                                     size.videoHeight);
            if (!frame || frame->empty)
                std::abort();
        }}));
    }

    const auto host = perf::currentHost();
    const bool calibrated = perf::isCalibrated(host, argv[2]);
    if (!perf::writeReport(argv[1], host, calibrated, results))
        return 1;
    for (const auto &r : results) {
        std::cout << r.name << ": p50/p95/max per run (ms):";
        for (const auto &s : r.runs)
            std::cout << ' ' << s.p50Us / 1000 << '/' << s.p95Us / 1000 << '/' << s.maxUs / 1000;
        std::cout << "  spread " << r.p95SpreadPercent << "%\n";
    }
    std::cout << (calibrated ? "calibrated host\n" : "uncalibrated host: observations only\n");
    return 0;
}
