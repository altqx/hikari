#include "audio_controller.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <map>

#include <utility>

namespace hikari::ui {

using application::AudioBox;

// Legacy ProviderFFMS2::DeleteOldAudioCache: with more files than the limit
// in the cache folder, the ones read longest ago go (legacy sorted on the
// last access time) until the surplus is gone. The cache file in use, under
// its final name or as it is being written (.part), is never removed: legacy
// on Windows could not remove an open file and went on to the next oldest
// (R5); legacy on Linux unlinked it and lost the cache (R3, data loss).
void deleteOldAudioCache(const std::filesystem::path &folder, const std::filesystem::path &inUse, int limit)
{
    if (limit < 1 || folder.empty())
        return;
    const QDir dir(QString::fromStdU16String(folder.u16string()));
    QString used, usedPart;
    if (!inUse.empty()) {
        used = QFileInfo(QString::fromStdU16String(inUse.u16string())).absoluteFilePath();
        usedPart = used + QStringLiteral(".part");
    }
    const auto entries = dir.entryInfoList(QDir::Files);
    if (entries.size() <= limit)
        return;
    std::multimap<QDateTime, QString> byAccess;
    for (const QFileInfo &entry : entries)
        if (used.isEmpty() || (entry.absoluteFilePath() != used && entry.absoluteFilePath() != usedPart))
            byAccess.emplace(entry.fileTime(QFileDevice::FileAccessTime), entry.absoluteFilePath());
    qsizetype removed = 0;
    const qsizetype surplus = entries.size() - limit;
    for (const auto &[when, path] : byAccess) {
        if (removed >= surplus)
            break;
        if (QFile::remove(path))
            ++removed;
    }
}

AudioController::AudioController(application::DisplayAudioPort &own, QObject *parent)
    : QObject(parent), m_box(own)
{
    m_box.setObserver([this] { boxChanged(); });
    m_box.setLog([this](const std::string &message, application::AudioBox::LogLevel level) {
        emit logged(QString::fromStdString(message), level == application::AudioBox::LogLevel::Debug);
    });
    m_box.setSettings([this] { return m_settings ? m_settings() : application::AudioCacheSettings{}; });
    m_box.setChooser([this](const std::vector<std::string> &rows, std::function<void(std::optional<int>)> answer) {
        ask(rows, std::move(answer), true);
    });
    m_box.setDefer([this](std::function<void()> step) { QTimer::singleShot(0, this, std::move(step)); });
    newView();
}

void AudioController::ask(const std::vector<std::string> &rows, std::function<void(std::optional<int>)> answer,
                          bool forBox)
{
    if (auto earlier = std::exchange(m_trackAnswer, nullptr))
        earlier(std::nullopt); // a newer question replaces an unanswered one
    m_trackChoices.clear();
    for (const auto &row : rows)
        m_trackChoices << QString::fromStdString(row);
    m_trackAnswer = std::move(answer);
    m_trackForBox = forBox;
    emit trackChoicesChanged();
}

void AudioController::askTrack(const std::vector<std::string> &rows, std::function<void(std::optional<int>)> answer)
{
    ask(rows, std::move(answer), false);
}

void AudioController::trimCache()
{
    if (m_box.state() == AudioBox::State::Opening) {
        m_trimPending = true; // the cache file in use is known once the track is open
        return;
    }
    m_trimPending = false;
    const auto settings = m_settings ? m_settings() : application::AudioCacheSettings{};
    deleteOldAudioCache(settings.cacheDir, m_box.cacheFile(), settings.cacheFilesLimit);
}

void AudioController::setSettings(std::function<application::AudioCacheSettings()> settings)
{
    m_settings = std::move(settings);
}

void AudioController::openFromVideo(const QString &path, int track, bool videoNewIndex, const QString &handoffIndexFile)
{
    m_box.openFromVideo(path.toStdString(), track, videoNewIndex, handoffIndexFile.toStdString());
}

void AudioController::chooseTrack(int row)
{
    auto answer = std::exchange(m_trackAnswer, nullptr);
    m_trackChoices.clear();
    emit trackChoicesChanged();
    if (answer)
        answer(row);
}

void AudioController::cancelTrackChoice()
{
    auto answer = std::exchange(m_trackAnswer, nullptr);
    m_trackChoices.clear();
    emit trackChoicesChanged();
    if (answer)
        answer(std::nullopt);
}

// A new legacy AudioBox: zoom and scale from the options, the first Line as
// the previous one (line_n), the panel's size kept.
void AudioController::newView()
{
    m_view = application::AudioView{};
    m_view.setSamplesPercent(m_options.horizontalZoom, false);
    m_view.setScale(application::audioScaleFromSlider(m_options.verticalZoom));
    m_previousActive = 0;
    m_fresh = true;
    m_cursor.reset();
    m_scrollbar = {};
    // A3: a new display has no mark and holds nothing
    const bool hadMark = m_timing.hasMark();
    m_timing.reset();
    m_needCommit = false;
    if (hadMark)
        emit markChanged();
}

QString AudioController::status() const
{
    switch (m_box.state()) {
    case AudioBox::State::Closed:
        return tr("No audio open");
    case AudioBox::State::Opening: {
        // legacy ProgressSink, titled for video
        const auto [done, total] = m_box.indexing();
        const int percent = total > 0 ? static_cast<int>(done * 100 / total) : 0;
        return tr("Indexing video %1%").arg(percent);
    }
    case AudioBox::State::Loading:
    case AudioBox::State::Ready:
        return QFileInfo(path()).fileName();
    }
    return {};
}

void AudioController::openAudio(const QString &path)
{
    if (path.isEmpty())
        return; // legacy: no file, nothing happens
    m_box.open(path.toStdString());
}

void AudioController::closeAudio()
{
    m_box.close();
    newView();
    emit changed();
    redraw();
}

void AudioController::boxChanged()
{
    const auto state = m_box.state();
    const bool loaded = state == AudioBox::State::Loading || state == AudioBox::State::Ready;
    const bool wasLoaded = m_seenState == AudioBox::State::Loading || m_seenState == AudioBox::State::Ready;
    m_seenState = state;
    if (loaded && !wasLoaded) {
        // legacy SetFile: loaded, the image, then the current Line (SetDialogue)
        const auto *audio = m_box.audio();
        m_view.setSource(audio->sampleRate(), audio->sampleCount());
        m_view.updateSamples();
        emit opened(path(), std::exchange(m_fresh, false));
        reselect();
    } else if (!loaded && m_box.error()) {
        // a failed SetFile destroys the box; the box logged what legacy logs
        newView();
        if (m_trackForBox && (!m_trackChoices.isEmpty() || m_trackAnswer)) {
            m_trackAnswer = nullptr;
            m_trackChoices.clear();
            emit trackChoicesChanged();
        }
    } else if (!loaded) {
        m_view.clearSource();
    }
    if (m_trimPending && state != AudioBox::State::Opening)
        trimCache();
    emit changed();
    redraw();
}

void AudioController::setLines(std::vector<application::AudioLineSpan> lines, int active, bool select)
{
    if (!select && active == m_active && lines == m_lines)
        return; // nothing the display shows changed
    m_lines = std::move(lines);
    m_active = active;
    if (select && (m_box.state() == AudioBox::State::Loading || m_box.state() == AudioBox::State::Ready))
        reselect();
    else
        redraw();
}

// Legacy SetDialogue.
void AudioController::reselect()
{
    if (m_active < 0 || m_active >= static_cast<int>(m_lines.size()))
        return redraw();
    m_needCommit = false; // A3: SetDialogue
    if (m_options.grabTimesOnSelect)
        std::tie(m_startMs, m_endMs) = application::legacyLineSelection(m_lines, m_active, m_previousActive);
    m_previousActive = m_active;
    update();
}

// Legacy Update: with auto-scroll the Line is made visible.
void AudioController::update(bool moveToEnd)
{
    if (m_options.autoScroll)
        m_view.makeVisible(m_startMs, m_endMs, false, moveToEnd, m_scrollbarThickness);
    redraw();
}

void AudioController::setKeyframes(std::vector<int> keyframesMs)
{
    if (keyframesMs == m_keyframes)
        return;
    m_keyframes = std::move(keyframesMs);
    redraw();
}

void AudioController::setVideoTime(std::optional<int> pausedMs)
{
    if (pausedMs == m_videoMs)
        return;
    m_videoMs = pausedMs;
    redraw();
}

application::AudioMarks AudioController::marks() const
{
    application::AudioMarks marks;
    marks.startMs = m_startMs;
    marks.endMs = m_endMs;
    marks.inactive = application::legacyInactiveLines(m_options.inactiveLines, m_lines, m_active);
    marks.keyframesMs = m_keyframes;
    marks.videoMs = m_videoMs;
    marks.focused = m_focused;
    // A3
    marks.modified = m_needCommit;
    if (m_timing.hasMark())
        marks.markMs = m_timing.markMs();
    marks.markTextHeight = m_markTextHeight;
    return marks;
}

void AudioController::resize(int width, int height, int timelineHeight, int scrollbarThickness)
{
    m_scrollbarThickness = scrollbarThickness;
    m_view.resize(width, height, timelineHeight, scrollbarThickness);
    redraw();
}

void AudioController::setCursor(std::optional<float> x)
{
    if (x == m_cursor)
        return;
    m_cursor = x;
    emit cursorChanged();
}

void AudioController::setFocused(bool focused)
{
    if (focused == m_focused)
        return;
    m_focused = focused;
    redraw();
}

// Legacy OnMouseEvent's wheel without Shift or Ctrl (those zoom: A2).
void AudioController::wheel(int rotation)
{
    if (!m_view.hasSource())
        return;
    const int step = -rotation * m_view.width() / 360;
    m_view.updatePosition(m_view.position() + step, false, m_scrollbarThickness);
    redraw();
}

void AudioController::setScrollPosition(int position)
{
    if (!m_view.hasSource())
        return;
    m_view.setPosition(position * 12);
    redraw();
}

const application::WaveformColumns &AudioController::columns()
{
    const auto *audio = m_box.audio();
    const ColumnsKey key{m_box.serial(), audio && audio->peaks(), m_view.position() * m_view.samples(), m_view.width(),
                         m_view.height(), m_view.samples(), m_view.scale()};
    if (!(key == m_columnsKey)) {
        m_columnsKey = key;
        m_columns = audio ? application::legacyWaveform(*audio, key.start, key.w, key.h, key.samples, key.scale)
                          : application::WaveformColumns{};
    }
    return m_columns;
}

std::vector<application::AudioShape> AudioController::scene(const application::AudioTextWidth &textWidth)
{
    switch (m_box.state()) {
    case AudioBox::State::Closed:
    case AudioBox::State::Opening:
        return {}; // legacy: not loaded, nothing drawn
    case AudioBox::State::Loading:
        return application::audioProgressScene(m_view, m_box.progress(), m_options);
    case AudioBox::State::Ready:
        break;
    }
    return application::audioScene(m_view, columns(), marks(), m_options, textWidth);
}

// Legacy UpdateImage: the samples per column first, then a new image.
void AudioController::redraw()
{
    if (m_view.hasSource())
        m_view.updateSamples();
    m_timing.drawn(m_view, {m_startMs, m_endMs, m_needCommit}); // A3: DoUpdateImage's selStart, selEnd, selMark
    m_scrollbar = m_view.scrollbar();
    ++m_revision;
    emit displayChanged();
}

// A3: timing.

application::AudioTimingOptions AudioController::timingOptions() const
{
    return m_timingSettings ? m_timingSettings() : application::AudioTimingOptions{};
}

application::AudioSnapContext AudioController::snapContext() const
{
    const auto options = timingOptions();
    application::AudioSnapContext snap;
    snap.view = &m_view;
    snap.snapToKeyframes = options.snapToKeyframes;
    snap.snapToOtherLines = options.snapToOtherLines;
    snap.drawKeyframes = m_options.drawKeyframes;
    snap.inactiveLines = m_options.inactiveLines;
    snap.keyframesMs = m_keyframes;
    snap.keyframeSnapMs = m_keyframeSnap;
    snap.lines = m_lines;
    snap.active = m_active;
    return snap;
}

// Legacy CommitChanges: the times go into the editor's fields and, with
// `save`, to the Line (EditBox::Send); then Update(moveToEnd).
void AudioController::commitChanges(bool nextLine, bool save, bool moveToEnd, application::AudioAdjacent adjacent)
{
    if (!loaded())
        return;
    if (save)
        m_needCommit = false;
    if (m_hooks.commit)
        m_hooks.commit({m_startMs, m_endMs, save, nextLine, m_timing.hold() != 0, adjacent});
    update(moveToEnd);
}

// AudioBox::OnCommit: CommitChanges(true), so the next Line follows whatever
// AUDIO_NEXT_LINE_ON_COMMIT says (legacy never reads it).
void AudioController::commit()
{
    emit focusRequested();
    commitChanges(true, true, false);
}

// Legacy ChangeLine: nothing past either end; otherwise the shown Line
// `delta` away becomes active (SubsGrid::SetActive, which runs SetDialogue
// again for the same Line).
void AudioController::changeLine(int delta)
{
    const int count = static_cast<int>(m_lines.size());
    if (m_active < 0 || m_active >= count)
        return;
    if ((m_active == 0 && delta < 0) || (m_active == count - 1 && delta > 0))
        return;
    const int before = m_active;
    const int next = application::legacyKeyFromPosition(m_lines, m_active, delta);
    if (m_hooks.setActive)
        m_hooks.setActive(next);
    if (next == before && m_active == before && loaded())
        reselect();
}

// AudioBox::OnNext / OnPrev: the display takes the focus when the new Line
// plays (A4's hook), and the new Line's selection plays.
void AudioController::nextLine()
{
    const bool play = !timingOptions().dontPlayWhenLineChanges;
    if (play)
        emit focusRequested();
    changeLine(1);
    if (play && m_hooks.play && loaded())
        m_hooks.play(m_startMs, m_endMs);
}

void AudioController::previousLine()
{
    const bool play = !timingOptions().dontPlayWhenLineChanges;
    if (play)
        emit focusRequested();
    changeLine(-1);
    if (play && m_hooks.play && loaded())
        m_hooks.play(m_startMs, m_endMs);
}

// AudioBox::OnGoto: MakeDialogueVisible(true).
void AudioController::goToSelection()
{
    emit focusRequested();
    if (!m_view.hasSource())
        return;
    m_view.makeVisible(m_startMs, m_endMs, true, false, m_scrollbarThickness);
    redraw();
}

// Legacy AddLead: the editor's fields take the times (UpdateTimeEditCtrls),
// the selection is modified, and AUDIO_AUTO_COMMIT commits it.
void AudioController::addLead(bool in, bool out)
{
    emit focusRequested();
    if (!loaded())
        return;
    const auto options = timingOptions();
    std::tie(m_startMs, m_endMs) =
        application::legacyAddLead(m_startMs, m_endMs, in, out, options.leadIn, options.leadOut);
    if (m_hooks.commit)
        m_hooks.commit({m_startMs, m_endMs, false, false, m_timing.hold() != 0, application::AudioAdjacent::None});
    m_needCommit = true;
    if (options.autoCommit)
        commitChanges(false, true, false);
    update();
}

// GLOBAL_SET_AUDIO_FROM_VIDEO / GLOBAL_SET_AUDIO_MARK_FROM_VIDEO: SetMark,
// then ChangePosition(time) (the time in the middle of the view).
void AudioController::showTime(int ms, bool mark)
{
    if (!m_view.hasSource() || m_view.samples() <= 0)
        return;
    if (mark) {
        m_timing.setMark(ms);
        emit markChanged();
    }
    std::int64_t samplepos = m_view.sampleAtMs(ms);
    samplepos = (samplepos / m_view.samples()) - (m_view.width() / 2);
    m_view.updatePosition(samplepos, false, m_scrollbarThickness);
    redraw();
}

application::AudioMouseResult AudioController::mouse(const application::AudioMouse &event)
{
    if (m_box.state() != AudioBox::State::Ready || !m_view.hasSource())
        return {}; // legacy: nothing without a player and provider
    application::AudioTiming::Selection selection{m_startMs, m_endMs, m_needCommit};
    const bool hadMark = m_timing.hasMark();
    const int mark = m_timing.markMs();
    auto result = m_timing.mouse(event, m_view, selection, snapContext(), timingOptions(), m_scrollbarThickness);
    m_startMs = selection.startMs;
    m_endMs = selection.endMs;
    m_needCommit = selection.modified;
    if (m_timing.hasMark() != hadMark || m_timing.markMs() != mark)
        emit markChanged();
    if (result.seekVideoMs && m_hooks.seekVideo)
        m_hooks.seekVideo(*result.seekVideoMs);
    if (result.playEnd && m_hooks.setPlayEnd)
        m_hooks.setPlayEnd(*result.playEnd);
    if (result.playSelection && m_hooks.play)
        m_hooks.play(m_startMs, m_endMs);
    if (result.commit) // legacy Commit(moveToEnd): CommitChanges(false, AUDIO_AUTO_COMMIT, moveToEnd)
        commitChanges(false, timingOptions().autoCommit, *result.commit, result.adjacent);
    else if (result.redraw)
        redraw();
    return result;
}

} // namespace hikari::ui
