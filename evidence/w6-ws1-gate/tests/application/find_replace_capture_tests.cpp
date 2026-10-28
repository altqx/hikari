// F1: the legacy captures of the Find and replace dialog replayed through
// FindReplace. tools/legacy-capture/plan.json's "ui" cases (card F1) drive
// the legacy app (ui_capture.py) with semantic steps; this test runs the
// same steps here and compares every state dump with what the legacy app
// recorded (tests/fixtures/legacy-observations/...): the Lines' texts, the
// selected rows, the active row and the Line editor's selection, and the
// title of every message box answered. The rewrite's run is written to
// <build>/tests/application/artifacts/find-replace-capture.json for
// tools/legacy-capture/compare_ui.py.
//
// The legacy editor selection is aegisub.gui.get_selection over
// EditBox::GetEditor (TextEdit, 1-based, end exclusive). The test host keeps
// it as the legacy EditBox did: a match shown in TextEdit selects it, a
// match in another editor or field leaves TextEdit unselected, and a
// message box clears it (the captures show [1, 1] after every box).

#include "hikari/application/find_replace.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstring>
#include <map>
#include <set>
#include <string>

using namespace hikari;
using namespace hikari::application;
using S = FindReplaceSettings;
using K = FindQuestion::Kind;

namespace {

QJsonObject readJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

std::u8string u8(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return {reinterpret_cast<const char8_t *>(b.constData()), static_cast<std::size_t>(b.size())};
}

QString qs(std::u8string_view s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

// What the legacy app had on screen: one box (a question, a message, a log
// line) waits for the plan's "answer" step.
struct Box {
    FindQuestion question;
    std::function<void(FindAnswer)> answer;
    QString title;
    QString text;
};

struct Host : FindReplaceHost {
    std::unique_ptr<EditSession> session;
    DocumentId id{1};
    std::vector<Box> boxes;
    // TextEdit's selection [start, end), or none.
    std::optional<std::pair<int, int>> editor;

    std::optional<FindTab> current() override { return FindTab{id, session.get(), u8"capture.ass"}; }
    std::vector<FindTab> tabs() override { return {*current()}; }
    void ask(const FindQuestion &q, std::function<void(FindAnswer)> answer) override
    {
        boxes.push_back({q, std::move(answer), qs(q.title), qs(q.text)});
        editor.reset();
    }
    // wxLogError shows "Hikarisub Error"; HikariLog the "Log window".
    void logLine(const FindLog &line) override
    {
        const bool error = line.kind == FindLog::Kind::InvalidRegex || line.kind == FindLog::Kind::MatchError;
        boxes.push_back({{}, {}, error ? QStringLiteral("Hikarisub Error") : QStringLiteral("Log window"), qs(line.text)});
    }
    void showLine(DocumentId, core::LineId line, bool keep, int role, int start, int end) override
    {
        Selection sel = session->selection();
        if (!keep)
            sel.selected = {line};
        sel.active = line;
        session->setSelection(sel);
        if (role == 1)
            editor = std::pair{start, end};
        else
            editor.reset();
    }
    std::optional<std::vector<std::u8string>> listFiles(const std::u8string &, const std::u8string &, bool, bool) override
    {
        return std::nullopt;
    }
    std::optional<std::u16string> readFile(const std::u8string &) override { return std::nullopt; }
    bool fileExists(const std::u8string &) override { return false; }
    bool backupFile(const std::u8string &) override { return false; }
    bool writeFile(const std::u8string &, const std::u16string &) override { return false; }
    void openFile(const std::u8string &, std::function<void(std::optional<DocumentId>)> done) override
    {
        done(std::nullopt);
    }
};

// The legacy "dump": rows among the events (comments included), the Text
// as Lua saw it (the translation of a translation mode Line when it has one).
QJsonObject dump(const Host &host)
{
    const auto &doc = host.session->document();
    const auto lines = doc.lines();
    const bool tl = doc.scriptInfo(u8"TLMode").value_or(u8"") == u8"Yes";
    QJsonArray texts;
    std::map<core::LineId, int> rows;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        rows[lines[i]->id] = static_cast<int>(i);
        texts.append(qs(tl && !lines[i]->translation.empty() ? lines[i]->translation : lines[i]->text));
    }
    QJsonArray selected;
    std::set<int> sel;
    for (const auto id : host.session->selection().selected)
        sel.insert(rows.at(id));
    for (int r : sel)
        selected.append(r);
    const auto active = host.session->selection().active;
    QJsonObject out;
    out[QStringLiteral("texts")] = texts;
    out[QStringLiteral("selected")] = selected;
    out[QStringLiteral("active")] = active ? rows.at(*active) : -1;
    // Lua's 1-based [start, end) as get_selection returns it.
    const auto ed = host.editor.value_or(std::pair{0, 0});
    out[QStringLiteral("editor_selection")] = QJsonArray{ed.first + 1, ed.second + 1};
    return out;
}

QJsonObject legacyDump(const QJsonObject &state)
{
    QJsonArray texts;
    for (const auto &l : state[QStringLiteral("lines")].toArray())
        texts.append(l.toObject()[QStringLiteral("text")]);
    QJsonObject out;
    out[QStringLiteral("texts")] = texts;
    out[QStringLiteral("selected")] = state[QStringLiteral("selected")];
    out[QStringLiteral("active")] = state[QStringLiteral("active")];
    out[QStringLiteral("editor_selection")] = state[QStringLiteral("editor_selection")];
    return out;
}

// Cases where the rewrite departs from what the legacy app did, by an
// approved departure (docs/qt/compatibility-decisions.md): the step whose
// dump differs, and why.
const std::map<std::string, std::string> kDepartures = {
    {"F1-replace-next-stale", "F1-stale-replace (R3-hang-crash-loss): legacy replaced the stale match again"},
    {"F1-find-all-end-of-text", "F1-end-of-text (R3-hang-crash-loss): legacy hangs"},
    {"F1-replace-all-empty-match", "F1-empty-match (R3-hang-crash-loss): legacy hangs"},
};

std::vector<std::byte> fileBytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray b = f.readAll();
    std::vector<std::byte> out(static_cast<std::size_t>(b.size()));
    std::memcpy(out.data(), b.constData(), out.size());
    return out;
}

} // namespace

TEST(FindReplaceCapture, ReplaysTheLegacyCaptures)
{
    const QJsonObject plan = readJson(QStringLiteral(HIKARI_LEGACY_CAPTURE_PLAN));
    const QJsonObject observed = readJson(QStringLiteral(HIKARI_F1F3_OBSERVATIONS));
    ASSERT_FALSE(plan.isEmpty());
    ASSERT_FALSE(observed.isEmpty());
    std::map<QString, QJsonObject> legacy;
    for (const auto &c : observed[QStringLiteral("cases")].toArray())
        for (const auto &o : c.toObject()[QStringLiteral("observations")].toArray())
            legacy[o.toObject()[QStringLiteral("id")].toString()] = o.toObject();

    QJsonArray artifact;
    int compared = 0;
    for (const auto &cv : plan[QStringLiteral("ui")].toObject()[QStringLiteral("cases")].toArray()) {
        const QJsonObject c = cv.toObject();
        if (c[QStringLiteral("card")].toString() != QStringLiteral("F1"))
            continue;
        const QString id = c[QStringLiteral("id")].toString();
        SCOPED_TRACE(id.toStdString());
        if (!legacy.contains(id)) {
            ADD_FAILURE() << "no legacy capture";
            continue;
        }
        const QJsonArray legacySteps = legacy[id][QStringLiteral("steps")].toArray();

        Host host;
        const QString input = QStringLiteral(HIKARI_CAPTURE_INPUTS "/") + c[QStringLiteral("inputs")][0].toString();
        host.session = std::make_unique<EditSession>(core::loadAss(fileBytes(input)).document);
        const auto lines = host.session->document().lines();
        host.session->setSelection(Selection{lines[0]->id, {lines[0]->id}, lines[0]->id, {}});
        // The captures ran the English interface, where legacy stays in the
        // "C" locale and wxString::Lower folds A-Z only: FindReplace's own
        // fold. The shell folds every letter (U1-unicode-case).
        FindReplace fr(host);
        const QJsonObject config = c[QStringLiteral("config")].toObject();
        S window = findReplaceFromOptions(config[QStringLiteral("FIND_REPLACE_OPTIONS")].toInt());
        window.styles = u8(config[QStringLiteral("FIND_REPLACE_STYLES")].toString());

        QJsonArray steps;
        const QJsonArray planSteps = c[QStringLiteral("steps")].toArray();
        for (qsizetype i = 0; i < planSteps.size(); ++i) {
            const QJsonObject step = planSteps[i].toObject();
            const QString d = step[QStringLiteral("do")].toString();
            QJsonObject rec{{QStringLiteral("do"), d}};
            if (d == QStringLiteral("select")) {
                const auto all = host.session->document().lines();
                Selection sel;
                for (const auto &r : step[QStringLiteral("rows")].toArray())
                    sel.selected.insert(all[r.toInt()]->id);
                sel.active = sel.anchor = all[step[QStringLiteral("rows")].toArray().last().toInt()]->id;
                host.session->setSelection(sel);
                host.editor.reset();
            } else if (d == QStringLiteral("open")) {
                window.tab = step[QStringLiteral("tab")].toString() == QStringLiteral("find") ? S::Tab::Find : S::Tab::Replace;
            } else if (d == QStringLiteral("find_text")) {
                window.find = u8(step[QStringLiteral("text")].toString());
            } else if (d == QStringLiteral("replace_text")) {
                window.replace = u8(step[QStringLiteral("text")].toString());
            } else if (d == QStringLiteral("find")) {
                fr.find(window);
            } else if (d == QStringLiteral("replace_next")) {
                fr.replace(window);
            } else if (d == QStringLiteral("replace_all")) {
                fr.replaceAll(window);
            } else if (d == QStringLiteral("find_all_current")) {
                fr.findAllInCurrent(window);
                QJsonArray results;
                for (const auto &r : fr.results())
                    results.append(r.header ? QStringLiteral("header %1").arg(QString::fromStdU16String(r.text))
                                            : QStringLiteral("Line %1: [%2,%3) %4")
                                                  .arg(r.idLine)
                                                  .arg(r.start)
                                                  .arg(r.start + r.length)
                                                  .arg(QString::fromStdU16String(r.text)));
                rec[QStringLiteral("results")] = results;
            } else if (d == QStringLiteral("results_replace")) {
                fr.replaceChecked(u8(step[QStringLiteral("text")].toString()));
            } else if (d == QStringLiteral("answer")) {
                if (host.boxes.empty()) {
                    rec[QStringLiteral("status")] = QStringLiteral("no-message-box");
                } else {
                    Box box = std::move(host.boxes.back());
                    host.boxes.pop_back();
                    rec[QStringLiteral("title")] = box.title;
                    rec[QStringLiteral("text")] = box.text;
                    const bool yes = step[QStringLiteral("answer")].toString() == QStringLiteral("yes") ||
                                     step[QStringLiteral("answer")].toString() == QStringLiteral("ok");
                    // Return presses the focused first button; Escape the escape id.
                    FindAnswer a = FindAnswer::Ok;
                    switch (box.question.kind) {
                    case K::Wrap:
                    case K::ConfirmFiles:
                        a = yes ? FindAnswer::Yes : FindAnswer::No;
                        break;
                    case K::Styles:
                        a = yes ? FindAnswer::Ok : FindAnswer::Cancel;
                        break;
                    case K::NoStyles:
                        a = yes ? FindAnswer::Yes : FindAnswer::Cancel;
                        break;
                    case K::Message:
                        break;
                    }
                    if (box.answer)
                        box.answer(a);
                    const QJsonObject l = legacySteps[i].toObject();
                    EXPECT_EQ(box.title.toStdString(), l[QStringLiteral("title")].toString().toStdString()) << "step " << i;
                }
            } else if (d == QStringLiteral("dump")) {
                const QJsonObject mine = dump(host);
                rec[QStringLiteral("state")] = mine;
                const QJsonObject l = legacySteps[i].toObject();
                if (!l[QStringLiteral("state")].isObject()) {
                    rec[QStringLiteral("legacy")] = QStringLiteral("no response");
                    EXPECT_TRUE(kDepartures.contains(id.toStdString())) << "step " << i << ": legacy did not answer";
                } else {
                    const QJsonObject theirs = legacyDump(l[QStringLiteral("state")].toObject());
                    rec[QStringLiteral("legacy")] = theirs;
                    ++compared;
                    if (mine != theirs) {
                        rec[QStringLiteral("differs")] = true;
                        EXPECT_TRUE(kDepartures.contains(id.toStdString()))
                            << "step " << i << ": rewrite "
                            << QJsonDocument(mine).toJson(QJsonDocument::Compact).toStdString() << "\n legacy "
                            << QJsonDocument(theirs).toJson(QJsonDocument::Compact).toStdString();
                    }
                }
            }
            if (!host.boxes.empty()) {
                QJsonArray open;
                for (const auto &b : host.boxes)
                    open.append(b.title + QStringLiteral(": ") + b.text);
                rec[QStringLiteral("boxes_open")] = open;
            }
            steps.append(rec);
        }
        QJsonObject out{{QStringLiteral("id"), id}, {QStringLiteral("steps"), steps}};
        if (kDepartures.contains(id.toStdString()))
            out[QStringLiteral("departure")] = QString::fromStdString(kDepartures.at(id.toStdString()));
        artifact.append(out);
    }
    EXPECT_GT(compared, 50);
    QDir().mkpath(QStringLiteral(HIKARI_CAPTURE_ARTIFACTS));
    QFile f(QStringLiteral(HIKARI_CAPTURE_ARTIFACTS "/find-replace-capture.json"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("cases"), artifact}}).toJson());
}
