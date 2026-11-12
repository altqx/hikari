#include "operations.h"

#include "hikari/application/audio_karaoke.h"
#include "hikari/application/furigana.h"
#include "hikari/application/kanji_timer.h"
#include "hikari/application/karaoke_split.h"
#include "hikari/application/karaoke_syllables.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/application/template_preview.h"
#include "hikari/application/vocal_onsets.h"
#include "hikari/core/karaoke_checks.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <span>

namespace hikari::api::detail {

namespace {

using application::AudioKaraoke;
using core::LineId;

constexpr int kMinSyllableMs = 10;

LineId lineId(const Json &id)
{
    return LineId{std::uint64_t(id.asInt())};
}

std::int64_t msOf(const core::TimeField &time)
{
    return time.value.microseconds() / 1000;
}

std::string utf8(std::u16string_view text)
{
    return narrow(core::toUtf8(text));
}

AudioKaraoke::Line karaokeLine(const core::LineRecord &line)
{
    return {core::toUtf16(line.text), int(msOf(line.start)), int(msOf(line.end))};
}

application::KaraokeCharClass classesOf(const Context &context)
{
    return context.host.karaoke ? context.host.karaoke->classes() : application::KaraokeCharClass::ascii();
}

std::unexpected<Error> noKaraokeHost()
{
    return fail(errors::kUnavailable, "The karaoke tools are not available here.");
}

std::unexpected<Error> badParam(std::string pointer, std::string why)
{
    return fail(errors::kInvalidParams, "Parameter " + pointer + ": " + why + ".",
                Json::object({{"pointer", Json(pointer)}}));
}

Json lineText(LineId id, std::u16string_view text)
{
    return Json::object({{"line", Json(std::int64_t(id.value))}, {"text", Json(utf8(text))}});
}

struct Found {
    Target target;
    const core::LineRecord *line = nullptr;
};

std::expected<Found, Error> oneLine(const Context &context, const Json &params, const char *key = "line")
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const LineId id = lineId(*params.find(key));
    const core::LineRecord *line = findLine(t->session->document(), id);
    if (!line)
        return noLine(id);
    return Found{*t, line};
}

std::expected<std::vector<const core::LineRecord *>, Error> linesOf(const core::Document &document, const Json &ids)
{
    std::vector<const core::LineRecord *> out;
    for (const Json &id : ids.asArray()) {
        const core::LineRecord *line = findLine(document, lineId(id));
        if (!line)
            return noLine(lineId(id));
        if (std::find(out.begin(), out.end(), line) == out.end())
            out.push_back(line);
    }
    return out;
}

Result syllables(const Context &context, const Json &params)
{
    auto found = oneLine(context, params);
    if (!found)
        return std::unexpected(found.error());
    const core::LineRecord &line = *found->line;
    const std::int64_t start = msOf(line.start);
    const auto all = application::karaokeSyllables(narrow(line.text), start, msOf(line.end));
    Json out = Json::array();
    for (std::size_t i = 1; i < all.size(); ++i)
        out.push(Json::object({{"text", Json(all[i].text)},
                               {"stripped", Json(all[i].stripped)},
                               {"tag", Json(all[i].tag)},
                               {"start", Json(start + all[i].start)},
                               {"end", Json(start + all[i].end)}}));
    return Json::object({{"line", Json(std::int64_t(line.id.value))}, {"syllables", std::move(out)}});
}

application::KaraokeSplitRules rulesOf(const Context &context)
{
    application::KaraokeSplitRules rules;
    if (application::Settings *settings = context.host.settings) {
        rules.everyN = settings->boolean("audio.mergeEveryNWithSyllable");
        rules.doubleConsonantApart = settings->boolean("audio.karaokeDoubleConsonantApart");
        rules.thaiClusters = settings->boolean("audio.karaokeThaiClusters");
    }
    if (context.host.karaoke)
        rules.hyphenation = context.host.karaoke->hyphenation();
    return rules;
}

Result split(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    auto lines = linesOf(t->session->document(), *params.find("lines"));
    if (!lines)
        return std::unexpected(lines.error());
    const bool syllabic = !params.find("bySyllable") || params.find("bySyllable")->asBool();
    const auto rules = rulesOf(context);
    const auto classes = classesOf(context);
    Json out = Json::array();
    for (const core::LineRecord *line : *lines) {
        AudioKaraoke kara;
        const AudioKaraoke::Line read = karaokeLine(*line);
        kara.split(read, syllabic, rules, classes);
        if (kara.count() == 0)
            continue;
        out.push(lineText(line->id, kara.text(read.startMs)));
    }
    return Json::object({{"lines", std::move(out)}});
}

Result timeSyllables(const Context &context, const Json &params)
{
    auto found = oneLine(context, params);
    if (!found)
        return std::unexpected(found.error());
    AudioKaraoke kara;
    const AudioKaraoke::Line read = karaokeLine(*found->line);
    kara.splitTags(read);
    const auto &ends = params.find("ends")->asArray();
    if (kara.count() == 0)
        return fail(errors::kRefused, "The Line has no karaoke syllables; split it first.",
                    Json::object({{"reason", Json("no_syllables")}}));
    if (ends.size() != std::size_t(kara.count()))
        return badParam("/ends", "the Line has " + std::to_string(kara.count()) + " syllables, so give " +
                                     std::to_string(kara.count()) + " ends");
    std::int64_t previous = read.startMs;
    for (std::size_t i = 0; i < ends.size(); ++i) {
        const std::int64_t end = ends[i].asInt();
        if (end < previous)
            return badParam("/ends/" + std::to_string(i), "each end must be at or after the one before and the Line's start");
        kara.times()[i] = int(end);
        previous = end;
    }
    return lineText(found->line->id, kara.text(read.startMs));
}

std::unexpected<Error> noOnsets()
{
    return fail(errors::kUnavailable, "No audio is open, or it is still loading.",
                Json::object({{"reason", Json("no_audio")}}));
}

void onsets(const Context &context, const Json &params, Reply reply)
{
    if (!context.host.karaoke)
        return reply(noKaraokeHost());
    const std::int64_t from = params.find("start")->asInt(), to = params.find("end")->asInt();
    context.host.karaoke->onsets([reply, from, to](std::optional<std::vector<int>> all) {
        if (!all)
            return reply(noOnsets());
        Json out = Json::array();
        for (const int ms : *all)
            if (ms >= from && ms <= to)
                out.push(Json(std::int64_t(ms)));
        reply(Json::object({{"onsets", std::move(out)}}));
    });
}

struct Timed {
    LineId id;
    AudioKaraoke::Line read;
};

Json snapped(const Timed &line, std::span<const int> onsets, int window)
{
    AudioKaraoke kara;
    kara.splitTags(line.read);
    if (kara.count() < 2)
        return {};
    auto &times = kara.times();
    int moved = 0;
    for (std::size_t i = 0; i + 1 < times.size(); ++i) {
        const int low = (i == 0 ? line.read.startMs : times[i - 1]) + kMinSyllableMs;
        const int high = times[i + 1] - kMinSyllableMs;
        const int to = application::snapToOnset(onsets, times[i], window, low, high);
        if (to != times[i]) {
            times[i] = to;
            ++moved;
        }
    }
    if (moved == 0)
        return {};
    Json item = lineText(line.id, kara.text(line.read.startMs));
    item["moved"] = Json(std::int64_t(moved));
    return item;
}

void snap(const Context &context, const Json &params, Reply reply)
{
    if (!context.host.karaoke)
        return reply(noKaraokeHost());
    auto t = target(context, params);
    if (!t)
        return reply(std::unexpected(t.error()));
    auto lines = linesOf(t->session->document(), *params.find("lines"));
    if (!lines)
        return reply(std::unexpected(lines.error()));
    std::vector<Timed> timed;
    for (const core::LineRecord *line : *lines)
        timed.push_back({line->id, karaokeLine(*line)});
    const int window = params.find("window") ? int(params.find("window")->asInt()) : application::kOnsetSnapWindowMs;
    context.host.karaoke->onsets([reply, timed = std::move(timed), window](std::optional<std::vector<int>> all) {
        if (!all)
            return reply(noOnsets());
        Json out = Json::array();
        for (const Timed &line : timed)
            if (Json item = snapped(line, *all, window); !item.isNull())
                out.push(std::move(item));
        reply(Json::object({{"lines", std::move(out)}}));
    });
}

Result kanji(const Context &context, const Json &params)
{
    auto romaji = oneLine(context, params, "romaji");
    if (!romaji)
        return std::unexpected(romaji.error());
    auto kanjiLine = oneLine(context, params, "kanji");
    if (!kanjiLine)
        return std::unexpected(kanjiLine.error());
    const auto read = karaokeLine(*romaji->line);
    const auto syllables = application::kanjiSyllables(read.text, read.startMs, read.endMs);
    if (syllables.empty())
        return fail(errors::kRefused, "The romaji Line has no karaoke syllables; time it first.",
                    Json::object({{"reason", Json("no_syllables")}}));
    const auto text = application::kanjiText(core::toUtf16(kanjiLine->line->text));
    const auto suggestion = application::suggestGroups(syllables, 0, text, 0, classesOf(context));
    const bool multi = params.find("multi") && params.find("multi")->asBool();
    Json out = lineText(kanjiLine->line->id, application::kanjiOutput(syllables, text, suggestion.groups, multi));
    out["clean"] = Json(suggestion.clean);
    return out;
}

std::u16string withoutBlocks(std::u16string_view text)
{
    std::u16string out;
    bool inside = false;
    for (const char16_t c : text) {
        if (c == u'{')
            inside = true;
        else if (c == u'}' && inside)
            inside = false;
        else if (!inside)
            out.push_back(c);
    }
    return out;
}

Result furigana(const Context &context, const Json &params)
{
    if (!context.host.karaoke)
        return noKaraokeHost();
    auto found = oneLine(context, params);
    if (!found)
        return std::unexpected(found.error());
    const application::ReadingAnalyzer analyze = context.host.karaoke->readings();
    if (!analyze)
        return fail(errors::kUnavailable, "No reading dictionary is installed.",
                    Json::object({{"reason", Json("no_dictionary")}}));
    const std::u16string text = withoutBlocks(core::toUtf16(found->line->text));
    Json runs = Json::array();
    for (const application::FuriganaRun &run : application::suggestReadings(text, analyze))
        runs.push(Json::object({{"base", Json(utf8(run.base))}, {"reading", Json(utf8(run.reading()))}}));
    return Json::object({{"line", Json(std::int64_t(found->line->id.value))}, {"runs", std::move(runs)}});
}

std::string_view problemName(core::KaraokeProblem::Kind kind)
{
    switch (kind) {
    case core::KaraokeProblem::Kind::Unclosed: return "unclosed";
    case core::KaraokeProblem::Kind::NotForAudioEditor: return "not_for_audio_editor";
    case core::KaraokeProblem::Kind::EditorOnly: return "editor_only";
    case core::KaraokeProblem::Kind::TotalNotDuration: return "total_not_duration";
    case core::KaraokeProblem::Kind::ZeroLength: return "zero_length";
    case core::KaraokeProblem::Kind::OutsideLine: return "outside_line";
    }
    return "unknown";
}

Result check(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const core::KaraokeCheck checked = core::checkKaraoke(t->session->document());
    Json problems = Json::array();
    for (const core::KaraokeProblem &p : checked.problems) {
        Json item = Json::object({{"line", Json(std::int64_t(p.line.value))}, {"kind", Json(problemName(p.kind))}});
        if (p.kind == core::KaraokeProblem::Kind::TotalNotDuration) {
            item["totalMs"] = Json(p.totalMs);
            item["durationMs"] = Json(p.durationMs);
        }
        if (!p.syllables.empty()) {
            Json numbers = Json::array();
            for (const std::size_t n : p.syllables)
                numbers.push(Json(std::int64_t(n)));
            item["syllables"] = std::move(numbers);
        }
        problems.push(std::move(item));
    }
    return Json::object({{"problems", std::move(problems)}, {"unlisted", Json(std::int64_t(checked.unlisted))}});
}

Json lineJson(const core::LineRecord &line)
{
    return Json::object({{"layer", Json(line.layer.value)},
                         {"start", Json(msOf(line.start))},
                         {"end", Json(msOf(line.end))},
                         {"style", Json(narrow(line.style))},
                         {"effect", Json(narrow(line.effect))},
                         {"text", Json(narrow(line.text))}});
}

std::unexpected<Error> noTemplater()
{
    return fail(errors::kUnavailable, "No karaoke templater is loaded.", Json::object({{"reason", Json("no_templater")}}));
}

std::unexpected<Error> templaterFailed(const std::string &error)
{
    return fail(errors::kRefused, "The templater stopped: " + (error.empty() ? std::string("it gave no result") : error),
                Json::object({{"reason", Json("templater_failed")}}));
}

void templatePreview(const Context &context, const Json &params, Reply reply)
{
    application::KaraokeTemplaterPort *templater = context.host.karaoke ? context.host.karaoke->templater() : nullptr;
    if (!templater || !templater->available())
        return reply(noTemplater());
    auto found = oneLine(context, params);
    if (!found)
        return reply(std::unexpected(found.error()));
    const LineId source = found->line->id;
    auto snapshot = application::templatePreviewSnapshot(found->target.session->document(), source);
    if (!snapshot)
        return reply(noLine(source));
    templater->preview(*snapshot, [reply, source](application::KaraokeTemplaterPort::Preview preview) {
        if (!preview.result)
            return reply(templaterFailed(preview.error));
        Json lines = Json::array();
        for (const core::LineRecord &line : application::templatePreviewLines(*preview.result, source))
            lines.push(lineJson(line));
        reply(Json::object({{"line", Json(std::int64_t(source.value))}, {"lines", std::move(lines)}}));
    });
}

void applyTemplate(const Context &context, const Json &params, Reply reply)
{
    application::KaraokeTemplaterPort *templater = context.host.karaoke ? context.host.karaoke->templater() : nullptr;
    if (!templater || !templater->available())
        return reply(noTemplater());
    auto t = target(context, params);
    if (!t)
        return reply(std::unexpected(t.error()));
    application::EditSession &session = *t->session;
    if (session.isProtected() || session.isReadOnly() || context.host.documents->workspace().reference() == t->id)
        return reply(fail(errors::kReadOnly, "The Document cannot be changed now."));
    const std::uint64_t revision = std::uint64_t(params.find("revision")->asInt());
    if (revision != session.revision())
        return reply(fail(errors::kStaleRevision, "The Document changed since that revision; read it again.",
                          Json::object({{"current", Json(session.revision())}})));
    const core::Document &document = session.document();
    application::Selection selection;
    if (const Json *ids = params.find("lines")) {
        auto lines = linesOf(document, *ids);
        if (!lines)
            return reply(std::unexpected(lines.error()));
        for (const core::LineRecord *line : *lines)
            selection.selected.insert(line->id);
    } else {
        for (const core::LineRecord *line : document.lines())
            if (application::isKaraokeLine(*line))
                selection.selected.insert(line->id);
    }
    if (selection.selected.empty())
        return reply(fail(errors::kRefused, "There are no karaoke Lines to apply the templates to.",
                          Json::object({{"reason", Json("no_karaoke_lines")}})));
    selection.active = *selection.selected.begin();
    application::MacroSnapshot snapshot = application::macroSnapshotOf(document, selection);
    snapshot.revision = revision;
    const std::string name = params.find("name") ? params.find("name")->asString() : "Apply karaoke template";
    templater->preview(snapshot, [host = context.host, caller = context.caller, document = t->id, snapshot, name,
                                  reply](application::KaraokeTemplaterPort::Preview preview) {
        if (!preview.result)
            return reply(templaterFailed(preview.error));
        application::EditSession *session = host.documents ? host.documents->session(document) : nullptr;
        if (!session)
            return reply(fail(errors::kNotFound, "The Document was closed."));
        application::AiReview *review = caller.reviewed ? host.review : nullptr;
        std::optional<core::Document> before;
        if (review)
            before = session->document();
        const application::ContentId previous = session->contentId();
        const std::uint64_t was = session->revision();
        const std::string label = caller.label.empty() ? name : caller.label + ": " + name;
        if (auto applied = application::applyMacroResult(*session, snapshot, *preview.result, label); !applied)
            return reply(fail(errors::kStaleRevision, "The Document changed while the templater ran; read it again.",
                              Json::object({{"current", Json(session->revision())}})));
        const bool changed = session->revision() != was;
        if (changed) {
            if (review)
                review->record(document, caller.id, caller.label, name, *before, previous, *session);
            host.documents->committed(document);
        }
        reply(Json::object({{"revision", Json(session->revision())}, {"changed", Json(changed)}}));
    });
}

void align(const Context &context, const Json &params, Reply reply)
{
    if (!context.host.karaoke)
        return reply(noKaraokeHost());
    auto t = target(context, params);
    if (!t)
        return reply(std::unexpected(t.error()));
    auto lines = linesOf(t->session->document(), *params.find("lines"));
    if (!lines)
        return reply(std::unexpected(lines.error()));
    AlignRequest request;
    request.program = params.find("program")->asString();
    request.startMs = std::numeric_limits<std::int64_t>::max();
    request.endMs = 0;
    for (const core::LineRecord *line : *lines) {
        request.startMs = std::min(request.startMs, msOf(line->start));
        request.endMs = std::max(request.endMs, msOf(line->end));
        const std::u16string words = withoutBlocks(core::toUtf16(line->text));
        request.text += (request.text.empty() ? "" : "\n") + utf8(words);
    }
    if (request.endMs <= request.startMs)
        return reply(badParam("/lines", "the Lines last no time"));
    for (const char *key : {"language", "dictionary", "acousticModel"})
        if (const Json *value = params.find(key))
            (key == std::string_view("language")     ? request.language
             : key == std::string_view("dictionary") ? request.dictionary
                                                     : request.acousticModel) = value->asString();
    for (const auto &[pointer, value] : {std::pair<const char *, const std::string &>{"/language", request.language},
                                          {"/dictionary", request.dictionary}, {"/acousticModel", request.acousticModel}})
        if (value.starts_with('-'))
            return reply(badParam(pointer, "must not start with -"));
    if (request.program == "mfa" && (request.dictionary.empty() || request.acousticModel.empty()))
        return reply(badParam("/dictionary", "MFA needs dictionary and acousticModel, such as english_mfa"));
    context.host.karaoke->align(request, [reply](AlignResult result) {
        if (result.outcome != "aligned")
            return reply(fail(result.outcome == "failed" ? errors::kRefused : errors::kUnavailable,
                              result.message, Json::object({{"reason", Json(result.outcome)}})));
        Json words = Json::array();
        for (const AlignedWord &word : result.words)
            words.push(Json::object({{"text", Json(word.text)}, {"start", Json(word.startMs)}, {"end", Json(word.endMs)}}));
        reply(Json::object({{"words", std::move(words)}}));
    });
}

constexpr std::string_view kDocument = R"("document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."})";

constexpr std::string_view kLineText = R"({"type": "object", "properties": {"line": {"$ref": "#/$defs/LineId"},
    "text": {"type": "string", "description": "The Line's text with the \\k tags written; nothing is applied."}},
    "required": ["line", "text"], "additionalProperties": false})";

std::string params(std::string_view body, std::string_view required)
{
    return R"({"type": "object", "properties": {)" + std::string(kDocument) +
           (body.empty() ? "" : ", " + std::string(body)) + R"(}, "required": [)" + std::string(required) +
           R"(], "additionalProperties": false})";
}

std::string linesResult(std::string_view extra = {})
{
    return R"({"type": "object", "properties": {"lines": {"type": "array", "items": {"type": "object", "properties": {
        "line": {"$ref": "#/$defs/LineId"}, "text": {"type": "string"})" +
           std::string(extra) + R"(}, "required": ["line", "text"], "additionalProperties": false},
        "description": "The Lines that change, with their new text; nothing is applied."}},
        "required": ["lines"], "additionalProperties": false})";
}

constexpr std::string_view kLines = R"("lines": {"type": "array", "minItems": 1, "maxItems": 1000, "items": {"$ref": "#/$defs/LineId"}})";

void add(Registry &registry, Access access, std::string name, std::string summary, const std::string &paramSchema,
         const std::string &result, Handler handler)
{
    addOperation(registry, access, std::move(name), std::move(summary), paramSchema, result, std::move(handler));
}

}

void addKaraokeOperations(Registry &registry)
{
    add(registry, Access::Read, "karaoke.syllables",
        "A Line's karaoke syllables as the templater reads them: text, tag, start and end in ms.",
        params(R"("line": {"$ref": "#/$defs/LineId"})", R"("line")"),
        R"({"type": "object", "properties": {"line": {"$ref": "#/$defs/LineId"}, "syllables": {"type": "array",
            "items": {"type": "object", "properties": {"text": {"type": "string"}, "stripped": {"type": "string"},
            "tag": {"type": "string"}, "start": {"type": "integer"}, "end": {"type": "integer"}},
            "required": ["text", "stripped", "tag", "start", "end"], "additionalProperties": false}}},
            "required": ["line", "syllables"], "additionalProperties": false})",
        now(syllables));
    add(registry, Access::Read, "karaoke.split",
        "Splits Lines without \\k tags into syllables as the audio editor's karaoke mode does, the time shared by "
        "character; Lines that already have \\k tags keep them.",
        params(std::string(kLines) + R"(, "bySyllable": {"type": "boolean", "description": "Default true: by syllable, with the language rules the user set; false: at spaces."})",
               R"("lines")"),
        linesResult(), now(split));
    add(registry, Access::Read, "karaoke.time",
        "Times a Line's existing syllables: each syllable's end in ms, in order; the \\k tags are rewritten.",
        params(R"("line": {"$ref": "#/$defs/LineId"}, "ends": {"type": "array", "minItems": 1, "maxItems": 4096,
            "items": {"type": "integer", "minimum": 0}, "description": "One per syllable, ascending, in ms from the video's start."})",
               R"("line", "ends")"),
        kLineText.data(), now(timeSyllables));
    add(registry, Access::Read, "karaoke.onsets", "Where sung notes start in the open audio, in ms, within a range.",
        R"({"type": "object", "properties": {"start": {"type": "integer", "minimum": 0}, "end": {"type": "integer", "minimum": 0}},
            "required": ["start", "end"], "additionalProperties": false})",
        R"({"type": "object", "properties": {"onsets": {"type": "array", "items": {"type": "integer"}}},
            "required": ["onsets"], "additionalProperties": false})",
        onsets);
    add(registry, Access::Read, "karaoke.snap",
        "Moves each syllable boundary inside the Lines to the nearest vocal onset within the window, keeping every "
        "syllable at least 10 ms long.",
        params(std::string(kLines) + R"(, "window": {"type": "integer", "minimum": 1, "maximum": 1000, "description": "Ms either side; default 100."})",
               R"("lines")"),
        linesResult(R"(, "moved": {"type": "integer", "description": "Boundaries moved."})"), snap);
    add(registry, Access::Read, "karaoke.kanji",
        "Copies a timed romaji Line's syllables onto its kanji Line as the Kanji timer suggests the groups.",
        params(R"("romaji": {"$ref": "#/$defs/LineId"}, "kanji": {"$ref": "#/$defs/LineId"},
            "multi": {"type": "boolean", "description": "Further syllables of a group become # syllables instead of joining the first."})",
               R"("romaji", "kanji")"),
        R"({"type": "object", "properties": {"line": {"$ref": "#/$defs/LineId"}, "text": {"type": "string"},
            "clean": {"type": "boolean", "description": "False when a group came out blank or the characters ran out: check it."}},
            "required": ["line", "text", "clean"], "additionalProperties": false})",
        now(kanji));
    add(registry, Access::Read, "karaoke.furigana", "The readings the dictionary suggests for a Line's kanji runs, in hiragana.",
        params(R"("line": {"$ref": "#/$defs/LineId"})", R"("line")"),
        R"({"type": "object", "properties": {"line": {"$ref": "#/$defs/LineId"}, "runs": {"type": "array",
            "items": {"type": "object", "properties": {"base": {"type": "string"}, "reading": {"type": "string"}},
            "required": ["base", "reading"], "additionalProperties": false}}},
            "required": ["line", "runs"], "additionalProperties": false})",
        now(furigana));
    add(registry, Access::Read, "karaoke.check",
        "The Karaoke check's problems: unclosed tags, totals that differ from the Line, empty syllables and syllables "
        "outside the Line.",
        params({}, {}),
        R"({"type": "object", "properties": {"problems": {"type": "array", "items": {"type": "object", "properties": {
            "line": {"$ref": "#/$defs/LineId"},
            "kind": {"enum": ["unclosed", "not_for_audio_editor", "editor_only", "total_not_duration", "zero_length", "outside_line"]},
            "totalMs": {"type": "integer"}, "durationMs": {"type": "integer"},
            "syllables": {"type": "array", "items": {"type": "integer"}, "description": "Which ones, from 1."}},
            "required": ["line", "kind"], "additionalProperties": false}},
            "unlisted": {"type": "integer", "description": "Problems past the listed ones."}},
            "required": ["problems", "unlisted"], "additionalProperties": false})",
        now(check));
    add(registry, Access::Read, "karaoke.template_preview",
        "The Lines the karaoke templater generates from one karaoke Line, without applying them.",
        params(R"("line": {"$ref": "#/$defs/LineId"})", R"("line")"),
        R"({"type": "object", "properties": {"line": {"$ref": "#/$defs/LineId"}, "lines": {"type": "array",
            "items": {"type": "object", "properties": {"layer": {"type": "integer"}, "start": {"type": "integer"},
            "end": {"type": "integer"}, "style": {"type": "string"}, "effect": {"type": "string"}, "text": {"type": "string"}},
            "required": ["layer", "start", "end", "style", "effect", "text"], "additionalProperties": false}}},
            "required": ["line", "lines"], "additionalProperties": false})",
        templatePreview);
    add(registry, Access::Write, "karaoke.apply_template",
        "Runs the karaoke templater on the karaoke Lines, or the Lines given, and applies what it generates as one "
        "undo step: old generated Lines are replaced.",
        params(R"("revision": {"$ref": "#/$defs/Revision"}, "lines": {"type": "array", "minItems": 1, "maxItems": 100000,
            "items": {"$ref": "#/$defs/LineId"}, "description": "Default: every karaoke Line."},
            "name": {"type": "string", "minLength": 1, "maxLength": 200, "description": "Default \"Apply karaoke template\"."})",
               R"("revision")"),
        R"({"type": "object", "properties": {"revision": {"$ref": "#/$defs/Revision"}, "changed": {"type": "boolean"}},
            "required": ["revision", "changed"], "additionalProperties": false})",
        applyTemplate);
    add(registry, Access::Write, "karaoke.align",
        "Runs the user's own WhisperX or Montreal Forced Aligner on the audio under the Lines and answers each word "
        "with its start and end in ms; the user approves every run, and nothing is applied.",
        params(std::string(kLines) + R"(, "program": {"enum": ["whisperx", "mfa"]},
            "language": {"type": "string", "maxLength": 32, "description": "WhisperX: a language code such as ja; default: it detects."},
            "dictionary": {"type": "string", "maxLength": 200, "description": "MFA: the pronunciation dictionary, such as english_mfa."},
            "acousticModel": {"type": "string", "maxLength": 200, "description": "MFA: the acoustic model, such as english_mfa."})",
               R"("lines", "program")"),
        R"({"type": "object", "properties": {"words": {"type": "array", "items": {"type": "object", "properties": {
            "text": {"type": "string"}, "start": {"type": "integer"}, "end": {"type": "integer"}},
            "required": ["text", "start", "end"], "additionalProperties": false}}},
            "required": ["words"], "additionalProperties": false})",
        align);
}

}
