#include "line_editor_controller.h"

#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/text_projection.h"

#include <QTextBoundaryFinder>

namespace hikari::ui {

namespace {

QString qs(std::u8string_view s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

std::u8string u8(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return std::u8string(reinterpret_cast<const char8_t *>(b.constData()), static_cast<std::size_t>(b.size()));
}

std::u16string u16(const QString &s)
{
    return std::u16string(reinterpret_cast<const char16_t *>(s.utf16()), static_cast<std::size_t>(s.size()));
}

QString timeText(core::DocumentTime t)
{
    return qs(core::legacy::assTimeText(t.microseconds() / 1000));
}

QString refusalText(core::MapRefusal r)
{
    switch (r) {
    case core::MapRefusal::SplitsCharacter: return QObject::tr("Select whole characters.");
    case core::MapRefusal::AssSyntax: return QObject::tr("Braces and backslashes are ASS syntax: edit them with tags shown.");
    case core::MapRefusal::Protected: return QObject::tr("This touches a drawing or malformed tag: edit it with tags shown.");
    case core::MapRefusal::SplitsToken: return QObject::tr("Select the whole line break or hard space.");
    case core::MapRefusal::Reinterpreted: return QObject::tr("Here the text would become drawing or tag content: edit it with tags shown.");
    }
    return {};
}

} // namespace

LineEditorController::LineEditorController(application::DocumentFiles &files, QObject *parent)
    : QObject(parent), m_files(files)
{
}

application::EditSession *LineEditorController::session() const
{
    return m_document ? m_files.session(*m_document) : nullptr;
}

void LineEditorController::setDocument(std::optional<application::DocumentId> document, bool editable)
{
    m_document = document;
    m_editable = editable;
    m_draftUndo.clear();
    m_draftRedo.clear();
    m_problem.clear();
    m_attempted.clear();
    refresh();
}

std::optional<core::LineRecord> LineEditorController::record() const
{
    auto *s = session();
    if (!s || !s->selection().active)
        return std::nullopt;
    if (s->draftLine() == s->selection().active)
        return s->draftRecord();
    for (const auto *line : s->document().lines())
        if (line->id == *s->selection().active)
            return *line;
    return std::nullopt;
}

bool LineEditorController::hasLine() const
{
    return record().has_value();
}

void LineEditorController::setShowTags(bool show)
{
    if (m_showTags == show)
        return;
    m_showTags = show;
    refresh();
}

QString LineEditorController::startText() const
{
    const auto r = record();
    return r ? timeText(r->start.value) : QString();
}

QString LineEditorController::endText() const
{
    const auto r = record();
    return r ? timeText(r->end.value) : QString();
}

QString LineEditorController::marginLeftText() const
{
    const auto r = record();
    return r ? QString::number(r->marginLeft.value) : QString();
}

QString LineEditorController::marginRightText() const
{
    const auto r = record();
    return r ? QString::number(r->marginRight.value) : QString();
}

QString LineEditorController::marginVerticalText() const
{
    const auto r = record();
    return r ? QString::number(r->marginVertical.value) : QString();
}

bool LineEditorController::dirty() const
{
    auto *s = session();
    return s && s->isDirty();
}

QString LineEditorController::saveStatus() const
{
    const auto status = m_document ? m_files.lastSave(*m_document) : std::nullopt;
    if (!status)
        return {};
    if (!status->outcome)
        return tr("Saving…");
    switch (*status->outcome) {
    case application::WriteOutcome::Written: return tr("Saved");
    case application::WriteOutcome::DurabilityUncertain: return tr("Saved; the disk could not confirm it is stored");
    case application::WriteOutcome::Cancelled: return tr("Save cancelled; nothing was written");
    case application::WriteOutcome::Failed: return tr("Save failed; the file on disk is unchanged");
    }
    return {};
}

QString LineEditorController::problemText() const
{
    auto *s = session();
    const auto problem = s ? s->draftProblem() : std::nullopt;
    if (!problem || s->invalidCommitPolicy() == application::InvalidCommitPolicy::Legacy)
        return {};
    return *problem == application::DraftProblem::EndBeforeStart ? tr("End is before Start.")
                                                                 : tr("Margins must be between 0 and 9999.");
}

void LineEditorController::refresh()
{
    const auto r = record();
    if (!r)
        m_text.clear();
    else if (m_showTags)
        m_text = qs(r->text);
    else {
        const auto p = core::project(core::toUtf16(r->text));
        m_text = QString::fromUtf16(p.text.data(), static_cast<qsizetype>(p.text.size()));
    }
    if (m_attempted.isEmpty())
        m_problem = problemText();
    emit changed();
}

void LineEditorController::fail(const QString &problem, const QString &attempted)
{
    m_problem = problem;
    m_attempted = attempted;
    emit changed();
}

bool LineEditorController::showLine(qulonglong id)
{
    auto *s = session();
    if (!s)
        return false;
    const auto before = s->historySize();
    const auto revision = s->revision();
    if (!s->navigateTo(core::LineId{id})) {
        fail(problemText());
        if (s->selection().active)
            emit lineChanged(s->selection().active->value);
        return false;
    }
    m_draftUndo.clear();
    m_draftRedo.clear();
    m_attempted.clear();
    if (s->historySize() != before || s->revision() != revision)
        committed();
    refresh();
    return true;
}

bool LineEditorController::setRaw(std::u8string raw)
{
    auto *s = session();
    const auto r = record();
    if (!s || !r || !m_editable)
        return false;
    if (raw == r->text)
        return true;
    if (!s->editDraftText(r->id, raw))
        return false;
    m_draftUndo.push_back(r->text);
    m_draftRedo.clear();
    m_attempted.clear();
    refresh();
    return true;
}

void LineEditorController::textEdited(const QString &newText, int cursor)
{
    if (!editable()) {
        refresh(); // a protected or empty editor never changes content
        return;
    }
    if (newText == m_text)
        return;
    const QString old = m_text;
    // Locate the change: the common suffix may not reach past the caret, so
    // repeated characters resolve to where the user actually typed.
    const qsizetype caret = std::clamp<qsizetype>(cursor, 0, newText.size());
    qsizetype suffix = 0;
    while (suffix < old.size() && suffix < newText.size() - caret &&
           old[old.size() - 1 - suffix] == newText[newText.size() - 1 - suffix])
        ++suffix;
    qsizetype prefix = 0;
    const qsizetype maxPrefix = std::min(old.size(), newText.size()) - suffix;
    while (prefix < maxPrefix && old[prefix] == newText[prefix])
        ++prefix;
    const QString inserted = newText.mid(prefix, newText.size() - suffix - prefix);
    const auto start = static_cast<std::size_t>(prefix);
    const auto end = static_cast<std::size_t>(old.size() - suffix);

    if (m_showTags) {
        setRaw(u8(newText));
        return;
    }
    std::vector<std::size_t> bounds{0};
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, old);
    for (qsizetype b; (b = finder.toNextBoundary()) >= 0;)
        bounds.push_back(static_cast<std::size_t>(b));
    const auto raw = core::toUtf16(record()->text);
    const auto result = core::mappedReplace(raw, start, end, u16(inserted), bounds);
    if (!result) {
        refresh(); // the field shows the unchanged source again
        fail(refusalText(result.error()), newText);
        return;
    }
    setRaw(core::toUtf8(*result));
}

void LineEditorController::setStartText(const QString &text)
{
    const auto r = record();
    if (!editable() || !r)
        return;
    if (!core::legacy::isCanonicalAssTime(u8(text))) {
        fail(tr("Start is not a time such as 0:00:01.00."), text);
        return;
    }
    const core::DocumentTime t(core::legacy::assTimeMilliseconds(u8(text)) * 1000);
    m_attempted.clear();
    session()->editDraft(r->id, application::DraftChange{.start = t});
    refresh();
}

void LineEditorController::setEndText(const QString &text)
{
    const auto r = record();
    if (!editable() || !r)
        return;
    if (!core::legacy::isCanonicalAssTime(u8(text))) {
        fail(tr("End is not a time such as 0:00:01.00."), text);
        return;
    }
    const core::DocumentTime t(core::legacy::assTimeMilliseconds(u8(text)) * 1000);
    m_attempted.clear();
    session()->editDraft(r->id, application::DraftChange{.end = t});
    refresh();
}

void LineEditorController::setMarginText(int which, const QString &text)
{
    const auto r = record();
    if (!editable() || !r)
        return;
    bool ok = false;
    const qlonglong value = text.trimmed().toLongLong(&ok);
    if (!ok) {
        if (session()->invalidCommitPolicy() == application::InvalidCommitPolicy::Legacy) {
            refresh(); // NumCtrl keeps the previous value
            return;
        }
        fail(tr("Margins must be whole numbers."), text);
        return;
    }
    application::DraftChange change;
    (which == 0 ? change.marginLeft : which == 1 ? change.marginRight : change.marginVertical) = value;
    m_attempted.clear();
    session()->editDraft(r->id, change);
    refresh();
}

bool LineEditorController::commit()
{
    auto *s = session();
    if (!s || !s->draftLine())
        return false;
    if (!s->commitDraft()) {
        if (s->draftLine()) {
            fail(problemText());
            return false;
        }
    }
    committed();
    return true;
}

bool LineEditorController::commitAndAdvance()
{
    auto *s = session();
    const auto r = record();
    if (!s || !r)
        return false;
    const auto lines = s->document().lines();
    const auto it = std::ranges::find_if(lines, [&](const core::LineRecord *l) { return l->id == r->id; });
    if (it == lines.end() || it + 1 == lines.end())
        return commit(); // the last Line: commit and stay
    if (!showLine((*(it + 1))->id.value))
        return false;
    emit lineChanged((*(it + 1))->id.value);
    return true;
}

void LineEditorController::discard()
{
    if (auto *s = session())
        s->discardDraft();
    m_draftUndo.clear();
    m_draftRedo.clear();
    m_attempted.clear();
    refresh();
}

bool LineEditorController::undo()
{
    auto *s = session();
    if (!s)
        return false;
    const auto r = record();
    if (!m_draftUndo.empty() && r) {
        m_draftRedo.push_back(r->text);
        const std::u8string previous = std::move(m_draftUndo.back());
        m_draftUndo.pop_back();
        s->editDraftText(r->id, previous);
        // Back at the committed Line: no draft remains.
        const auto lines = s->document().lines();
        const auto it = std::ranges::find_if(lines, [&](const core::LineRecord *l) { return l->id == r->id; });
        const auto d = s->draftRecord();
        if (it != lines.end() && d && d->text == (*it)->text && d->start.value == (*it)->start.value &&
            d->end.value == (*it)->end.value && d->marginLeft.value == (*it)->marginLeft.value &&
            d->marginRight.value == (*it)->marginRight.value &&
            d->marginVertical.value == (*it)->marginVertical.value)
            s->discardDraft();
        m_attempted.clear();
        refresh();
        return true;
    }
    const auto before = s->revision();
    if (!s->undo()) {
        fail(problemText());
        return false;
    }
    if (s->revision() != before)
        committed();
    if (s->selection().active)
        emit lineChanged(s->selection().active->value);
    return true;
}

bool LineEditorController::redo()
{
    auto *s = session();
    if (!s)
        return false;
    const auto r = record();
    if (!m_draftRedo.empty() && r) {
        m_draftUndo.push_back(r->text);
        const std::u8string next = std::move(m_draftRedo.back());
        m_draftRedo.pop_back();
        s->editDraftText(r->id, next);
        refresh();
        return true;
    }
    if (!s->redo())
        return false;
    committed();
    if (s->selection().active)
        emit lineChanged(s->selection().active->value);
    return true;
}

bool LineEditorController::save()
{
    if (!m_document)
        return false;
    auto plan = m_files.prepareSave(*m_document);
    if (!plan) {
        fail(plan.error() == application::SaveRefusal::InvalidDraft ? problemText() : tr("This Document can't be saved here."));
        return false;
    }
    committed(); // Save committed the draft first
    auto started = m_files.startSave(std::move(*plan));
    if (!started) {
        switch (started.error()) {
        case application::SaveRefusal::ExternalChange:
            fail(tr("The file changed on disk since it was opened. Reload it, or save elsewhere."));
            break;
        case application::SaveRefusal::Collision:
            fail(tr("Another open Document uses that file."));
            break;
        default:
            fail(tr("The Document can't be saved right now."));
            break;
        }
        return false;
    }
    refresh();
    return true;
}

void LineEditorController::writeFinished()
{
    refresh();
}

void LineEditorController::committed()
{
    m_draftUndo.clear();
    m_draftRedo.clear();
    if (m_onCommitted)
        m_onCommitted();
    refresh();
}

} // namespace hikari::ui
