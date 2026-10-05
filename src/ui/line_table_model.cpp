#include "line_table_model.h"
#include "theme.h"

#include "hikari/application/grid_filtering.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/line_formats.h"
#include "hikari/core/srt.h"
#include "line_measures.h"

#include <QString>

#include <cmath>

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
    return !(m_hidden & hideBit(static_cast<Column>(column)));
}

void LineTableModel::setHiddenColumns(int mask)
{
    if (mask == m_hidden)
        return;
    m_hidden = mask;
    emit headerDataChanged(Qt::Horizontal, 0, ColumnCount - 1);
}

LineTableModel::LineTableModel(QObject *parent) : QAbstractTableModel(parent)
{
    // K2: the theme's comparison colours, live.
    theme::onChanged(this, [this] { setComparisonColours(themeComparisonColours()); });
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
        const auto result = m_spelling(std::u16string_view(reinterpret_cast<const char16_t *>(text.utf16()),
                                                           static_cast<std::size_t>(text.size())),
                                       m_format, true);
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
    m_format = document.format();
    // Legacy measures the translation when translation mode shows one.
    m_translationMode = document.scriptInfo(u8"TLMode") == u8"Yes";
    const auto lines = document.lines();
    for (const core::LineRecord *line : lines) {
        m_rowById.emplace(line->id.value, static_cast<int>(m_rows.size()));
        m_rows.push_back(Row{*line, std::nullopt, 0});
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
    const Row &r = m_rows[static_cast<std::size_t>(index.row())];
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
        case TextColumn:
            // SubsGridWindow.cpp:376 and 415: without the original shown, a
            // translated Line shows its translation (isTl = hasTLMode &&
            // TextTl != "").
            return qs(!m_showOriginal && m_translationMode && !line.translation.empty() ? line.translation : line.text);
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
    if (role != Qt::DisplayRole)
        return {};
    // Legacy headings.
    static const char *names[] = {"#",     "L.",   "Start", "End",    "Styles", "Actor", "Left",
                                  "Right", "Vert.", "Effect", "CPS", "Wraps",  "Text", "Translation"};
    if (section == TextColumn && m_showOriginal)
        return tr("Original text"); // SubsGridWindow.cpp:329
    return tr(names[section]);
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
                  {ComparisonMarksRole, "comparisonMarks"}});
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
