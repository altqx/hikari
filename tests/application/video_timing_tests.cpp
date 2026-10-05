// V6: timing and selecting Lines from the video, and how the video follows
// the active Line. ReplaysTheLegacyObservations runs every case of the legacy
// probe (tools/legacy-capture/video_timing_capture.cpp over
// inputs/video-timing-cases.txt; tests/fixtures/legacy-observations/
// local-v6-video-timing-20261005) through the rewrite and needs legacy's
// times, selections and video calls exactly, on the CFR and VFR timelines,
// apart from the ops of the four approved V6 departures (kDepartures), which
// expect legacy's result with exactly that change. The other tests pin the
// transaction rules (one undo step each, none when nothing changes, the
// pending draft first), the departures and the Grid press's trigger
// (SubsGridWindow.cpp:1647).

#include "hikari/application/video_timing.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

std::int64_t ms(core::DocumentTime t)
{
    return t.microseconds() / 1000;
}

core::DocumentTime at(int value)
{
    return core::DocumentTime(std::int64_t{value} * 1000);
}

// ---- the probe's cases -------------------------------------------------------

struct LineSpec {
    int start = 0, end = 0;
    std::string flags;
    bool has(char c) const { return flags.find(c) != std::string::npos; }
};

struct Case {
    std::string name;
    float fps = 0.f;
    std::vector<int> timecodes, keyframes;
    std::vector<LineSpec> lines;
    int active = 0;
    bool hasEditLine = false;
    int editStart = 0, editEnd = 0;
    VideoState state = VideoState::None;
    int tell = 0, duration = 0;
    std::string format = "ass";
    int inactive = 1;
    std::vector<std::string> ops;
};

std::vector<Case> readCases(const char *path)
{
    std::ifstream in(path);
    std::vector<Case> cases;
    std::string text;
    while (std::getline(in, text)) {
        if (!text.empty() && text.back() == '\r')
            text.pop_back();
        std::istringstream line(text);
        std::string op;
        line >> op;
        if (op.empty() || op[0] == '#')
            continue;
        if (op == "case") {
            cases.emplace_back();
            line >> cases.back().name;
            continue;
        }
        Case &c = cases.back();
        if (op == "timebase") {
            line >> c.fps;
            int v;
            while (line >> v)
                c.timecodes.push_back(v);
        } else if (op == "keyframes") {
            int v;
            while (line >> v)
                c.keyframes.push_back(v);
        } else if (op == "line") {
            LineSpec l;
            line >> l.start >> l.end >> l.flags;
            c.lines.push_back(l);
        } else if (op == "active") {
            line >> c.active;
        } else if (op == "editline") {
            c.hasEditLine = true;
            line >> c.editStart >> c.editEnd;
        } else if (op == "video") {
            std::string s;
            line >> s >> c.tell >> c.duration;
            c.state = s == "playing"   ? VideoState::Playing
                      : s == "paused"  ? VideoState::Paused
                      : s == "stopped" ? VideoState::Stopped
                                       : VideoState::None;
        } else if (op == "format") {
            line >> c.format;
        } else if (op == "inactive") {
            line >> c.inactive;
        } else if (op != "end") {
            c.ops.push_back(text);
        }
    }
    return cases;
}

// Timebase::FromTimecodes(timecodes, fps): without a rate, the average of the
// timecodes (Timebase.cpp:31-40).
LegacyTimebase timebaseOf(const Case &c)
{
    float fps = c.fps;
    if (fps <= 0.f && c.timecodes.size() > 1 && c.timecodes.back() > c.timecodes.front())
        fps = 1000.f * static_cast<float>(c.timecodes.size() - 1) /
              static_cast<float>(c.timecodes.back() - c.timecodes.front());
    return LegacyTimebase(c.timecodes, fps);
}

// The case's Lines as a Document: the probe's flags become a Comment, an
// unparsed record (legacy NonDialogue), no text, or a translation only.
struct Built {
    EditSession session;
    std::vector<core::LineId> ids;
    ShownLine shown; // the Lines legacy's isVisible shows
};

Built build(const Case &c)
{
    std::string text = "[Events]\n";
    for (std::size_t i = 0; i < c.lines.size(); ++i)
        text += "Dialogue: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,,line " + std::to_string(i) + "\n";
    core::Document document = load(text);
    std::vector<core::LineId> ids;
    for (const auto *l : document.lines())
        ids.push_back(l->id);
    std::set<core::LineId> hidden, selected;
    for (std::size_t i = 0; i < c.lines.size(); ++i) {
        const LineSpec &spec = c.lines[i];
        document.editLine(ids[i], [&](core::LineRecord &l) {
            l.start.value = at(spec.start);
            l.end.value = at(spec.end);
            l.comment = spec.has('C');
            l.unparsed = spec.has('N');
            if (spec.has('E') || spec.has('T'))
                l.text.clear();
            if (spec.has('T'))
                l.translation = u8"tl";
        });
        if (spec.has('H'))
            hidden.insert(ids[i]);
        if (spec.has('S'))
            selected.insert(ids[i]);
    }
    Built b{EditSession(std::move(document)), ids, [hidden](core::LineId id) { return !hidden.contains(id); }};
    b.session.setSelection(Selection{ids[static_cast<std::size_t>(c.active)], selected, {}, {}});
    if (c.hasEditLine)
        b.session.editDraft(ids[static_cast<std::size_t>(c.active)],
                            DraftChange{.start = at(c.editStart), .end = at(c.editEnd)});
    return b;
}

std::vector<AudioLineSpan> spansOf(const Case &c)
{
    std::vector<AudioLineSpan> spans;
    for (const auto &l : c.lines)
        spans.push_back({l.start, l.end, !l.has('H')});
    return spans;
}

FollowedLine followed(const Case &c, int row, bool editLine)
{
    const auto spans = spansOf(c);
    FollowedLine line{c.lines[static_cast<std::size_t>(row)].start, c.lines[static_cast<std::size_t>(row)].end, 0};
    if (editLine && c.hasEditLine) {
        line.startMs = c.editStart;
        line.endMs = c.editEnd;
    }
    const int next = legacyKeyFromPosition(spans, row, 1);
    line.nextStartMs = next >= 0 ? spans[static_cast<std::size_t>(next)].startMs : line.startMs;
    return line;
}

// The legacy calls as what the video and the audio box were asked to do.
VideoFollow fromCalls(const QJsonArray &calls, VideoState state)
{
    VideoFollow f;
    bool played = false;
    for (const auto &value : calls) {
        std::istringstream call(value.toString().toStdString());
        std::string name;
        call >> name;
        if (name == "Play") {
            played = true;
        } else if (name == "Pause") {
            if (played)
                f.playThenPause = true; // Stopped: Play(); Pause()
            else
                f.pause = true;
        } else if (name == "Seek") {
            int t = 0, start = 1;
            call >> t >> start;
            f.seekMs = t;
            f.seekStart = start != 0;
        } else if (name == "PlayLine") {
            int a = 0, b = 0;
            call >> a >> b;
            // RendererVideo::PlayLine does nothing without a video (RendererVideo.cpp:624)
            if (state != VideoState::None)
                f.playVideo = std::pair(a, b);
        } else if (name == "OnPlaySelection") {
            f.playAudio = true;
        } else if (name == "audio") {
            std::string what;
            int atEnd = 0;
            call >> what >> atEnd;
            if (what == "Update")
                f.audioUpdate = atEnd != 0;
        }
    }
    // a seek while the video plays on (ShowEditOnVideo; SetVideoLineTime with choice 0)
    f.seekWhilePlaying = f.seekMs && state == VideoState::Playing && !f.pause;
    return f;
}

std::vector<int> rowsOf(const std::vector<core::LineId> &ids, const std::vector<core::LineId> &found)
{
    std::vector<int> rows;
    for (const auto id : found)
        rows.push_back(static_cast<int>(std::find(ids.begin(), ids.end(), id) - ids.begin()));
    return rows;
}

std::string describe(const VideoFollow &f)
{
    std::ostringstream out;
    out << "playThenPause=" << f.playThenPause << " pause=" << f.pause << " seek="
        << (f.seekMs ? std::to_string(*f.seekMs) : "-") << (f.seekStart ? "s" : "e") << " keepPlaying="
        << f.seekWhilePlaying << " playVideo="
        << (f.playVideo ? std::to_string(f.playVideo->first) + "-" + std::to_string(f.playVideo->second) : "-")
        << " playAudio=" << f.playAudio << " audioUpdate=" << (f.audioUpdate ? (*f.audioUpdate ? "end" : "start") : "-");
    return out.str();
}

// The approved V6 departures (docs/qt/compatibility-decisions.md, user
// 2026-10-05): each op expects legacy's result with exactly this change, and
// its legacy observation, kept as the old evidence, must still differ.
struct Departure {
    const char *id;
    std::optional<int> value; // selvideo: the row; snap: the snapped time; setstart/setend: unused (no step)
};
const std::map<std::pair<std::string, std::string>, Departure> kDepartures = {
    // legacy falls back to the hidden row 0
    {{"select-from-video-first-hidden", "at 200 selvideo"}, {"V6-select-shown-fallback", 1}},
    {{"select-from-video-all-hidden", "selvideo"}, {"V6-select-shown-fallback", std::nullopt}},
    // legacy's mode-1 loop stops before the next Line (Start 950)
    {{"snap-first-line-mode-1", "snap end 1"}, {"V6-snap-next-line", 950}},
    // legacy skips the other Line's End (1990) after its blocked Start
    {{"snap-end-blocked-start", "snap end 1"}, {"V6-snap-both-boundaries", 1990}},
    // legacy records a step with nothing changed
    {{"insert-times-unchanged", "setstart 0"}, {"V6-insert-time-no-op", std::nullopt}},
    {{"insert-times-unchanged", "setend 0"}, {"V6-insert-time-no-op", std::nullopt}},
};

} // namespace

// Every op of every case: the rewrite gives legacy's results, apart from the
// approved departures' ops, which give legacy's with exactly that change.
TEST(VideoTimingCapture, ReplaysTheLegacyObservations)
{
    const auto cases = readCases(HIKARI_VIDEO_TIMING_CASES);
    ASSERT_GE(cases.size(), 40u);
    QFile file(QStringLiteral(HIKARI_VIDEO_TIMING_OBSERVATIONS));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    std::map<std::string, QJsonArray> observed;
    for (const QByteArray &line : file.readAll().split('\n'))
        if (!line.trimmed().isEmpty()) {
            const auto object = QJsonDocument::fromJson(line).object();
            observed[object["case"].toString().toStdString()] = object["ops"].toArray();
        }
    ASSERT_EQ(observed.size(), cases.size());
    std::map<std::string, int> compared;
    std::set<std::pair<std::string, std::string>> departed;
    for (const auto &c : cases) {
        SCOPED_TRACE(c.name);
        const auto ops = observed.at(c.name);
        ASSERT_EQ(static_cast<std::size_t>(ops.size()), c.ops.size());
        const LegacyTimebase timebase = timebaseOf(c);
        for (std::size_t i = 0; i < c.ops.size(); ++i) {
            const QJsonObject legacy = ops[static_cast<qsizetype>(i)].toObject();
            SCOPED_TRACE(c.ops[i]);
            ASSERT_EQ(legacy["op"].toString().toStdString(), c.ops[i]);
            std::istringstream in(c.ops[i]);
            std::string op;
            in >> op;
            int tell = c.tell;
            if (op == "at")
                in >> tell >> op;
            const QJsonArray calls = legacy["calls"].toArray();
            auto called = [&](const char *prefix) {
                for (const auto &v : calls)
                    if (v.toString().startsWith(QLatin1String(prefix)))
                        return true;
                return false;
            };
            Built b = build(c);
            const auto departure = kDepartures.find({c.name, c.ops[i]});
            if (departure != kDepartures.end()) {
                departed.insert(departure->first);
                SCOPED_TRACE(departure->second.id);
            }
            const Departure *changed = departure != kDepartures.end() ? &departure->second : nullptr;
            if (op == "frametime") {
                EXPECT_EQ(legacyFrameTime(timebase, tell, true), legacy["start"].toInt());
                EXPECT_EQ(legacyFrameTime(timebase, tell, false), legacy["end"].toInt());
            } else if (op == "setstart" || op == "setend") {
                int offset = 0;
                in >> offset;
                const bool end = op == "setend";
                const auto before = b.session.historySize();
                if (c.state != VideoState::None) { // the command's guard (Application::setTimeFromVideo)
                    const int value = legacyInsertTimeFromVideo(timebase, tell, !end, offset);
                    const auto done = setTimesFromVideo(b.session, end, value);
                    EXPECT_EQ(done.has_value(), called("SetModified"));
                }
                if (changed) { // V6-insert-time-no-op: legacy's step, none here
                    EXPECT_TRUE(called("SetModified"));
                    EXPECT_EQ(b.session.historySize(), before);
                } else {
                    EXPECT_EQ(b.session.historySize(), before + (called("SetModified") ? 1 : 0));
                }
                const auto lines = b.session.document().lines();
                const QJsonArray times = legacy["lines"].toArray();
                ASSERT_EQ(static_cast<std::size_t>(times.size()), lines.size());
                for (std::size_t r = 0; r < lines.size(); ++r) {
                    EXPECT_EQ(ms(lines[r]->start.value), times[static_cast<qsizetype>(r)][0].toInt()) << "row " << r;
                    EXPECT_EQ(ms(lines[r]->end.value), times[static_cast<qsizetype>(r)][1].toInt()) << "row " << r;
                }
            } else if (op == "selvideo") {
                if (c.state == VideoState::None) { // SelVideoLine returns at once
                    EXPECT_EQ(legacy["current"].toInt(), c.active);
                } else if (changed) { // V6-select-shown-fallback
                    const auto id = lineAtVideoTime(b.session.document(), b.shown, tell, c.duration);
                    const std::optional<int> row = id ? std::optional(rowsOf(b.ids, {*id}).front()) : std::nullopt;
                    EXPECT_EQ(row, changed->value);
                    EXPECT_NE(row, std::optional(legacy["current"].toInt())); // legacy's hidden row 0
                    EXPECT_FALSE(b.shown(b.ids[static_cast<std::size_t>(legacy["current"].toInt())]));
                } else {
                    const auto id = lineAtVideoTime(b.session.document(), b.shown, tell, c.duration);
                    ASSERT_TRUE(id);
                    const int row = rowsOf(b.ids, {*id}).front();
                    EXPECT_EQ(row, legacy["current"].toInt());
                    EXPECT_EQ(QJsonArray{row}, legacy["selected"].toArray());
                }
            } else if (op == "selvisible") {
                int ignore = 0;
                in >> ignore;
                const ShownLine shown = ignore ? ShownLine{} : b.shown;
                // VideoBox::Tell without a video is 0
                const int time = c.state == VideoState::None ? 0 : tell;
                const auto found = linesShownOnVideo(b.session.document(), shown, b.session.draftRecord()
                                                                                      ? b.session.draftRecord()
                                                                                      : std::optional(*b.session.document().lines()[static_cast<std::size_t>(c.active)]),
                                                     time);
                QJsonArray rows;
                for (const int r : rowsOf(b.ids, found))
                    rows.append(r);
                EXPECT_EQ(rows, legacy["selected"].toArray());
                EXPECT_EQ(found.empty() ? c.active : rowsOf(b.ids, found).front(), legacy["current"].toInt());
            } else if (op == "snap") {
                std::string which;
                int abox = 0;
                in >> which >> abox;
                const bool start = which == "start";
                const QJsonArray edit = legacy["edit"].toArray();
                const int startMs = c.hasEditLine ? c.editStart : c.lines[static_cast<std::size_t>(c.active)].start;
                const int endMs = c.hasEditLine ? c.editEnd : c.lines[static_cast<std::size_t>(c.active)].end;
                if (!abox) { // OnAudioSnap needs the audio box (HikariSubFrame.cpp:2531)
                    EXPECT_FALSE(called("Send"));
                } else {
                    std::vector<int> snapTimes;
                    for (const int k : c.keyframes)
                        snapTimes.push_back(timebase.startTimeFor(timebase.frameAt(k)));
                    const auto spans = spansOf(c);
                    KeyframeSnapContext context{c.keyframes, snapTimes, spans, c.active, c.inactive};
                    const auto snapped = legacyKeyframeSnap(context, startMs, endMs, start);
                    if (changed) { // V6-snap-next-line, V6-snap-both-boundaries: legacy did not snap
                        EXPECT_FALSE(called("Send Snapping to keyframe"));
                        EXPECT_EQ(edit[0].toInt(), startMs);
                        EXPECT_EQ(edit[1].toInt(), endMs);
                        EXPECT_EQ(snapped, changed->value);
                    } else {
                        EXPECT_EQ(snapped.has_value(), called("Send Snapping to keyframe"));
                        EXPECT_EQ(start ? snapped.value_or(startMs) : startMs, edit[0].toInt());
                        EXPECT_EQ(start ? endMs : snapped.value_or(endMs), edit[1].toInt());
                    }
                }
            } else if (op == "setline") {
                int row = 0, seek = 0, play = 0, nochangeline = 0, autoPlay = 0, abox = 0;
                in >> row >> seek >> play >> nochangeline >> autoPlay >> abox;
                const bool rowChanged = row != c.active;
                // the goto-done path keeps the edit box's Line (EditBox.cpp:366-371)
                const auto line = followed(c, row, nochangeline && !rowChanged);
                auto follow = followShownLine(static_cast<SeekAfter>(seek), static_cast<PlayAfter>(play), c.state,
                                              rowChanged, nochangeline != 0, autoPlay != 0, abox != 0, line);
                if (follow.playVideo) // the video applies Timebase::PlayEndBefore
                    follow.playVideo->second = timebase.msAt(timebase.frameAt(follow.playVideo->second) - 1);
                EXPECT_EQ(describe(follow), describe(fromCalls(calls, c.state)));
            } else if (op == "press") {
                int seek = 0, play = 0, dclick = 0, ctrl = 0, endColumn = 0, rowChanged = 0, abox = 0;
                in >> seek >> play >> dclick >> ctrl >> endColumn >> rowChanged >> abox;
                GridPress press{dclick != 0, ctrl != 0, rowChanged != 0, true, endColumn != 0, c.format == "tmp", abox != 0};
                const auto follow = followGridPress(static_cast<SeekAfter>(seek), static_cast<PlayAfter>(play), c.state,
                                                    press, followed(c, c.active, true));
                EXPECT_EQ(describe(follow), describe(fromCalls(calls, c.state)));
            } else if (op == "edit") {
                int seek = 0, visual = 0;
                in >> seek >> visual;
                const auto follow = followEdit(static_cast<SeekAfter>(seek), c.state, visual > 0,
                                               followed(c, c.active, true));
                EXPECT_EQ(describe(follow), describe(fromCalls(calls, c.state)));
            } else {
                ADD_FAILURE() << "unknown op";
            }
            ++compared[op];
        }
    }
    // every departure's op ran
    EXPECT_EQ(departed.size(), kDepartures.size());
    // every command and choice was exercised
    for (const char *op : {"frametime", "setstart", "setend", "selvideo", "selvisible", "snap", "setline", "press", "edit"})
        EXPECT_GT(compared[op], 0) << op;
}

// ---- the rules around the commands ----------------------------------------

namespace {

struct Fixture : ::testing::Test {
    EditSession session{load("[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                             "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,b\n"
                             "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,c\n")};
    core::LineId a{1}, b{2}, c{3};
    void select(std::set<core::LineId> lines, core::LineId active) { session.setSelection(Selection{active, lines, active, {}}); }
    const core::LineRecord &line(std::size_t i) const { return *session.document().lines()[i]; }
};

} // namespace

TEST_F(Fixture, SetStartTimeIsOneNamedStepThatUndoRestores)
{
    select({a, b}, b);
    const auto steps = session.historySize();
    ASSERT_TRUE(setTimesFromVideo(session, false, 3500));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Setting start time");
    EXPECT_EQ(ms(line(0).start.value), 3500);
    EXPECT_EQ(ms(line(0).end.value), 3500); // End < stime: the End follows
    EXPECT_EQ(ms(line(1).start.value), 3500);
    EXPECT_EQ(ms(line(1).end.value), 4000);
    EXPECT_EQ(ms(line(2).start.value), 5000); // not selected
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{a, b}));
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(ms(line(0).start.value), 1000);
    EXPECT_EQ(ms(line(0).end.value), 2000);
}

TEST_F(Fixture, SetEndTimeMovesALaterStartBack)
{
    select({b, c}, c);
    ASSERT_TRUE(setTimesFromVideo(session, true, 4500));
    EXPECT_EQ(session.history().back().name, "Setting end time");
    EXPECT_EQ(ms(line(1).start.value), 3000);
    EXPECT_EQ(ms(line(1).end.value), 4500);
    EXPECT_EQ(ms(line(2).start.value), 4500); // Start > etime: the Start follows
    EXPECT_EQ(ms(line(2).end.value), 4500);
}

// V6-insert-time-no-op: legacy's CopyDialogue copied every selected Line, so
// SetModified recorded a step even when no time changed; nothing is recorded.
TEST_F(Fixture, UnchangedTimesRecordNoStep)
{
    select({a, b}, a);
    const auto steps = session.historySize();
    ASSERT_TRUE(setTimesFromVideo(session, false, 1000)); // a starts at 1000; b's 3000 moves...
    EXPECT_EQ(session.historySize(), steps + 1);          // ...so one step
    select({a}, a);
    const auto revision = session.revision();
    ASSERT_TRUE(setTimesFromVideo(session, false, 1000)); // nothing changes
    ASSERT_TRUE(setTimesFromVideo(session, true, 2000));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.revision(), revision);
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{a}));
}

// A pending draft is still committed as its own step when the insert then
// changes nothing.
TEST_F(Fixture, UnchangedTimesStillCommitThePendingDraft)
{
    select({a}, a);
    ASSERT_TRUE(session.editDraftText(a, u8"typed"));
    const auto steps = session.historySize();
    ASSERT_TRUE(setTimesFromVideo(session, false, 1000));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(line(0).text, u8"typed");
    EXPECT_FALSE(session.draftLine());
}

// V6-select-shown-fallback: before every shown Line (and on none), the
// fallback is the first shown Line, not a hidden Document row 0.
TEST(VideoTimingSelect, FallsBackToTheFirstShownLine)
{
    const auto document = load("[Events]\n"
                               "Dialogue: 0,0:00:00.00,0:00:00.50,Default,,0,0,0,,hidden\n"
                               "Comment: 0,0:00:00.60,0:00:00.70,Default,,0,0,0,,shown comment\n"
                               "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,b\n"
                               "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,c\n");
    const auto lines = document.lines();
    const core::LineId hidden = lines[0]->id;
    const ShownLine shown = [&](core::LineId id) { return id != hidden; };
    // Tell 200: no Line starts before it and the next (1000) is further
    // away than 0: the earlier fallback (ip), the first shown row (the
    // Comment, as legacy's row 0 could be one)
    EXPECT_EQ(lineAtVideoTime(document, shown, 200, 10000), lines[1]->id);
    // Tell 600: the next Line is nearer
    EXPECT_EQ(lineAtVideoTime(document, shown, 600, 10000), lines[2]->id);
    // Tell 9000: no Line starts after it and the video's end (10000) is
    // nearer than c's start: the later fallback (idr), the first shown row
    EXPECT_EQ(lineAtVideoTime(document, shown, 9000, 10000), lines[1]->id);
    // no Line shown: nothing
    EXPECT_EQ(lineAtVideoTime(document, [](core::LineId) { return false; }, 200, 10000), std::nullopt);
    // every Line shown: row 0 as legacy
    EXPECT_EQ(lineAtVideoTime(document, ShownLine{}, 200, 10000), lines[0]->id);
}

// V6-snap-next-line and V6-snap-both-boundaries.
TEST(VideoTimingSnap, ModeOneReachesTheNextLine)
{
    const std::vector<AudioLineSpan> spans{{0, 900, true}, {950, 1990, true}, {2010, 2980, true}, {2000, 2600, true}};
    KeyframeSnapContext context{{}, {}, spans, 1, 1};
    // the active End 1990: the next Line's Start (2010, 20 ms) wins
    EXPECT_EQ(legacyKeyframeSnap(context, 950, 1990, false), 2010);
    // the Line after the next is outside mode 1's range
    context.inactiveLinesMode = 2;
    EXPECT_EQ(legacyKeyframeSnap(context, 950, 1990, false), 2000);
    // a hidden next Line: GetKeyFromPosition passes over it to the next shown one
    const std::vector<AudioLineSpan> hiddenNext{{950, 1990, true}, {2010, 2980, false}, {2030, 2600, true}};
    KeyframeSnapContext second{{}, {}, hiddenNext, 0, 1};
    EXPECT_EQ(legacyKeyframeSnap(second, 950, 1990, false), 2030);
    // the last shown Line: the range ends at the Line itself
    KeyframeSnapContext last{{}, {}, spans, 3, 1};
    EXPECT_EQ(legacyKeyframeSnap(last, 2000, 2600, false), 2980); // the previous Line's End
}

TEST(VideoTimingSnap, ABlockedStartLeavesTheEndATarget)
{
    // Snapping the End 2000: the other Line's Start 500 is before the
    // active Start (blocked), its End 1990 is a target.
    const std::vector<AudioLineSpan> spans{{1000, 2000, true}, {500, 1990, true}};
    for (const int mode : {1, 2, 3}) {
        KeyframeSnapContext context{{}, {}, spans, 0, mode};
        EXPECT_EQ(legacyKeyframeSnap(context, 1000, 2000, false), 1990) << mode;
    }
}

TEST_F(Fixture, ThePendingDraftIsCommittedFirstAsItsOwnStep)
{
    select({a}, a);
    ASSERT_TRUE(session.editDraftText(a, u8"typed"));
    const auto steps = session.historySize();
    ASSERT_TRUE(setTimesFromVideo(session, false, 1500));
    EXPECT_EQ(session.historySize(), steps + 2); // Send(EDITBOX_LINE_EDITION), then the command
    EXPECT_EQ(line(0).text, u8"typed");
    EXPECT_EQ(ms(line(0).start.value), 1500);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(line(0).text, u8"typed");
    EXPECT_EQ(ms(line(0).start.value), 1000);
}

TEST_F(Fixture, NothingSelectedIsRefusedWithoutAStep)
{
    session.setSelection(Selection{a, {}, {}, {}});
    const auto steps = session.historySize();
    EXPECT_FALSE(setTimesFromVideo(session, false, 1500));
    EXPECT_EQ(session.historySize(), steps);
}

TEST(VideoTimingInsert, NegativeTimesClampAtZeroAfterTruncation)
{
    // ZEROIT truncates towards zero: -15 becomes -10, and NewTime makes it 0.
    const LegacyTimebase cfr({0, 41, 83, 125}, 24.f);
    EXPECT_EQ(legacyInsertTimeFromVideo(cfr, 0, true, -15), -10);
    EXPECT_EQ(legacyInsertTimeFromVideo(cfr, 41, true, 0), 20); // StartTimeFor(1) = 25
    EXPECT_EQ(legacyInsertTimeFromVideo(cfr, 41, false, 0), 60); // EndTimeFor(1) = 67
}

// SubsGridWindow.cpp:1647: a double click always seeks; a press seeks for
// choices 1-3 on another Line while the play-after choice does not play the video.
TEST(VideoFollowPress, TheGridPressSeeksOnlyWhenLegacyDid)
{
    const FollowedLine line{1000, 2000, 2500};
    auto seeks = [&](SeekAfter seek, PlayAfter play, GridPress press) {
        return followGridPress(seek, play, VideoState::Paused, press, line).seekMs.has_value();
    };
    GridPress click{false, false, true, true, false, false};
    EXPECT_FALSE(seeks(SeekAfter::DoubleClick, PlayAfter::Nothing, click));
    EXPECT_TRUE(seeks(SeekAfter::EveryLineChange, PlayAfter::Nothing, click));
    EXPECT_TRUE(seeks(SeekAfter::ClickOrEditWhenPaused, PlayAfter::AudioToEnd, click));
    EXPECT_TRUE(seeks(SeekAfter::ClickOrEdit, PlayAfter::Nothing, click));
    EXPECT_FALSE(seeks(SeekAfter::EditWhenPaused, PlayAfter::Nothing, click));
    EXPECT_FALSE(seeks(SeekAfter::Edit, PlayAfter::Nothing, click));
    EXPECT_FALSE(seeks(SeekAfter::ClickOrEdit, PlayAfter::VideoToEnd, click)); // the video plays instead
    GridPress same = click;
    same.rowChanged = false;
    EXPECT_FALSE(seeks(SeekAfter::ClickOrEdit, PlayAfter::Nothing, same));
    GridPress ctrl = click;
    ctrl.ctrl = true;
    EXPECT_TRUE(seeks(SeekAfter::ClickOrEdit, PlayAfter::Nothing, ctrl));
    ctrl.changeActive = false;
    EXPECT_FALSE(seeks(SeekAfter::ClickOrEdit, PlayAfter::Nothing, ctrl));
    GridPress dclick{true, false, false, false, false, false};
    EXPECT_TRUE(seeks(SeekAfter::Edit, PlayAfter::VideoToNextStart, dclick));
    EXPECT_FALSE(followGridPress(SeekAfter::Edit, PlayAfter::Nothing, VideoState::None, dclick, line).seekMs);
}
