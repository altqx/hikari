#include "hikari/application/export_steps.h"

#include "hikari/application/automation_registry.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/application/tag_removal.h"
#include "hikari/application/timecodes_file.h"
#include "hikari/core/style.h"
#include "hikari/core/tag_cleanup.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>
#include <string_view>

namespace hikari::application {

namespace {

constexpr char kLuaPrefix[] = "lua:";
constexpr char kKaraokeTemplateFilter[] = "Karaoke template";

std::vector<AvailableStep> builtInSteps()
{
    return {{kKaraokeTemplateStep, "Apply karaoke template", kKaraokeTemplateFilter, 2000, {}, false},
            {kTransformFrameRateStep, "Transform frame rate", "Transform Framerate", 1000, {}, true},
            {kRemoveTagsStep, "Remove tags", "Clean Tags", 0, {}, true},
            {kFixStylesStep, "Set unknown styles to Default", "Fix Styles", -5000, {}, false}};
}

std::string asciiLower(std::u8string_view text)
{
    std::string out(text.size(), '\0');
    std::transform(text.begin(), text.end(), out.begin(),
                   [](char8_t c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return out;
}

template <class T> std::optional<T> settingAs(const ExportStepSettings &settings, const std::string &name)
{
    const auto it = settings.find(name);
    if (it == settings.end())
        return std::nullopt;
    if (const auto *v = std::get_if<T>(&it->second))
        return *v;
    if constexpr (std::is_same_v<T, double>)
        if (const auto *i = std::get_if<int>(&it->second))
            return static_cast<double>(*i);
    return std::nullopt;
}

core::tagcleanup::Options removeTagsOptions(const ExportStepSettings &settings)
{
    namespace tc = core::tagcleanup;
    tc::Options options;
    const std::string families = settingAs<std::string>(settings, "families").value_or("");
    for (std::size_t p = 0; p < families.size();) {
        const auto comma = std::min(families.find(',', p), families.size());
        const int f = std::atoi(families.substr(p, comma - p).c_str());
        if (comma > p && f >= 0 && static_cast<std::size_t>(f) < tc::kFamilyCount)
            options.families.set(static_cast<std::size_t>(f));
        p = comma + 1;
    }
    const std::string scope = settingAs<std::string>(settings, "scope").value_or("all");
    options.scope = scope == "start" ? tc::BlockScope::Start : scope == "inline" ? tc::BlockScope::Inline : tc::BlockScope::All;
    const auto flag = [&](const char *name) { return settingAs<bool>(settings, name).value_or(false); };
    options.cleanup = {flag("removeComments"), flag("mergeBlocks"), flag("dropRedundant"), flag("cleanTags"),
                       flag("deleteEmptyLines")};
    return options;
}

class FrameRateTransform {
public:
    explicit FrameRateTransform(const FrameRateChange &change) : m_in(change.input), m_out(change.output) {}

    void line(core::LineRecord &line)
    {
        m_oldStart = line.start.value.microseconds() / 1000;
        m_oldEnd = line.end.value.microseconds() / 1000;
        m_oldK = m_newK = 0;
        m_newStart = truncCs(convert(m_oldStart));
        m_newEnd = truncCs(convert(m_oldEnd) + 9);
        line.text = blocks(line.text);
        line.start.value = core::DocumentTime(m_newStart * 1000);
        line.end.value = core::DocumentTime(m_newEnd * 1000);
    }

private:
    enum class Class { Other, Start, End, Karaoke };

    static std::int64_t truncCs(std::int64_t ms) { return ms / 10 * 10; }

    std::int64_t convert(std::int64_t ms) const
    {
        const std::int64_t frame = m_out.frameAt(ms);
        const std::int64_t frameStart = m_out.startOf(frame);
        const std::int64_t frameDur = m_out.startOf(frame + 1) - frameStart;
        const double dist = static_cast<double>(ms - frameStart) / static_cast<double>(frameDur);
        const std::int64_t newStart = m_in.startOf(frame);
        const std::int64_t newDur = m_in.startOf(frame + 1) - newStart;
        return static_cast<std::int64_t>(static_cast<double>(newStart) + static_cast<double>(newDur) * dist);
    }

    std::int64_t value(Class kind, std::int64_t param)
    {
        switch (kind) {
        case Class::Start: {
            const std::int64_t v = convert(truncCs(m_oldStart) + param) - m_newStart;
            return v == 0 && param != 0 ? 1 : v;
        }
        case Class::End:
            return m_newEnd - convert(truncCs(m_oldEnd) - param);
        case Class::Karaoke: {
            const std::int64_t start = m_oldStart / 10 + m_oldK + param;
            const std::int64_t v = (convert(start * 10) - m_newStart) / 10 - m_newK;
            m_oldK += param;
            m_newK += v;
            return v;
        }
        case Class::Other:
            break;
        }
        return param;
    }

    static std::int64_t atoiOf(std::u8string_view token)
    {
        return std::atoll(std::string(token.begin(), token.end()).c_str());
    }

    static std::u8string text(std::int64_t v)
    {
        const std::string s = std::to_string(v);
        return {s.begin(), s.end()};
    }

    // Each parameter's trimmed span; commas inside parentheses don't split.
    static std::vector<std::pair<std::size_t, std::size_t>> params(std::u8string_view args)
    {
        std::vector<std::pair<std::size_t, std::size_t>> out;
        int depth = 0;
        std::size_t from = 0;
        const auto push = [&](std::size_t to) {
            std::size_t a = from, b = to;
            while (a < b && (args[a] == u8' ' || args[a] == u8'\t'))
                ++a;
            while (b > a && (args[b - 1] == u8' ' || args[b - 1] == u8'\t'))
                --b;
            out.emplace_back(a, b - a);
        };
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (args[i] == u8'(')
                ++depth;
            else if (args[i] == u8')')
                --depth;
            else if (args[i] == u8',' && depth == 0) {
                push(i);
                from = i + 1;
            }
        }
        push(args.size());
        return out;
    }

    static std::vector<Class> classes(std::u8string_view tag, std::size_t count)
    {
        using enum Class;
        if (tag == u8"move")
            return count >= 6 ? std::vector{Other, Other, Other, Other, Start, Start} : std::vector<Class>{};
        if (tag == u8"fad")
            return {Start, End};
        if (tag == u8"fade")
            return {Other, Other, Other, Start, Start, Start, Start};
        if (tag == u8"t")
            return count == 3 || count == 4 ? std::vector{Start, Start} : std::vector<Class>{};
        return {};
    }

    std::u8string block(std::u8string_view body)
    {
        std::u8string out;
        std::size_t i = 0;
        while (i < body.size()) {
            if (body[i] != u8'\\') {
                out.push_back(body[i++]);
                continue;
            }
            const bool transform = body.substr(i + 1).starts_with(u8"t(");
            std::size_t end = i + 1;
            int depth = 0;
            while (end < body.size() && (depth > 0 || body[end] != u8'\\')) {
                if (transform)
                    depth += body[end] == u8'(' ? 1 : body[end] == u8')' ? -1 : 0;
                ++end;
            }
            out += tag(body.substr(i, end - i));
            i = end;
        }
        return out;
    }

    std::u8string tag(std::u8string_view tag)
    {
        const std::u8string_view name = tag.substr(1);
        for (const std::u8string_view karaoke : {u8"kf", u8"ko", u8"k", u8"K"}) {
            if (!name.starts_with(karaoke))
                continue;
            const std::u8string_view rest = name.substr(karaoke.size());
            if (karaoke == u8"k" && !rest.empty() &&
                !(std::isdigit(static_cast<unsigned char>(rest[0])) || rest[0] == u8'-' || rest[0] == u8' '))
                break;
            std::u8string out = u8"\\" + std::u8string(karaoke);
            return out + text(value(Class::Karaoke, atoiOf(rest)));
        }
        for (const std::u8string_view timed : {u8"move", u8"fade", u8"fad", u8"t"}) {
            if (!name.starts_with(timed) || name.size() <= timed.size() || name[timed.size()] != u8'(')
                continue;
            const std::size_t open = timed.size() + 1;
            std::size_t close = name.size();
            int depth = 0;
            for (std::size_t k = open; k < name.size(); ++k) {
                if (name[k] == u8'(')
                    ++depth;
                else if (name[k] == u8')' && depth-- == 0) {
                    close = k;
                    break;
                }
            }
            const std::u8string_view args = name.substr(open, close - open);
            const auto spans = params(args);
            const auto kinds = classes(timed, spans.size());
            std::u8string out;
            std::size_t copied = 0;
            for (std::size_t k = 0; k < kinds.size() && k < spans.size(); ++k) {
                if (kinds[k] == Class::Other || spans[k].second == 0)
                    continue;
                out += args.substr(copied, spans[k].first - copied);
                out += text(value(kinds[k], atoiOf(args.substr(spans[k].first, spans[k].second))));
                copied = spans[k].first + spans[k].second;
            }
            out += args.substr(copied);
            return u8"\\" + std::u8string(timed) + u8"(" + out + std::u8string(name.substr(close));
        }
        return std::u8string(tag);
    }

    std::u8string blocks(std::u8string_view line)
    {
        std::u8string out;
        std::size_t i = 0;
        while (i < line.size()) {
            const auto open = line.find(u8'{', i);
            if (open == std::u8string_view::npos) {
                out += line.substr(i);
                break;
            }
            const auto close = line.find(u8'}', open);
            out += line.substr(i, open + 1 - i);
            const std::size_t end = close == std::u8string_view::npos ? line.size() : close;
            out += block(line.substr(open + 1, end - open - 1));
            i = end;
        }
        return out;
    }

    const AegisubFrameTimes &m_in, &m_out;
    std::int64_t m_oldStart = 0, m_oldEnd = 0, m_newStart = 0, m_newEnd = 0, m_oldK = 0, m_newK = 0;
};

using State = ExportStepOutcome::State;
using Problem = ExportStepOutcome::Problem;

struct StepsRun : std::enable_shared_from_this<StepsRun> {
    core::Document document;
    std::vector<ExportStep> steps;
    std::vector<AvailableStep> available;
    ExportStepsContext context;
    std::function<void(ExportStepsResult)> done;
    std::vector<ExportStepOutcome> outcomes;
    std::size_t next = 0;
    bool stopping = false;
    bool filterRunning = false;

    void finish(bool failed)
    {
        for (std::size_t k = next; k < steps.size(); ++k)
            outcomes.push_back({steps[k].id, nameOf(steps[k].id), State::NotRun});
        auto callback = std::move(done);
        callback({std::move(document), std::move(outcomes), failed, stopping && !failed});
    }

    void stop()
    {
        stopping = true;
        if (filterRunning && context.filters)
            context.filters->stop();
    }

    std::string nameOf(const std::string &id) const
    {
        if (const auto *step = findStep(available, id))
            return step->name;
        const auto parts = luaStepParts(id);
        return parts ? parts->second : id;
    }

    void record(ExportStepOutcome outcome)
    {
        const bool failed = outcome.state == State::Failed;
        outcomes.push_back(std::move(outcome));
        ++next;
        if (failed)
            return finish(true);
        advance();
    }

    void advance()
    {
        if (next >= steps.size() || stopping)
            return finish(false);
        const ExportStep &step = steps[next];
        ExportStepOutcome outcome{step.id, nameOf(step.id), State::Done};
        if (context.started)
            context.started(next, steps.size(), outcome.name);
        if (step.id == kFixStylesStep) {
            outcome.lines = fixUnknownStyles(document);
            return record(std::move(outcome));
        }
        if (step.id == kTransformFrameRateStep)
            return record(transform(step, std::move(outcome)));
        if (step.id == kRemoveTagsStep) {
            EditSession session(document);
            const auto removed = removeTags(session, removeTagsOptions(step.settings), TagRemovalLines::File);
            outcome.lines = removed ? *removed : 0;
            document = session.document();
            return record(std::move(outcome));
        }
        runFilter(step, std::move(outcome));
    }

    ExportStepOutcome transform(const ExportStep &step, ExportStepOutcome outcome)
    {
        const bool constant = settingAs<std::string>(step.settings, "output").value_or("variable") == "constant";
        auto input = constantFrameTimes(settingAs<double>(step.settings, "input").value_or(0));
        auto output = constant ? constantFrameTimes(settingAs<double>(step.settings, "outputFps").value_or(0))
                               : context.timecodes;
        if (!input || !output) {
            outcome.state = State::Failed;
            outcome.problem = !input || constant ? Problem::BadFrameRate : Problem::NoTimecodes;
            return outcome;
        }
        if (settingAs<bool>(step.settings, "reverse").value_or(false))
            std::swap(input, output);
        outcome.lines = transformFrameRate(document, {*input, *output});
        return outcome;
    }

    void runFilter(const ExportStep &step, ExportStepOutcome outcome)
    {
        const AvailableStep *available = findStep(this->available, step.id);
        std::string script, filter;
        if (step.id == kKaraokeTemplateStep) {
            script = available ? available->script : std::string();
            filter = kKaraokeTemplateFilter;
        } else if (const auto parts = luaStepParts(step.id)) {
            std::tie(script, filter) = *parts;
        }
        if (script.empty() || !context.filters || !context.filters->available(script, filter)) {
            outcome.state = State::Skipped;
            return record(std::move(outcome));
        }
        auto session = std::make_shared<EditSession>(document);
        const auto snapshot = snapshotForMacro(*session);
        if (!snapshot) {
            outcome.state = State::Failed;
            outcome.problem = Problem::NotApplied;
            return record(std::move(outcome));
        }
        auto self = shared_from_this();
        const bool karaoke = step.id == kKaraokeTemplateStep;
        filterRunning = true;
        const bool started = context.filters->run(
            script, filter, *snapshot, settingsDialog(step.settings),
            [self, session, snapshot = *snapshot, outcome, karaoke](std::expected<MacroResult, std::string> result) mutable {
                self->filterRunning = false;
                if (!result && self->stopping) {
                    outcome.state = State::Stopped;
                    return self->record(std::move(outcome));
                }
                if (!result) {
                    outcome.state = State::Failed;
                    outcome.problem = Problem::ScriptError;
                    outcome.error = std::move(result.error());
                    return self->record(std::move(outcome));
                }
                if (karaoke)
                    outcome.lines = static_cast<int>(std::ranges::count_if(
                        result->dialogues, [](const MacroDialogueLine &d) { return d.id == 0; }));
                if (!applyMacroResult(*session, snapshot, *result, outcome.name)) {
                    outcome.state = State::Failed;
                    outcome.problem = Problem::NotApplied;
                    return self->record(std::move(outcome));
                }
                self->document = session->document();
                self->record(std::move(outcome));
            });
        if (!started) {
            filterRunning = false;
            outcome.state = State::Failed;
            outcome.problem = Problem::Busy;
            record(std::move(outcome));
        }
    }
};

}

std::string luaStepId(const std::string &scriptFile, const std::string &filterName)
{
    return kLuaPrefix + scriptFile + "/" + filterName;
}

std::optional<std::pair<std::string, std::string>> luaStepParts(const std::string &id)
{
    if (!id.starts_with(kLuaPrefix))
        return std::nullopt;
    const std::string rest = id.substr(std::string_view(kLuaPrefix).size());
    const auto slash = rest.find('/');
    if (slash == std::string::npos)
        return std::nullopt;
    return std::pair{rest.substr(0, slash), rest.substr(slash + 1)};
}

std::vector<AvailableStep> availableSteps(const std::vector<ScriptStatus> &scripts)
{
    std::vector<AvailableStep> steps = builtInSteps();
    const auto builtIns = static_cast<std::ptrdiff_t>(steps.size());
    std::set<std::string> ids;
    for (const auto &script : scripts) {
        if (script.state != ScriptStatus::State::Ready && script.state != ScriptStatus::State::Running)
            continue;
        const std::string file = scriptFileName(script.path);
        for (const auto &filter : script.info.filters) {
            const auto builtIn =
                std::find_if(steps.begin(), steps.begin() + builtIns, [&](const AvailableStep &step) {
                    return step.aegisubName == filter.name;
                });
            if (builtIn != steps.begin() + builtIns) {
                if (builtIn->id == kKaraokeTemplateStep && builtIn->script.empty())
                    builtIn->script = file;
                continue;
            }
            const std::string id = luaStepId(file, filter.name);
            if (!ids.insert(id).second)
                continue;
            steps.push_back({id, filter.name, filter.name, filter.priority, file, filter.hasConfig});
        }
    }
    std::ranges::stable_sort(steps, std::greater<>(), &AvailableStep::priority);
    return steps;
}

const AvailableStep *findStep(const std::vector<AvailableStep> &available, const std::string &id)
{
    const auto it = std::ranges::find(available, id, &AvailableStep::id);
    return it == available.end() ? nullptr : &*it;
}

ExportStepSettings defaultSettings(const std::string &id)
{
    if (id == kRemoveTagsStep)
        return {{"cleanTags", true}, {"scope", std::string("all")}};
    if (id == kTransformFrameRateStep)
        return {{"input", 23.976}, {"output", std::string("variable")}, {"outputFps", 23.976}, {"reverse", false}};
    return {};
}

ExportRecipe newStepList(const std::vector<AvailableStep> &available)
{
    ExportRecipe recipe;
    for (const auto &step : available)
        recipe.steps.push_back({step.id, false, defaultSettings(step.id)});
    return recipe;
}

std::vector<std::string> exportFilterNames(std::string_view line)
{
    std::vector<std::string> names;
    for (std::size_t p = 0; p <= line.size();) {
        const auto bar = std::min(line.find('|', p), line.size());
        if (bar > p)
            names.emplace_back(line.substr(p, bar - p));
        p = bar + 1;
    }
    return names;
}

std::vector<std::string> exportFilterNames(const core::Document &document)
{
    const auto line = document.projectGarbage(kExportFiltersKey);
    return line ? exportFilterNames(std::string(line->begin(), line->end())) : std::vector<std::string>();
}

ExportRecipe recipeFromExportFilters(const std::vector<std::string> &names, const std::vector<AvailableStep> &available)
{
    ExportRecipe recipe;
    std::set<std::string> seen;
    for (const auto &name : names) {
        const auto it = std::ranges::find(available, name, &AvailableStep::aegisubName);
        const std::string id = it != available.end() ? it->id : luaStepId({}, name);
        if (seen.insert(id).second)
            recipe.steps.push_back({id, true, defaultSettings(id)});
    }
    return recipe;
}

std::string exportFiltersLine(const ExportRecipe &recipe, const std::vector<AvailableStep> &available)
{
    std::string line;
    for (const auto &step : recipe.steps) {
        if (!step.on)
            continue;
        std::string name;
        if (const auto *found = findStep(available, step.id))
            name = found->aegisubName;
        else if (const auto parts = luaStepParts(step.id))
            name = parts->second;
        if (name.empty())
            continue;
        if (!line.empty())
            line += '|';
        line += name;
    }
    return line;
}

int fixUnknownStyles(core::Document &document)
{
    std::set<std::string> styles;
    for (const auto &style : core::decodeStyles(document))
        styles.insert(asciiLower(style.name));
    return static_cast<int>(document.editLines([&](core::LineRecord &line) {
        if (line.unparsed || styles.contains(asciiLower(line.style)))
            return false;
        line.style = u8"Default";
        return true;
    }));
}

int transformFrameRate(core::Document &document, const FrameRateChange &change)
{
    FrameRateTransform transform(change);
    return static_cast<int>(document.editLines([&](core::LineRecord &line) {
        if (line.unparsed)
            return false;
        const auto before = std::tuple(line.start.value, line.end.value, line.text);
        transform.line(line);
        return std::tuple(line.start.value, line.end.value, line.text) != before;
    }));
}

std::optional<AegisubFrameTimes> constantFrameTimes(double fps)
{
    if (!std::isfinite(fps) || fps <= 0 || fps > 1000)
        return std::nullopt;
    return AegisubFrameTimes::fromTimecodes(
        Timecodes{TimecodesFormat::V2, {0}, static_cast<std::int64_t>(fps * kTimecodesDenominator), 0});
}

DialogRequest settingsDialog(const ExportStepSettings &settings)
{
    DialogRequest request;
    for (const auto &[name, value] : settings) {
        DialogControl control;
        control.name = name;
        if (const auto *s = std::get_if<std::string>(&value)) {
            control.kind = "edit";
            control.text = *s;
        } else if (const auto *i = std::get_if<int>(&value)) {
            control.kind = "intedit";
            control.intValue = *i;
        } else if (const auto *d = std::get_if<double>(&value)) {
            control.kind = "floatedit";
            control.number = *d;
        } else if (const auto *b = std::get_if<bool>(&value)) {
            control.kind = "checkbox";
            control.checked = *b;
        } else {
            continue;
        }
        request.controls.push_back(std::move(control));
    }
    return request;
}

StopExportSteps runExportSteps(core::Document document, const ExportRecipe &recipe,
                               const std::vector<AvailableStep> &available, const ExportStepsContext &context,
                               std::function<void(ExportStepsResult)> done)
{
    auto run = std::make_shared<StepsRun>();
    run->document = std::move(document);
    for (const auto &step : recipe.steps)
        if (step.on)
            run->steps.push_back(step);
    run->available = available;
    run->context = context;
    run->done = std::move(done);
    run->advance();
    return [weak = std::weak_ptr<StepsRun>(run)] {
        if (const auto run = weak.lock())
            run->stop();
    };
}

}
