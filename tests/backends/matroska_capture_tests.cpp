// Y9: the rewrite's MKV extraction against legacy Demux run on the same
// files (tests/fixtures/legacy-observations/local-y9-demux-20261013, captured
// by tools/legacy-capture's legacy_demux_capture from the mkvmerge-muxed
// files in tests/fixtures/matroska-muxed). Each case goes through the real
// media helper: the packets FFMS2's extension hands over (start, duration,
// total and bytes, in order), the Line legacy's packet callback built from
// each, then the whole load as the application runs it
// (MatroskaSubtitleLoad: the chooser and its labels, the progress percents,
// the messages, the staged Document's Script Info, and its Styles and
// Lines field by field against what legacy's own Styles::parseStyle and
// Dialogue::SetRaw read), and the font attachments with their names and
// bytes.

#include "hikari/application/matroska.h"
#include "hikari/backends/ffms_matroska.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/style.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 30'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

std::string str(const QJsonValue &v)
{
    return v.toString().toStdString();
}

std::string str(const std::u8string &s)
{
    return std::string(s.begin(), s.end());
}

std::string fromHex(const std::string &hex)
{
    std::string out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        out += char(std::stoi(hex.substr(i, 2), nullptr, 16));
    return out;
}

// A recorded byte string: "<key>" as UTF-8, or "<key>_hex".
std::string bytesOf(const QJsonObject &o, const char *key)
{
    if (o.contains(key))
        return str(o[key]);
    return fromHex(str(o[QString::fromLatin1(key) + QStringLiteral("_hex")]));
}

std::string fnv1a(const std::vector<std::byte> &bytes)
{
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::byte b : bytes) {
        h ^= std::uint64_t(b);
        h *= 0x100000001b3ull;
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

std::vector<QJsonObject> observations()
{
    QFile file(QStringLiteral(HIKARI_DEMUX_OBSERVATIONS));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    std::vector<QJsonObject> out;
    for (const QByteArray &line : file.readAll().split('\n'))
        if (!line.trimmed().isEmpty())
            out.push_back(QJsonDocument::fromJson(line).object());
    return out;
}

std::vector<QJsonObject> opsOf(const QJsonObject &c, const char *kind)
{
    std::vector<QJsonObject> out;
    for (const auto &v : c["ops"].toArray())
        if (v.toObject()["op"].toString() == QLatin1String(kind))
            out.push_back(v.toObject());
    return out;
}

// The time fields of a raw line Demux built: "Dialogue: L,H:MM:SS.cc,..."
// or "HH:MM:SS,mmm --> HH:MM:SS,mmm\r\n..." (SubsTime::raw, ASS and SRT), in
// milliseconds.
std::pair<std::int64_t, std::int64_t> rawTimes(const std::string &raw)
{
    auto ass = [](const std::string &t) {
        const auto c1 = t.find(':'), c2 = t.find(':', c1 + 1), dot = t.find('.');
        return std::stoll(t.substr(0, c1)) * 3'600'000 + std::stoll(t.substr(c1 + 1, c2 - c1 - 1)) * 60'000 +
               std::stoll(t.substr(c2 + 1, dot - c2 - 1)) * 1000 + std::stoll(t.substr(dot + 1)) * 10;
    };
    auto srt = [](const std::string &t) {
        return std::stoll(t.substr(0, 2)) * 3'600'000 + std::stoll(t.substr(3, 2)) * 60'000 +
               std::stoll(t.substr(6, 2)) * 1000 + std::stoll(t.substr(9, 3));
    };
    if (raw.starts_with("Dialogue: ") || raw.starts_with("Comment: ")) {
        const auto c1 = raw.find(','), c2 = raw.find(',', c1 + 1), c3 = raw.find(',', c2 + 1);
        return {ass(raw.substr(c1 + 1, c2 - c1 - 1)), ass(raw.substr(c2 + 1, c3 - c2 - 1))};
    }
    return {srt(raw.substr(0, 12)), srt(raw.substr(17, 12))};
}

// The ten fields of a Dialogue/Comment line legacy's GetRaw wrote (the text
// keeps its commas).
std::vector<std::string> assFields(const std::string &line)
{
    std::vector<std::string> out;
    std::size_t at = line.find(' ') + 1;
    for (int i = 0; i < 9; ++i) {
        const auto comma = line.find(',', at);
        out.push_back(line.substr(at, comma - at));
        at = comma + 1;
    }
    out.push_back(line.substr(at));
    return out;
}

std::string withoutNewline(std::string s)
{
    if (s.ends_with("\r\n"))
        s.resize(s.size() - 2);
    return s;
}

std::string joined(const std::vector<std::u8string> &fields)
{
    std::string out;
    for (std::size_t i = 0; i < fields.size(); ++i)
        out += (i ? "," : "") + str(fields[i]);
    return out;
}

std::int64_t ms(core::DocumentTime t)
{
    return t.microseconds() / 1000;
}

struct Load {
    std::optional<std::string> cannotOpen;
    bool noTracks = false;
    std::optional<std::vector<std::u8string>> labels;
    bool reading = false;
    std::vector<int> progress;
    std::optional<MatroskaLoaded> loaded;
    std::optional<MatroskaFailure> ended;
    bool done() const { return loaded || ended; }
};

struct MatroskaCapture : ::testing::Test {
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "matroska_capture_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }
    backends::FfmsMatroska port{QStringLiteral(HIKARI_MEDIA_HELPER)};

    static std::string path(const QJsonObject &c)
    {
        return std::string(HIKARI_MUXED_MKV) + "/" + str(c["file"]);
    }
};

} // namespace

TEST_F(MatroskaCapture, ReplaysTheLegacyDemuxProbe)
{
    const auto cases = observations();
    ASSERT_EQ(cases.size(), 12u);
    int compared = 0;
    for (const QJsonObject &c : cases) {
        if (c["kind"].toString() != QLatin1String("subs"))
            continue;
        SCOPED_TRACE(str(c["case"]));
        const std::string file = path(c);

        // The legacy answers: the chooser's row (Cancel when absent) and the
        // packet during which the progress dialog's Cancel was pressed.
        const auto chooser = opsOf(c, "chooser");
        const auto items = opsOf(c, "chooser-item");
        const auto readTrack = opsOf(c, "read-track");
        const bool cancelledRead = c["result"].toBool() == false && !readTrack.empty();
        const auto legacyProgress = opsOf(c, "progress");

        Load load;
        MatroskaSubtitleLoad *runner = nullptr;
        MatroskaSubtitleLoad::Hooks hooks;
        hooks.cannotOpen = [&](const std::string &e) { load.cannotOpen = e; };
        hooks.noTracks = [&] { load.noTracks = true; };
        hooks.chooseTrack = [&](const std::vector<std::u8string> &labels) {
            load.labels = labels;
            // The row legacy read, or the chooser's Cancel.
            int row = -1;
            if (!readTrack.empty())
                for (std::size_t i = 0; i < items.size(); ++i)
                    if (str(items[i]["text"]).starts_with(std::to_string(readTrack[0]["n"].toInt()) + " "))
                        row = int(i);
            if (row < 0)
                runner->cancel();
            else
                runner->choose(row);
        };
        hooks.reading = [&] { load.reading = true; };
        hooks.progress = [&](int percent) {
            load.progress.push_back(percent);
            if (cancelledRead && load.progress.size() == legacyProgress.size())
                runner->cancel();
        };
        hooks.loaded = [&](MatroskaLoaded l) { load.loaded = std::move(l); };
        hooks.ended = [&](MatroskaFailure f) { load.ended = f; };
        MatroskaSubtitleLoad run(port, std::move(hooks));
        runner = &run;
        run.start(file);
        ASSERT_TRUE(waitFor([&] { return load.done(); }));

        // Demux::Open's failure: the log line, FFMS2's text naming the file.
        const auto logs = opsOf(c, "log");
        if (!c["open"].toBool()) {
            ASSERT_EQ(logs.size(), 1u);
            ASSERT_TRUE(load.cannotOpen);
            std::string expected = str(logs[0]["text"]);
            const std::string named = str(c["file"]);
            expected.replace(expected.find(named), named.size(), file);
            EXPECT_EQ("Indexing error occurred: " + *load.cannotOpen, expected);
            EXPECT_EQ(load.ended, MatroskaFailure::CannotOpen);
            ++compared;
            continue;
        }
        EXPECT_FALSE(load.cannotOpen);
        EXPECT_TRUE(logs.empty());

        // No track Demux reads: the message, nothing else.
        const auto messages = opsOf(c, "message");
        EXPECT_EQ(load.noTracks, !messages.empty());
        if (!messages.empty()) {
            EXPECT_EQ(str(messages[0]["text"]), "The file does not contain any subtitle tracks.\x1f");
            EXPECT_FALSE(load.loaded);
            ++compared;
            continue;
        }

        // The chooser, shown for more than one track, with legacy's labels.
        EXPECT_EQ(load.labels.has_value(), !chooser.empty());
        if (load.labels) {
            EXPECT_EQ(str(chooser[0]["text"]), "Choose subtitle track");
            ASSERT_EQ(load.labels->size(), items.size());
            for (std::size_t i = 0; i < items.size(); ++i)
                EXPECT_EQ(str((*load.labels)[i]), str(items[i]["text"]));
        }
        if (readTrack.empty()) { // the chooser's Cancel
            EXPECT_EQ(load.ended, MatroskaFailure::Cancelled);
            EXPECT_FALSE(load.reading);
            ++compared;
            continue;
        }
        EXPECT_TRUE(load.reading);
        const auto progressTitle = opsOf(c, "progress-title");
        ASSERT_EQ(progressTitle.size(), 1u);
        EXPECT_EQ(str(progressTitle[0]["text"]), "Loading subtitles from Matroska.");
        if (cancelledRead) { // Cancel in the progress dialog: nothing loaded
            EXPECT_EQ(load.ended, MatroskaFailure::Cancelled);
            EXPECT_FALSE(load.loaded);
            EXPECT_TRUE(opsOf(c, "clearing").empty());
            ++compared;
            continue;
        }
        ASSERT_TRUE(load.loaded);
        EXPECT_EQ(load.loaded->track, readTrack[0]["n"].toInt());

        // One progress step per packet with legacy's percent.
        ASSERT_EQ(load.progress.size(), legacyProgress.size());
        for (std::size_t i = 0; i < legacyProgress.size(); ++i)
            EXPECT_EQ(load.progress[i], legacyProgress[i]["n"].toInt()) << i;

        // The packets as FFMS2's extension gives them, and the Line legacy's
        // callback built from each (Demux.cpp:262-306).
        std::optional<std::expected<MatroskaSubtitles, MatroskaError>> read;
        std::vector<std::pair<std::int64_t, std::int64_t>> progress;
        port.subtitles(file, load.loaded->track,
                       [&](std::int64_t s, std::int64_t t) { progress.push_back({s, t}); },
                       [&](auto r) { read = std::move(r); });
        ASSERT_TRUE(waitFor([&] { return read.has_value(); }));
        ASSERT_TRUE(*read) << read->error().text;
        const QJsonArray packets = c["packets"].toArray();
        ASSERT_EQ((*read)->packets.size(), std::size_t(packets.size()));
        const auto lines = opsOf(c, "line");
        ASSERT_GE(lines.size(), std::size_t(packets.size()));
        const std::size_t headerLines = lines.size() - std::size_t(packets.size());
        // The codec Demux's codecType came from (Demux.cpp:111-117).
        std::optional<std::expected<std::vector<MatroskaTrack>, MatroskaError>> tracks;
        port.subtitleTracks(file, [&](auto r) { tracks = std::move(r); });
        ASSERT_TRUE(waitFor([&] { return tracks.has_value(); }));
        ASSERT_TRUE(*tracks);
        std::u8string codecName;
        for (const auto &t : offeredTracks(**tracks))
            if (t.track == load.loaded->track)
                codecName = t.codec;
        const auto matroskaCodec = core::matroskaCodec(codecName);
        for (qsizetype i = 0; i < packets.size(); ++i) {
            SCOPED_TRACE("packet " + std::to_string(i));
            const QJsonObject p = packets[i].toObject();
            const MatroskaPacket &got = (*read)->packets[std::size_t(i)];
            EXPECT_EQ(got.start, p["start"].toInteger());
            EXPECT_EQ(got.duration, p["duration"].toInteger());
            EXPECT_EQ(progress[std::size_t(i)].second, p["total"].toInteger());
            EXPECT_EQ(got.line, bytesOf(p, "line"));
            EXPECT_EQ(str(core::matroskaLine(got.start, got.duration, got.line, matroskaCodec)),
                      bytesOf(lines[headerLines + std::size_t(i)], "text"));
        }

        // The staged Document: legacy's Script Info (in order), then its
        // Styles and Lines.
        const core::Document &doc = load.loaded->document;
        const QJsonArray sinfo = c["sinfo"].toArray();
        const auto saved = core::encodeAss(doc);
        const std::string ass(reinterpret_cast<const char *>(saved.data()), saved.size());
        std::size_t at = 0;
        for (const auto &kv : sinfo) {
            const std::string key = str(kv.toArray()[0]), value = str(kv.toArray()[1]);
            const auto got = doc.scriptInfo(std::u8string(key.begin(), key.end()));
            ASSERT_TRUE(got) << key;
            EXPECT_EQ(str(*got), value) << key;
            const auto next = ass.find("\r\n" + key + ":", at);
            EXPECT_NE(next, std::string::npos) << key << " out of legacy's order";
            at = next == std::string::npos ? at : next + 1;
        }
        // Each Style as legacy read it (Styles::parseStyle with the layout
        // Demux chose, then GetRaw): the Document's Style line and its
        // decoded values give the same fields.
        const auto styles = opsOf(c, "style");
        std::vector<const core::StyleRecord *> styleRecords;
        for (const auto &section : doc.sections())
            for (const auto &record : section.records)
                if (const auto *style = std::get_if<core::StyleRecord>(&record))
                    styleRecords.push_back(style);
        const auto decoded = core::decodeStyles(doc);
        ASSERT_EQ(styleRecords.size(), styles.size());
        ASSERT_EQ(decoded.size(), styles.size());
        for (std::size_t i = 0; i < styles.size(); ++i) {
            const std::string legacy = withoutNewline(bytesOf(styles[i], "parsed"));
            SCOPED_TRACE(legacy);
            // The record keeps its fields untrimmed: the space after "Style:".
            EXPECT_EQ("Style:" + joined(styleRecords[i]->fields), legacy);
            EXPECT_EQ("Style: " + joined(core::legacy::styleRawFields(decoded[i])), legacy);
        }

        // Each Line as legacy read it (Dialogue::SetRaw, then GetRaw): kind,
        // layer, times, style, name, margins, effect and text; a SubRip
        // Line's times and text.
        const auto docLines = doc.lines();
        ASSERT_EQ(docLines.size(), lines.size());
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const std::string raw = bytesOf(lines[i], "text");
            const std::string legacy = bytesOf(lines[i], "parsed");
            SCOPED_TRACE(raw);
            const core::LineRecord &got = *docLines[i];
            EXPECT_FALSE(got.unparsed);
            const auto [start, end] = rawTimes(raw);
            EXPECT_EQ(ms(got.start.value), start);
            EXPECT_EQ(ms(got.end.value), end);
            EXPECT_EQ(rawTimes(legacy), std::make_pair(start, end));
            if (legacy.starts_with("Dialogue: ") || legacy.starts_with("Comment: ")) {
                const auto f = assFields(withoutNewline(legacy));
                EXPECT_EQ(got.comment, legacy.starts_with("Comment: "));
                EXPECT_EQ(std::to_string(got.layer.value), f[0]);
                EXPECT_EQ(str(got.style), f[3]);
                EXPECT_EQ(str(got.actor), f[4]);
                EXPECT_EQ(std::to_string(got.marginLeft.value), f[5]);
                EXPECT_EQ(std::to_string(got.marginRight.value), f[6]);
                EXPECT_EQ(std::to_string(got.marginVertical.value), f[7]);
                EXPECT_EQ(str(got.effect), f[8]);
                EXPECT_EQ(str(got.text), f[9]);
            } else {
                // "<start> --> <end>\r\n<text, \N as \r\n>\r\n\r\n".
                ASSERT_TRUE(legacy.ends_with("\r\n\r\n"));
                const auto text = legacy.find("\r\n");
                std::string expected = legacy.substr(text + 2, legacy.size() - 4 - (text + 2));
                for (std::size_t pos = 0; (pos = expected.find("\r\n", pos)) != std::string::npos; pos += 2)
                    expected.replace(pos, 2, "\\N");
                EXPECT_FALSE(got.comment);
                EXPECT_EQ(str(got.text), expected);
            }
        }
        EXPECT_EQ(opsOf(c, "endload").size(), 1u);
        ++compared;
    }
    EXPECT_EQ(compared, 9);
}

TEST_F(MatroskaCapture, ReplaysTheLegacyFontList)
{
    int compared = 0;
    for (const QJsonObject &c : observations()) {
        if (c["kind"].toString() != QLatin1String("fonts"))
            continue;
        SCOPED_TRACE(str(c["case"]));
        std::optional<std::expected<std::vector<MatroskaAttachment>, MatroskaError>> got;
        port.attachments(path(c), [&](auto r) { got = std::move(r); });
        ASSERT_TRUE(waitFor([&] { return got.has_value(); }));
        if (!c["open"].toBool()) {
            ASSERT_FALSE(*got);
            EXPECT_EQ(got->error().failure, MatroskaFailure::CannotOpen);
            ++compared;
            continue;
        }
        ASSERT_TRUE(*got) << got->error().text;
        // GetFontList's names and SaveFont's bytes (Demux.cpp:178-260).
        const auto fonts = matroskaFonts(**got);
        const QJsonArray legacy = c["fonts"].toArray();
        ASSERT_EQ(fonts.size(), std::size_t(legacy.size()));
        for (std::size_t i = 0; i < fonts.size(); ++i) {
            const QJsonObject f = legacy[qsizetype(i)].toObject();
            EXPECT_EQ(QString::fromStdU16String(fonts[i].name).toStdString(), bytesOf(f, "name"));
            EXPECT_TRUE(f["saved"].toBool());
            EXPECT_EQ(qint64(fonts[i].data->size()), f["size"].toInteger());
            EXPECT_EQ(fnv1a(*fonts[i].data), str(f["fnv1a64"]));
        }
        ++compared;
    }
    EXPECT_EQ(compared, 3);
}
