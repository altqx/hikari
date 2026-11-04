#pragma once

#include "hikari/api/api.h"
#include "hikari/api/host.h"
#include "hikari/application/audio_display.h"
#include "hikari/core/ass_load.h"

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace hikari::api::testing {

inline core::Document loadAss(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

inline constexpr std::string_view kAlpha =
    "[Script Info]\n"
    "Title: alpha\n"
    "ScriptType: v4.00+\n"
    "PlayResX: 1920\n"
    "\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
    "Style: Sign,Verdana,32,&H0000FFFF,&H000000FF,&H00000000,&H80000000,-1,0,0,0,100,100,0,0,3,0,0,8,0,0,40,1\n"
    "\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,Ann,0,0,0,,Hello there\n"
    "Comment: 0,0:00:03.00,0:00:04.00,Sign,,0,0,0,note,a sign note\n"
    "Dialogue: 1,0:00:05.00,0:00:06.50,Default,Bob,10,20,30,,General Kenobi\n";

inline constexpr std::string_view kBeta =
    "[Script Info]\n"
    "Title: beta\n"
    "ScriptType: v4.00+\n"
    "\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,other\n";

// 64x36 video at 25 fps whose frame n has every pixel's blue at x and red at n;
// one second of mono audio at 2000 Hz rising from -1000 by one per sample.
class FakeMedia final : public application::MacroMediaPort {
public:
    FakeMedia()
    {
        std::vector<std::int16_t> ramp(2000);
        for (std::size_t i = 0; i < ramp.size(); ++i)
            ramp[i] = static_cast<std::int16_t>(int(i) - 1000);
        audio.appendFrames(ramp.data(), std::int64_t(ramp.size()), 1);
        audio.finish();
    }
    std::optional<std::int64_t> frameFromMs(std::int64_t ms) const override
    {
        return videoOpen ? std::optional(ms / 40) : std::nullopt;
    }
    std::optional<std::int64_t> msFromFrame(std::int64_t frame) const override
    {
        return videoOpen ? std::optional(frame * 40) : std::nullopt;
    }
    std::optional<application::AegisubFrameTimes> frameTimes() const override { return std::nullopt; }
    std::optional<VideoSize> videoSize() const override
    {
        return videoOpen ? std::optional(VideoSize{64, 36, 16, 9}) : std::nullopt;
    }
    std::optional<std::vector<std::int64_t>> keyframes() const override
    {
        return videoOpen ? std::optional(std::vector<std::int64_t>{0, 10}) : std::nullopt;
    }
    void frame(std::int64_t index, bool withSubtitles, std::function<void(std::optional<Frame>)> reply) override
    {
        lastWithSubtitles = withSubtitles;
        if (!videoOpen || index > 99)
            return reply(std::nullopt);
        Frame out{64, 36, std::vector<std::byte>(64 * 36 * 4)};
        for (int y = 0; y < 36; ++y)
            for (int x = 0; x < 64; ++x) {
                std::byte *p = out.bgra.data() + (y * 64 + x) * 4;
                p[0] = std::byte(x);
                p[2] = std::byte(index);
                p[3] = std::byte(255);
            }
        reply(std::move(out));
    }
    std::optional<std::pair<std::int64_t, std::int64_t>> audioSelection() const override { return std::nullopt; }
    std::optional<Project> project() const override { return Project{7, {}, {}, {}}; }
    std::optional<std::string> fileName() const override { return std::nullopt; }
    std::optional<const application::DisplayAudio *> peakAudio() const override
    {
        if (!audioOpen)
            return std::nullopt;
        return audioLoading ? nullptr : &audio;
    }

    bool videoOpen = true;
    bool audioOpen = true;
    bool audioLoading = false;
    bool lastWithSubtitles = false;
    application::DisplayAudio audio{2000, 2000};
};

class FakeFiles final : public FileHost {
public:
    std::optional<std::string> read(const std::string &path, std::size_t atMost) const override
    {
        const auto it = contents.find(path);
        return it == contents.end() ? std::nullopt : std::optional(it->second.substr(0, atMost));
    }
    bool write(const std::string &path, std::string_view bytes) override
    {
        if (path.starts_with("/locked/"))
            return false;
        contents[path] = std::string(bytes);
        return true;
    }
    std::map<std::string, std::string> contents;
};

// Document 1 "a.ass" (the editing target, Lines 1-3, Lines 1 and 3 selected,
// 3 active) and document 2 "b.ass", never saved.
class FakeHost final : public DocumentHost, public CommandHost, public PanelHost {
public:
    FakeHost()
    {
        const auto a = open("a.ass", kAlpha, "/subs/a.ass");
        open("b.ass", kBeta, std::nullopt);
        application::Selection selection;
        selection.active = core::LineId{3};
        selection.selected = {core::LineId{1}, core::LineId{3}};
        sessions.at(a.value)->setSelection(selection);
        commands["GLOBAL_SAVE_SUBS"] = CommandState{true, "", {"Ctrl-S"}};
        commands["GLOBAL_UNDO"] = CommandState{true, "", {"Ctrl-Z"}};
        panelStates = {PanelState{"Grid", "Subtitles grid", true, false, true},
                       PanelState{"Video", "Video", false, false, false}};
    }

    application::DocumentId open(std::string title, std::string_view ass, std::optional<std::string> path)
    {
        const auto id = workspace_.add(std::move(title));
        sessions[id.value] = std::make_unique<application::EditSession>(loadAss(ass));
        if (path)
            paths[id.value] = *path;
        return id;
    }

    Host host() { return Host{this, this, this, &settings, &media, &files}; }

    const application::Workspace &workspace() const override { return workspace_; }
    application::EditSession *session(application::DocumentId document) const override
    {
        const auto it = sessions.find(document.value);
        return it == sessions.end() ? nullptr : it->second.get();
    }
    std::optional<std::string> path(application::DocumentId document) const override
    {
        const auto it = paths.find(document.value);
        return it == paths.end() ? std::nullopt : std::optional(it->second);
    }
    bool select(application::DocumentId document, const application::Selection &selection) override
    {
        session(document)->setSelection(selection);
        if (api)
            api->selectionChanged(document);
        return true;
    }
    void committed(application::DocumentId document) override
    {
        ++commits;
        if (api)
            api->documentChanged(document);
    }

    application::EditSession *target() const { return session(*workspace_.editingTarget()); }

    // GLOBAL_UNDO follows the editing target's history, as the shell's Undo does.
    std::optional<CommandState> state(std::string_view command) const override
    {
        const auto it = commands.find(command);
        if (it == commands.end())
            return std::nullopt;
        CommandState state = it->second;
        if (command == "GLOBAL_UNDO" && !target()->canUndo()) {
            state.enabled = false;
            state.disabledReason = "Nothing to undo";
        }
        return state;
    }
    bool run(std::string_view command) override
    {
        bool ok = true;
        if (command == "GLOBAL_UNDO") {
            ok = target()->undo();
            if (ok && api)
                api->documentChanged(*workspace_.editingTarget());
        }
        ran.emplace_back(command);
        if (api)
            api->commandRan(command, ok);
        return ok;
    }

    std::vector<PanelState> panels() const override { return panelStates; }
    bool setOpen(std::string_view panel, bool open) override
    {
        for (PanelState &p : panelStates)
            if (p.id == panel) {
                p.open = open;
                if (!open)
                    p.focused = false;
                if (api)
                    api->panelChanged(panel);
                return true;
            }
        return false;
    }
    bool focus(std::string_view panel) override
    {
        bool found = false;
        for (PanelState &p : panelStates) {
            const bool was = p.focused;
            p.focused = p.id == panel;
            if (p.focused)
                p.open = found = true;
            if (api && (was != p.focused))
                api->panelChanged(p.id);
        }
        return found;
    }

    Api *api = nullptr;
    int commits = 0;
    std::vector<std::string> ran;

    application::Workspace workspace_;
    std::map<std::uint64_t, std::unique_ptr<application::EditSession>> sessions;
    std::map<std::uint64_t, std::string> paths;
    std::map<std::string, CommandState, std::less<>> commands;
    std::vector<PanelState> panelStates;
    application::MemorySettingsStorage storage;
    application::Settings settings{storage};
    FakeMedia media;
    FakeFiles files;
};

}
