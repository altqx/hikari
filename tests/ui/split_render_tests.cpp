#include "automation_services_qt.h"
#include "hikari/application/grid_split.h"
#include "hikari/backends/libass_renderer.h"
#include "hikari/core/ass_load.h"

#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QtTest>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

constexpr int kWidth = 320, kHeight = 120;

std::string header()
{
    return "[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 120\nScaledBorderAndShadow: yes\n\n"
           "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
           "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, "
           "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
           "Style: Default,Titillium Web,32,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,2,"
           "10,10,10,1\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
}

std::vector<std::byte> bytesOf(const std::string &s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

std::string time(core::DocumentTime t)
{
    const auto cs = t.microseconds() / 10'000;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%02d:%02d.%02d", int(cs / 360'000), int(cs / 6000 % 60), int(cs / 100 % 60),
                  int(cs % 100));
    return buf;
}

std::string events(const core::Document &d)
{
    std::string out;
    for (const auto *l : d.lines())
        out += "Dialogue: 0," + time(l->start.value) + "," + time(l->end.value) + ",Default,,0,0,0,," +
               std::string(l->text.begin(), l->text.end()) + "\n";
    return out;
}

std::shared_ptr<const std::vector<std::byte>> font()
{
    QFile f(QStringLiteral(HIKARI_TEST_FONT));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray d = f.readAll();
    auto bytes = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(d.size()));
    std::memcpy(bytes->data(), d.constData(), bytes->size());
    return bytes;
}

std::vector<std::uint8_t> render(const std::string &script, std::int64_t ms)
{
    backends::LibassRenderer renderer;
    if (!renderer.prepare(RenderSnapshot{bytesOf(script), {FontLease{"Titillium Web", font()}}, "Titillium Web", false}))
        return {};
    const auto frame = renderer.render(core::DocumentTime(ms * 1000), kWidth, kHeight);
    if (!frame)
        return {};
    std::vector<std::uint8_t> alpha;
    for (int y = 0; y < frame->height; ++y)
        for (int x = 0; x < frame->width; ++x)
            alpha.push_back(frame->pixels[static_cast<std::size_t>(y) * frame->stride + x * 4 + 3]);
    return alpha;
}

struct Difference {
    int ink = 0;
    int differing = 0;
};

Difference compare(const std::vector<std::uint8_t> &a, const std::vector<std::uint8_t> &b, int slack = 0)
{
    Difference d;
    if (a.size() != b.size())
        return {1, 1};
    const auto near = [&](const std::vector<std::uint8_t> &from, const std::vector<std::uint8_t> &to, int x, int y) {
        const int value = from[std::size_t(y * kWidth + x)];
        for (int dy = -slack; dy <= slack; ++dy)
            for (int dx = -slack; dx <= slack; ++dx) {
                const int nx = x + dx, ny = y + dy;
                if (nx >= 0 && ny >= 0 && nx < kWidth && ny < kHeight &&
                    std::abs(value - int(to[std::size_t(ny * kWidth + nx)])) <= 96)
                    return true;
            }
        return false;
    };
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) {
            const std::size_t i = std::size_t(y * kWidth + x);
            d.ink += a[i] > 0;
            d.differing += !near(a, b, x, y) || !near(b, a, x, y);
        }
    return d;
}

EditSession sessionOf(const std::string &dialogue)
{
    EditSession session{core::loadAss(bytesOf(header() + dialogue)).document};
    std::set<core::LineId> all;
    for (const auto *l : session.document().lines())
        all.insert(l->id);
    session.setSelection(Selection{*all.begin(), all, *all.begin(), {}});
    return session;
}

std::vector<std::pair<std::size_t, bool>> noWords(std::u16string_view text)
{
    return {{text.size(), true}};
}

}

class SplitRenderTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QVERIFY(QFontDatabase::addApplicationFont(QStringLiteral(HIKARI_TEST_FONT)) >= 0); }

    void tagBlockPiecesRenderWhereTheLineDid_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("colours") << QStringLiteral("Hello {\\c&H0000FF&}split{\\c&H00FF00&} world");
        QTest::newRow("border") << QStringLiteral("{\\bord2}one {\\3c&H0000FF&\\bord3}two{\\r} three");
        QTest::newRow("rotated") << QStringLiteral("{\\frz12}tilted {\\c&H00FFFF&}line");
        QTest::newRow("top left") << QStringLiteral("{\\an7\\pos(20,20)}left {\\c&HFF0000&}aligned");
    }
    void tagBlockPiecesRenderWhereTheLineDid()
    {
        QFETCH(QString, text);
        const std::string dialogue = "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,," + text.toStdString() + "\n";
        auto session = sessionOf(dialogue);
        ui::GdiFontLeases leases;
        leases.hold({FontLease{"Titillium Web", font()}});
        ui::QtTextMeasurePort measure;
        QVERIFY(splitByText(session, SplitText::TagBlocks, measure, noWords));
        QVERIFY(session.document().lines().size() > 1);
        const auto before = render(header() + dialogue, 1000);
        const auto after = render(header() + events(session.document()), 1000);
        QVERIFY(!before.empty() && !after.empty());
        const auto d = compare(before, after, 1);
        QVERIFY(d.ink > 200);
        QVERIFY2(d.differing * 100 <= d.ink * 3,
                 qPrintable(QStringLiteral("%1 of %2 ink pixels differ: %3")
                                .arg(d.differing)
                                .arg(d.ink)
                                .arg(QString::fromStdString(events(session.document())))));
    }

    void aLeasedFontIsMeasuredInItsOwnFaceOnlyWhileHeld()
    {
#ifndef Q_OS_WIN
        QSKIP("Only GDI, on Windows, measures without the lease");
#else
        const std::vector<std::string> style{"Default", "Titillium Web", "32", "&H00FFFFFF", "&H000000FF", "&H00000000",
                                             "&H00000000", "0", "0", "0", "0", "100", "100", "0", "0", "1", "0", "0",
                                             "2", "10", "10", "10", "1"};
        ui::QtTextMeasurePort measure;
        const auto width = [&] { return measure.measure(style, "Hello split world").value_or(TextExtents{}).width; };
        const double fallback = width();
        ui::GdiFontLeases leases;
        leases.hold({FontLease{"Titillium Web", font()}});
        const double own = width();
        QVERIFY2(own > 0 && own != fallback, qPrintable(QStringLiteral("%1 vs %2").arg(own).arg(fallback)));
        leases.hold({});
        QCOMPARE(width(), fallback);
#endif
    }

    void timedPiecesRenderAsTheLineDid()
    {
        const std::string dialogue = "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,"
                                     "{\\move(40,60,280,90,500,4500)\\t(1000,3000,\\frz30\\fscx150)\\fad(400,600)}Moving\n";
        auto session = sessionOf(dialogue);
        QVERIFY(splitEvery(session, {}, 1, SplitUnit::Seconds));
        QCOMPARE(session.document().lines().size(), std::size_t(5));
        for (const std::int64_t ms : {100, 300, 900, 1500, 2600, 3200, 4300, 4700, 4950}) {
            const auto before = render(header() + dialogue, ms);
            const auto after = render(header() + events(session.document()), ms);
            const auto d = compare(before, after);
            QVERIFY2(d.differing == 0, qPrintable(QStringLiteral("%1 ms: %2 of %3 differ").arg(ms).arg(d.differing).arg(d.ink)));
        }
    }
};

QTEST_MAIN(SplitRenderTests)
#include "split_render_tests.moc"
