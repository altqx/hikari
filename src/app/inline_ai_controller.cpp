#include "hikari/app/inline_ai_controller.h"

#include "hikari/api/api.h"
#include "hikari/app/agent_controller.h"
#include "hikari/app/ai_features.h"
#include "hikari/app/review_controller.h"
#include "hikari/application/ai_review.h"
#include "hikari/application/document_files.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/workspace.h"
#include "hikari/backends/ai_agent.h"
#include "hikari/backends/ai_inline_run.h"
#include "line_measures.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <map>
#include <set>

namespace hikari::app {

namespace ai = hikari::application::ai;
namespace in = hikari::application::ai_inline;
using api::Json;
using backends::ai::InlineRun;

namespace {

constexpr int kMaxCps = 15;
constexpr int kParallel = 4;
constexpr int kRecentKept = 5;

QString qs(std::u8string_view s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

std::string str(std::u8string_view s)
{
    return {reinterpret_cast<const char *>(s.data()), s.size()};
}

std::optional<ai::Role> roleFromId(const QString &id)
{
    if (id == QLatin1String("chat"))
        return ai::Role::Chat;
    if (id == QLatin1String("translator"))
        return ai::Role::Translator;
    if (id == QLatin1String("reviewer"))
        return ai::Role::Reviewer;
    return std::nullopt;
}

QString inlineRoleId(ai::Role role)
{
    switch (role) {
    case ai::Role::Translator: return QStringLiteral("translator");
    case ai::Role::Reviewer: return QStringLiteral("reviewer");
    default: return QStringLiteral("chat");
    }
}

QVariantMap parsed(const QString &json)
{
    return QJsonDocument::fromJson(json.toUtf8()).object().toVariantMap();
}

std::size_t rowOf(const core::Document &document, core::LineId id)
{
    const auto lines = document.lines();
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (lines[i]->id == id)
            return i;
    return lines.size();
}

const core::LineRecord *lineOf(const core::Document &document, core::LineId id)
{
    for (const core::LineRecord *line : document.lines())
        if (line->id == id)
            return line;
    return nullptr;
}

in::Neighbours neighboursOf(const core::Document &document, std::size_t row, int count)
{
    in::Neighbours out;
    if (count <= 0)
        return out;
    const auto lines = document.lines();
    for (std::size_t i = row; i-- > 0 && static_cast<int>(out.before.size()) < count;)
        if (!lines[i]->comment && !in::stripped(lines[i]->text).empty())
            out.before.insert(out.before.begin(), in::stripped(lines[i]->text));
    for (std::size_t i = row + 1; i < lines.size() && static_cast<int>(out.after.size()) < count; ++i)
        if (!lines[i]->comment && !in::stripped(lines[i]->text).empty())
            out.after.push_back(in::stripped(lines[i]->text));
    return out;
}

}

enum class LineState { Waiting, Changed, Same, DroppedTag, Empty, Failed, Edited, Written, Dropped };

struct InlineAiController::Line {
    std::uint64_t key = 0;
    core::LineId id;
    std::u8string before; // the Line's whole text when it was sent
    std::u8string after;
    LineState state = LineState::Waiting;
    QString error;
};

struct InlineAiController::Run {
    application::DocumentId document;
    QVariantMap ask;
    in::Ask parsed;
    ai::Role role = ai::Role::Chat;
    std::string label; // the action as the review and History name it
    std::string mode;
    std::optional<std::pair<std::size_t, std::size_t>> span;
    std::vector<Line> lines;
    std::size_t skipped = 0;
    std::unique_ptr<InlineRun> engine;
    QString end;
    double cost = 0;
    bool costKnown = true;
    bool dismissed = false;
};

InlineAiController::InlineAiController(Wiring wiring, QObject *parent) : QObject(parent), m_wiring(std::move(wiring))
{
    connect(&m_wiring.ai, &AiFeatures::switched, this, [this] {
        if (!on())
            m_run.reset();
        emit changed();
        emit runChanged();
    });
    connect(&m_wiring.settings, &ui::SettingsStore::changed, this, [this](const QString &id) {
        if (id.startsWith(QLatin1String("ai.")))
            emit changed();
    });
}

InlineAiController::~InlineAiController() = default;

bool InlineAiController::on() const
{
    return m_wiring.ai.on();
}

QVariantList InlineAiController::actions() const
{
    QVariantList out;
    for (const QString &row : m_wiring.settings.list(kActionsSetting)) {
        QVariantMap a = parsed(row);
        if (a.value(QStringLiteral("name")).toString().trimmed().isEmpty())
            continue;
        if (!roleFromId(a.value(QStringLiteral("role")).toString()))
            a[QStringLiteral("role")] = QStringLiteral("chat");
        out.push_back(a);
    }
    return out;
}

void InlineAiController::setActions(const QVariantList &list)
{
    QStringList rows;
    for (const QVariant &v : list) {
        const QVariantMap a = v.toMap();
        const QString name = a.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty())
            continue;
        QJsonObject o;
        o[QStringLiteral("name")] = name;
        o[QStringLiteral("instruction")] = a.value(QStringLiteral("instruction")).toString().trimmed();
        o[QStringLiteral("role")] = inlineRoleId(roleFromId(a.value(QStringLiteral("role")).toString()).value_or(ai::Role::Chat));
        o[QStringLiteral("ask")] = a.value(QStringLiteral("ask")).toBool();
        rows.push_back(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
    }
    m_wiring.settings.set(kActionsSetting, rows);
}

QString InlineAiController::translateTo() const
{
    const QString language = m_wiring.settings.text(kTranslateToSetting).trimmed();
    return language.isEmpty() ? QStringLiteral("English") : language;
}

QStringList InlineAiController::recentInstructions() const
{
    return m_wiring.settings.list(kRecentSetting).mid(0, kRecentKept);
}

QVariantMap InlineAiController::last() const
{
    return m_lastAsk;
}

int InlineAiController::maxCps() const
{
    return kMaxCps;
}

std::string InlineAiController::mode() const
{
    const std::string m = m_wiring.settings.text(AgentController::kModeSetting).toStdString();
    return m == "manual" || m == "full" ? m : std::string("auto");
}

const core::Document *InlineAiController::documentOf(application::DocumentId document) const
{
    application::EditSession *session = m_wiring.files.session(document);
    return session ? &session->document() : nullptr;
}

std::optional<in::Ask> InlineAiController::askOf(const QVariantMap &map, std::string *label, ai::Role *role) const
{
    const auto action = in::actionFromId(map.value(QStringLiteral("action")).toString().toStdString());
    if (!action)
        return std::nullopt;
    in::Ask ask;
    ask.action = *action;
    ask.maxCps = kMaxCps;
    ai::Role r = ai::Role::Chat;
    QString name;
    switch (*action) {
    case in::Action::Rewrite:
        ask.instruction = map.value(QStringLiteral("instruction")).toString().trimmed().toStdString();
        if (ask.instruction.empty())
            return std::nullopt;
        name = QCoreApplication::translate("HikariSub.InlineAi", "Rewrite");
        break;
    case in::Action::Shorten:
        name = QCoreApplication::translate("HikariSub.InlineAi", "Shorten");
        break;
    case in::Action::Spelling:
        r = ai::Role::Reviewer;
        name = QCoreApplication::translate("HikariSub.InlineAi", "Fix spelling");
        break;
    case in::Action::Translate: {
        r = ai::Role::Translator;
        const QString language = map.value(QStringLiteral("language")).toString().trimmed();
        ask.language = (language.isEmpty() ? translateTo() : language).toStdString();
        name = QCoreApplication::translate("HikariSub.InlineAi", "Translate");
        break;
    }
    case in::Action::Custom: {
        const QVariantList all = actions();
        const int index = map.value(QStringLiteral("custom"), -1).toInt();
        if (index < 0 || index >= all.size())
            return std::nullopt;
        const QVariantMap custom = all[index].toMap();
        const QString typed = map.value(QStringLiteral("instruction")).toString().trimmed();
        ask.instruction = (typed.isEmpty() ? custom.value(QStringLiteral("instruction")).toString() : typed).toStdString();
        if (ask.instruction.empty())
            return std::nullopt;
        r = roleFromId(custom.value(QStringLiteral("role")).toString()).value_or(ai::Role::Chat);
        name = custom.value(QStringLiteral("name")).toString();
        break;
    }
    }
    if (label)
        *label = name.toStdString();
    if (role)
        *role = r;
    return ask;
}

std::optional<InlineAiController::Picked> InlineAiController::pick(const in::Ask &ask, bool editor) const
{
    const auto target = m_wiring.workspace.editingTarget();
    application::EditSession *session = target ? m_wiring.files.session(*target) : nullptr;
    if (!session)
        return std::nullopt;
    const application::Selection &selection = session->selection();
    std::vector<core::LineId> lines;
    if (editor || selection.selected.empty()) {
        if (selection.active)
            lines.push_back(*selection.active);
    } else {
        lines.assign(selection.selected.begin(), selection.selected.end());
    }
    return pick(ask, *target, std::move(lines));
}

std::optional<InlineAiController::Picked> InlineAiController::pick(const in::Ask &ask, application::DocumentId document,
                                                                   std::vector<core::LineId> lines) const
{
    const core::Document *doc = documentOf(document);
    if (!doc)
        return std::nullopt;
    std::ranges::sort(lines, {}, [doc](core::LineId id) { return rowOf(*doc, id); });
    Picked out{document, {}, 0};
    ui::MeasureOptions measure;
    measure.allCharsForCps = m_wiring.settings.boolean("grid.calcSpacesAndPunctuationForCps");
    for (const core::LineId id : lines) {
        const core::LineRecord *line = lineOf(*doc, id);
        if (!line)
            continue;
        if (in::skipOf(*line) != in::Skip::None) {
            ++out.skipped;
            continue;
        }
        if (ask.action == in::Action::Shorten) {
            const ui::LineMeasures m = ui::measureLine(qs(line->text), doc->format(), measure);
            if (ui::legacyCps(m.chars, line->start.value.microseconds() / 1000, line->end.value.microseconds() / 1000) <=
                ask.maxCps)
                continue;
        }
        out.send.push_back(id);
    }
    return out;
}

QVariantMap InlineAiController::plan(const QVariantMap &askMap, bool editor) const
{
    QVariantMap out;
    ai::Role role = ai::Role::Chat;
    QVariantMap probe = askMap;
    if (probe.value(QStringLiteral("action")).toString() == QLatin1String("rewrite") &&
        probe.value(QStringLiteral("instruction")).toString().trimmed().isEmpty())
        probe[QStringLiteral("instruction")] = QStringLiteral("-");
    if (probe.value(QStringLiteral("action")).toString() == QLatin1String("custom") &&
        probe.value(QStringLiteral("instruction")).toString().trimmed().isEmpty())
        probe[QStringLiteral("instruction")] = QStringLiteral("-");
    const auto ask = askOf(probe, nullptr, &role);
    const auto picked = ask ? pick(*ask, editor) : std::nullopt;
    const ai::Providers *providers = m_wiring.ai.providers();
    const auto model = providers ? providers->config().modelFor(role) : std::nullopt;
    out[QStringLiteral("role")] = inlineRoleId(role);
    out[QStringLiteral("ready")] = model.has_value();
    out[QStringLiteral("send")] = picked ? static_cast<int>(picked->send.size()) : 0;
    out[QStringLiteral("skipped")] = picked ? static_cast<int>(picked->skipped) : 0;
    out[QStringLiteral("confirm")] = picked && static_cast<int>(picked->send.size()) > kConfirmOver;
    out[QStringLiteral("model")] = QString();
    out[QStringLiteral("provider")] = QString();
    double estimate = -1;
    if (model && providers) {
        out[QStringLiteral("model")] = QString::fromStdString(model->modelId);
        if (const ai::Provider *p = providers->config().provider(model->providerId))
            out[QStringLiteral("provider")] = QString::fromStdString(p->name);
        const auto price = backends::ai::priceOf(&providers->config(), *model);
        const core::Document *doc = picked ? documentOf(picked->document) : nullptr;
        if (price && doc && ask) {
            estimate = 0;
            const int context = static_cast<int>(m_wiring.settings.integer(kContextLinesSetting));
            for (const core::LineId id : picked->send) {
                const core::LineRecord *line = lineOf(*doc, id);
                if (!line)
                    continue;
                const in::Prepared p = in::prepare(*ask, *line, neighboursOf(*doc, rowOf(*doc, id), context));
                const double input = static_cast<double>(p.system.size() + p.user.size()) / 4.0;
                const double output = static_cast<double>(p.sent.size()) / 4.0 * 1.5 + 8.0;
                estimate += (input * price->input + output * price->output) / 1'000'000.0;
            }
        }
    }
    out[QStringLiteral("estimate")] = estimate;
    const double cap = m_wiring.agent.dailyCap();
    out[QStringLiteral("dailyCap")] = cap;
    out[QStringLiteral("dailyLeft")] = cap > 0 ? std::max(0.0, cap - m_wiring.agent.spentToday()) : -1.0;
    out[QStringLiteral("mode")] = QString::fromStdString(mode());
    return out;
}

bool InlineAiController::start(const QVariantMap &askMap, bool editor, int spanStart, int spanEnd,
                               const QString &fieldText)
{
    if (!on() || (m_run && m_run->engine && m_run->engine->running()))
        return false;
    std::string label;
    ai::Role role = ai::Role::Chat;
    const auto ask = askOf(askMap, &label, &role);
    if (!ask)
        return false;
    auto picked = pick(*ask, editor);
    if (!picked || picked->send.empty())
        return false;
    std::optional<std::pair<std::size_t, std::size_t>> span;
    if (editor && picked->send.size() == 1 && spanStart >= 0 && spanEnd > spanStart) {
        const core::Document *doc = documentOf(picked->document);
        const core::LineRecord *line = doc ? lineOf(*doc, picked->send.front()) : nullptr;
        if (line) {
            const QString text = qs(line->text);
            if (text == fieldText && spanEnd <= text.size()) {
                const std::size_t first = static_cast<std::size_t>(text.left(spanStart).toUtf8().size());
                const std::size_t last = static_cast<std::size_t>(text.left(spanEnd).toUtf8().size());
                span = std::pair{first, last};
            }
        }
    }
    return launch(askMap, std::move(*picked), span);
}

bool InlineAiController::launch(const QVariantMap &askMap, Picked picked,
                                std::optional<std::pair<std::size_t, std::size_t>> span)
{
    std::string label;
    ai::Role role = ai::Role::Chat;
    const auto ask = askOf(askMap, &label, &role);
    const core::Document *doc = documentOf(picked.document);
    if (!ask || !doc || picked.send.empty())
        return false;

    auto run = std::make_unique<Run>();
    run->document = picked.document;
    run->ask = askMap;
    run->parsed = *ask;
    run->role = role;
    run->label = label;
    run->mode = mode();
    run->span = span;
    run->skipped = picked.skipped;
    const int context = static_cast<int>(m_wiring.settings.integer(kContextLinesSetting));
    std::vector<InlineRun::Job> jobs;
    std::uint64_t key = 1;
    for (const core::LineId id : picked.send) {
        const core::LineRecord *line = lineOf(*doc, id);
        if (!line)
            continue;
        run->lines.push_back({key, id, line->text, {}, LineState::Waiting, {}});
        jobs.push_back({key, in::prepare(*ask, *line, neighboursOf(*doc, rowOf(*doc, id), context), span)});
        ++key;
    }

    if (ask->action == in::Action::Rewrite) {
        const QString typed = QString::fromStdString(ask->instruction);
        QStringList recent = m_wiring.settings.list(kRecentSetting);
        recent.removeAll(typed);
        recent.prepend(typed);
        m_wiring.settings.set(kRecentSetting, recent.mid(0, kRecentKept));
    }
    m_lastAsk = askMap;
    if (ask->action == in::Action::Translate)
        m_lastAsk[QStringLiteral("language")] = QString::fromStdString(ask->language);
    m_lastAsk[QStringLiteral("label")] = QString::fromStdString(label);

    m_run = std::move(run);
    InlineRun::Wiring w;
    w.requests = [this] { return m_wiring.ai.requests(); };
    w.config = [this]() -> const ai::Config * {
        const ai::Providers *providers = m_wiring.ai.providers();
        return providers ? &providers->config() : nullptr;
    };
    w.spend = [this](double dollars) { m_wiring.agent.spend(dollars); };
    w.overDailyCap = [this] {
        const double cap = m_wiring.agent.dailyCap();
        return cap > 0 && m_wiring.agent.spentToday() >= cap;
    };
    w.lineDone = [this](const InlineRun::Result &result) {
        if (!m_run)
            return;
        const auto it = std::ranges::find(m_run->lines, result.key, &Line::key);
        if (it == m_run->lines.end())
            return;
        if (result.error) {
            it->state = LineState::Failed;
            it->error = result.error->detail.empty() ? QString::fromLatin1(ai::errorKindId(result.error->kind))
                                                     : QString::fromStdString(result.error->detail);
        } else {
            switch (result.answer.outcome) {
            case in::Outcome::Changed:
                it->state = LineState::Changed;
                it->after = result.answer.text;
                break;
            case in::Outcome::Same: it->state = LineState::Same; break;
            case in::Outcome::DroppedTag: it->state = LineState::DroppedTag; break;
            case in::Outcome::Empty: it->state = LineState::Empty; break;
            }
        }
        dropEdited();
        emit runChanged();
    };
    w.finished = [this](InlineRun::End end) {
        if (!m_run)
            return;
        m_run->end = end == InlineRun::End::Done      ? QStringLiteral("done")
                     : end == InlineRun::End::Stopped ? QStringLiteral("stopped")
                     : end == InlineRun::End::DailyCap ? QStringLiteral("dailyCap")
                                                       : QStringLiteral("noModel");
        for (Line &line : m_run->lines)
            if (line.state == LineState::Waiting)
                line.state = LineState::Dropped;
        if (m_run->mode != "manual") {
            std::vector<std::uint64_t> keys;
            for (const Line &line : m_run->lines)
                if (line.state == LineState::Changed)
                    keys.push_back(line.key);
            write(keys);
        }
        const QString why = m_run->end;
        QMetaObject::invokeMethod(this, [this] {
            if (m_run && m_run->engine && !m_run->engine->running()) {
                m_run->cost = m_run->engine->cost();
                m_run->costKnown = m_run->engine->costKnown();
                m_run->engine.reset();
            }
            emit runChanged();
        }, Qt::QueuedConnection);
        emit runChanged();
        emit finished(why);
    };
    w.parallel = kParallel;
    m_run->engine = std::make_unique<InlineRun>(role, std::move(jobs), std::move(w));
    emit changed();
    emit runChanged();
    m_run->engine->start();
    return true;
}

void InlineAiController::refreshed()
{
    if (!m_run)
        return;
    dropEdited();
    emit runChanged();
}

void InlineAiController::dropEdited()
{
    if (!m_run || !m_run->engine)
        return;
    const core::Document *doc = documentOf(m_run->document);
    for (Line &line : m_run->lines) {
        if (line.state != LineState::Waiting && line.state != LineState::Changed)
            continue;
        const core::LineRecord *now = doc ? lineOf(*doc, line.id) : nullptr;
        if (now && now->text == line.before)
            continue;
        if (line.state == LineState::Waiting)
            m_run->engine->drop(line.key);
        line.state = LineState::Edited;
    }
}

bool InlineAiController::write(const std::vector<std::uint64_t> &keys)
{
    if (!m_run)
        return false;
    application::EditSession *session = m_wiring.files.session(m_run->document);
    if (!session)
        return false;
    const core::Document &doc = session->document();
    Json edits = Json::array();
    std::vector<Line *> written;
    for (Line &line : m_run->lines) {
        if (line.state != LineState::Changed || std::ranges::find(keys, line.key) == keys.end())
            continue;
        const core::LineRecord *now = lineOf(doc, line.id);
        if (!now || now->text != line.before) {
            line.state = LineState::Edited;
            continue;
        }
        edits.push(Json::object({{"op", "set_line"},
                                 {"line", Json(line.id.value)},
                                 {"fields", Json::object({{"text", str(line.after)}})}}));
        written.push_back(&line);
    }
    if (written.empty())
        return false;
    const QString name = QCoreApplication::translate("HikariSub.InlineAi", "%1 (%n Line(s))", nullptr,
                                                   static_cast<int>(written.size())).arg(QString::fromStdString(m_run->label));
    QString turn = QString::fromStdString(m_run->label);
    if (!m_run->parsed.instruction.empty() && m_run->parsed.action != in::Action::Custom)
        turn += QStringLiteral(" · ") + QString::fromStdString(m_run->parsed.instruction);
    else if (m_run->parsed.action == in::Action::Translate)
        turn += QStringLiteral(" · ") + QString::fromStdString(m_run->parsed.language);
    m_wiring.review.beginTurn(kCallerId, turn.toStdString());
    api::Caller caller{kCallerId, "AI", false, {m_run->document}, true};
    auto ok = std::make_shared<bool>(false);
    m_wiring.api.call(caller, "document.apply",
                      Json::object({{"document", Json(m_run->document.value)},
                                    {"revision", Json(session->revision())},
                                    {"name", name.toStdString()},
                                    {"edits", std::move(edits)}}),
                      [ok](api::Result result) { *ok = result.has_value(); });
    for (Line *line : written)
        line->state = *ok ? LineState::Written : LineState::Failed;
    return *ok;
}

void InlineAiController::stop()
{
    if (m_run && m_run->engine)
        m_run->engine->stop();
}

bool InlineAiController::apply(const QVariantList &keys)
{
    if (!m_run || (m_run->engine && m_run->engine->running()))
        return false;
    std::vector<std::uint64_t> chosen;
    for (const QVariant &k : keys)
        chosen.push_back(k.toULongLong());
    const bool ok = write(chosen);
    for (Line &line : m_run->lines)
        if (line.state == LineState::Changed)
            line.state = LineState::Dropped;
    emit runChanged();
    return ok;
}

void InlineAiController::discard()
{
    if (!m_run || (m_run->engine && m_run->engine->running()))
        return;
    for (Line &line : m_run->lines)
        if (line.state == LineState::Changed)
            line.state = LineState::Dropped;
    emit runChanged();
}

bool InlineAiController::retry()
{
    if (!m_run || (m_run->engine && m_run->engine->running()))
        return false;
    std::vector<core::LineId> lines;
    for (const Line &line : m_run->lines)
        if (line.state == LineState::Failed || line.state == LineState::DroppedTag || line.state == LineState::Empty)
            lines.push_back(line.id);
    if (lines.empty())
        return false;
    const auto ask = askOf(m_run->ask, nullptr, nullptr);
    if (!ask)
        return false;
    auto picked = pick(*ask, m_run->document, std::move(lines));
    if (!picked || picked->send.empty())
        return false;
    const QVariantMap askMap = m_run->ask;
    const auto span = m_run->span;
    return launch(askMap, std::move(*picked), span);
}

void InlineAiController::dismiss()
{
    if (m_run && !(m_run->engine && m_run->engine->running())) {
        m_run->dismissed = true;
        emit runChanged();
    }
}

void InlineAiController::show(qulonglong key)
{
    if (!m_run || !m_wiring.select)
        return;
    const auto it = std::ranges::find(m_run->lines, static_cast<std::uint64_t>(key), &Line::key);
    if (it != m_run->lines.end())
        m_wiring.select(m_run->document, it->id);
}

bool InlineAiController::repeatLast(bool editor)
{
    if (m_lastAsk.isEmpty())
        return false;
    return start(m_lastAsk, editor);
}

QVariantMap InlineAiController::progress() const
{
    QVariantMap out;
    const bool running = m_run && m_run->engine && m_run->engine->running();
    out[QStringLiteral("running")] = running;
    if (!m_run)
        return out;
    std::size_t done = 0, total = 0;
    for (const Line &line : m_run->lines) {
        if (line.state == LineState::Dropped && running)
            continue;
        if (line.state == LineState::Edited && line.after.empty())
            continue;
        ++total;
        if (line.state != LineState::Waiting)
            ++done;
    }
    out[QStringLiteral("action")] = m_run->ask.value(QStringLiteral("action"));
    out[QStringLiteral("label")] = QString::fromStdString(m_run->label);
    out[QStringLiteral("done")] = static_cast<int>(done);
    out[QStringLiteral("total")] = static_cast<int>(total);
    out[QStringLiteral("cost")] = m_run->engine ? m_run->engine->cost() : m_run->cost;
    out[QStringLiteral("costKnown")] = m_run->engine ? m_run->engine->costKnown() : m_run->costKnown;
    out[QStringLiteral("mode")] = QString::fromStdString(m_run->mode);
    out[QStringLiteral("end")] = m_run->end;
    const ai::Providers *providers = m_wiring.ai.providers();
    const auto model = providers ? providers->config().modelFor(m_run->role) : std::nullopt;
    out[QStringLiteral("role")] = inlineRoleId(m_run->role);
    out[QStringLiteral("model")] = model ? QString::fromStdString(model->modelId) : QString();
    return out;
}

QVariantList InlineAiController::results() const
{
    QVariantList out;
    if (!m_run || m_run->mode != "manual" || (m_run->engine && m_run->engine->running()))
        return out;
    const core::Document *doc = documentOf(m_run->document);
    for (const Line &line : m_run->lines) {
        if (line.state != LineState::Changed && line.state != LineState::Edited)
            continue;
        if (line.after.empty())
            continue;
        const core::LineRecord *now = doc ? lineOf(*doc, line.id) : nullptr;
        QVariantMap r;
        r[QStringLiteral("key")] = QVariant::fromValue<qulonglong>(line.key);
        r[QStringLiteral("row")] = doc ? static_cast<int>(rowOf(*doc, line.id)) + 1 : 0;
        r[QStringLiteral("diff")] = diffHtml(qs(in::stripped(line.before)), qs(in::stripped(line.after)));
        r[QStringLiteral("stale")] = line.state == LineState::Edited || !now || now->text != line.before;
        out.push_back(r);
    }
    return out;
}

QVariantMap InlineAiController::notice() const
{
    QVariantMap out;
    if (!m_run || m_run->dismissed || (m_run->engine && m_run->engine->running()))
        return out;
    const core::Document *doc = documentOf(m_run->document);
    QVariantList failed, same, edited;
    for (const Line &line : m_run->lines) {
        const int row = doc ? static_cast<int>(rowOf(*doc, line.id)) + 1 : 0;
        switch (line.state) {
        case LineState::Failed:
        case LineState::DroppedTag:
        case LineState::Empty: {
            QVariantMap f;
            f[QStringLiteral("row")] = row;
            f[QStringLiteral("why")] = line.state == LineState::DroppedTag ? QStringLiteral("tag")
                                       : line.state == LineState::Empty    ? QStringLiteral("empty")
                                                                           : QStringLiteral("failed");
            f[QStringLiteral("detail")] = line.error;
            failed.push_back(f);
            break;
        }
        case LineState::Same: same.push_back(row); break;
        case LineState::Edited: edited.push_back(row); break;
        default: break;
        }
    }
    if (failed.isEmpty() && same.isEmpty() && edited.isEmpty() && m_run->skipped == 0 && m_run->end == QLatin1String("done"))
        return out;
    out[QStringLiteral("failed")] = failed;
    out[QStringLiteral("same")] = same;
    out[QStringLiteral("edited")] = edited;
    out[QStringLiteral("skipped")] = static_cast<int>(m_run->skipped);
    out[QStringLiteral("end")] = m_run->end;
    out[QStringLiteral("label")] = QString::fromStdString(m_run->label);
    return out;
}

}
