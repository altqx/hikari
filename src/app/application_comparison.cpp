// R1: subtitle comparison (legacy Notebook::ContextMenu's "Subtitle
// comparison" menu and its ID_CHECK_EVENT handler, Notebook.cpp:78-127 and
// 915-956; SubsGrid::SubsComparison, RemoveComparison and the calls that run
// them again, at 20d647c4).
//
// The editing target (legacy CG1, the active tab) is compared with the tab
// the menu was opened on (CG2). Comparing reads both Documents and never
// changes either one: the result is application state (each Document's
// table), painted by whichever Grid shows that Document, the editing Grid or
// the reference tray. Legacy ran the comparison again whenever a grid with a
// table was edited (SetModified), undone (DoUndo) or had lines hidden or shown
// with "Compare by visible lines" on (RefreshSubsOnVideo); here that is when
// a Document with a table has a new revision, except that opening or closing
// a hidden block or a group (FilterPartial) runs it only with that criterion.

#include "hikari/app/application.h"

#include "hikari/application/grid_clipboard.h"
#include "hikari/application/settings.h"

#include <QColor>

#include <algorithm>

namespace hikari::app {

namespace {

QString qs(const std::u8string &s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

std::u8string u8(const QString &s)
{
    const QByteArray bytes = s.toUtf8();
    return std::u8string(reinterpret_cast<const char8_t *>(bytes.constData()), static_cast<std::size_t>(bytes.size()));
}

constexpr const char *kType = "comparison.type";     // SUBS_COMPARISON_TYPE
constexpr const char *kStyles = "comparison.styles"; // SUBS_COMPARISON_STYLES

} // namespace

application::ComparedDocument Application::comparedDocument(application::DocumentId id) const
{
    application::ComparedDocument compared;
    if (const auto *session = m_files->session(id)) {
        compared.document = &session->document();
        compared.selected = session->selection().selected; // file->IsSelected
        compared.translationMode = application::translationMode(*session); // the grid's hasTLMode
    }
    return compared;
}

void Application::recompare()
{
    // SubsComparison works on CG1 and CG2 whichever grid ran it. Legacy
    // dereferenced them unchecked: a grid that kept a table from an earlier
    // pair, edited after "Turn off comparison" or after either compared tab
    // was gone, crashed it (R1-stale-table in the report); here nothing runs.
    const auto first = m_comparison.first(), second = m_comparison.second();
    if (!first || !second)
        return;
    const auto a = comparedDocument(*first), b = comparedDocument(*second);
    if (!a.document || !b.document)
        return;
    m_comparison.recompare(a, b, m_settings->integer(kType));
}

void Application::refreshComparison()
{
    const auto tabled = m_comparison.tabled();
    bool edited = false;
    for (const auto id : tabled) {
        const auto *session = m_files->session(id);
        if (!session)
            continue;
        const auto it = m_comparedRevisions.find(id.value);
        if (it == m_comparedRevisions.end() || it->second != session->revision())
            edited = true;
    }
    if (!edited)
        return;
    // FilterPartial runs only RefreshSubsOnVideo, which compares again with
    // "Compare by visible lines" on (SubsGrid.cpp:1914); every other change
    // is a SetModified or DoUndo, which always does (SubsGridBase.cpp:1000-
    // 1002, 1136-1138).
    if (!m_partialFilter || (m_settings->integer(kType) & application::compare_by::Visible))
        recompare();
    for (const auto id : m_comparison.tabled())
        if (const auto *session = m_files->session(id))
            m_comparedRevisions[id.value] = session->revision();
}

void Application::loadComparisonColours()
{
    // The theme colours SubsGridWindow::PaintD reads (an unreadable value
    // keeps the default theme's).
    const auto colour = [this](std::string_view id, QRgb fallback) {
        const auto parsed = application::parseSettingColour(m_settings->settings().text(id));
        return QColor::fromRgba(parsed.value_or(fallback));
    };
    m_shell->setComparisonColours({colour(application::kComparisonOutlineSetting, 0xFF2700FF),
                                   colour(application::kComparisonMismatchSetting, 0xFF272B32),
                                   colour(application::kComparisonMatchSetting, 0xFF3A3E45),
                                   colour(application::kComparisonCommentMismatchSetting, 0xFF003176),
                                   colour(application::kComparisonCommentMatchSetting, 0xFF3662A1)});
}

QVariantMap Application::openComparisonMenu(int index)
{
    // Notebook.cpp:917-939. canCompare: a tab other than the active one,
    // with more than one tab.
    const auto tabs = m_workspace.tabs();
    const auto target = m_workspace.editingTarget();
    const bool onTab = index >= 0 && index < static_cast<int>(tabs.size());
    const bool canCompare = onTab && tabs.size() > 1 && target && tabs[static_cast<std::size_t>(index)] != *target;
    QVariantList styleItems;
    bool selectionsEnabled = false;
    if (canCompare) {
        const auto *first = m_files->session(*target);
        const auto *second = m_files->session(tabs[static_cast<std::size_t>(index)]);
        if (first && second) {
            // GetCommonStyles: the active tab's styles that the other has;
            // each one in SUBS_COMPARISON_STYLES is checked and chosen again.
            std::vector<std::u8string> option;
            for (const QString &name : m_settings->list(kStyles))
                option.push_back(u8(name));
            for (const auto &item : m_comparison.openMenu(
                     application::commonStyles(first->document(), second->document()), option))
                styleItems << QVariantMap{{QStringLiteral("name"), qs(item.name)},
                                          {QStringLiteral("checked"), item.checked}};
            // "Compare by selections": both files have selections.
            selectionsEnabled = !first->selection().selected.empty() && !second->selection().selected.empty();
        }
    }
    const int type = m_settings->integer(kType);
    namespace by = application::compare_by;
    return {{QStringLiteral("enabled"), canCompare || m_comparison.active()},
            {QStringLiteral("canCompare"), canCompare},
            {QStringLiteral("active"), m_comparison.active()},
            {QStringLiteral("times"), (type & by::Times) != 0},
            {QStringLiteral("visible"), (type & by::Visible) != 0},
            {QStringLiteral("selections"), (type & by::Selections) != 0},
            {QStringLiteral("selectionsEnabled"), selectionsEnabled},
            {QStringLiteral("styles"), (type & by::Styles) != 0},
            {QStringLiteral("chosenStyles"), m_comparison.chosenStylesShown()},
            {QStringLiteral("styleItems"), styleItems}};
}

int Application::toggleComparisonBit(int bit)
{
    // MENU_COMPARE + 1..4 (Notebook.cpp:85-96), then Options.SetInt.
    namespace by = application::compare_by;
    int type = m_settings->integer(kType);
    if (bit == by::Times || bit == by::Visible || bit == by::Selections || bit == by::Styles) {
        type = application::SubtitleComparison::toggleBit(type, bit);
        m_settings->set(kType, type);
    }
    return type;
}

bool Application::toggleComparisonStyle(const QString &name, bool checked)
{
    // Case 4448 (Notebook.cpp:97-123): the chosen styles are saved to
    // SUBS_COMPARISON_STYLES and the ChosenStyles bit follows them.
    const int type = m_comparison.toggleStyle(m_settings->integer(kType), u8(name), checked);
    QStringList styles;
    for (const auto &style : m_comparison.chosenStyles())
        styles << qs(style);
    m_settings->set(kStyles, styles);
    m_settings->set(kType, type);
    return m_comparison.chosenStylesShown();
}

bool Application::compareWithTab(int index)
{
    // MENU_COMPARE (Notebook.cpp:947-953): CG1 the active tab, CG2 the tab
    // the menu was opened on; hasCompare. Legacy then scrolled CG2 to CG1's
    // compared Line when both were shown, which is reference navigation (R2).
    const auto tabs = m_workspace.tabs();
    const auto target = m_workspace.editingTarget();
    if (index < 0 || index >= static_cast<int>(tabs.size()) || tabs.size() < 2 || !target)
        return false;
    const auto second = tabs[static_cast<std::size_t>(index)];
    if (second == *target)
        return false;
    const auto a = comparedDocument(*target), b = comparedDocument(second);
    if (!a.document || !b.document)
        return false;
    m_comparison.compare(*target, second, a, b, m_settings->integer(kType));
    for (const auto id : {*target, second})
        m_comparedRevisions[id.value] = m_files->session(id)->revision();
    refreshViews();
    return true;
}

void Application::turnOffComparison()
{
    // MENU_COMPARE - 1: RemoveComparison.
    m_comparison.remove();
    refreshViews();
}

} // namespace hikari::app
