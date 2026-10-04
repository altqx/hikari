#pragma once

// The application side of automation host services (L3; docs/qt/automation.md).
// The router answers each HostServiceRequest once through a platform port; a
// port that is not attached answers Unavailable (the legacy nil). Ports run on
// the GUI thread; a file picker answers later, and a run that ends first
// withdraws it.

#include "hikari/application/audio_display.h"
#include "hikari/application/automation.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hikari::application {

// Media and the target Document, as the legacy tab exposed them.
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
    // The audio aegisub.get_frequency_peaks reads (legacy: the video's FFMS2
    // provider, else the audio box's): nullopt without audio (legacy "needs
    // loaded audio by FFMS2"), a null pointer while the audio box has none
    // yet ("cannot get audio provider").
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
    std::string title, dir, file, wildcard; // wildcard in the legacy wx form: "Label|*.ext|..."
    bool mustExist = true;       // Open
    bool promptOverwrite = true; // Save
    std::string script;          // the script that asked
};

class FilePickerPort {
public:
    virtual ~FilePickerPort() = default;
    // Answers once with the chosen paths, or nullopt when cancelled.
    virtual void pick(const FilePickerRequest &request, std::function<void(std::optional<std::vector<std::string>>)> reply) = 0;
    virtual void withdraw() = 0; // the run ended while the picker was open
};

// The Line editor and the status bar.
class EditorPort {
public:
    virtual ~EditorPort() = default;
    virtual std::optional<std::pair<std::int64_t, std::int64_t>> selection() const = 0; // nullopt: no editor
    virtual void setSelection(std::int64_t start, std::int64_t end) = 0;
    virtual bool modified() const = 0;
    virtual void setStatusText(const std::string &text) = 0;
};

// Directories for decode_path, in native form without a trailing separator.
struct AutomationPathContext {
    std::string audioPath, subtitlePath, videoPath; // files of the target Document
    std::string automationDir;                      // <application>/Automation
    std::string dictionaryDir;                      // <application>/Dictionary
    bool windows = false;
};

// Legacy decode_path: "?audio", "?data", "?dictionary", "?local", "?script",
// "?temp", "?user" and "?video" prefixes, recognised by their 2nd and 5th
// characters, become directories; separators are normalised for the platform.
std::string decodeAutomationPath(std::string path, const AutomationPathContext &context);

class AutomationServiceRouter {
public:
    using Reply = std::function<void(HostServiceReply)>;

    void setMedia(MacroMediaPort *port) { m_media = port; }
    void setClipboard(ClipboardPort *port) { m_clipboard = port; }
    void setTextMeasure(TextMeasurePort *port) { m_measure = port; }
    void setFilePicker(FilePickerPort *port) { m_picker = port; }
    void setEditor(EditorPort *port) { m_editor = port; }
    void setPathContext(std::function<AutomationPathContext()> context) { m_paths = std::move(context); }

    void handle(const HostServiceRequest &request, Reply reply);
    // The run ended: an open picker is withdrawn and its answer dropped.
    void withdraw();

private:
    MacroMediaPort *m_media = nullptr;
    ClipboardPort *m_clipboard = nullptr;
    TextMeasurePort *m_measure = nullptr;
    FilePickerPort *m_picker = nullptr;
    EditorPort *m_editor = nullptr;
    std::function<AutomationPathContext()> m_paths;
    bool m_pickerOpen = false;
};

} // namespace hikari::application
