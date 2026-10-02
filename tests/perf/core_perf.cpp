// First core workloads for the performance harness: loading a generated
// 50,000-Line ASS script (the contract's G fixture size) and frame lookups.
// Observations on an uncalibrated host, not budget passes.

#include "perf_harness.h"

#include "hikari/core/ass_load.h"
#include "hikari/core/frame_timeline.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace hikari;

namespace {

std::vector<std::byte> generateScript(int lines)
{
    std::string s = "[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    char buf[256];
    for (int i = 0; i < lines; ++i) {
        const int ms = i * 1000;
        std::snprintf(buf, sizeof buf,
                      "Dialogue: 0,%d:%02d:%02d.%02d,%d:%02d:%02d.%02d,Default,,0,0,0,,{\\i1}Line %d{\\i0} with some "
                      "text\n",
                      ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, 0, (ms + 900) / 3600000, (ms + 900) / 60000 % 60,
                      (ms + 900) / 1000 % 60, 90, i);
        s += buf;
    }
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: hikari_core_perf <report.json> <reference-hosts.json>\n";
        return 2;
    }
    const auto script = generateScript(50'000);
    std::vector<perf::BenchmarkResult> results;

    results.push_back(perf::run({"core.ass_load.50k_lines", 5, 200, perf::kWarmWarmup, [&] {
        const auto r = core::loadAss(script);
        if (r.document.lines().size() != 50'000)
            std::abort();
    }}));

    const auto timeline = core::FrameTimeline::constantRate(*core::FrameRate::make(24000, 1001), core::DocumentTime(0));
    std::int64_t t = 0;
    // One lookup is near the clock's resolution, so one timed operation is a
    // batch of 1,000 lookups.
    results.push_back(perf::run({"core.frame_lookup.ntsc.x1000", 5, 200, perf::kWarmWarmup, [&] {
        for (int i = 0; i < 1'000; ++i) {
            t = (t + 41'711) % 3'600'000'000;
            if (!timeline.frameAtOrAfter(core::DocumentTime(t)))
                std::abort();
        }
    }}));

    const auto host = perf::currentHost();
    const bool calibrated = perf::isCalibrated(host, argv[2]);
    if (!perf::writeReport(argv[1], host, calibrated, results))
        return 1;
    for (const auto &r : results) {
        std::cout << r.name << ": p95 per run (us):";
        for (const auto &s : r.runs)
            std::cout << ' ' << s.p95Us;
        std::cout << "  spread " << r.p95SpreadPercent << "%\n";
    }
    std::cout << (calibrated ? "calibrated host\n" : "uncalibrated host: observations only\n");
    return 0;
}
