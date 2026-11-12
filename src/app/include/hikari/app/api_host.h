#pragma once

#include "hikari/api/api.h"
#include "hikari/api/folder_skills.h"
#include "hikari/application/document_files.h"
#include "hikari/application/document_overlay.h"
#include "hikari/application/video_session.h"

#include <QJSValue>
#include <QObject>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace hikari::backends {
class SubtitleRendererSelection;
}

namespace hikari::app {

// The application's side of the Hikari API. The shell supplies commands and
// panels through two QML functions; observe() turns whatever changed since
// its last call into API events.
class ApiHost final : public QObject,
                      public api::DocumentHost,
                      public api::CommandHost,
                      public api::PanelHost,
                      public api::FileHost,
                      public api::RenderHost,
                      public api::TypesetHost,
                      public api::ProjectHost,
                      public api::TranslationHost,
                      public api::EffectsHost {
    Q_OBJECT
    // (command, run) => null for a command the shell cannot run; else
    // {enabled, reason} without run, or whether it ran.
    Q_PROPERTY(QJSValue commandHandler READ commandHandler WRITE setCommandHandler)
    // (panel, action) => with action "list" every panel's
    // {panel, title, open, floating, focused}; with "open", "close" or "focus"
    // whether the panel exists.
    Q_PROPERTY(QJSValue panelHandler READ panelHandler WRITE setPanelHandler)

public:
    struct Wiring {
        application::Workspace &workspace;
        application::DocumentFiles &files;
        application::Settings *settings = nullptr;
        application::MacroMediaPort *media = nullptr;
        // The editing target's selection, as the Grid sets it.
        std::function<bool(application::Selection)> selectTarget;
        std::function<void(application::DocumentId committed)> refresh;
        std::function<std::vector<std::string>(std::string_view command)> keys;
        std::function<const application::VideoSession *()> video;
        std::function<std::vector<std::byte>(const core::Document &)> encode;
        std::function<std::vector<application::FontLease>(application::DocumentId)> fonts;
        std::function<std::string()> renderer;
        // Empty: the user has no skills.
        std::filesystem::path skills;
        api::AutomationHost *automation = nullptr;
        std::function<application::visual::perspective::TextMeasure()> measure;
        // The Project of the window the user works in.
        std::function<std::optional<api::ProjectState>()> project;
        std::function<std::optional<std::filesystem::path>(application::DocumentId)> translationFolder;
        api::KaraokeHost *karaoke = nullptr;
        std::function<application::NodeEffectOptions()> effects;
        std::function<std::vector<std::string>(const std::string &root)> aiExclude;
        api::PluginHost *plugins = nullptr;
    };
    explicit ApiHost(Wiring wiring, QObject *parent = nullptr);
    ~ApiHost() override;

    api::Api &api() { return m_api; }
    application::AiReview &review() { return m_review; }
    void observe();

    QJSValue commandHandler() const { return m_commandHandler; }
    void setCommandHandler(const QJSValue &handler) { m_commandHandler = handler; }
    QJSValue panelHandler() const { return m_panelHandler; }
    void setPanelHandler(const QJSValue &handler) { m_panelHandler = handler; }
    Q_INVOKABLE void commandRan(const QString &command, bool ok);
    Q_INVOKABLE void panelChanged(const QString &panel);

    const application::Workspace &workspace() const override { return m_wiring.workspace; }
    application::EditSession *session(application::DocumentId document) const override;
    std::optional<std::string> path(application::DocumentId document) const override;
    bool select(application::DocumentId document, const application::Selection &selection) override;
    void committed(application::DocumentId document) override;

    std::optional<api::CommandState> state(std::string_view command) const override;
    bool run(std::string_view command) override;

    std::vector<api::PanelState> panels() const override;
    bool setOpen(std::string_view panel, bool open) override;
    bool focus(std::string_view panel) override;

    std::optional<std::string> read(const std::string &path, std::size_t atMost) const override;
    bool write(const std::string &path, std::string_view bytes) override;

    std::optional<std::string> projectFolder(application::DocumentId document) const override;
    std::optional<application::OverlayFrame> render(application::DocumentId id, const core::Document &document,
                                                    std::int64_t frame, int width, int height) override;
    application::visual::perspective::TextMeasure measure() const override;
    application::NodeEffectOptions options() override;

    std::optional<api::ProjectState> project() const override;
    std::vector<std::string> aiExclude(const std::string &root) const override;

private:
    struct Seen {
        std::uint64_t revision = 0;
        application::Selection selection;
    };
    QJSValue callPanels(std::string_view panel, const char *action) const;

    Wiring m_wiring;
    application::AiReview m_review;
    std::unique_ptr<api::SkillTiers> m_skills;
    api::Api m_api;
    QJSValue m_commandHandler;
    QJSValue m_panelHandler;
    std::map<std::uint64_t, Seen> m_seen;
    std::vector<application::DocumentId> m_seenDocuments;
    std::optional<application::DocumentId> m_seenTarget;
    std::optional<api::ProjectState> m_seenProject;
    bool m_observing = false;
    std::unique_ptr<backends::SubtitleRendererSelection> m_renderer;
    std::unique_ptr<application::DocumentOverlay> m_overlay;
};

}
