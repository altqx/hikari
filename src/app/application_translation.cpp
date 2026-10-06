// E5: translation mode controls in the application (legacy EditBox's
// "Translator mode" check box, OnTlMode and OnAutoMoveTags; SubsGrid's
// showOriginal; TL_MODE_HIDE_ORIGINAL_ON_VIDEO in SubsGrid::GetVisible and
// SaveFile, at 20d647c4).

#include "hikari/app/application.h"

#include "hikari/core/ass_save.h"

namespace hikari::app {

namespace {

constexpr const char *kShowOriginal = "translation.showOriginal";             // TL_MODE_SHOW_ORIGINAL
constexpr const char *kHideOriginal = "translation.hideOriginalOnVideo";      // TL_MODE_HIDE_ORIGINAL_ON_VIDEO
constexpr const char *kMoveTags = "translation.autoMoveTagsFromOriginal";      // AUTO_MOVE_TAGS_FROM_ORIGINAL

} // namespace

void Application::setUpTranslationControls()
{
    // Legacy SetTextWithTags splits only with Visual <= CROSS.
    m_editor->setVisualToolActive([this] { return m_visualTools && m_visualTools->activeFamily() != 0; });
    const auto apply = [this] {
        m_editor->setMoveTags(m_settings->boolean(kMoveTags));
        // SaveFile reads the option at every save and autosave.
        m_files->setSaveOptions(core::AssSaveOptions{.hideOriginalOnVideo = m_settings->boolean(kHideOriginal)});
    };
    apply();
    connect(m_settings.get(), &ui::SettingsStore::changed, this, [this, apply](const QString &id) {
        if (id == QLatin1String(kMoveTags) || id == QLatin1String(kHideOriginal))
            apply();
        // GetVisible reads the option when the video next takes the subtitles.
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
        entry = OriginalColumnsEntry{generation, {}}; // SubsGrid::Clearing
    return entry.columns;
}

bool Application::showOriginal(application::DocumentId document)
{
    auto *session = m_files->session(document);
    return session && originalColumns(document).observe(session->document(), m_settings->boolean(kShowOriginal));
}

bool Application::translatorModeAvailable() const
{
    // HikariSubFrame.cpp:2401: editor && form == ASS && SubsPath != "" (the
    // text formats load as ASS in legacy, here as PlainText).
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    const auto format = session->document().format();
    const auto destination = m_files->destination(*target);
    return (format == core::SubtitleFormat::Ass || format == core::SubtitleFormat::PlainText) && destination &&
           !destination->value.empty();
}

bool Application::turnOnTranslationMode()
{
    const auto target = m_workspace.editingTarget();
    auto *session = targetSession();
    if (!target || !session || !translatorModeAvailable())
        return false;
    showOriginal(*target); // the Grid's state before the switch
    if (!application::turnOnTranslationModeStep(*session))
        return false;
    originalColumns(*target).turnedOn(session->document(), m_settings->boolean(kShowOriginal));
    m_editor->reloadFromSession(); // OnTlMode: SetLine(currentLine)
    refreshViews();
    return true;
}

void Application::setMoveTags(bool on)
{
    m_settings->set(kMoveTags, on); // OnAutoMoveTags: Options.SetBool, SaveOptions
}

} // namespace hikari::app
