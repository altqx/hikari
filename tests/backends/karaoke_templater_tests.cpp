// KE1: the bundled karaoke templater (Autoload/kara-templater.lua) against a
// corpus of real templates (karaoke-corpus/: the Aegisub templater docs'
// templates, Karaoke Mugen's, Kara Effector's ke4 functions, errors and
// karaoke tag variants). Each corpus Document is templated in the isolated
// Lua helper and saved; the saved bytes must equal both the committed
// golden file and what the unchanged 2.1.7 script, as Aegisub 3.2.2 shipped
// it (karaoke-corpus/kara-templater-aegisub-3.2.2.lua), produces in the same
// host. Text extents come from a deterministic fake, so the goldens do not
// depend on the fonts of the machine.
//
// HIKARI_UPDATE_KARAOKE_GOLDENS=1 rewrites the goldens from the unchanged
// script (never from the bundled one).
#include "hikari/application/automation.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/kanji_timer.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/backends/lua_script_host.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/text_projection.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <gtest/gtest.h>

#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

using hikari::application::HostService;
using hikari::application::HostServiceReply;
using hikari::application::HostServiceRequest;
using hikari::backends::LuaScriptHost;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 50'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

QString corpusDir()
{
    return QStringLiteral(HIKARI_KARAOKE_CORPUS);
}

QString pristineScript()
{
    return corpusDir() + QStringLiteral("/kara-templater-aegisub-3.2.2.lua");
}

QString bundledScript()
{
    return QStringLiteral(HIKARI_KARAOKE_TEMPLATER);
}

std::vector<std::byte> readBytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    std::vector<std::byte> out(static_cast<std::size_t>(data.size()));
    std::memcpy(out.data(), data.constData(), out.size());
    return out;
}

std::string textOf(const std::vector<std::byte> &bytes)
{
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

// Deterministic text_extents: 0.55 em per character below U+2E80 and 1 em
// from there on (CJK), scaled by the style's ScaleX/ScaleY and Spacing
// (positional style fields, Name first).
HostServiceReply textExtents(const HostServiceRequest &r)
{
    const auto number = [&](std::size_t i) { return i < r.style.size() ? std::stod(r.style[i]) : 0.0; };
    const double size = number(2), scaleX = number(11) / 100, scaleY = number(12) / 100, spacing = number(13);
    const QString text = QString::fromStdString(r.strings.at(0));
    double ems = 0;
    int chars = 0;
    for (const char32_t c : text.toUcs4()) {
        ems += c >= 0x2E80 ? 1.0 : 0.55;
        ++chars;
    }
    HostServiceReply out;
    out.numbers = {ems * size * scaleX + spacing * chars, size * scaleY, size * 0.21, 0};
    return out;
}

struct Outcome {
    std::optional<LuaScriptHost::RunOutcome> outcome;
    QString message;
    QStringList log;
};

class KaraokeTemplater : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "karaoke_templater_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }

    std::unique_ptr<LuaScriptHost> load(const QString &script)
    {
        auto host = std::make_unique<LuaScriptHost>(QStringLiteral(HIKARI_LUA_HELPER), script,
                                                    QStringLiteral(HIKARI_LUA_INCLUDE));
        host->setServiceHandler([](const HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
            reply(r.service == HostService::TextExtents ? textExtents(r) : HostServiceReply::unavailable());
        });
        QObject::connect(host.get(), &LuaScriptHost::logged, [this](const QString &t) { run.log << t; });
        QObject::connect(host.get(), &LuaScriptHost::finished,
                         [this](LuaScriptHost::RunOutcome o, const QString &m) {
                             run.outcome = o;
                             run.message = m;
                         });
        host->load();
        waitFor([&] { return host->state() != LuaScriptHost::State::Loading; });
        return host;
    }

    static int macro(const LuaScriptHost &host, const std::string &name)
    {
        for (std::size_t i = 0; i < host.info().macros.size(); ++i)
            if (host.info().macros[i].name == name)
                return static_cast<int>(i);
        ADD_FAILURE() << "no macro " << name;
        return -1;
    }

    // Runs `macroName` of `script` on the session's Document and applies its
    // result as the automation shell does.
    bool apply(hikari::application::EditSession &session, const QString &script, const std::string &macroName)
    {
        auto host = load(script);
        if (host->state() != LuaScriptHost::State::Ready) {
            ADD_FAILURE() << host->lastError().toStdString();
            return false;
        }
        const auto snapshot = hikari::application::snapshotForMacro(session);
        if (!snapshot)
            return false;
        run = {};
        if (!host->run(macro(*host, macroName), *snapshot))
            return false;
        if (!waitFor([&] { return run.outcome.has_value(); })) {
            ADD_FAILURE() << "the run did not end";
            return false;
        }
        if (*run.outcome != LuaScriptHost::RunOutcome::Ok || !host->lastResult()) {
            ADD_FAILURE() << "outcome " << static_cast<int>(*run.outcome) << ": " << run.message.toStdString()
                          << run.log.join(QString()).toStdString();
            return false;
        }
        report = QJsonDocument::fromJson(QByteArray::fromStdString(host->lastResult()->report)).object();
        return hikari::application::applyMacroResult(session, *snapshot, *host->lastResult(), macroName).has_value();
    }

    // Runs `macroName` without applying its result.
    std::optional<LuaScriptHost::RunOutcome> outcomeOf(hikari::application::EditSession &session,
                                                       const QString &script, const std::string &macroName)
    {
        auto host = load(script);
        const auto snapshot = hikari::application::snapshotForMacro(session);
        run = {};
        if (!snapshot || !host->run(macro(*host, macroName), *snapshot) ||
            !waitFor([&] { return run.outcome.has_value(); }))
            return std::nullopt;
        return run.outcome;
    }

    std::string templatedBytes(const std::vector<std::byte> &bytes, const QString &script)
    {
        hikari::application::EditSession session(hikari::core::loadAss(bytes).document);
        if (!apply(session, script, "Apply karaoke template"))
            return {};
        return textOf(hikari::core::encodeAss(session.document()));
    }

    std::string templated(const QString &document, const QString &script)
    {
        return templatedBytes(readBytes(document), script);
    }

    Outcome run;
    QJsonObject report; // the last applied run's
};

QStringList corpus()
{
    QStringList documents;
    for (const QString &name : QDir(corpusDir()).entryList({QStringLiteral("*.ass")}, QDir::Files, QDir::Name))
        if (!name.endsWith(QLatin1String(".out.ass")))
            documents << name;
    return documents;
}

QString goldenOf(const QString &document)
{
    return corpusDir() + QLatin1Char('/') + QFileInfo(document).completeBaseName() + QStringLiteral(".out.ass");
}

std::vector<std::byte> bytesOf(const std::string &text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

// A corpus file with each `from` replaced by its `to`, once each.
std::vector<std::byte> editedCorpus(const QString &name, const std::vector<std::pair<std::string, std::string>> &edits)
{
    std::string text = textOf(readBytes(corpusDir() + QLatin1Char('/') + name));
    for (const auto &[from, to] : edits) {
        const auto at = text.find(from);
        EXPECT_NE(at, std::string::npos) << from;
        if (at != std::string::npos)
            text.replace(at, from.size(), to);
    }
    return bytesOf(text);
}

std::string saved(const hikari::application::EditSession &session)
{
    return textOf(hikari::core::encodeAss(session.document()));
}

const hikari::core::LineRecord *lineWithText(const hikari::core::Document &document, std::u8string_view text,
                                             std::u8string_view effect)
{
    for (const auto *l : document.lines())
        if (l->text == text && l->effect == effect)
            return l;
    return nullptr;
}

void select(hikari::application::EditSession &session, const std::vector<hikari::core::LineId> &lines)
{
    session.setSelection(hikari::application::Selection{lines.front(), {lines.begin(), lines.end()}});
}

// Commits new text for a Line, as the Line editor does.
bool editText(hikari::application::EditSession &session, hikari::core::LineId line, std::u8string text)
{
    select(session, {line});
    return session.editDraftText(line, std::move(text)) && session.commitDraft();
}

constexpr char kSelected[] = "Apply karaoke template to selected lines";
constexpr char8_t kFirstT1a[] =
    u8"{\\k32}ha{\\k28}ru{\\k0} {\\k41}no{\\k0} {\\k25}ka{\\k30}ze{\\k0} {\\k45}ga{\\k0} {\\k22}fu{\\k60}ku";

TEST_F(KaraokeTemplater, CorpusCoversEveryKind)
{
    const QStringList files = corpus();
    for (const char *name : {"stock-tutorials.ass", "stock-modifiers.ass", "stock-environment.ass",
                             "stock-furigana.ass", "stock-inline-fx.ass", "km-minimal.ass", "ke4.ass",
                             "errors.ass", "late-template.ass"})
        EXPECT_TRUE(files.contains(QLatin1String(name))) << name;
}

// The acceptance lock: byte-identical output before and after KE1.
TEST_F(KaraokeTemplater, BundledTemplaterMatchesAegisub322ByteForByte)
{
    const bool update = qEnvironmentVariableIsSet("HIKARI_UPDATE_KARAOKE_GOLDENS");
    for (const QString &name : corpus()) {
        SCOPED_TRACE(name.toStdString());
        const QString document = corpusDir() + QLatin1Char('/') + name;
        const std::string reference = templated(document, pristineScript());
        ASSERT_FALSE(reference.empty());
        if (update) {
            QFile out(goldenOf(document));
            ASSERT_TRUE(out.open(QIODevice::WriteOnly));
            out.write(reference.data(), static_cast<qint64>(reference.size()));
        }
        const std::string golden = textOf(readBytes(goldenOf(document)));
        EXPECT_EQ(reference, golden) << "the unchanged 2.1.7 script no longer gives the golden output";
        const std::string bundled = templated(document, bundledScript());
        EXPECT_EQ(bundled, golden);
    }
}


// KE1-no-50-line-gate: stock 2.1.7 offers the macro only when a template is
// among the first 50 dialogue lines; HikariSub looks at every Line.
TEST_F(KaraokeTemplater, MacroIsOfferedWhereverTheTemplateIs)
{
    const auto validates = [&](const QString &script) {
        auto loaded = hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/late-template.ass")));
        hikari::application::EditSession session(std::move(loaded.document));
        auto host = load(script);
        const auto snapshot = hikari::application::snapshotForMacro(session);
        run = {};
        EXPECT_TRUE(host->validate(macro(*host, "Apply karaoke template"), *snapshot));
        EXPECT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
        return run.outcome == LuaScriptHost::RunOutcome::Ok;
    };
    EXPECT_FALSE(validates(pristineScript())) << "stock refuses a template after the first 50 Lines";
    EXPECT_TRUE(validates(bundledScript()));
}

// Code Lines alone do not offer the macro, in 2.1.7 or now: without a
// template a run generates nothing.
TEST_F(KaraokeTemplater, CodeLinesAloneDoNotOfferTheMacro)
{
    const std::string text =
        "[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
        "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
        "MarginR, MarginV, Encoding\n"
        "Style: Err,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,2,8,20,20,20,1\n\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Comment: 0,0:00:00.00,0:00:05.00,Err,,0,0,0,code once,n = 1\n"
        "Dialogue: 0,0:00:01.00,0:00:03.00,Err,,0,0,0,,{\\k50}ha{\\k50}ru\n";
    for (const QString &script : {pristineScript(), bundledScript()}) {
        hikari::application::EditSession session(hikari::core::loadAss(bytesOf(text)).document);
        auto host = load(script);
        const auto snapshot = hikari::application::snapshotForMacro(session);
        run = {};
        ASSERT_TRUE(host->validate(macro(*host, "Apply karaoke template"), *snapshot));
        ASSERT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
        EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::NotValid) << script.toStdString();
    }
}

// Without any template the macro is still not offered.
TEST_F(KaraokeTemplater, MacroIsNotOfferedWithoutATemplate)
{
    std::string text = textOf(readBytes(corpusDir() + QStringLiteral("/late-template.ass")));
    const auto at = text.find("Comment: 0,0:00:00.00,0:00:05.00,Late,,0,0,0,template syl");
    ASSERT_NE(at, std::string::npos);
    text.erase(at);
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    hikari::application::EditSession session(hikari::core::loadAss(bytes).document);
    auto host = load(bundledScript());
    const auto snapshot = hikari::application::snapshotForMacro(session);
    run = {};
    ASSERT_TRUE(host->validate(macro(*host, "Apply karaoke template"), *snapshot));
    ASSERT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::NotValid);
}


// KE1-template-problems: each problem with a template or code Line names the
// Line (its Grid number) after 2.1.7's message, and the macro returns those
// Lines as the selection, the first active. The output stays 3.2.2's.
TEST_F(KaraokeTemplater, TemplateProblemsNameAndSelectTheirLines)
{
    auto loaded = hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/errors.ass")));
    hikari::application::EditSession session(std::move(loaded.document));
    std::vector<hikari::core::LineId> ids;
    for (const auto *l : session.document().lines())
        ids.push_back(l->id);
    ASSERT_TRUE(apply(session, bundledScript(), "Apply karaoke template"));
    // Lines 1-9 each have a problem; 10 and 11 (TT#141's 3.2-era code line
    // and its template) have none.
    std::set<hikari::core::LineId> problems(ids.begin(), ids.begin() + 9);
    EXPECT_EQ(session.selection().selected, problems);
    EXPECT_EQ(session.selection().active, ids.front());
    const QString log = run.log.join(QString());
    EXPECT_TRUE(log.contains(QStringLiteral("Failed to parse Lua code: ")));
    for (int number = 1; number <= 9; ++number) {
        const QString name = (number <= 2 ? QStringLiteral("Code Line: %1\n") : QStringLiteral("Template Line: %1\n"))
                                 .arg(number);
        EXPECT_TRUE(log.contains(name)) << name.toStdString();
    }
    EXPECT_FALSE(log.contains(QStringLiteral("Line: 10\n")));
    EXPECT_TRUE(log.contains(QStringLiteral("Runtime error in template expression: ")));
    EXPECT_TRUE(log.contains(QStringLiteral("Expression producing error: nosuch.field\nTemplate with expression: "
                                            "{\\pos(!nosuch.field!,$y)}\nTemplate Line: 3\n\n")));
    EXPECT_TRUE(log.contains(QStringLiteral("Unknown variable name: nosuchvar\nIn karaoke template: "
                                            "{\\pos($x,$nosuchvar)}\nTemplate Line: 5\n\n")));
}

// A named line template joins the text of all its Lines (2.1.7); a problem
// names the Line whose text the variable or expression is in.
TEST_F(KaraokeTemplater, NamedTemplateProblemsNameTheLineTheyAreIn)
{
    const std::string text =
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\n\n[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
        "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
        "MarginR, MarginV, Encoding\n"
        "Style: Err,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,2,8,20,20,20,1\n\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Comment: 0,0:00:00.00,0:00:05.00,Err,,0,0,0,template line joined,{\\t($start,$end,\\fscx120)}\n"
        "Comment: 0,0:00:00.00,0:00:05.00,Err,,0,0,0,template line joined,{\\pos($x,$nosuchvar)}\n"
        "Comment: 0,0:00:00.00,0:00:05.00,Err,,0,0,0,template line joined,{\\fscy!nosuch.field!}\n"
        "Dialogue: 0,0:00:01.00,0:00:03.00,Err,,0,0,0,,{\\k50}ha{\\k50}ru\n";
    hikari::application::EditSession session(hikari::core::loadAss(bytesOf(text)).document);
    std::vector<hikari::core::LineId> ids;
    for (const auto *l : session.document().lines())
        ids.push_back(l->id);
    ASSERT_EQ(ids.size(), 4u);
    ASSERT_TRUE(apply(session, bundledScript(), "Apply karaoke template"));
    const QString log = run.log.join(QString());
    EXPECT_TRUE(log.contains(QStringLiteral("Unknown variable name: nosuchvar\n")));
    EXPECT_TRUE(log.contains(QStringLiteral("Expression producing error: nosuch.field\n")));
    EXPECT_TRUE(log.contains(QStringLiteral("Template Line: 2\n")));
    EXPECT_TRUE(log.contains(QStringLiteral("Template Line: 3\n")));
    EXPECT_FALSE(log.contains(QStringLiteral("Template Line: 1\n")));
    EXPECT_EQ(session.selection().selected, (std::set<hikari::core::LineId>{ids[1], ids[2]}));
    EXPECT_EQ(session.selection().active, ids[1]);
    // The output is 2.1.7's.
    EXPECT_EQ(saved(session), templatedBytes(bytesOf(text), pristineScript()));
}

// A commented fx line is not a generated line: a run keeps it, so it counts
// in the numbers of the Lines after it.
TEST_F(KaraokeTemplater, CommentedFxLinesStayAndCount)
{
    const std::string text =
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\n\n[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
        "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
        "MarginR, MarginV, Encoding\n"
        "Style: Err,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,2,8,20,20,20,1\n\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Comment: 0,0:00:00.00,0:00:05.00,Err,,0,0,0,fx,a note kept as fx\n"
        "Comment: 0,0:00:00.00,0:00:05.00,Err,,0,0,0,template syl,{\\pos($x,$nosuchvar)}\n"
        "Dialogue: 0,0:00:01.00,0:00:03.00,Err,,0,0,0,,{\\k50}ha{\\k50}ru\n";
    hikari::application::EditSession session(hikari::core::loadAss(bytesOf(text)).document);
    const auto note = session.document().lines().front()->id;
    const auto templateLine = session.document().lines()[1]->id;
    ASSERT_TRUE(apply(session, bundledScript(), "Apply karaoke template"));
    const auto &lines = session.document().lines();
    ASSERT_GE(lines.size(), 2u);
    EXPECT_EQ(lines[0]->id, note);
    EXPECT_EQ(lines[1]->id, templateLine);
    EXPECT_TRUE(run.log.join(QString()).contains(QStringLiteral("Template Line: 2\n")));
    EXPECT_EQ(session.selection().selected, (std::set<hikari::core::LineId>{templateLine}));
}

// Stock 2.1.7 names no Line and leaves the selection as it was.
TEST_F(KaraokeTemplater, StockTemplaterNamesNoLine)
{
    auto loaded = hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/errors.ass")));
    hikari::application::EditSession session(std::move(loaded.document));
    const auto last = session.document().lines().back()->id;
    session.setSelection(hikari::application::Selection{last, {last}});
    ASSERT_TRUE(apply(session, pristineScript(), "Apply karaoke template"));
    EXPECT_EQ(session.selection().selected, (std::set<hikari::core::LineId>{last}));
    EXPECT_FALSE(run.log.join(QString()).contains(QStringLiteral("Line: ")));
}

// A Document without problems keeps its selection.
TEST_F(KaraokeTemplater, CleanRunKeepsTheSelection)
{
    auto loaded = hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/stock-tutorials.ass")));
    hikari::application::EditSession session(std::move(loaded.document));
    const auto last = session.document().lines().back()->id;
    session.setSelection(hikari::application::Selection{last, {last}});
    ASSERT_TRUE(apply(session, bundledScript(), "Apply karaoke template"));
    EXPECT_EQ(session.selection().active, last);
    EXPECT_EQ(session.selection().selected, (std::set<hikari::core::LineId>{last}));
    EXPECT_TRUE(run.log.isEmpty()) << run.log.join(QString()).toStdString();
}

// KE1-regenerate-selected: a whole run marks each generated Line with the
// karaoke Line it came from (in memory only; the saved bytes stay 3.2.2's).
TEST_F(KaraokeTemplater, WholeRunMarksGeneratedLinesWithTheirKaraokeLine)
{
    hikari::application::EditSession session(
        hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/stock-tutorials.ass"))).document);
    ASSERT_TRUE(apply(session, bundledScript(), "Apply karaoke template"));
    std::map<hikari::core::LineId, const hikari::core::LineRecord *> byId;
    for (const auto *l : session.document().lines())
        byId[l->id] = l;
    std::map<hikari::core::LineId, int> generated;
    for (const auto *l : session.document().lines()) {
        if (l->effect != u8"fx") {
            EXPECT_FALSE(l->generatedFrom.value);
            continue;
        }
        ASSERT_TRUE(byId.contains(l->generatedFrom));
        const auto *source = byId[l->generatedFrom];
        EXPECT_EQ(source->effect, u8"karaoke");
        EXPECT_EQ(source->style, l->style);
        ++generated[l->generatedFrom];
    }
    EXPECT_EQ(generated.size(), 6u) << "every karaoke Line has output";
}

// With every Line selected, the selected-Lines macro gives the whole run's
// Document (km-minimal's stale generated Line is replaced where it was
// rather than at the end, so only its order differs).
TEST_F(KaraokeTemplater, SelectingEveryLineGivesTheWholeRun)
{
    for (const QString &name : corpus()) {
        SCOPED_TRACE(name.toStdString());
        hikari::application::EditSession session(
            hikari::core::loadAss(readBytes(corpusDir() + QLatin1Char('/') + name)).document);
        std::vector<hikari::core::LineId> all;
        for (const auto *l : session.document().lines())
            all.push_back(l->id);
        select(session, all);
        ASSERT_TRUE(apply(session, bundledScript(), kSelected));
        const std::string golden = textOf(readBytes(goldenOf(name)));
        if (name != QLatin1String("km-minimal.ass")) {
            EXPECT_EQ(saved(session), golden);
            continue;
        }
        const auto sortedLines = [](const std::string &text) {
            QStringList lines = QString::fromStdString(text).split(QLatin1Char('\n'));
            lines.sort();
            return lines;
        };
        EXPECT_NE(saved(session), golden);
        EXPECT_EQ(sortedLines(saved(session)), sortedLines(golden));
    }
}

// Regenerating one edited karaoke Line replaces only its generated Lines, in
// their place, as one undo step: the Document is the one a whole run on the
// edited file gives, and every other Line keeps its identity.
TEST_F(KaraokeTemplater, RegenerateSelectedReplacesOnlyItsOutputAsOneUndoStep)
{
    hikari::application::EditSession session(
        hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/stock-tutorials.ass"))).document);
    ASSERT_TRUE(apply(session, bundledScript(), "Apply karaoke template"));
    const auto *source = lineWithText(session.document(), kFirstT1a, u8"karaoke");
    ASSERT_NE(source, nullptr);
    const auto sourceId = source->id;
    std::set<hikari::core::LineId> others;
    for (const auto *l : session.document().lines())
        if (l->generatedFrom != sourceId)
            others.insert(l->id);
    std::u8string text(kFirstT1a);
    text.replace(0, 8, u8"{\\k32}HA");
    ASSERT_TRUE(editText(session, sourceId, text));
    const std::string before = saved(session);
    const std::size_t steps = session.historySize();

    select(session, {sourceId});
    ASSERT_TRUE(apply(session, bundledScript(), kSelected));
    EXPECT_EQ(saved(session), templatedBytes(editedCorpus(QStringLiteral("stock-tutorials.ass"),
                                                          {{"T1a,,0,0,0,,{\\k32}ha", "T1a,,0,0,0,,{\\k32}HA"}}),
                                             bundledScript()));
    std::set<hikari::core::LineId> now;
    for (const auto *l : session.document().lines())
        now.insert(l->id);
    for (const auto id : others)
        EXPECT_TRUE(now.contains(id));
    EXPECT_TRUE(run.log.isEmpty()) << run.log.join(QString()).toStdString();
    EXPECT_EQ(session.historySize(), steps + 1);

    ASSERT_TRUE(session.undo());
    EXPECT_EQ(saved(session), before);
}

// Selecting one of a karaoke Line's generated Lines regenerates that Line.
TEST_F(KaraokeTemplater, SelectingAGeneratedLineRegeneratesItsKaraokeLine)
{
    hikari::application::EditSession session(
        hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/stock-tutorials.ass"))).document);
    ASSERT_TRUE(apply(session, bundledScript(), "Apply karaoke template"));
    const auto sourceId = lineWithText(session.document(), kFirstT1a, u8"karaoke")->id;
    std::u8string text(kFirstT1a);
    text.replace(0, 8, u8"{\\k32}HA");
    ASSERT_TRUE(editText(session, sourceId, text));
    std::optional<hikari::core::LineId> output;
    for (const auto *l : session.document().lines())
        if (l->generatedFrom == sourceId)
            output = l->id;
    ASSERT_TRUE(output);
    select(session, {*output});
    ASSERT_TRUE(apply(session, bundledScript(), kSelected));
    EXPECT_EQ(saved(session), templatedBytes(editedCorpus(QStringLiteral("stock-tutorials.ass"),
                                                          {{"T1a,,0,0,0,,{\\k32}ha", "T1a,,0,0,0,,{\\k32}HA"}}),
                                             bundledScript()));
}

// Templates can carry state from one karaoke Line to the next (here a code
// line counter): a selected Line's output is what a whole run gives it, as
// the run goes through the Lines before it in order.
TEST_F(KaraokeTemplater, RegenerateSelectedKeepsStateCarriedAcrossLines)
{
    const std::string head = "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n\n[V4+ Styles]\n"
                             "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                             "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
                             "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                             "Style: K,Arial,30,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,"
                             "2,0,8,10,10,10,1\n\n[Events]\n"
                             "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                             "Comment: 0,0:00:00.00,0:00:00.00,K,,0,0,0,code once,n = 0\n"
                             "Comment: 0,0:00:00.00,0:00:00.00,K,,0,0,0,code line,n = n + 1\n"
                             "Comment: 0,0:00:00.00,0:00:00.00,K,,0,0,0,template line,{\\pos(320,!n*40!)}\n";
    const auto document = [&](const char *third) {
        return bytesOf(head + "Dialogue: 0,0:00:01.00,0:00:02.00,K,,0,0,0,,{\\k50}a{\\k50}b\n"
                              "Dialogue: 0,0:00:03.00,0:00:04.00,K,,0,0,0,,{\\k50}c{\\k50}d\n"
                              "Dialogue: 0,0:00:05.00,0:00:06.00,K,,0,0,0,," +
                       third + "\n");
    };
    hikari::application::EditSession session(hikari::core::loadAss(document("{\\k50}e{\\k50}f")).document);
    ASSERT_TRUE(apply(session, bundledScript(), "Apply karaoke template"));
    const auto third = lineWithText(session.document(), u8"{\\k50}e{\\k50}f", u8"karaoke")->id;
    ASSERT_TRUE(editText(session, third, u8"{\\k50}E{\\k50}F"));
    select(session, {third});
    ASSERT_TRUE(apply(session, bundledScript(), kSelected));
    const std::string whole = templatedBytes(document("{\\k50}E{\\k50}F"), bundledScript());
    EXPECT_EQ(saved(session), whole);
    EXPECT_NE(whole.find("{\\pos(320,120)}E{\\pos(320,120)}F"), std::string::npos) << whole;
}

// A Document opened from a file has no marks: the generated Lines the other
// karaoke Lines' current output accounts for stay, the rest are replaced.
// A second run then mixes marked and unmarked Lines.
TEST_F(KaraokeTemplater, RegenerateSelectedWorksOnAnOpenedFile)
{
    const std::pair<std::string, std::string> first{"T1a,,0,0,0,karaoke,{\\k32}ha", "T1a,,0,0,0,karaoke,{\\k32}HA"};
    const std::pair<std::string, std::string> second{"T2c,,0,0,0,karaoke,{\\k32}ha{\\k28}ru",
                                                     "T2c,,0,0,0,karaoke,{\\k32}ha{\\k14}r{\\k14}u"};
    hikari::application::EditSession session(
        hikari::core::loadAss(editedCorpus(QStringLiteral("stock-tutorials.out.ass"), {first})).document);
    std::u8string text(kFirstT1a);
    text.replace(0, 8, u8"{\\k32}HA");
    select(session, {lineWithText(session.document(), text, u8"karaoke")->id});
    ASSERT_TRUE(apply(session, bundledScript(), kSelected));
    const auto input = [](std::pair<std::string, std::string> edit) {
        edit.first.replace(edit.first.find("karaoke"), 7, "");
        edit.second.replace(edit.second.find("karaoke"), 7, "");
        return edit;
    };
    EXPECT_EQ(saved(session),
              templatedBytes(editedCorpus(QStringLiteral("stock-tutorials.ass"), {input(first)}), bundledScript()));

    // T2c's syl template gives one Line per syllable: one more now
    std::optional<hikari::core::LineId> t2cId;
    for (const auto *l : session.document().lines())
        if (l->style == u8"T2c" && l->effect == u8"karaoke")
            t2cId = l->id;
    ASSERT_TRUE(t2cId);
    std::u8string edited(kFirstT1a);
    edited.replace(edited.find(u8"{\\k28}ru"), 8, u8"{\\k14}r{\\k14}u");
    ASSERT_TRUE(editText(session, *t2cId, edited));
    select(session, {*t2cId});
    ASSERT_TRUE(apply(session, bundledScript(), kSelected));
    EXPECT_EQ(saved(session), templatedBytes(editedCorpus(QStringLiteral("stock-tutorials.ass"),
                                                          {input(first), input(second)}),
                                             bundledScript()));
}

// When the other karaoke Lines' generated Lines are not what the templates
// give now (here a template changed), the unmarked ones cannot be told
// apart: the run is cancelled with an explanation and changes nothing.
TEST_F(KaraokeTemplater, RegenerateSelectedRefusesWhenTheRestCannotBeMatched)
{
    hikari::application::EditSession session(
        hikari::core::loadAss(editedCorpus(QStringLiteral("stock-tutorials.out.ass"),
                                           {{"T1a,,0,0,0,template line,{\\r\\t($start,$mid,\\fscy120)",
                                             "T1a,,0,0,0,template line,{\\r\\t($start,$mid,\\fscy130)"}}))
            .document);
    std::optional<hikari::core::LineId> t2c;
    for (const auto *l : session.document().lines())
        if (l->style == u8"T2c" && l->effect == u8"karaoke")
            t2c = l->id;
    ASSERT_TRUE(t2c);
    select(session, {*t2c});
    EXPECT_EQ(outcomeOf(session, bundledScript(), kSelected), LuaScriptHost::RunOutcome::Cancelled);
    EXPECT_TRUE(run.log.join(QString()).contains(QStringLiteral("Cannot tell which generated lines")));
}

// Two karaoke Lines that generate the same lines: in a file, the identical
// generated lines belong to the karaoke Lines in order, as a run made them,
// so regenerating the second replaces the second's.
TEST_F(KaraokeTemplater, RegenerateSelectedTellsIdenticalOutputsApartByOrder)
{
    const std::string again =
        "Dialogue: 0,0:00:01.85,0:00:09.06,T1a,,0,0,0,,{\\k32}ha{\\k28}ru{\\k0} {\\k41}no{\\k0} {\\k25}ka{\\k30}ze"
        "{\\k0} {\\k45}ga{\\k0} {\\k22}fu{\\k60}ku\n";
    const std::string input = textOf(readBytes(corpusDir() + QStringLiteral("/stock-tutorials.ass")));
    ASSERT_NE(input.find(again), std::string::npos) << "the first T1a karaoke Line";
    ASSERT_TRUE(input.ends_with('\n'));
    hikari::application::EditSession session(
        hikari::core::loadAss(bytesOf(templatedBytes(bytesOf(input + again), bundledScript()))).document);
    const hikari::core::LineRecord *second = nullptr;
    for (const auto *l : session.document().lines())
        if (l->text == kFirstT1a && l->effect == u8"karaoke")
            second = l;
    ASSERT_TRUE(second);
    ASSERT_NE(second, lineWithText(session.document(), kFirstT1a, u8"karaoke"));
    std::u8string text(kFirstT1a);
    text.replace(0, 8, u8"{\\k32}HA");
    const auto secondId = second->id;
    ASSERT_TRUE(editText(session, secondId, text));
    select(session, {secondId});
    ASSERT_TRUE(apply(session, bundledScript(), kSelected));
    std::string edited = again;
    edited.replace(edited.find("{\\k32}ha"), 8, "{\\k32}HA");
    EXPECT_EQ(saved(session), templatedBytes(bytesOf(input + edited), bundledScript()));
}

// In a file, a karaoke Line that now generates fewer lines (here none: it is
// no longer a karaoke Line) leaves old generated lines nothing accounts for.
// They are the selected Lines' only when they can be: of a selected Line's
// style or its lines' now, and between the generated lines of the karaoke
// Lines around it. Otherwise the run is refused rather than deleting them.
TEST_F(KaraokeTemplater, RegenerateSelectedRefusesLinesLeftFromAnotherLine)
{
    const auto notKaraoke = [](hikari::application::EditSession &session, hikari::core::LineId line) {
        select(session, {line});
        hikari::application::DraftChange change;
        change.effect = u8"";
        return session.editDraft(line, change) && session.commitDraft();
    };
    {
        // T2c's old lines are not of T1a's style.
        hikari::application::EditSession session(
            hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/stock-tutorials.out.ass"))).document);
        std::optional<hikari::core::LineId> t2c;
        for (const auto *l : session.document().lines())
            if (l->style == u8"T2c" && l->effect == u8"karaoke")
                t2c = l->id;
        ASSERT_TRUE(t2c);
        ASSERT_TRUE(notKaraoke(session, *t2c));
        select(session, {lineWithText(session.document(), kFirstT1a, u8"karaoke")->id});
        EXPECT_EQ(outcomeOf(session, bundledScript(), kSelected), LuaScriptHost::RunOutcome::Cancelled);
        EXPECT_TRUE(run.log.join(QString()).contains(QStringLiteral("Cannot tell which generated lines")));
    }
    {
        // The first T1a Line's old lines are of the last one's style, but
        // before the other karaoke Lines' generated lines, not after them.
        const std::string again =
            "Dialogue: 0,0:00:01.85,0:00:09.06,T1a,,0,0,0,,{\\k32}ha{\\k28}ru{\\k0} {\\k41}no{\\k0} {\\k25}ka{\\k30}ze"
            "{\\k0} {\\k45}ga{\\k0} {\\k22}fu{\\k60}ku\n";
        const std::string input = textOf(readBytes(corpusDir() + QStringLiteral("/stock-tutorials.ass")));
        hikari::application::EditSession session(
            hikari::core::loadAss(bytesOf(templatedBytes(bytesOf(input + again), bundledScript()))).document);
        std::vector<hikari::core::LineId> copies;
        for (const auto *l : session.document().lines())
            if (l->text == kFirstT1a && l->effect == u8"karaoke" && l->style == u8"T1a")
                copies.push_back(l->id);
        ASSERT_EQ(copies.size(), 2u);
        ASSERT_TRUE(notKaraoke(session, copies.front()));
        select(session, {copies.back()});
        EXPECT_EQ(outcomeOf(session, bundledScript(), kSelected), LuaScriptHost::RunOutcome::Cancelled);
    }
}

// TLMode: a translated karaoke Line is templated from its translation, and
// its generated lines carry its original text as theirs. When a karaoke
// Line's original changed, its old generated lines are not what the
// templates give it now, so a run for another Line is refused.
TEST_F(KaraokeTemplater, RegenerateSelectedComparesTheOriginalToo)
{
    const auto edit = [](hikari::application::EditSession &session, hikari::core::LineId line,
                         std::u8string original, std::u8string translation) {
        select(session, {line});
        hikari::application::DraftChange change;
        change.text = std::move(original);
        change.translation = std::move(translation);
        return session.editDraft(line, change) && session.commitDraft();
    };
    const auto outcome = [&](std::u8string sourceOriginal) {
        hikari::application::EditSession session(
            hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/stock-tutorials.out.ass"))).document);
        const auto *source = lineWithText(session.document(), kFirstT1a, u8"karaoke");
        EXPECT_TRUE(source);
        if (!source)
            return std::optional<LuaScriptHost::RunOutcome>();
        // As a TLMode run leaves them: the karaoke Line translated with its
        // karaoke, each generated line's translation the generated text and
        // its original the karaoke Line's.
        std::vector<std::pair<hikari::core::LineId, std::u8string>> generated;
        std::optional<hikari::core::LineId> t2c;
        for (const auto *l : session.document().lines()) {
            if (l->style == source->style && l->effect == u8"fx" && l->start.value == source->start.value)
                generated.emplace_back(l->id, l->text);
            if (l->style == u8"T2c" && l->effect == u8"karaoke")
                t2c = l->id;
        }
        EXPECT_FALSE(generated.empty());
        EXPECT_TRUE(t2c);
        const auto sourceId = source->id;
        for (const auto &[id, text] : generated)
            EXPECT_TRUE(edit(session, id, kFirstT1a, text));
        EXPECT_TRUE(edit(session, sourceId, std::move(sourceOriginal), kFirstT1a));
        select(session, {*t2c});
        return outcomeOf(session, bundledScript(), kSelected);
    };
    EXPECT_EQ(outcome(kFirstT1a), LuaScriptHost::RunOutcome::Ok) << run.log.join(QString()).toStdString();
    std::u8string changed(kFirstT1a);
    changed.replace(0, 8, u8"{\\k32}HA");
    EXPECT_EQ(outcome(changed), LuaScriptHost::RunOutcome::Cancelled);
    EXPECT_TRUE(run.log.join(QString()).contains(QStringLiteral("Cannot tell which generated lines")));
}

// The selected-Lines macro is offered when a karaoke Line or a generated
// Line is selected, not for a selection of template Lines alone.
TEST_F(KaraokeTemplater, SelectedLinesMacroNeedsAKaraokeOrGeneratedLine)
{
    hikari::application::EditSession session(
        hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/stock-tutorials.out.ass"))).document);
    const auto validates = [&](hikari::core::LineId line) {
        select(session, {line});
        auto host = load(bundledScript());
        const auto snapshot = hikari::application::snapshotForMacro(session);
        run = {};
        EXPECT_TRUE(host->validate(macro(*host, kSelected), *snapshot));
        EXPECT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
        return run.outcome == LuaScriptHost::RunOutcome::Ok;
    };
    const auto &lines = session.document().lines();
    EXPECT_FALSE(validates(lines.front()->id)) << "a template Line";
    EXPECT_TRUE(validates(lineWithText(session.document(), kFirstT1a, u8"karaoke")->id));
    EXPECT_TRUE(validates(lines.back()->id)) << "a generated Line";
}

// Problems found in a selected-Lines run name the Lines by the numbers they
// have once the generated Lines are replaced, and select them.
TEST_F(KaraokeTemplater, SelectedRunNamesProblemLinesByTheirNumbersAfterTheRun)
{
    std::string text = textOf(readBytes(corpusDir() + QStringLiteral("/stock-tutorials.out.ass")));
    text += "Comment: 0,0:00:00.00,0:00:05.00,T2c,,0,0,0,template syl,{\\pos(!nosuch.field!,$y)}\n";
    hikari::application::EditSession session(hikari::core::loadAss(bytesOf(text)).document);
    std::optional<hikari::core::LineId> t2c;
    for (const auto *l : session.document().lines())
        if (l->style == u8"T2c" && l->effect == u8"karaoke")
            t2c = l->id;
    ASSERT_TRUE(t2c);
    const auto broken = session.document().lines().back()->id;
    const std::size_t count = session.document().lines().size();
    select(session, {*t2c});
    ASSERT_TRUE(apply(session, bundledScript(), kSelected));
    const auto &lines = session.document().lines();
    ASSERT_GT(lines.size(), count) << "the new template adds a Line per syllable";
    std::size_t number = 0;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (lines[i]->id == broken)
            number = i + 1;
    ASSERT_EQ(number, lines.size());
    EXPECT_TRUE(run.log.join(QString()).contains(QStringLiteral("Template Line: %1\n\n").arg(number)))
        << run.log.join(QString()).toStdString();
    EXPECT_EQ(session.selection().selected, (std::set<hikari::core::LineId>{broken}));
}

// KE1-template-timing: a run attaches a report: what it did, and per
// template Line its time and the Lines it generated.
TEST_F(KaraokeTemplater, WholeRunReportsWhatEachTemplateCost)
{
    hikari::application::EditSession session(
        hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/stock-tutorials.ass"))).document);
    ASSERT_TRUE(apply(session, bundledScript(), "Apply karaoke template"));
    int generated = 0;
    for (const auto *l : session.document().lines())
        generated += l->effect == u8"fx";
    EXPECT_EQ(report[QStringLiteral("report")].toString(), QStringLiteral("karaoke-templater"));
    EXPECT_EQ(report[QStringLiteral("version")].toInt(), 1);
    EXPECT_EQ(report[QStringLiteral("mode")].toString(), QStringLiteral("all"));
    EXPECT_EQ(report[QStringLiteral("karaoke_lines")].toInt(), 6);
    EXPECT_EQ(report[QStringLiteral("generated")].toInt(), generated);
    EXPECT_EQ(report[QStringLiteral("problems")].toInt(), 0);
    EXPECT_GE(report[QStringLiteral("seconds")].toDouble(), report[QStringLiteral("prepare_seconds")].toDouble());
    const QJsonArray templates = report[QStringLiteral("templates")].toArray();
    ASSERT_EQ(templates.size(), 5);
    int sum = 0;
    for (int k = 0; k < templates.size(); ++k) {
        const QJsonObject t = templates[k].toObject();
        EXPECT_EQ(t[QStringLiteral("line")].toInt(), k + 1);
        EXPECT_GE(t[QStringLiteral("seconds")].toDouble(), 0.0);
        sum += t[QStringLiteral("generated")].toInt();
    }
    EXPECT_EQ(sum, generated);
    EXPECT_EQ(templates[0].toObject()[QStringLiteral("effect")].toString(), QStringLiteral("template line"));
    EXPECT_EQ(templates[0].toObject()[QStringLiteral("style")].toString(), QStringLiteral("T1a"));
    EXPECT_EQ(templates[4].toObject()[QStringLiteral("effect")].toString(), QStringLiteral("template syl"));
    EXPECT_EQ(templates[4].toObject()[QStringLiteral("generated")].toInt(), 13) << "one Line per syllable";
}

// The report counts the problems the run named.
TEST_F(KaraokeTemplater, ReportCountsTheProblems)
{
    hikari::application::EditSession session(
        hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/errors.ass"))).document);
    ASSERT_TRUE(apply(session, bundledScript(), "Apply karaoke template"));
    EXPECT_EQ(report[QStringLiteral("problems")].toInt(), run.log.join(QString()).count(QStringLiteral(" Line: ")));
    EXPECT_GT(report[QStringLiteral("problems")].toInt(), 9);
}

// A selected-Lines run reports the selected Lines' work only, its template
// Lines numbered as they are after the run.
TEST_F(KaraokeTemplater, SelectedRunReportsTheSelectedLinesWork)
{
    std::string text = textOf(readBytes(corpusDir() + QStringLiteral("/stock-tutorials.out.ass")));
    text += "Comment: 0,0:00:00.00,0:00:05.00,T2c,,0,0,0,template syl,{\\an5\\pos($scenter,!$smiddle+40!)}\n";
    hikari::application::EditSession session(hikari::core::loadAss(bytesOf(text)).document);
    std::optional<hikari::core::LineId> t2c;
    for (const auto *l : session.document().lines())
        if (l->style == u8"T2c" && l->effect == u8"karaoke")
            t2c = l->id;
    ASSERT_TRUE(t2c);
    const auto added = session.document().lines().back()->id;
    select(session, {*t2c});
    ASSERT_TRUE(apply(session, bundledScript(), kSelected));
    int generated = 0;
    for (const auto *l : session.document().lines())
        generated += l->generatedFrom == *t2c;
    EXPECT_EQ(report[QStringLiteral("mode")].toString(), QStringLiteral("selected"));
    EXPECT_EQ(report[QStringLiteral("karaoke_lines")].toInt(), 1);
    EXPECT_EQ(report[QStringLiteral("generated")].toInt(), generated);
    EXPECT_EQ(generated, 26) << "two syl templates, 13 syllables";
    const QJsonArray templates = report[QStringLiteral("templates")].toArray();
    ASSERT_EQ(templates.size(), 6);
    const auto &lines = session.document().lines();
    const QJsonObject last = templates.last().toObject();
    EXPECT_EQ(lines[static_cast<std::size_t>(last[QStringLiteral("line")].toInt() - 1)]->id, added);
    EXPECT_EQ(last[QStringLiteral("generated")].toInt(), 13);
    EXPECT_EQ(templates[0].toObject()[QStringLiteral("generated")].toInt(), 0) << "T1a's Lines were not run";
}

// The problems of a report's run, by Line number: each one's message and
// where it is in that Line's text (start 0 for none).
struct Issue {
    QString kind, message;
    int start = 0, length = 0;
};

std::multimap<int, Issue> issuesOf(const QJsonObject &report)
{
    std::multimap<int, Issue> out;
    for (const QJsonValue v : report[QStringLiteral("issues")].toArray()) {
        const QJsonObject o = v.toObject();
        out.emplace(o[QStringLiteral("line")].toInt(),
                    Issue{o[QStringLiteral("kind")].toString(), o[QStringLiteral("message")].toString(),
                          o[QStringLiteral("start")].toInt(), o[QStringLiteral("length")].toInt()});
    }
    return out;
}

// KE2: the report lists each problem once with its Line, 2.1.7's message
// without the Line's number, and where the variable or expression is in the
// Line's text (1-based bytes), so the template editor can mark it.
TEST_F(KaraokeTemplater, ReportedProblemsSayWhereInTheLineTheyAre)
{
    hikari::application::EditSession session(
        hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/errors.ass"))).document);
    ASSERT_TRUE(apply(session, bundledScript(), "Apply karaoke template"));
    const auto issues = issuesOf(report);
    ASSERT_EQ(issues.count(3), 1u) << "one entry however many syllables ran it";
    const Issue expression = issues.find(3)->second;
    EXPECT_EQ(expression.kind, QStringLiteral("template"));
    EXPECT_TRUE(expression.message.startsWith(QStringLiteral("Runtime error in template expression: ")));
    EXPECT_TRUE(expression.message.endsWith(QStringLiteral("Template with expression: {\\pos(!nosuch.field!,$y)}")))
        << expression.message.toStdString();
    EXPECT_EQ(expression.start, 7); // {\pos(!nosuch.field!,$y)}
    EXPECT_EQ(expression.length, 14);
    ASSERT_EQ(issues.count(4), 1u);
    EXPECT_TRUE(issues.find(4)->second.message.startsWith(QStringLiteral("Error parsing expression: ")));
    EXPECT_EQ(issues.find(4)->second.start, 7); // {\fscx!1 +!}
    EXPECT_EQ(issues.find(4)->second.length, 5);
    ASSERT_EQ(issues.count(5), 1u);
    EXPECT_EQ(issues.find(5)->second.message,
              QStringLiteral("Unknown variable name: nosuchvar\nIn karaoke template: {\\pos($x,$nosuchvar)}"));
    EXPECT_EQ(issues.find(5)->second.start, 10); // {\pos($x,$nosuchvar)}
    EXPECT_EQ(issues.find(5)->second.length, 10);
    ASSERT_GE(issues.count(1), 1u);
    EXPECT_EQ(issues.find(1)->second.kind, QStringLiteral("code"));
    EXPECT_EQ(issues.find(1)->second.start, 0);
    EXPECT_GE(issues.count(6), 1u) << "an unknown modifier";
    EXPECT_EQ(issues.find(6)->second.start, 0);
    EXPECT_EQ(issues.count(10) + issues.count(11), 0u);
}

// A named line template's problem is placed in the text of the Line it is
// in, after the variables and expressions of the Lines before it.
TEST_F(KaraokeTemplater, NamedTemplateProblemsArePlacedInTheirOwnLine)
{
    const std::string text =
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\n\n[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
        "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
        "MarginR, MarginV, Encoding\n"
        "Style: Err,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,2,8,20,20,20,1\n\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Comment: 0,0:00:00.00,0:00:05.00,Err,,0,0,0,template line joined,{\\t($start,$end,\\fscx!100+20!)}\n"
        "Comment: 0,0:00:00.00,0:00:05.00,Err,,0,0,0,template line joined,{\\pos($x,$nosuchvar)}\n"
        "Comment: 0,0:00:00.00,0:00:05.00,Err,,0,0,0,template line joined,{\\fscy!nosuch.field!}\n"
        "Dialogue: 0,0:00:01.00,0:00:03.00,Err,,0,0,0,,{\\k50}ha{\\k50}ru\n";
    hikari::application::EditSession session(hikari::core::loadAss(bytesOf(text)).document);
    ASSERT_TRUE(apply(session, bundledScript(), "Apply karaoke template"));
    const auto issues = issuesOf(report);
    EXPECT_EQ(issues.count(1), 0u);
    ASSERT_EQ(issues.count(2), 1u);
    EXPECT_EQ(issues.find(2)->second.start, 10); // {\pos($x,$nosuchvar)}
    EXPECT_EQ(issues.find(2)->second.length, 10);
    ASSERT_EQ(issues.count(3), 1u);
    EXPECT_EQ(issues.find(3)->second.start, 7); // {\fscy!nosuch.field!}
    EXPECT_EQ(issues.find(3)->second.length, 14);
}

// An expression a variable's value brings in is nowhere in the template's
// text: its problem has no place, and the written one after it keeps its own.
TEST_F(KaraokeTemplater, AnExpressionFromAVariableHasNoPlaceInTheText)
{
    const std::string text =
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\n\n[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
        "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
        "MarginR, MarginV, Encoding\n"
        "Style: Err,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,2,8,20,20,20,1\n\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Comment: 0,0:00:00.00,0:00:05.00,Err,,0,0,0,template line,{$actor\\fscx!nosuch.field!}\n"
        "Dialogue: 0,0:00:01.00,0:00:03.00,Err,!nosuch.actor!,0,0,0,,{\\k50}ha{\\k50}ru\n";
    hikari::application::EditSession session(hikari::core::loadAss(bytesOf(text)).document);
    ASSERT_TRUE(apply(session, bundledScript(), "Apply karaoke template"));
    const auto issues = issuesOf(report);
    ASSERT_EQ(issues.count(1), 2u);
    std::map<QString, Issue> byExpression;
    for (auto [at, end] = issues.equal_range(1); at != end; ++at)
        byExpression[at->second.message.section(QLatin1Char('\n'), 1, 1)] = at->second;
    const Issue injected = byExpression[QStringLiteral("Expression producing error: nosuch.actor")];
    EXPECT_EQ(injected.start, 0) << "not in the template's text";
    const Issue written = byExpression[QStringLiteral("Expression producing error: nosuch.field")];
    EXPECT_EQ(written.start, 13); // {$actor\fscx!nosuch.field!}
    EXPECT_EQ(written.length, 14);
}

// A selected-Lines run numbers its problems' Lines as they are after it.
TEST_F(KaraokeTemplater, SelectedRunPlacesProblemsInTheLinesAfterTheRun)
{
    std::string text = textOf(readBytes(corpusDir() + QStringLiteral("/stock-tutorials.out.ass")));
    text += "Comment: 0,0:00:00.00,0:00:05.00,T2c,,0,0,0,template syl,{\\pos(!nosuch.field!,$y)}\n";
    hikari::application::EditSession session(hikari::core::loadAss(bytesOf(text)).document);
    std::optional<hikari::core::LineId> t2c;
    for (const auto *l : session.document().lines())
        if (l->style == u8"T2c" && l->effect == u8"karaoke")
            t2c = l->id;
    ASSERT_TRUE(t2c);
    select(session, {*t2c});
    ASSERT_TRUE(apply(session, bundledScript(), kSelected));
    const auto issues = issuesOf(report);
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues.begin()->first, static_cast<int>(session.document().lines().size()));
    EXPECT_EQ(issues.begin()->second.start, 7);
}


// The stock script attaches nothing.
TEST_F(KaraokeTemplater, StockTemplaterAttachesNoReport)
{
    hikari::application::EditSession session(
        hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/stock-tutorials.ass"))).document);
    ASSERT_TRUE(apply(session, pristineScript(), "Apply karaoke template"));
    EXPECT_TRUE(report.isEmpty());
}

} // namespace

// KA8 (#354): the Kanji timer's multi-highlight output through the bundled
// templater. A `template syl multi` effect highlights a kanji group once per
// romaji syllable sung over it; on the folded output (KA3's), once per group.
TEST_F(KaraokeTemplater, KanjiTimerHashSyllablesHighlightOncePerSyllable)
{
    using namespace hikari::application;
    auto u16 = [](std::string_view utf8) {
        return hikari::core::toUtf16(std::u8string_view(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
    };
    const auto syllables = kanjiSyllables(
        u16("{\\k15}ge{\\k16}dou {\\k13}kou{\\k9}ge{\\k7}ki, {\\k6}fu{\\k9}sho{\\k9}ku {\\k12}kou{\\k12}ge{\\k7}ki"), 0, 0);
    const auto kanji = kanjiText(u16("外道攻撃、腐蝕攻撃"));
    const std::vector<KanjiGroup> groups{{1, 1}, {1, 1}, {1, 1}, {2, 2}, {3, 1}, {1, 1}, {1, 1}, {1, 1}};
    // each fx Line's highlight ($start-$end) and the text it highlights
    const auto highlights = [&](bool multi) {
        const auto line = hikari::core::toUtf8(kanjiOutput(syllables, kanji, groups, multi));
        const std::string text =
            "[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\n\n[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
            "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
            "MarginL, MarginR, MarginV, Encoding\n"
            "Style: Kanji,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,2,8,20,20,20,"
            "1\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            "Comment: 0,0:00:00.00,0:00:05.00,Kanji,,0,0,0,template syl multi,{\\t($start,$end,\\bord5)}\n"
            "Dialogue: 0,0:00:01.00,0:00:02.15,Kanji,,0,0,0,," +
            std::string(reinterpret_cast<const char *>(line.data()), line.size()) + "\n";
        hikari::application::EditSession session(hikari::core::loadAss(bytesOf(text)).document);
        std::vector<std::string> out;
        if (!apply(session, bundledScript(), "Apply karaoke template"))
            return out;
        for (const auto *l : session.document().lines()) {
            if (l->effect != u8"fx")
                continue;
            const std::string fx(reinterpret_cast<const char *>(l->text.data()), l->text.size());
            const auto t = fx.find("\\t(");
            const auto comma = fx.find(',', t);
            const std::string shown = fx.substr(fx.rfind('}') + 1);
            if (t == std::string::npos || shown.empty())
                continue; // (the empty syllable before the first tag)
            out.push_back(fx.substr(t + 3, fx.find(',', comma + 1) - t - 3) + " " + shown);
        }
        return out;
    };
    EXPECT_EQ(highlights(true), (std::vector<std::string>{"0,150 外", "150,310 道", "310,440 攻", "440,530 撃、",
                                                          "530,600 撃、", "600,660 腐", "660,750 腐", "750,840 腐",
                                                          "840,960 蝕", "960,1080 攻", "1080,1150 撃"}));
    EXPECT_EQ(highlights(false), (std::vector<std::string>{"0,150 外", "150,310 道", "310,440 攻", "440,600 撃、",
                                                           "600,840 腐", "840,960 蝕", "960,1080 攻", "1080,1150 撃"}));
}
