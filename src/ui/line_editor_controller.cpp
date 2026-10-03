#include "line_editor_controller.h"

#include "hikari/core/ass_load.h"
#include "hikari/core/checked.h"
#include "hikari/core/editor_font_colour.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/style.h"
#include "hikari/core/tag_commands.h"
#include "hikari/core/text_projection.h"

#include <QTextBoundaryFinder>

#include <algorithm>
#include <cstdlib>

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

bool LineEditorController::translationMode() const
{
    auto *s = session();
    return s && s->document().scriptInfo(u8"TLMode") == u8"Yes";
}

void LineEditorController::refresh()
{
    const auto r = record();
    for (int role = 0; role < 2; ++role) {
        if (!r)
            m_shown[role].clear();
        else if (m_showTags)
            m_shown[role] = qs(roleText(*r, role));
        else {
            const auto p = core::project(core::toUtf16(roleText(*r, role)));
            m_shown[role] = QString::fromUtf16(p.text.data(), static_cast<qsizetype>(p.text.size()));
        }
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

bool LineEditorController::setRaw(int role, std::u8string raw)
{
    auto *s = session();
    const auto r = record();
    if (!s || !r || !m_editable)
        return false;
    if (raw == roleText(*r, role))
        return true;
    application::DraftChange change;
    (role == 0 ? change.text : change.translation) = std::move(raw);
    if (!s->editDraft(r->id, change))
        return false;
    m_draftUndo.push_back({r->text, r->translation});
    m_draftRedo.clear();
    m_attempted.clear();
    refresh();
    return true;
}

void LineEditorController::textEdited(const QString &newText, int cursor)
{
    edit(0, newText, cursor);
}

void LineEditorController::translationEdited(const QString &newText, int cursor)
{
    edit(1, newText, cursor);
}

void LineEditorController::edit(int role, const QString &newText, int cursor)
{
    if (!editable()) {
        refresh(); // a protected or empty editor never changes content
        return;
    }
    if (newText == m_shown[role])
        return;
    const QString old = m_shown[role];
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
        setRaw(role, u8(newText));
        return;
    }
    std::vector<std::size_t> bounds{0};
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, old);
    for (qsizetype b; (b = finder.toNextBoundary()) >= 0;)
        bounds.push_back(static_cast<std::size_t>(b));
    const auto raw = core::toUtf16(roleText(*record(), role));
    const auto result = core::mappedReplace(raw, start, end, u16(inserted), bounds);
    if (!result) {
        refresh(); // the field shows the unchanged source again
        fail(refusalText(result.error()), newText);
        return;
    }
    setRaw(role, core::toUtf8(*result));
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
    if (it == lines.end())
        return false;
    if (it + 1 == lines.end()) {
        // SubsGrid::NextLine on the last Line: commit, then append a copy of it
        // starting at its End, five seconds long, with no text (its own step).
        if (s->draftLine() && !s->commitDraft() && s->draftLine()) {
            fail(problemText());
            return false;
        }
        const core::LineRecord last = **(s->document().lines().end() - 1);
        core::LineRecord next = last;
        next.text.clear();
        next.translation.clear();
        next.start.value = last.end.value;
        next.end.value = core::DocumentTime(last.end.value.microseconds() + 5'000'000);
        if (s->document().format() == core::SubtitleFormat::MicroDvd) {
            // Frames: the new Line starts at the last end frame; its end is the
            // frame at End + 5 s rounded up, as legacy SubsTime computes it.
            // Without the Document's own rate (C01-fps-isolation) the end
            // frame is left empty rather than guessed.
            next.startFrame = last.endFrame;
            next.endFrame.reset();
            if (const auto &rate = s->document().frameRate()) {
                const auto &fps = rate->framesPerSecond();
                if (const auto frame = core::mulDiv(next.end.value.microseconds(), fps.numerator(),
                                                    fps.denominator(), 1'000'000, core::Rounding::Ceil))
                    next.endFrame = *frame;
            }
        }
        std::optional<core::LineId> added;
        const auto result = s->run(application::Command{
            "Append Line", s->revision(), {last.id}, [&](core::Document &d) {
                added = d.insertLineAfter(last.id, next);
                return added.has_value();
            }});
        if (!result || !added)
            return false;
        committed();
        if (!showLine(added->value))
            return false;
        emit lineChanged(added->value);
        return true;
    }
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

void LineEditorController::placeCaretAfterChange(const QString (&before)[2])
{
    for (int role = 0; role < 2; ++role) {
        const QString &a = before[role], &b = m_shown[role];
        if (a == b)
            continue;
        qsizetype prefix = 0;
        while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix])
            ++prefix;
        qsizetype suffix = 0;
        while (suffix < a.size() - prefix && suffix < b.size() - prefix &&
               a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix])
            ++suffix;
        qsizetype caret = b.size() - suffix;
        if (caret > 0 && caret < b.size() && b[caret].isLowSurrogate())
            ++caret; // never between the halves of a surrogate pair
        m_selectionStart = m_selectionEnd = static_cast<int>(caret);
        m_selectionRole = role;
        emit selectionRequested();
        return;
    }
}

bool LineEditorController::undo()
{
    auto *s = session();
    if (!s)
        return false;
    const auto r = record();
    const QString before[2] = {m_shown[0], m_shown[1]};
    const auto line = s->selection().active;
    if (!m_draftUndo.empty() && r) {
        m_draftRedo.push_back({r->text, r->translation});
        const Snapshot previous = std::move(m_draftUndo.back());
        m_draftUndo.pop_back();
        s->editDraft(r->id, application::DraftChange{.text = previous.text, .translation = previous.translation});
        // Back at the committed Line: no draft remains.
        const auto lines = s->document().lines();
        const auto it = std::ranges::find_if(lines, [&](const core::LineRecord *l) { return l->id == r->id; });
        const auto d = s->draftRecord();
        if (it != lines.end() && d && d->text == (*it)->text && d->translation == (*it)->translation &&
            d->start.value == (*it)->start.value && d->end.value == (*it)->end.value &&
            d->marginLeft.value == (*it)->marginLeft.value && d->marginRight.value == (*it)->marginRight.value &&
            d->marginVertical.value == (*it)->marginVertical.value)
            s->discardDraft();
        m_attempted.clear();
        refresh();
        placeCaretAfterChange(before);
        return true;
    }
    const auto revision = s->revision();
    if (!s->undo()) {
        fail(problemText());
        return false;
    }
    if (s->revision() != revision)
        committed();
    if (s->selection().active == line)
        placeCaretAfterChange(before);
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
    const QString before[2] = {m_shown[0], m_shown[1]};
    const auto line = s->selection().active;
    if (!m_draftRedo.empty() && r) {
        m_draftUndo.push_back({r->text, r->translation});
        const Snapshot next = std::move(m_draftRedo.back());
        m_draftRedo.pop_back();
        s->editDraft(r->id, application::DraftChange{.text = next.text, .translation = next.translation});
        refresh();
        placeCaretAfterChange(before);
        return true;
    }
    if (!s->redo())
        return false;
    committed();
    if (s->selection().active == line)
        placeCaretAfterChange(before);
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

bool LineEditorController::toggleTag(const QString &tag, int selectionStart, int selectionEnd)
{
    return toggleTagIn(0, tag, selectionStart, selectionEnd);
}

bool LineEditorController::toggleTagIn(int role, const QString &tag, int selectionStart, int selectionEnd)
{
    const auto r = record();
    if (!editable() || !r || tag.size() != 1 || !QStringLiteral("bius").contains(tag))
        return false;
    // The Style's value; legacy GetStyle(0, name) takes the first Style of that name.
    bool styleValue = false;
    for (const auto &style : core::decodeStyles(session()->document()))
        if (style.name == r->style) {
            const char16_t t = tag[0].unicode();
            styleValue = t == u'b' ? style.bold : t == u'i' ? style.italic : t == u'u' ? style.underline
                                                                                      : style.strikeOut;
            break;
        }
    // Legacy subsFormat < SRT (ASS, and plain text, which loads as ASS) uses
    // override tags; SRT and MicroDVD use their own markup (PutinNonass).
    const auto format = session()->document().format();
    if (format == core::SubtitleFormat::Ass || format == core::SubtitleFormat::PlainText)
        return editRaw(role, selectionStart, selectionEnd, [&](core::legacy::EditorText t) {
            return core::legacy::toggleTag(std::move(t), tag[0].unicode(), styleValue);
        });
    if (format == core::SubtitleFormat::Srt || format == core::SubtitleFormat::MicroDvd)
        return editRaw(role, selectionStart, selectionEnd, [&](core::legacy::EditorText t) {
            return core::legacy::toggleNonAssTag(std::move(t), tag[0].unicode(), format == core::SubtitleFormat::Srt);
        });
    return false; // MPL2 and TMPlayer: no legacy action
}

bool LineEditorController::editRaw(int role, int selectionStart, int selectionEnd,
                                   const std::function<core::legacy::EditorText(core::legacy::EditorText)> &change)
{
    const auto r = record();
    if (!r)
        return false;
    const std::u16string raw = core::toUtf16(roleText(*r, role));
    long from = selectionStart, to = selectionEnd;
    std::optional<core::Projection> projection;
    if (!m_showTags) {
        projection = core::project(raw);
        // The caret goes after boundary tags; a selection's end before them.
        from = static_cast<long>(core::rawOffset(*projection, static_cast<std::size_t>(selectionStart), true));
        to = selectionEnd == selectionStart
                 ? from
                 : static_cast<long>(core::rawOffset(*projection, static_cast<std::size_t>(selectionEnd), false));
    }
    const core::legacy::EditorText result = change({raw, from, to});
    if (!setRaw(role, core::toUtf8(result.text)))
        return false;
    if (m_showTags) {
        m_selectionStart = static_cast<int>(result.selectionStart);
        m_selectionEnd = static_cast<int>(result.selectionEnd);
    } else {
        const auto after = core::project(result.text);
        m_selectionStart = static_cast<int>(core::displayOffset(after, static_cast<std::size_t>(result.selectionStart)));
        m_selectionEnd = static_cast<int>(core::displayOffset(after, static_cast<std::size_t>(result.selectionEnd)));
    }
    m_selectionRole = role;
    emit selectionRequested();
    return true;
}

std::optional<std::u16string> LineEditorController::styleTagValue(std::u16string_view tag) const
{
    const auto r = record();
    if (!r)
        return std::nullopt;
    // Legacy GetStyle(0, name): the first Style of that name.
    for (const auto &style : core::decodeStyles(session()->document()))
        if (style.name == r->style) {
            const auto value = core::legacy::styleTagValue(style, core::toUtf8(std::u16string(tag)));
            return value ? std::optional(core::toUtf16(*value)) : std::nullopt;
        }
    return std::nullopt;
}

bool LineEditorController::applyTagButton(int role, const QString &tag, int type, int selectionStart, int selectionEnd)
{
    const auto r = record();
    if (!editable() || !r || tag.isEmpty() || type < 0 || type > 2)
        return false;
    const std::u16string text = tag.toStdU16String();
    if (type != 2)
        return editRaw(role, selectionStart, selectionEnd, [&](core::legacy::EditorText t) {
            return core::legacy::applyTagButton(std::move(t), text, type == 1,
                                                [this](std::u16string_view name) { return styleTagValue(name); });
        });
    auto *s = session();
    if (s->selection().selected.size() < 2)
        return editRaw(role, selectionStart, selectionEnd, [&](core::legacy::EditorText t) {
            return core::legacy::insertTagButtonText(std::move(t), text);
        });
    // Several Lines: the text goes into each selected Line's text (its
    // translation when it has one) at the caret, as one step.
    long from = selectionStart;
    if (!m_showTags)
        from = static_cast<long>(core::rawOffset(core::project(core::toUtf16(roleText(*r, role))),
                                                 static_cast<std::size_t>(selectionStart), true));
    if (s->draftLine() && !s->commitDraft())
        return false;
    std::vector<core::LineId> lines;
    for (const auto *l : s->document().lines())
        if (s->selection().selected.contains(l->id))
            lines.push_back(l->id);
    const auto ran = s->run(application::Command{
        "Editing multiple lines", s->revision(), {lines.begin(), lines.end()}, [&](core::Document &d) {
            for (const auto id : lines)
                if (!d.editLine(id, [&](core::LineRecord &l) {
                        auto &field = l.translation.empty() ? l.text : l.translation;
                        field = core::toUtf8(core::legacy::insertTagButtonTextAt(core::toUtf16(field), from, text));
                    }))
                    return false;
            return true;
        }});
    if (!ran)
        return false;
    reloadFromSession();
    if (m_onCommitted)
        m_onCommitted();
    return true;
}

std::optional<core::StyleValues> LineEditorController::lineStyle() const
{
    const auto r = record();
    if (!r)
        return std::nullopt;
    // Legacy GetStyle(0, name): the first Style of that name.
    for (auto &style : core::decodeStyles(session()->document()))
        if (style.name == r->style)
            return style;
    return std::nullopt;
}

bool LineEditorController::beginDialog(int role, int selectionStart, int selectionEnd)
{
    m_dialog.reset();
    const auto r = record();
    if (!editable() || !r)
        return false;
    auto *s = session();
    DialogSession d;
    d.role = role;
    d.several = s->selection().selected.size() >= 2;
    const auto format = s->document().format();
    d.ass = format == core::SubtitleFormat::Ass || format == core::SubtitleFormat::PlainText;
    d.format = format == core::SubtitleFormat::Srt        ? core::legacy::NonAssFormat::Srt
               : format == core::SubtitleFormat::MicroDvd ? core::legacy::NonAssFormat::MicroDvd
               : format == core::SubtitleFormat::Mpl2     ? core::legacy::NonAssFormat::Mpl2
                                                          : core::legacy::NonAssFormat::TmPlayer;
    d.original = roleText(*r, role);
    const std::u16string raw = core::toUtf16(d.original);
    long from = selectionStart, to = selectionEnd;
    if (!m_showTags) {
        const auto projection = core::project(raw);
        from = static_cast<long>(core::rawOffset(projection, static_cast<std::size_t>(selectionStart), true));
        to = selectionEnd == selectionStart
                 ? from
                 : static_cast<long>(core::rawOffset(projection, static_cast<std::size_t>(selectionEnd), false));
    }
    // Several Lines: legacy FindTag searches from position 0.
    d.state = d.several ? core::legacy::EditorText{raw, 0, 0} : core::legacy::EditorText{raw, from, to};
    d.draftUndo = m_draftUndo.size();
    d.hadDraft = s->draftLine().has_value();
    d.opened = core::legacy::EditorText{raw, from, to};
    d.historyCursor = s->historyCursor();
    m_dialog = std::move(d);
    return true;
}

void LineEditorController::publishRawSelection(const core::legacy::EditorText &state)
{
    if (m_showTags) {
        m_selectionStart = static_cast<int>(state.selectionStart);
        m_selectionEnd = static_cast<int>(state.selectionEnd);
    } else {
        const auto shown = core::project(state.text);
        m_selectionStart = static_cast<int>(core::displayOffset(shown, static_cast<std::size_t>(state.selectionStart)));
        m_selectionEnd = static_cast<int>(core::displayOffset(shown, static_cast<std::size_t>(state.selectionEnd)));
    }
    m_selectionRole = m_dialog ? m_dialog->role : 0;
    emit selectionRequested();
}

bool LineEditorController::applyDialogChange(
    const std::function<core::legacy::StepResult(const core::legacy::EditorText &, long)> &one,
    const std::function<void(std::u16string &, std::u16string &)> &several)
{
    if (!m_dialog || !editable())
        return false;
    auto &d = *m_dialog;
    auto *s = session();
    if (!d.several) {
        auto result = one(d.state, d.position);
        if (result.state.text != d.state.text && !setRaw(d.role, core::toUtf8(result.state.text)))
            return false;
        d.state = std::move(result.state);
        d.position = result.position;
        publishRawSelection(d.state);
        return true;
    }
    // Legacy PutTagInText/PutinNonass with several Lines: every selected Line,
    // one "Editing multiple lines" step per change.
    if (s->draftLine() && !s->commitDraft())
        return false;
    std::vector<core::LineId> lines;
    for (const auto *l : s->document().lines())
        if (s->selection().selected.contains(l->id))
            lines.push_back(l->id);
    bool changedAny = false;
    const auto ran = s->run(application::Command{
        "Editing multiple lines", s->revision(), {lines.begin(), lines.end()}, [&](core::Document &doc) {
            for (const auto id : lines)
                if (!doc.editLine(id, [&](core::LineRecord &l) {
                        std::u16string text = core::toUtf16(l.text), translation = core::toUtf16(l.translation);
                        several(text, translation);
                        const auto t8 = core::toUtf8(text), tr8 = core::toUtf8(translation);
                        changedAny = changedAny || t8 != l.text || tr8 != l.translation;
                        l.text = t8;
                        l.translation = tr8;
                    }))
                    return false;
            return changedAny;
        }});
    if (!ran)
        return !changedAny;
    reloadFromSession();
    if (m_onCommitted)
        m_onCommitted();
    return true;
}

namespace {

QVariantMap fontMap(const core::legacy::FontValues &f)
{
    return {{QStringLiteral("name"), QString::fromStdU16String(f.name)},
            {QStringLiteral("size"), QString::fromStdU16String(f.size)},
            {QStringLiteral("bold"), f.bold},
            {QStringLiteral("italic"), f.italic},
            {QStringLiteral("underline"), f.underline},
            {QStringLiteral("strikeOut"), f.strikeOut}};
}

core::legacy::FontValues fontOf(const QVariantMap &m)
{
    return {m.value(QStringLiteral("name")).toString().toStdU16String(),
            m.value(QStringLiteral("size")).toString().toStdU16String(),
            m.value(QStringLiteral("bold")).toBool(),
            m.value(QStringLiteral("italic")).toBool(),
            m.value(QStringLiteral("underline")).toBool(),
            m.value(QStringLiteral("strikeOut")).toBool()};
}

QVariantMap colourMap(const core::legacy::TagColour &c)
{
    return {{QStringLiteral("r"), c.r}, {QStringLiteral("g"), c.g}, {QStringLiteral("b"), c.b}, {QStringLiteral("a"), c.a}};
}

core::legacy::TagColour colourOf(const QVariantMap &m)
{
    return {m.value(QStringLiteral("r")).toInt(), m.value(QStringLiteral("g")).toInt(), m.value(QStringLiteral("b")).toInt(),
            m.value(QStringLiteral("a")).toInt()};
}

core::legacy::TagColour tagColour(const core::Colour &c)
{
    return {static_cast<int>(c.r), static_cast<int>(c.g), static_cast<int>(c.b), static_cast<int>(c.a)};
}

} // namespace

QVariantMap LineEditorController::beginFont(int role, int selectionStart, int selectionEnd)
{
    if (!beginDialog(role, selectionStart, selectionEnd))
        return {};
    auto &d = *m_dialog;
    core::legacy::FontValues font = core::legacy::defaultFontValues();
    if (d.ass) {
        // Legacy GetStyle(0, name) falls back to the default Style.
        if (const auto style = lineStyle())
            font = {core::toUtf16(style->fontname), core::toUtf16(style->fontsize), style->bold, style->italic,
                    style->underline, style->strikeOut};
        font = core::legacy::fontInEffect(d.state, font, &d.position);
    }
    d.actualFont = d.editedFont = font;
    return fontMap(font);
}

bool LineEditorController::changeFont(const QVariantMap &font)
{
    if (!m_dialog)
        return false;
    auto &d = *m_dialog;
    const auto result = fontOf(font);
    const auto steps = core::legacy::fontSteps(d.editedFont, result, d.actualFont, d.ass);
    d.editedFont = result;
    if (steps.empty())
        return true;
    return applyDialogChange(
        [&](const core::legacy::EditorText &state, long position) {
            return core::legacy::applySteps(state, steps, d.format, position);
        },
        [&](std::u16string &text, std::u16string &translation) {
            core::legacy::applyStepsToLine(text, translation, steps, d.format);
        });
}

QVariantMap LineEditorController::beginColour(int number, int role, int selectionStart, int selectionEnd)
{
    if (number < 1 || number > 4 || !beginDialog(role, selectionStart, selectionEnd))
        return {};
    m_dialog->number = number;
    return switchColour(number);
}

QVariantMap LineEditorController::switchColour(int number)
{
    if (!m_dialog || number < 1 || number > 4)
        return {};
    auto &d = *m_dialog;
    d.number = number;
    if (d.ass) {
        core::legacy::TagColour style{255, 255, 255, 0};
        if (const auto s = lineStyle())
            style = tagColour(number == 1 ? s->primary : number == 2 ? s->secondary : number == 3 ? s->outline : s->back);
        m_lastColour = core::legacy::colourInEffect(d.state, number, style, &d.position);
    }
    // The line formats keep the last colour (legacy actualColor is not read).
    d.actualColour = m_lastColour;
    return colourMap(d.actualColour);
}

bool LineEditorController::changeColour(const QVariantMap &colour)
{
    if (!m_dialog)
        return false;
    auto &d = *m_dialog;
    const auto chosen = colourOf(colour);
    if (!d.ass) {
        const std::vector<core::legacy::EditStep> steps{core::legacy::colourNonAssStep(chosen)};
        return applyDialogChange(
            [&](const core::legacy::EditorText &state, long position) {
                return core::legacy::applySteps(state, steps, d.format, position);
            },
            [&](std::u16string &text, std::u16string &translation) {
                core::legacy::applyStepsToLine(text, translation, steps, d.format);
            });
    }
    return applyDialogChange(
        [&](const core::legacy::EditorText &state, long) {
            return core::legacy::changeColour(state, d.number, d.actualColour, chosen);
        },
        [&](std::u16string &text, std::u16string &translation) {
            std::u16string &field = translation.empty() ? text : translation;
            field = core::legacy::changeColourInLine(std::move(field), d.number, d.actualColour, chosen);
        });
}

void LineEditorController::endDialog(bool accepted)
{
    if (!m_dialog)
        return;
    DialogSession d = std::move(*m_dialog);
    m_dialog.reset();
    auto *s = session();
    if (!s)
        return;
    if (!accepted) {
        // Legacy DummyUndo: every change of the dialog is taken back.
        if (d.several) {
            if (s->historyCursor() != d.historyCursor)
                s->goTo(d.historyCursor);
            reloadFromSession();
            return;
        }
        if (d.hadDraft) {
            setRaw(d.role, d.original);
            if (m_draftUndo.size() > d.draftUndo)
                m_draftUndo.resize(d.draftUndo);
            refresh();
        } else if (s->draftLine()) {
            s->discardDraft();
            m_draftUndo.resize(std::min(m_draftUndo.size(), d.draftUndo));
            reloadFromSession();
        }
        m_dialog = std::move(d);
        publishRawSelection(m_dialog->opened);
        m_dialog.reset();
        return;
    }
    if (d.several)
        return;
    const long caret = core::legacy::caretAfterDialog(d.state.text, d.position);
    d.state.selectionStart = d.state.selectionEnd = caret;
    m_dialog = std::move(d);
    publishRawSelection(m_dialog->state);
    m_dialog.reset();
}

bool LineEditorController::splitLine(int role, int selectionStart, int selectionEnd)
{
    const auto r = record();
    if (!editable() || !r)
        return false;
    // "\N" for ASS and SRT, "|" for the line-based formats.
    const auto format = session()->document().format();
    const std::u16string split = format == core::SubtitleFormat::Ass || format == core::SubtitleFormat::Srt
                                     ? std::u16string(u"\\N")
                                     : std::u16string(u"|");
    std::u16string raw = core::toUtf16(roleText(*r, role));
    long start = selectionStart, end = selectionEnd;
    if (!m_showTags) {
        const auto p = core::project(raw);
        start = static_cast<long>(core::rawOffset(p, static_cast<std::size_t>(selectionStart), true));
        end = selectionEnd == selectionStart
                  ? start
                  : static_cast<long>(core::rawOffset(p, static_cast<std::size_t>(selectionEnd), false));
    }
    if (start > 0 && raw[static_cast<std::size_t>(start - 1)] == u' ')
        --start;
    if (end < static_cast<long>(raw.size()) && raw[static_cast<std::size_t>(end)] == u' ')
        ++end;
    if (start != end)
        raw.erase(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
    raw.insert(static_cast<std::size_t>(start), split);
    const long caret = start + static_cast<long>(split.size());
    if (!setRaw(role, core::toUtf8(raw)))
        return false;
    const auto after = core::project(raw);
    m_selectionStart = m_selectionEnd =
        m_showTags ? static_cast<int>(caret) : static_cast<int>(core::displayOffset(after, static_cast<std::size_t>(caret)));
    m_selectionRole = role;
    emit selectionRequested();
    return true;
}

bool LineEditorController::toggleUnconfirmedAndAdvance()
{
    auto *s = session();
    const auto r = record();
    if (!editable() || !r)
        return false;
    if (!translationMode()) {
        fail(tr("Unconfirmed applies in translation mode only."));
        return false;
    }
    const bool now = !r->unconfirmed; // ChangeState(4) toggles
    const auto result = s->run(application::Command{
        "Mark unconfirmed", s->revision(), {r->id},
        [&](core::Document &d) { return d.setLineUnconfirmed(r->id, now); }});
    if (!result) {
        fail(problemText());
        return false;
    }
    committed();
    return commitAndAdvance();
}

bool LineEditorController::findNext(std::size_t &cursor, const std::function<bool(const core::LineRecord &)> &match,
                                    const QString &none)
{
    auto *s = session();
    if (!s)
        return false;
    if (!translationMode()) {
        fail(tr("This search applies in translation mode only."));
        return false;
    }
    const auto lines = s->document().lines();
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t i = cursor; i < lines.size(); ++i) {
            if (lines[i]->visibility == core::LineVisibility::Hidden)
                continue;
            if (match(*lines[i])) {
                cursor = i + 1;
                if (!showLine(lines[i]->id.value))
                    return false;
                emit lineChanged(lines[i]->id.value);
                return true;
            }
        }
        if (cursor == 0)
            break;
        cursor = 0; // wrap once, as the legacy goto does
    }
    fail(none);
    return false;
}

bool LineEditorController::findNextUnconfirmed()
{
    return findNext(m_nextUnconfirmed, [](const core::LineRecord &l) { return l.unconfirmed; },
                    tr("No unconfirmed lines found"));
}

bool LineEditorController::findNextUntranslated()
{
    return findNext(m_nextUntranslated, [](const core::LineRecord &l) { return l.translation.empty(); },
                    tr("No untranslated lines found"));
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

bool LineEditorController::pasteAllToTranslation()
{
    const auto r = record();
    if (!translationMode() || !editable() || !r)
        return false;
    return setRaw(1, r->text);
}

bool LineEditorController::pasteSelectionToTranslation(int originalStart, int originalEnd, int translationCaret)
{
    if (!translationMode() || !editable() || originalStart == originalEnd)
        return false;
    const QString original = m_shown[0];
    const int from = std::clamp(std::min(originalStart, originalEnd), 0, static_cast<int>(original.size()));
    const int to = std::clamp(std::max(originalStart, originalEnd), 0, static_cast<int>(original.size()));
    const QString piece = original.mid(from, to - from);
    const int caret = std::clamp(translationCaret, 0, static_cast<int>(m_shown[1].size()));
    QString translated = m_shown[1];
    translated.insert(caret, piece);
    edit(1, translated, caret + static_cast<int>(piece.size()));
    if (m_shown[1] != translated)
        return false; // the mapped edit was refused
    m_selectionStart = m_selectionEnd = caret + static_cast<int>(piece.size());
    m_selectionRole = 1;
    emit selectionRequested();
    return true;
}

bool LineEditorController::commentOutOriginal()
{
    const auto r = record();
    if (!translationMode() || !editable() || !r)
        return false;
    return setRaw(0, u8"{" + r->text + u8"}");
}

bool LineEditorController::insertTimeDifference(bool fromEnd, int selectionStart, int selectionEnd)
{
    const auto r = record();
    const auto video = m_videoTime ? m_videoTime() : std::nullopt;
    if (!editable() || !r || !video)
        return false;
    const std::int64_t start = r->start.value.microseconds() / 1000, end = r->end.value.microseconds() / 1000;
    if (*video < start || *video > end)
        return false; // legacy rings the bell
    const auto zeroIt = [](std::int64_t ms) { return ms / 10 * 10; };
    const std::int64_t diff = fromEnd ? std::abs(*video - zeroIt(end)) : *video - zeroIt(start);
    const int role = translationMode() ? 1 : 0;
    const QString number = QString::number(diff);
    const int from = std::clamp(std::min(selectionStart, selectionEnd), 0, static_cast<int>(m_shown[role].size()));
    const int to = std::clamp(std::max(selectionStart, selectionEnd), 0, static_cast<int>(m_shown[role].size()));
    QString text = m_shown[role];
    text.replace(from, to - from, number);
    edit(role, text, from + static_cast<int>(number.size()));
    if (m_shown[role] != text)
        return false;
    m_selectionStart = m_selectionEnd = from + static_cast<int>(number.size());
    m_selectionRole = role;
    emit selectionRequested();
    return true;
}

QStringList LineEditorController::history() const
{
    QStringList out;
    auto *s = session();
    if (!s)
        return out;
    for (const auto &step : s->history()) {
        const QString name = QString::fromStdString(step.name);
        out << (step.activeRow ? tr("%1, active line %2").arg(name).arg(step.activeRow) : name);
    }
    return out;
}

int LineEditorController::historyCursor() const
{
    auto *s = session();
    return s ? static_cast<int>(s->historyCursor()) : -1;
}

bool LineEditorController::canUndoToLastSave() const
{
    auto *s = session();
    const auto saved = s ? s->savedStep() : std::nullopt;
    return saved && *saved != s->historyCursor();
}

bool LineEditorController::goToHistory(int step)
{
    auto *s = session();
    if (!s || step < 0)
        return false;
    const auto revision = s->revision();
    if (!s->goTo(static_cast<std::size_t>(step))) {
        fail(problemText());
        return false;
    }
    if (s->revision() != revision)
        committed();
    if (s->selection().active)
        emit lineChanged(s->selection().active->value);
    return true;
}

bool LineEditorController::undoToLastSave()
{
    auto *s = session();
    const auto saved = s ? s->savedStep() : std::nullopt;
    return saved && goToHistory(static_cast<int>(*saved));
}

void LineEditorController::reportFieldSelection(int role, int start, int end)
{
    if (role == 0 || role == 1)
        m_fieldSelection[role] = {std::min(start, end), std::max(start, end)};
}

void LineEditorController::selectInField(int start, int end, int role)
{
    if (role != 0 && role != 1)
        role = translationMode() ? 1 : 0;
    const int size = static_cast<int>(m_shown[role].size());
    m_selectionStart = std::clamp(start, 0, size);
    m_selectionEnd = std::clamp(end, 0, size);
    m_selectionRole = role;
    m_fieldSelection[role] = {std::min(m_selectionStart, m_selectionEnd), std::max(m_selectionStart, m_selectionEnd)};
    emit selectionRequested();
}

void LineEditorController::reloadFromSession()
{
    m_draftUndo.clear();
    m_draftRedo.clear();
    refresh();
}

} // namespace hikari::ui
