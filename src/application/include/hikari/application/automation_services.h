#pragma once

// Ports run on the GUI thread; a port that is not attached answers Unavailable.

#include "hikari/application/audio_display.h"
#include "hikari/application/automation.h"
#include "hikari/application/resample.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hikari::application {

class MacroMediaPort {
public:
    virtual ~MacroMediaPort() = default;
    struct VideoSize {
        int width = 0, height = 0, arX = 1, arY = 1;
    };
    struct Frame {
        int width = 0, height = 0;
        std::vector<std::byte> bgra; // width * 4 bytes per row, top row first
    };
    struct Project {
        std::int64_t videoFrameShown = 0;
        std::string audioFile, videoFile, keyframesFile;
    };
    // nullopt everywhere: no video (or no audio) open.
    virtual std::optional<std::int64_t> frameFromMs(std::int64_t ms) const = 0;
    virtual std::optional<std::int64_t> msFromFrame(std::int64_t frame) const = 0;
    virtual std::optional<VideoSize> videoSize() const = 0;
    virtual std::optional<std::vector<std::int64_t>> keyframes() const = 0;
    virtual void frame(std::int64_t frame, bool withSubtitles, std::function<void(std::optional<Frame>)> reply) = 0;
    virtual std::optional<std::pair<std::int64_t, std::int64_t>> audioSelection() const = 0;
    virtual std::optional<Project> project() const = 0;
    virtual std::optional<std::string> fileName() const = 0; // nullopt: the Document has no file
    // nullopt without audio; a null pointer while the audio box has none yet.
    virtual std::optional<const DisplayAudio *> peakAudio() const { return std::nullopt; }
};

class ClipboardPort {
public:
    virtual ~ClipboardPort() = default;
    virtual std::string text() const = 0;
    virtual bool setText(const std::string &text) = 0;
};

struct TextExtents {
    double width = 0, height = 0, descent = 0, externalLeading = 0;
};

class TextMeasurePort {
public:
    virtual ~TextMeasurePort() = default;
    // `style` holds the positional ASS v4+ style fields, Name first.
    virtual std::optional<TextExtents> measure(const std::vector<std::string> &style, const std::string &text) = 0;
};

struct FilePickerRequest {
    enum class Mode { Open, OpenMultiple, Save };
    Mode mode = Mode::Open;
    std::string title, dir, file, wildcard; // wildcard: "Label|*.ext|..."
    bool mustExist = true;       // Open
    bool promptOverwrite = true; // Save
    std::string script;
};

class FilePickerPort {
public:
    virtual ~FilePickerPort() = default;
    // Answers once with the chosen paths, or nullopt when cancelled.
    virtual void pick(const FilePickerRequest &request, std::function<void(std::optional<std::vector<std::string>>)> reply) = 0;
    virtual void withdraw() = 0; // the run ended while the picker was open
};

class EditorPort {
public:
    virtual ~EditorPort() = default;
    virtual std::optional<std::pair<std::int64_t, std::int64_t>> selection() const = 0; // nullopt: no editor
    virtual void setSelection(std::int64_t start, std::int64_t end) = 0;
    virtual bool modified() const = 0;
    virtual void setStatusText(const std::string &text) = 0;
};

// Asked when a macro reads the script resolution while PlayResX or PlayResY is
// missing. Answered once with the resolution to write, or nullopt to write nothing.
struct ScriptResolutionRequest {
    std::string script;
    Resolution written; // 0: missing
};

class ScriptResolutionPort {
public:
    virtual ~ScriptResolutionPort() = default;
    virtual void resolve(const ScriptResolutionRequest &request, std::function<void(std::optional<Resolution>)> reply) = 0;
    virtual void withdraw() = 0; // the run ended while the question was open
};

// Directories for decode_path, in native form without a trailing separator.
struct AutomationPathContext {
    std::string audioPath, subtitlePath, videoPath;
    std::string automationDir;                      // <application>/Automation
    std::string dictionaryDir;                      // <application>/Dictionary
    bool windows = false;
    // Where scripts write (?user, ?local, ?temp); empty: automationDir.
    std::string userAutomationDir;
};

// decode_path's "?audio", "?data", "?dictionary", "?local", "?script", "?temp",
// "?user" and "?video" prefixes, matched by their 2nd and 5th characters,
// become directories; separators are normalised for the platform.
std::string decodeAutomationPath(std::string path, const AutomationPathContext &context);

// Native, without a trailing separator. An installed copy adds the user's
// folders: its autoload folders load after the shipped Autoload, its include
// folders come before the shipped Include.
struct AutomationFolders {
    std::vector<std::string> autoload; // the shipped one first
    // Searched in this order after the script's own; the shipped one is always last, and the Lua helper
    // loads its stock modules from there first.
    std::vector<std::string> include;
    std::vector<std::string> made;     // made at start: log, temp, autosave, autoload, include
};
// user empty or the same folder as shipped: a portable copy.
AutomationFolders automationFolders(const std::string &shipped, const std::string &user, bool windows);
// True when the absolute script path lies inside folder or below it, compared by case on Windows; a "." or
// ".." step in the rest refuses.
bool scriptInFolder(const std::string &script, const std::string &folder, bool windows);
// A later folder's script replaces an earlier one of the same name (by case on
// Windows) in its place. Only .lua and .moon files.
std::vector<std::string> chooseAutoloadScripts(
    const std::vector<std::pair<std::string, std::vector<std::string>>> &folders, bool windows);

class AutomationServiceRouter {
public:
    using Reply = std::function<void(HostServiceReply)>;

    void setMedia(MacroMediaPort *port) { m_media = port; }
    void setClipboard(ClipboardPort *port) { m_clipboard = port; }
    void setTextMeasure(TextMeasurePort *port) { m_measure = port; }
    void setFilePicker(FilePickerPort *port) { m_picker = port; }
    void setEditor(EditorPort *port) { m_editor = port; }
    void setScriptResolution(ScriptResolutionPort *port) { m_resolution = port; }
    void setPathContext(std::function<AutomationPathContext()> context) { m_paths = std::move(context); }
    // aegisub.gettext's lookup; without one the service is Unavailable and the
    // script gets its source.
    void setTranslation(std::function<std::string(const std::string &)> translate) { m_translate = std::move(translate); }

    void handle(const HostServiceRequest &request, Reply reply);
    // Withdraws an open picker or resolution question and drops its answer.
    void withdraw();

private:
    MacroMediaPort *m_media = nullptr;
    ClipboardPort *m_clipboard = nullptr;
    TextMeasurePort *m_measure = nullptr;
    FilePickerPort *m_picker = nullptr;
    EditorPort *m_editor = nullptr;
    ScriptResolutionPort *m_resolution = nullptr;
    std::function<AutomationPathContext()> m_paths;
    std::function<std::string(const std::string &)> m_translate;
    bool m_pickerOpen = false;
    bool m_resolutionOpen = false;
};

}
