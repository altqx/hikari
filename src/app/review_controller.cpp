#include "hikari/app/review_controller.h"

#include "hikari/application/word_diff.h"
#include "theme.h"

#include <QCoreApplication>

#include <algorithm>
#include <set>

namespace hikari::app {

namespace {

using application::AiReview;

QString qs(const std::u8string &text)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(text.data()), qsizetype(text.size()));
}

QString qs(const std::string &text)
{
    return QString::fromStdString(text);
}

QString shortText(const std::u8string &text)
{
    constexpr qsizetype kMost = 80;
    const QString full = qs(text);
    return full.size() > kMost ? full.left(kMost - 1) + QStringLiteral("…") : full;
}

QString itemTitle(const AiReview::Item &item, std::optional<int> row)
{
    switch (item.kind) {
    case AiReview::ItemKind::Line:
        if (!item.after)
            return QCoreApplication::translate("HikariSub.Review", "Deleted Line");
        if (!row)
            return QCoreApplication::translate("HikariSub.Review", "Line");
        return (item.before ? QCoreApplication::translate("HikariSub.Review", "Line %1")
                            : QCoreApplication::translate("HikariSub.Review", "New Line %1"))
            .arg(*row);
    case AiReview::ItemKind::Style:
        return QCoreApplication::translate("HikariSub.Review", "Style %1").arg(qs(item.key));
    case AiReview::ItemKind::Info:
        return QCoreApplication::translate("HikariSub.Review", "Script Info %1").arg(qs(item.key));
    }
    return {};
}

QString itemDetail(const AiReview::Item &item)
{
    switch (item.kind) {
    case AiReview::ItemKind::Line: return shortText(item.after ? item.after->text : item.before->text);
    case AiReview::ItemKind::Style:
        if (!item.styleAfter)
            return QCoreApplication::translate("HikariSub.Review", "Deleted");
        return item.styleBefore ? QString() : QCoreApplication::translate("HikariSub.Review", "New");
    case AiReview::ItemKind::Info:
        return item.infoAfter ? shortText(*item.infoAfter) : QCoreApplication::translate("HikariSub.Review", "Removed");
    }
    return {};
}

absl::flat_hash_map<std::uint64_t, int> rowsOf(const core::Document &document)
{
    absl::flat_hash_map<std::uint64_t, int> rows;
    const auto lines = document.lines();
    for (std::size_t i = 0; i < lines.size(); ++i)
        rows.emplace(lines[i]->id.value, int(i) + 1);
    return rows;
}

QString diffHtml(const QString &before, const QString &after)
{
    const auto view = [](const QString &text) {
        return std::u16string_view(reinterpret_cast<const char16_t *>(text.utf16()), std::size_t(text.size()));
    };
    const auto &roles = ui::theme::current().roles;
    QColor fill = roles.success;
    fill.setAlphaF(0.22F);
    const QString added = QStringLiteral("<span style=\"background-color:%1\">").arg(fill.name(QColor::HexArgb));
    const QString removed = QStringLiteral("<s style=\"color:%1\">").arg(roles.danger.name());
    QString html;
    for (const application::DiffPart &part : application::wordDiff(view(before), view(after))) {
        const bool gone = part.kind == application::DiffPart::Kind::Removed;
        const QString run = (gone ? before : after).mid(qsizetype(part.start), qsizetype(part.length)).toHtmlEscaped();
        switch (part.kind) {
        case application::DiffPart::Kind::Same: html += run; break;
        case application::DiffPart::Kind::Removed: html += removed + run + QStringLiteral("</s>"); break;
        case application::DiffPart::Kind::Added: html += added + run + QStringLiteral("</span>"); break;
        }
    }
    return html;
}

}

ReviewController::ReviewController(Wiring wiring, QObject *parent) : QObject(parent), m_wiring(std::move(wiring)) {}

application::EditSession *ReviewController::targetSession() const
{
    const auto document = target();
    return document ? m_wiring.files.session(*document) : nullptr;
}

std::optional<core::LineId> ReviewController::activeLine() const
{
    const auto *session = targetSession();
    return session ? session->selection().active : std::nullopt;
}

void ReviewController::refreshed()
{
    m_marks.clear();
    m_waiting.clear();
    m_pending.clear();
    for (const application::DocumentId document : m_wiring.review.documents()) {
        const auto *session = m_wiring.files.session(document);
        if (!session) {
            m_wiring.review.forget(document);
            continue;
        }
        if (!on())
            continue;
        auto &marks = m_marks[document.value];
        int offGrid = 0;
        for (const AiReview::Pending &p : m_wiring.review.pending(document, session->document())) {
            m_pending[{p.change, p.item}] = p.editedSince;
            const AiReview::Item &item = m_wiring.review.change(p.change)->items[p.item];
            if (item.kind != AiReview::ItemKind::Line || !item.after) {
                ++offGrid;
                continue;
            }
            const auto [mark, fresh] = marks.try_emplace(item.line.value);
            if (fresh)
                mark->second.before = item.before;
            mark->second.editedSince = mark->second.editedSince || p.editedSince;
            mark->second.newest = p.change;
        }
        m_waiting[document.value] = int(marks.size()) + offGrid;
    }
    emit changed();
    emit currentChanged();
}

const ReviewController::Mark *ReviewController::mark(application::DocumentId document, core::LineId line) const
{
    const auto marks = m_marks.find(document.value);
    if (marks == m_marks.end())
        return nullptr;
    const auto found = marks->second.find(line.value);
    return found == marks->second.end() ? nullptr : &found->second;
}

int ReviewController::waitingIn(application::DocumentId document) const
{
    const auto found = m_waiting.find(document.value);
    return found == m_waiting.end() ? 0 : found->second;
}

int ReviewController::waiting() const
{
    const auto document = target();
    return document ? waitingIn(*document) : 0;
}

QVariantList ReviewController::turns() const
{
    QVariantList out;
    if (!on())
        return out;
    const auto documents = m_wiring.workspace.documents();
    const auto targetDocument = target();
    absl::flat_hash_map<std::uint64_t, absl::flat_hash_map<std::uint64_t, int>> rows;
    for (const application::DocumentId document : documents)
        if (const auto *session = m_wiring.files.session(document))
            rows.emplace(document.value, rowsOf(session->document()));
    const auto &turns = m_wiring.review.turns();
    for (auto turn = turns.rbegin(); turn != turns.rend(); ++turn) {
        QVariantList changes;
        bool restorable = false;
        for (const application::DocumentId document : documents) {
            const auto &rowOf = rows[document.value];
            for (const AiReview::Change *change : m_wiring.review.changes(document)) {
                if (change->turn != turn->id)
                    continue;
                QVariantList items;
                int waitingItems = 0;
                for (std::size_t i = 0; i < change->items.size(); ++i) {
                    const AiReview::Item &item = change->items[i];
                    restorable = restorable || !item.undone;
                    const auto pending = m_pending.find({change->id, i});
                    if (pending == m_pending.end())
                        continue;
                    ++waitingItems;
                    const auto row = rowOf.find(item.line.value);
                    const QString title =
                        itemTitle(item, row == rowOf.end() ? std::nullopt : std::optional<int>(row->second));
                    const QString detail = itemDetail(item);
                    items << QVariantMap{{QStringLiteral("item"), int(i)},
                                         {QStringLiteral("title"), title},
                                         {QStringLiteral("detail"), detail},
                                         {QStringLiteral("editedSince"), pending->second}};
                }
                const std::string *title = m_wiring.workspace.title(document);
                changes << QVariantMap{{QStringLiteral("change"), qulonglong(change->id)},
                                       {QStringLiteral("caller"), qs(change->caller)},
                                       {QStringLiteral("name"), qs(change->name)},
                                       {QStringLiteral("document"), qulonglong(document.value)},
                                       {QStringLiteral("title"), title ? qs(*title) : QString()},
                                       {QStringLiteral("target"), targetDocument == document},
                                       {QStringLiteral("waiting"), waitingItems},
                                       {QStringLiteral("items"), items}};
            }
        }
        if (changes.isEmpty())
            continue;
        out << QVariantMap{{QStringLiteral("turn"), qulonglong(turn->id)},
                           {QStringLiteral("number"), qulonglong(turn->number)},
                           {QStringLiteral("label"), qs(turn->label)},
                           {QStringLiteral("restorable"), restorable},
                           {QStringLiteral("changes"), changes}};
    }
    return out;
}

std::vector<core::LineId> ReviewController::waitingLinesInRowOrder() const
{
    std::vector<core::LineId> out;
    const auto document = target();
    const auto *session = targetSession();
    if (!document || !session)
        return out;
    for (const core::LineRecord *line : session->document().lines())
        if (mark(*document, line->id))
            out.push_back(line->id);
    return out;
}

QVariantMap ReviewController::current() const
{
    const auto document = target();
    const auto line = activeLine();
    const Mark *found = document && line ? mark(*document, *line) : nullptr;
    if (!found)
        return {};
    const auto *session = targetSession();
    const core::LineRecord *now = nullptr;
    for (const core::LineRecord *l : session->document().lines())
        if (l->id == *line)
            now = l;
    const auto lines = waitingLinesInRowOrder();
    const auto index = std::ranges::find(lines, *line) - lines.begin();
    const AiReview::Change *change = m_wiring.review.change(found->newest);
    return {{QStringLiteral("caller"), qs(change->caller)},
            {QStringLiteral("name"), qs(change->name)},
            {QStringLiteral("index"), int(index) + 1},
            {QStringLiteral("count"), int(lines.size())},
            {QStringLiteral("editedSince"), found->editedSince},
            {QStringLiteral("inserted"), !found->before},
            {QStringLiteral("before"), diffHtml(found->before ? qs(found->before->text) : QString(),
                                                now ? qs(now->text) : QString())}};
}

void ReviewController::keep(qulonglong change, int item)
{
    const AiReview::Change *picked = change ? m_wiring.review.change(change) : nullptr;
    const auto document = picked ? std::optional(picked->document) : target();
    const auto *session = document ? m_wiring.files.session(*document) : nullptr;
    if (!session)
        return;
    m_wiring.review.keep(*document, session->document(),
                         Target{change, item < 0 ? std::nullopt : std::optional<std::size_t>(item)});
    m_wiring.refresh();
}

int ReviewController::editedSince(qulonglong change, int item) const
{
    const AiReview::Change *picked = change ? m_wiring.review.change(change) : nullptr;
    const auto document = picked ? std::optional(picked->document) : target();
    const auto *session = document ? m_wiring.files.session(*document) : nullptr;
    if (!session)
        return 0;
    return int(m_wiring.review
                   .editedSince(*document, session->document(),
                                Target{change, item < 0 ? std::nullopt : std::optional<std::size_t>(item)})
                   .size());
}

bool ReviewController::undoIn(application::DocumentId document, Target target, bool force)
{
    auto *session = m_wiring.files.session(document);
    if (!session)
        return false;
    const auto undone = m_wiring.review.undo(document, *session, target, force);
    if (!undone) {
        m_wiring.refused(undone.error());
        return false;
    }
    if (*undone)
        m_wiring.committed(document);
    return *undone;
}

bool ReviewController::undo(qulonglong change, int item, bool force)
{
    const AiReview::Change *picked = change ? m_wiring.review.change(change) : nullptr;
    const auto document = picked ? std::optional(picked->document) : target();
    return document &&
           undoIn(*document, Target{change, item < 0 ? std::nullopt : std::optional<std::size_t>(item)}, force);
}

void ReviewController::keepLine()
{
    const auto document = target();
    const auto line = activeLine();
    if (!document || !line)
        return;
    m_wiring.review.keep(*document, targetSession()->document(), Target{.line = line});
    m_wiring.refresh();
}

int ReviewController::lineEditedSince() const
{
    const auto document = target();
    const auto line = activeLine();
    if (!document || !line)
        return 0;
    return int(m_wiring.review.editedSince(*document, targetSession()->document(), Target{.line = line}).size());
}

bool ReviewController::undoLine(bool force)
{
    const auto document = target();
    const auto line = activeLine();
    return document && line && undoIn(*document, Target{.line = line}, force);
}

bool ReviewController::step(int direction)
{
    const auto lines = waitingLinesInRowOrder();
    if (lines.empty())
        return false;
    const auto *session = targetSession();
    const auto rows = rowsOf(session->document());
    const auto active = session->selection().active;
    const int from = active && rows.contains(active->value) ? rows.at(active->value) : 0;
    std::optional<core::LineId> next;
    if (direction >= 0) {
        const auto after = std::ranges::find_if(lines, [&](core::LineId l) { return rows.at(l.value) > from; });
        next = after != lines.end() ? *after : lines.front();
    } else {
        const auto before =
            std::ranges::find_if(lines.rbegin(), lines.rend(), [&](core::LineId l) { return rows.at(l.value) < from; });
        next = before != lines.rend() ? *before : lines.back();
    }
    return m_wiring.selectInTarget(application::Selection{next, {*next}, std::nullopt, std::nullopt});
}

void ReviewController::show(qulonglong change, int item)
{
    const AiReview::Change *picked = m_wiring.review.change(change);
    if (!picked || target() != picked->document)
        return;
    const auto *session = targetSession();
    const auto rows = rowsOf(session->document());
    for (std::size_t i = 0; i < picked->items.size(); ++i) {
        const AiReview::Item &it = picked->items[i];
        if ((item >= 0 && std::size_t(item) != i) || it.kind != AiReview::ItemKind::Line ||
            !rows.contains(it.line.value))
            continue;
        m_wiring.selectInTarget(application::Selection{it.line, {it.line}, std::nullopt, std::nullopt});
        return;
    }
}

QVariantList ReviewController::checkpoint(qulonglong turn) const
{
    QVariantList out;
    const auto sessions = [this](application::DocumentId document) { return m_wiring.files.session(document); };
    for (const AiReview::CheckpointDocument &doc : m_wiring.review.checkpoint(turn, sessions)) {
        const std::string *title = m_wiring.workspace.title(doc.document);
        out << QVariantMap{{QStringLiteral("title"), title ? qs(*title) : QString()},
                           {QStringLiteral("lines"), int(doc.lines)},
                           {QStringLiteral("others"), int(doc.others)},
                           {QStringLiteral("editedSince"), int(doc.editedSince.size())}};
    }
    return out;
}

bool ReviewController::restore(qulonglong turn, bool force)
{
    std::vector<application::DocumentId> touched;
    const auto sessions = [&](application::DocumentId document) {
        touched.push_back(document);
        return m_wiring.files.session(document);
    };
    const auto restored = m_wiring.review.restore(turn, sessions, force);
    if (!restored) {
        m_wiring.refused(restored.error());
        return false;
    }
    std::ranges::sort(touched);
    const auto [first, last] = std::ranges::unique(touched);
    touched.erase(first, last);
    for (const application::DocumentId document : touched)
        m_wiring.committed(document);
    return *restored > 0;
}

bool ReviewController::isAiStep(int step) const
{
    const auto document = target();
    const auto *session = targetSession();
    if (!document || !session || step < 0)
        return false;
    const auto history = session->history();
    return std::size_t(step) < history.size() && m_wiring.review.isAiStep(*document, history[std::size_t(step)].content);
}

int ReviewController::turnBefore(int step) const
{
    const auto document = target();
    const auto *session = targetSession();
    if (!document || !session || step < 0)
        return 0;
    const auto history = session->history();
    if (std::size_t(step) >= history.size())
        return 0;
    const auto turn = m_wiring.review.turnStartingAt(*document, history[std::size_t(step)].content);
    const auto *found = turn ? m_wiring.review.turn(*turn) : nullptr;
    return found ? int(found->number) : 0;
}

}
