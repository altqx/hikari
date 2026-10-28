#include "line_table_model.h"
#include "theme.h"

#include "hikari/application/grid_filtering.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/legacy_regex.h"
#include "hikari/core/line_formats.h"
#include "hikari/core/srt.h"
#include "line_measures.h"

#include <QCoreApplication>
#include <QString>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace hikari::ui {

namespace {

QString qs(const std::u8string &s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

// SubsTime::raw for the Document's format.
QString legacyTime(const core::TimeField &time, std::optional<std::int64_t> frame, core::SubtitleFormat format)
{
    const std::int64_t ms = time.value.microseconds() / 1000;
    switch (format) {
    case core::SubtitleFormat::Srt: return qs(core::legacy::srtTimeText(ms));
    case core::SubtitleFormat::TMPlayer: return qs(core::legacy::tmpTimeText(ms));
    case core::SubtitleFormat::MicroDvd: return QString::number(frame.value_or(0));
    case core::SubtitleFormat::Mpl2:
        return QString::number(static_cast<std::int64_t>(std::ceil(static_cast<float>(ms) * (10.0f / 1000.0f))));
    default: return qs(core::legacy::assTimeText(ms));
    }
}

} // namespace

int LineTableModel::hideBit(Column column)
{
    switch (column) {
    case LayerColumn: return 1;
    case StartColumn: return 2;
    case EndColumn: return 4;
    case StyleColumn: return 8;
    case ActorColumn: return 16;
    case MarginLeftColumn: return 32;
    case MarginRightColumn: return 64;
    case MarginVerticalColumn: return 128;
    case EffectColumn: return 256;
    case CpsColumn: return 512;
    case WrapsColumn: return 8192;
    default: return 0;
    }
}

bool LineTableModel::columnShown(int column) const
{
    if (column < 0 || column >= ColumnCount)
        return false;
    const bool ass = m_format == core::SubtitleFormat::Ass || m_format == core::SubtitleFormat::PlainText;
    const bool tmp = m_format == core::SubtitleFormat::TMPlayer;
    switch (column) {
    case LayerColumn:
    case StyleColumn:
    case ActorColumn:
    case MarginLeftColumn:
    case MarginRightColumn:
    case MarginVerticalColumn:
    case EffectColumn:
        if (!ass)
            return false;
        break;
    case EndColumn:
    case CpsColumn:
        if (tmp)
            return false;
        break;
    case TranslationColumn:
        return m_showOriginal; // SubsGridWindow.cpp:330
    default:
        break;
    }
    return !((m_hidden | emptyColumns()) & hideBit(static_cast<Column>(column)));
}

void LineTableModel::setHiddenColumns(int mask)
{
    if (mask == m_hidden)
        return;
    m_hidden = mask;
    emit headerDataChanged(Qt::Horizontal, 0, ColumnCount - 1);
}

namespace {

// GR3: the columns that hide themselves when empty (hideBit's Layer, Actor,
// Left, Right, Vertical and Effect).
constexpr int kAutoHideBits = 1 | 16 | 32 | 64 | 128 | 256;

bool blank(const std::u8string &text)
{
    return std::all_of(text.begin(), text.end(), [](char8_t c) { return c == u8' ' || c == u8'\t'; });
}

// The auto-hidden columns' bits a Line leaves empty.
int emptyColumnsOf(const core::LineRecord &line)
{
    int bits = 0;
    if (line.layer.value == 0)
        bits |= LineTableModel::hideBit(LineTableModel::LayerColumn);
    if (blank(line.actor))
        bits |= LineTableModel::hideBit(LineTableModel::ActorColumn);
    if (line.marginLeft.value == 0)
        bits |= LineTableModel::hideBit(LineTableModel::MarginLeftColumn);
    if (line.marginRight.value == 0)
        bits |= LineTableModel::hideBit(LineTableModel::MarginRightColumn);
    if (line.marginVertical.value == 0)
        bits |= LineTableModel::hideBit(LineTableModel::MarginVerticalColumn);
    if (blank(line.effect))
        bits |= LineTableModel::hideBit(LineTableModel::EffectColumn);
    return bits;
}

} // namespace

int LineTableModel::emptyColumns() const
{
    if (!m_autoHide)
        return 0;
    return m_draft ? m_empty & emptyColumnsOf(m_draft->line) : m_empty;
}

void LineTableModel::setAutoHideColumns(bool on)
{
    if (on == m_autoHide)
        return;
    m_autoHide = on;
    emit headerDataChanged(Qt::Horizontal, 0, ColumnCount - 1);
}

LineTableModel::LineTableModel(QObject *parent) : QAbstractTableModel(parent)
{
    // K2: the theme's comparison and (E6) label colours, live.
    theme::onChanged(this, [this] {
        setComparisonColours(themeComparisonColours());
        setLabelColours(themeLabelColours());
    });
}

void LineTableModel::setFrameTimebase(std::optional<application::LegacyTimebase> frames)
{
    if (frames && !frames->exact())
        frames.reset(); // only an exact timebase shows frames
    if (!frames && !m_frames)
        return;
    m_frames = std::move(frames);
    if (!m_rows.empty())
        emit dataChanged(index(0, StartColumn), index(static_cast<int>(m_rows.size()) - 1, EndColumn), {Qt::DisplayRole});
}

const LineTableModel::Measures &LineTableModel::measuresOf(const Row &row) const
{
    if (row.measures)
        return *row.measures;
    Measures m;
    const core::LineRecord &line = row.line;
    if (!line.comment) {
        const auto &text = m_translationMode && !line.translation.empty() ? line.translation : line.text;
        const LineMeasures measured = measureLine(qs(text), m_format);
        if (m_format != core::SubtitleFormat::TMPlayer) {
            const int cps = legacyCps(measured.chars, line.start.value.microseconds() / 1000,
                                      line.end.value.microseconds() / 1000);
            m.cps = QString::number(cps);
            m.cpsTooHigh = cps > 15;
        }
        m.wraps = measured.wraps;
        m.badWraps = measured.badWraps;
    }
    row.measures = std::move(m);
    return *row.measures;
}

const QString &LineTableModel::shownTextOf(const Row &row) const
{
    if (row.shownText)
        return *row.shownText;
    // E5 (SubsGridWindow.cpp:376 and 415): without the original shown, a
    // translated Line shows its translation (isTl = hasTLMode && TextTl != "").
    const core::LineRecord &line = row.line;
    QString text = qs(!m_showOriginal && m_translationMode && !line.translation.empty() ? line.translation : line.text);
    if (tagsSwapped()) {
        // SubsGrid::TagsPattern (SubsGridWindow.cpp:34-39): wxRegEx
        // "\{[^\{]*\}" (SRT "\<[^\<]*\>"), advanced syntax, its
        // ReplaceAll taking GRID_TAGS_SWAP_CHARACTER as a replacement (so
        // "&" and "\0".."\9" there are back-references, as in legacy);
        // SubsGridWindow.cpp:405-410 replaces the shown text only.
        static const core::LegacyRegex ass(u"\\{[^\\{]*\\}", core::LegacyRegex::Advanced);
        static const core::LegacyRegex srt(u"\\<[^\\<]*\\>", core::LegacyRegex::Advanced);
        std::u16string raw = text.toStdU16String();
        (m_format == core::SubtitleFormat::Srt ? srt : ass).replaceAll(raw, m_tagSwap.toStdU16String());
        text = QString::fromStdU16String(raw);
    }
    row.shownText = std::move(text);
    return *row.shownText;
}

void LineTableModel::setSpelling(Spelling spelling)
{
    if (!spelling) {
        m_spelling = {};
        return;
    }
    m_spelling = [spelling = std::move(spelling)](std::u16string_view text, core::SubtitleFormat format, bool spell,
                                                  int) { return spelling(text, format, spell); };
}

void LineTableModel::setHideTags(bool hide, const QString &swap)
{
    if (hide == m_hideTags && swap == m_tagSwap)
        return;
    m_hideTags = hide;
    m_tagSwap = swap;
    // HideOverrideTags clears SpellErrors and refreshes (SubsGridWindow.cpp:1959-1965).
    for (const Row &row : m_rows) {
        row.shownText.reset();
        row.spellMarks.reset();
    }
    if (m_draft) {
        m_draft->shownText.reset();
        m_draft->spellMarks.reset();
    }
    if (!m_rows.empty())
        emit dataChanged(index(0, TextColumn), index(rowCount() - 1, TextColumn), {Qt::DisplayRole, SpellMarksRole});
}

int LineTableModel::labelSlot(int state)
{
    return state == 0 ? 0 : state == 2 ? 2 : state == 1 ? 1 : 3;
}

LineTableModel::LabelColours LineTableModel::themeLabelColours(bool dark)
{
    // GRID_LABEL_NORMAL, _MODIFIED, _SAVED, _DOUBTFUL (config.cpp:422-425).
    // Light's normal label is lighter than legacy's #7F7F7F mid grey, on
    // which neither the text nor the field role reached 4.5:1 for the
    // number's digits; it stays darker than the saved label.
    if (dark)
        return {QColor(0x2F, 0x31, 0x36), QColor(0x32, 0x2F, 0x4E), QColor(0x20, 0x22, 0x25), QColor(0x92, 0x5B, 0x1F)};
    return {QColor(0x9C, 0x9C, 0x9C), QColor(0xB0, 0xAD, 0xD8), QColor(0xBF, 0xBF, 0xBF), QColor(0x92, 0x5B, 0x1F)};
}

LineTableModel::LabelColours LineTableModel::themeLabelColours()
{
    return themeLabelColours(theme::isDark(theme::current().code));
}

void LineTableModel::setLabelColours(const LabelColours &colours)
{
    if (colours == m_labelColours)
        return;
    m_labelColours = colours;
    emit headerDataChanged(Qt::Horizontal, 0, 0);
}

const QVariantList &LineTableModel::spellMarksOf(const Row &row) const
{
    if (row.spellMarks)
        return *row.spellMarks;
    QVariantList marks;
    const core::LineRecord &line = row.line;
    // SubsGridWindow: comments are not checked, and marks are drawn on the
    // last column only. In translation mode that is the translation column
    // (not built yet), so the Text column, showing the original, has none.
    if (m_spelling && !line.comment && !line.text.empty() && !m_translationMode) {
        const QString text = qs(line.text);
        // E6: with tags hidden, Init gets the tag length (SubsGridWindow.cpp:385-386).
        const auto result = m_spelling(std::u16string_view(reinterpret_cast<const char16_t *>(text.utf16()),
                                                           static_cast<std::size_t>(text.size())),
                                       m_format, true, tagsSwapped() ? static_cast<int>(m_tagSwap.size()) : -1);
        for (const int offset : result.errors)
            marks << offset;
    }
    row.spellMarks = std::move(marks);
    return *row.spellMarks;
}

void LineTableModel::setDocument(const core::Document &document)
{
    setDocument(document, nullptr);
}

LineTableModel::ComparisonColours LineTableModel::themeComparisonColours()
{
    // GRID_COMPARISON_OUTLINE, _BACKGROUND_NOT_MATCH, _BACKGROUND_MATCH,
    // _COMMENT_BACKGROUND_NOT_MATCH, _COMMENT_BACKGROUND_MATCH (theme.cpp).
    return theme::current().content.comparison;
}

void LineTableModel::setComparisonColours(const ComparisonColours &colours)
{
    if (colours == m_comparisonColours)
        return;
    m_comparisonColours = colours;
    emit headerDataChanged(Qt::Horizontal, 0, 0);
    if (!m_rows.empty())
        emit dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1), {ComparisonRole});
}

void LineTableModel::setDocument(const core::Document &document,
                                 const std::vector<application::LineComparison> *comparison)
{
    beginResetModel();
    m_rows.clear();
    m_rowById.clear();
    m_draft.reset();
    m_draftRow = -1;
    m_format = document.format();
    // Legacy measures the translation when translation mode shows one.
    m_translationMode = document.scriptInfo(u8"TLMode") == u8"Yes";
    const auto lines = document.lines();
    m_empty = kAutoHideBits;
    for (const core::LineRecord *line : lines) {
        m_empty &= emptyColumnsOf(*line);
        m_rowById.emplace(line->id.value, static_cast<int>(m_rows.size()));
        m_rows.push_back(Row{*line, std::nullopt, 0});
        // E6: Dialogue::GetState: the changed-Line mark (bits 1 and 2), 4
        // Unconfirmed, 8 bookmarked.
        m_rows.back().state = (m_changeState ? m_changeState(*line) : 0) | (line->unconfirmed ? 4 : 0) |
                              (line->bookmark ? 8 : 0);
    }
    // Hidden-block marks (legacy CheckIfHasHiddenBlock), in one pass from the
    // end: hiddenRun[k] counts the hidden Lines from k up to the next shown one.
    m_filtered = application::isFiltered(document);
    const std::size_t n = lines.size();
    std::vector<int> hiddenRun(n + 1, 0);
    for (std::size_t k = n; k-- > 0;)
        hiddenRun[k] = lines[k]->visibility == core::LineVisibility::Hidden ? hiddenRun[k + 1] + 1 : 0;
    auto markAfter = [&](std::ptrdiff_t row) {
        const std::size_t first = static_cast<std::size_t>(row + 1);
        if (first < n && lines[first]->visibility == core::LineVisibility::VisibleBlock)
            return first == 0 || lines[first - 1]->visibility != core::LineVisibility::VisibleBlock ? 2 : 0;
        return first <= n && hiddenRun[first] > 0 ? 1 : 0;
    };
    m_headerBlock = markAfter(-1);
    for (std::size_t r = 0; r + 1 < n; ++r)
        if (lines[r]->group == core::GroupMarker::Description)
            m_rows[r].groupClosed = lines[r + 1]->group == core::GroupMarker::Closed;
    for (std::size_t r = 0; r < n; ++r)
        m_rows[r].blockMark = markAfter(static_cast<std::ptrdiff_t>(r));
    // R1: a row is painted from Comparison->at(key) (SubsGridWindow.cpp:419-
    // 420): a mismatch while lineCompare has entries, a match when the texts
    // were equal. A row past the table (legacy std::out_of_range) paints
    // plainly (R1-stale-table).
    if (comparison)
        for (std::size_t r = 0; r < n && r < comparison->size(); ++r) {
            const auto &c = (*comparison)[r];
            m_rows[r].comparison = c.mismatch() ? 2 : c.match() ? 1 : 0;
            for (std::size_t m = 1; m + 1 < c.marks.size(); m += 2)
                m_rows[r].comparisonMarks << c.marks[m] << c.marks[m + 1];
        }
    // Keep only selection that still names existing Lines.
    std::erase_if(m_selection.selected, [&](core::LineId id) { return !m_rowById.contains(id.value); });
    if (m_selection.active && !m_rowById.contains(m_selection.active->value))
        m_selection.active.reset();
    if (m_anchor && !m_rowById.contains(m_anchor->value))
        m_anchor.reset();
    endResetModel();
}

void LineTableModel::setSelection(const application::Selection &selection, std::optional<core::LineId> anchor)
{
    std::vector<core::LineId> touched(m_selection.selected.begin(), m_selection.selected.end());
    touched.insert(touched.end(), selection.selected.begin(), selection.selected.end());
    for (const auto &id : {m_selection.active, selection.active, m_anchor, anchor})
        if (id)
            touched.push_back(*id);
    m_selection = selection;
    m_anchor = anchor;
    emitStateChanged(touched);
}

void LineTableModel::setDraft(std::optional<core::LineRecord> draft)
{
    const int previous = m_draftRow;
    const int empty = emptyColumns();
    m_draft.reset();
    m_draftRow = -1;
    if (const auto row = draft ? rowOf(draft->id) : std::nullopt) {
        // The committed row's marks and state, with the draft's content and
        // its own measures, shown text and spelling marks.
        Row shown = m_rows[static_cast<std::size_t>(*row)];
        // R1's comparison is the committed Line's: it holds while the draft
        // leaves what the comparison reads (compareSubtitles: the compared
        // text, the times, the style) as it was, and the row shows none
        // otherwise until the commit compares it again.
        const core::LineRecord &committed = shown.line;
        if (application::comparedText(*draft, m_translationMode) !=
                application::comparedText(committed, m_translationMode) ||
            draft->start.value != committed.start.value || draft->end.value != committed.end.value ||
            draft->style != committed.style) {
            shown.comparison = 0;
            shown.comparisonMarks.clear();
        }
        shown.line = std::move(*draft);
        shown.measures.reset();
        shown.spellMarks.reset();
        shown.shownText.reset();
        m_draft = std::move(shown);
        m_draftRow = *row;
    }
    if (previous >= 0 && previous != m_draftRow)
        emitRowChanged(previous);
    if (m_draftRow >= 0)
        emitRowChanged(m_draftRow);
    if (emptyColumns() != empty) // GR3: the draft fills or empties an auto-hidden column
        emit headerDataChanged(Qt::Horizontal, 0, ColumnCount - 1);
}

const LineTableModel::Row &LineTableModel::shownRow(int row) const
{
    return row == m_draftRow && m_draft ? *m_draft : m_rows[static_cast<std::size_t>(row)];
}

void LineTableModel::emitRowChanged(int row)
{
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

void LineTableModel::emitStateChanged(const std::vector<core::LineId> &ids)
{
    for (const auto &id : ids)
        if (const auto row = rowOf(id))
            emit dataChanged(index(*row, 0), index(*row, ColumnCount - 1), {ActiveRole, SelectedRole, AnchorRole});
}

std::optional<int> LineTableModel::rowOf(core::LineId id) const
{
    const auto it = m_rowById.find(id.value);
    if (it == m_rowById.end())
        return std::nullopt;
    return it->second;
}

std::optional<core::LineId> LineTableModel::lineAt(int row) const
{
    const core::LineRecord *record = recordAt(row);
    return record ? std::optional(record->id) : std::nullopt;
}

const core::LineRecord *LineTableModel::recordAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return nullptr;
    return &m_rows[static_cast<std::size_t>(row)].line;
}

int LineTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int LineTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant LineTableModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Row &r = shownRow(index.row());
    const core::LineRecord &line = r.line;
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case NumberColumn: return index.row() + 1;
        case LayerColumn: return QString::number(line.layer.value);
        case StartColumn:
            if (m_frames)
                return QString::number(m_frames->frameAt(static_cast<int>(line.start.value.microseconds() / 1000)));
            return legacyTime(line.start, line.startFrame, m_format);
        case EndColumn:
            if (m_frames)
                return QString::number(m_frames->frameAt(static_cast<int>(line.end.value.microseconds() / 1000)) - 1);
            return legacyTime(line.end, line.endFrame, m_format);
        case StyleColumn: return qs(line.style);
        case ActorColumn: return qs(line.actor);
        case MarginLeftColumn: return QString::number(line.marginLeft.value);
        case MarginRightColumn: return QString::number(line.marginRight.value);
        case MarginVerticalColumn: return QString::number(line.marginVertical.value);
        case EffectColumn: return qs(line.effect);
        case CpsColumn: return measuresOf(r).cps;
        case WrapsColumn: return measuresOf(r).wraps;
        case TextColumn: return shownTextOf(r);
        case TranslationColumn: return qs(line.translation);
        default: return {};
        }
    case LineIdRole:
        return QVariant::fromValue<qulonglong>(line.id.value);
    case CommentRole:
        return line.comment;
    case ActiveRole:
        return m_selection.active == line.id;
    case SelectedRole:
        return m_selection.selected.contains(line.id);
    case AnchorRole:
        return m_anchor == line.id;
    case StartMicrosecondsRole:
        return QVariant::fromValue<qlonglong>(line.start.value.microseconds());
    case EndMicrosecondsRole:
        return QVariant::fromValue<qlonglong>(line.end.value.microseconds());
    case HiddenBlockRole:
        return r.blockMark;
    case DocumentRowRole:
        return index.row();
    case GroupRole:
        switch (line.group) {
        case core::GroupMarker::Description: return 1;
        case core::GroupMarker::Opened: return 2;
        case core::GroupMarker::Closed: return 3;
        default: return 0;
        }
    case GroupClosedRole:
        return r.groupClosed;
    case CpsTooHighRole:
        return measuresOf(r).cpsTooHigh;
    case BadWrapsRole:
        return measuresOf(r).badWraps;
    case SpellMarksRole:
        return spellMarksOf(r);
    case ComparisonRole:
        return r.comparison;
    case ComparisonMarksRole:
        return r.comparisonMarks;
    case LineStateRole:
        return r.state;
    case DraftRole:
        return index.row() == m_draftRow;
    default:
        return {};
    }
}

QVariant LineTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || section < 0 || section >= ColumnCount)
        return {};
    if (role == ColumnShownRole)
        return columnShown(section);
    if (role == FilteredRole)
        return m_filtered;
    if (role == HeaderBlockRole)
        return m_headerBlock;
    if (role == ComparisonColoursRole)
        return QVariantList(m_comparisonColours.begin(), m_comparisonColours.end());
    if (role == LabelColoursRole)
        return QVariantList(m_labelColours.begin(), m_labelColours.end());
    if (role == Qt::ToolTipRole)
        return headerToolTip(section);
    if (role != Qt::DisplayRole)
        return {};
    // Legacy headings (SubsGridWindow.cpp:310-330). "#" is the one legacy
    // never translates (L"#").
    if (section == NumberColumn)
        return QStringLiteral("#");
    if (section == TextColumn && m_showOriginal)
        return QCoreApplication::translate("HikariSub.Grid", "Original text"); // SubsGridWindow.cpp:329
    static const char *const names[] = {nullptr,
                                        QT_TRANSLATE_NOOP("HikariSub.Grid", "L."),
                                        QT_TRANSLATE_NOOP("HikariSub.Grid", "Start"),
                                        QT_TRANSLATE_NOOP("HikariSub.Grid", "End"),
                                        QT_TRANSLATE_NOOP("HikariSub.Grid", "Styles"),
                                        QT_TRANSLATE_NOOP("HikariSub.Grid", "Actor"),
                                        QT_TRANSLATE_NOOP("HikariSub.Grid", "Left"),
                                        QT_TRANSLATE_NOOP("HikariSub.Grid", "Right"),
                                        QT_TRANSLATE_NOOP("HikariSub.Grid", "Vert."),
                                        QT_TRANSLATE_NOOP("HikariSub.Grid", "Effect"),
                                        QT_TRANSLATE_NOOP("HikariSub.Grid", "CPS"),
                                        QT_TRANSLATE_NOOP("HikariSub.Grid", "Wraps"),
                                        QT_TRANSLATE_NOOP("HikariSub.Grid", "Text"),
                                        QT_TRANSLATE_NOOP("HikariSub.Grid", "Translation")};
    static_assert(std::size(names) == ColumnCount);
    return QCoreApplication::translate("HikariSub.Grid", names[section]);
}

// An abbreviated heading's full name, the Grid's hover text; empty for a
// heading that is already a word ("Start", "Styles", "Text"), so no name is
// said twice. Legacy names these columns in its Paste columns choices
// (SubsGrid.cpp:501-508: "Layer", "Left margin", "Right margin", "Vertical
// margin") and its Hide columns menu (SubsGrid.cpp:199-200: "characters per
// second", "line wraps").
QString LineTableModel::headerToolTip(int section)
{
    switch (section) {
    case LayerColumn: return QCoreApplication::translate("HikariSub.Grid", "Layer");
    case MarginLeftColumn: return QCoreApplication::translate("HikariSub.Grid", "Left margin");
    case MarginRightColumn: return QCoreApplication::translate("HikariSub.Grid", "Right margin");
    case MarginVerticalColumn: return QCoreApplication::translate("HikariSub.Grid", "Vertical margin");
    case CpsColumn: return QCoreApplication::translate("HikariSub.Grid", "Characters per second");
    case WrapsColumn: return QCoreApplication::translate("HikariSub.Grid", "Line wraps");
    default: return {};
    }
}

QHash<int, QByteArray> LineTableModel::roleNames() const
{
    auto roles = QAbstractTableModel::roleNames();
    roles.insert({{LineIdRole, "lineId"},
                  {CommentRole, "comment"},
                  {ActiveRole, "active"},
                  {SelectedRole, "selected"},
                  {AnchorRole, "anchor"},
                  {StartMicrosecondsRole, "startMicroseconds"},
                  {EndMicrosecondsRole, "endMicroseconds"},
                  {CpsTooHighRole, "cpsTooHigh"},
                  {BadWrapsRole, "badWraps"},
                  {SpellMarksRole, "spellMarks"},
                  {ComparisonRole, "comparison"},
                  {ComparisonMarksRole, "comparisonMarks"},
                  {LineStateRole, "lineState"},
                  {DraftRole, "draft"}});
    return roles;
}

LineFilterModel::LineFilterModel(QObject *parent) : QSortFilterProxyModel(parent) {}

void LineFilterModel::setLineModel(LineTableModel *model)
{
    m_lines = model;
    setSourceModel(model);
}

void LineFilterModel::setPredicate(Predicate predicate)
{
    beginFilterChange();
    m_predicate = std::move(predicate);
    endFilterChange(Direction::Rows);
}

bool LineFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &) const
{
    if (!m_predicate || !m_lines)
        return true;
    const core::LineRecord *record = m_lines->recordAt(sourceRow);
    return record && m_predicate(*record);
}

int LineFilterModel::hiddenSelectedCount() const
{
    if (!m_lines)
        return 0;
    int hidden = 0;
    for (const auto &id : m_lines->selection().selected) {
        const auto row = m_lines->rowOf(id);
        if (row && !filterAcceptsRow(*row, {}))
            ++hidden;
    }
    return hidden;
}

int LineFilterModel::nearestVisibleRow(core::LineId id) const
{
    if (!m_lines)
        return -1;
    const auto source = m_lines->rowOf(id);
    if (!source)
        return -1;
    const int rows = m_lines->rowCount();
    for (int distance = 0; distance < rows; ++distance)
        for (int candidate : {*source + distance, *source - distance}) {
            if (candidate < 0 || candidate >= rows)
                continue;
            const QModelIndex proxy = mapFromSource(m_lines->index(candidate, 0));
            if (proxy.isValid())
                return proxy.row();
        }
    return -1;
}

} // namespace hikari::ui
