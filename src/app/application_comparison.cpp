#include "hikari/app/application.h"

#include "hikari/application/grid_clipboard.h"
#include "hikari/application/settings.h"

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

constexpr const char *kType = "comparison.type";
constexpr const char *kStyles = "comparison.styles";

} // namespace

application::ComparedDocument Application::comparedDocument(application::DocumentId id) const
{
    application::ComparedDocument compared;
    if (const auto *session = m_files->session(id)) {
        compared.document = &session->document();
        compared.selected = session->selection().selected;
        compared.translationMode = application::translationMode(*session);
    }
    return compared;
}

void Application::recompare()
{
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
    if (!m_partialFilter || (m_settings->integer(kType) & application::compare_by::Visible))
        recompare();
    for (const auto id : m_comparison.tabled())
        if (const auto *session = m_files->session(id))
            m_comparedRevisions[id.value] = session->revision();
}

QVariantMap Application::openComparisonMenu(int index)
{
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
            std::vector<std::u8string> option;
            for (const QString &name : m_settings->list(kStyles))
                option.push_back(u8(name));
            for (const auto &item : m_comparison.openMenu(
                     application::commonStyles(first->document(), second->document()), option))
                styleItems << QVariantMap{{QStringLiteral("name"), qs(item.name)},
                                          {QStringLiteral("checked"), item.checked}};
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
    const auto tabs = m_workspace.tabs();
    const auto target = m_workspace.editingTarget();
    if (index < 0 || index >= static_cast<int>(tabs.size()) || tabs.size() < 2 || !target)
        return false;
    return comparePair(*target, tabs[static_cast<std::size_t>(index)]);
}

bool Application::compareWithReference()
{
    const auto target = m_workspace.editingTarget();
    const auto reference = m_workspace.reference();
    if (!target || !reference)
        return false;
    return comparePair(*target, *reference);
}

bool Application::comparePair(application::DocumentId first, application::DocumentId second)
{
    if (second == first)
        return false;
    const auto a = comparedDocument(first), b = comparedDocument(second);
    if (!a.document || !b.document)
        return false;
    m_comparison.compare(first, second, a, b, m_settings->integer(kType));
    for (const auto id : {first, second})
        m_comparedRevisions[id.value] = m_files->session(id)->revision();
    refreshViews();
    return true;
}

void Application::turnOffComparison()
{
    m_comparison.remove();
    refreshViews();
}

} // namespace hikari::app
