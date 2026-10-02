// Painted Grid frame cost over a 50,000-Line model at scattered scroll
// positions (V1-G's first measurement; uncalibrated observations).

#include "perf_harness.h"

#include "line_grid.h"
#include "line_table_model.h"
#include "hikari/core/ass_load.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

using namespace hikari;

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 3)
        return 2;
    std::string s = "[Events]\n";
    char buf[160];
    for (int i = 0; i < 50'000; ++i) {
        std::snprintf(buf, sizeof buf, "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\i1}line %d{\\i0} text\n", i);
        s += buf;
    }
    std::vector<std::byte> bytes(s.size());
    std::memcpy(bytes.data(), s.data(), s.size());
    ui::LineTableModel model;
    model.setDocument(core::loadAss(bytes).document);
    ui::LineGrid grid;
    grid.setSize(QSizeF(1280, 720));
    grid.setModel(&model);
    QImage frame(1280, 720, QImage::Format_ARGB32_Premultiplied);
    double y = 0;
    std::vector<perf::BenchmarkResult> results;
    results.push_back(perf::run({"ui.grid_paint.50k_lines.1280x720", 5, 200, std::chrono::milliseconds(500), [&] {
        y = y > 900'000 ? 0 : y + 4'321;
        grid.setContentY(y);
        QPainter painter(&frame);
        grid.paint(&painter);
    }}));
    const auto host = perf::currentHost();
    const bool calibrated = perf::isCalibrated(host, argv[2]);
    if (!perf::writeReport(argv[1], host, calibrated, results))
        return 1;
    for (const auto &r : results) {
        std::cout << r.name << ": p95 per run (us):";
        for (const auto &st : r.runs)
            std::cout << ' ' << st.p95Us;
        std::cout << "  spread " << r.p95SpreadPercent << "%\n";
    }
    return 0;
}
