#include "hikari/app/application.h"

#include "hikari/application/grid_clipboard.h"
#include "hikari/core/ass_save.h"

#include <QCoreApplication>

namespace hikari::app {

namespace {

constexpr const char *kShowOriginal = "translation.showOriginal";
constexpr const char *kHideOriginal = "translation.hideOriginalOnVideo";
constexpr const char *kMoveTags = "translation.autoMoveTagsFromOriginal";

} // namespace

void Application::setUpTranslationControls()
{
    m_editor->setVisualToolActive([this] { return m_visualTools && m_visualTools->activeFamily() != 0; });
    const auto apply = [this] {
        m_editor->setMoveTags(m_settings->boolean(kMoveTags));
        m_shell->setRunMarks(m_settings->boolean(kMoveTags));
        m_files->setSaveOptions(core::AssSaveOptions{.hideOriginalOnVideo = m_settings->boolean(kHideOriginal)});
    };
    apply();
    connect(m_settings.get(), &ui::SettingsStore::changed, this, [this, apply](const QString &id) {
        if (id == QLatin1String(kMoveTags) || id == QLatin1String(kHideOriginal))
            apply();
        if (id == QLatin1String(kMoveTags))
            refreshViews();
        if (id == QLatin1String(kHideOriginal))
            m_videoRevision.reset();
    });
}

std::vector<std::byte> Application::rendererScript(const core::Document &document) const
{
    return core::encodeAss(document,
                           core::AssSaveOptions{.hideOriginalOnVideo = m_settings->boolean(kHideOriginal), .renderer = true});
}

application::OriginalColumns &Application::originalColumns(application::DocumentId document)
{
    auto &entry = m_originalColumns[document.value];
    const auto generation = m_files->generation(document);
    if (entry.generation != generation)
        entry = OriginalColumnsEntry{generation, {}};
    return entry.columns;
}

bool Application::showOriginal(application::DocumentId document)
{
    auto *session = m_files->session(document);
    return session && originalColumns(document).observe(session->document(), m_settings->boolean(kShowOriginal));
}

bool Application::translatorModeAvailable() const
{
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    const auto destination = m_files->destination(*target);
    return session->document().format() == core::SubtitleFormat::Ass && destination && !destination->value.empty();
}

QString Application::translationHint() const
{
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    if (!session || session->document().format() != core::SubtitleFormat::PlainText)
        return {};
    return QCoreApplication::translate("HikariSub.Translation",
                                       "Translation needs an ASS file: plain text cannot store it.");
}

bool Application::turnOnTranslationMode()
{
    const auto target = m_workspace.editingTarget();
    auto *session = targetSession();
    if (!target || !session || !translatorModeAvailable())
        return false;
    // MUST KILL: called only to record the Grid's columns before the switch
    showOriginal(*target);
    if (!application::turnOnTranslationModeStep(*session))
        return false;
    originalColumns(*target).turnedOn(session->document(), m_settings->boolean(kShowOriginal));
    m_editor->reloadFromSession();
    refreshViews();
    return true;
}

bool Application::translationQuestionDue() const
{
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    return session && translatorModeAvailable() && !application::translationMode(*session) &&
           !m_translationDeclined.contains(target->value);
}

void Application::declineTranslationMode()
{
    if (const auto target = m_workspace.editingTarget())
        m_translationDeclined.insert(target->value);
}

void Application::setMoveTags(bool on)
{
    m_settings->set(kMoveTags, on);
}

} // namespace hikari::app
