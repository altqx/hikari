#pragma once

// Reading never touches a Document: the loaded one is staged until the caller applies it.

#include "hikari/application/font_collector.h"
#include "hikari/core/document.h"
#include "hikari/core/matroska.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application {

// Strings are FFMS2's bytes, not decoded.
struct MatroskaTrack {
    int track = 0; // the container's stream index
    std::string name, language, codec;
};
struct MatroskaPacket {
    std::int64_t start = 0, duration = 0;
    std::string line;
};
struct MatroskaSubtitles {
    std::string codecPrivate;
    std::vector<MatroskaPacket> packets; // in the callback's order
};
struct MatroskaAttachment {
    int track = 0;
    bool hasFilename = false;
    std::string filename, mimetype;
    std::shared_ptr<const std::vector<std::byte>> data;
};

enum class MatroskaFailure {
    CannotOpen, // text is FFMS2's error
    Cancelled,
    HelperLost, // the helper ended or could not start
    Failed,
};
struct MatroskaError {
    MatroskaFailure failure = MatroskaFailure::Failed;
    std::string text;
};

// One request runs at a time; each resolves exactly once.
class MatroskaPort {
public:
    using Tracks = std::function<void(std::expected<std::vector<MatroskaTrack>, MatroskaError>)>;
    using Progress = std::function<void(std::int64_t start, std::int64_t total)>;
    using Read = std::function<void(std::expected<MatroskaSubtitles, MatroskaError>)>;
    using Attachments = std::function<void(std::expected<std::vector<MatroskaAttachment>, MatroskaError>)>;

    virtual ~MatroskaPort() = default;
    virtual void subtitleTracks(const std::string &path, Tracks done) = 0;
    virtual void subtitles(const std::string &path, int track, Progress progress, Read done) = 0;
    virtual void attachments(const std::string &path, Attachments done) = 0;
    // The running request resolves Cancelled.
    virtual void cancel() = 0;
};

// The text tracks (ass, ssa, subrip, srt, text), named "<track> <name> (<language>, <codec>)".
struct OfferedTrack {
    int track = 0;
    std::u8string label;
    std::u8string codec;
};
std::vector<OfferedTrack> offeredTracks(const std::vector<MatroskaTrack> &tracks);

// The font attachments (font/ttf, font/otf, application/x-truetype-font,
// application/vnd.ms-opentype) in order, named by their file name without directory (UTF-8, else
// Latin-1), or "attachment_<track>.otf/.ttf" when that leaves none.
struct MatroskaFont {
    int track = 0;
    std::u16string name;
    std::shared_ptr<const std::vector<std::byte>> data;
};
std::vector<MatroskaFont> matroskaFonts(const std::vector<MatroskaAttachment> &attachments);

// The path without its extension, plus ".ass" (".srt" for SRT).
std::string matroskaSubtitlePath(const std::string &mkvPath, core::SubtitleFormat format);

// In any case: subtitles from ".mkv" or ".ogm", fonts from ".mkv"; isMkvExtension: the text
// after the last dot is "mkv".
bool subtitlesFromMkvEnabled(const std::u16string &videoName);
bool fontsFromMkvEnabled(const std::u16string &videoPath);
bool isMkvExtension(const std::u16string &videoPath);

struct MatroskaLoaded {
    std::string mkvPath;
    int track = 0;
    std::u8string label;
    core::Document document;
    std::string subtitlePath;
};

// Lists the tracks, asks when several, then reads with progress and cancel. Runs on the port's
// thread.
class MatroskaSubtitleLoad {
public:
    struct Hooks {
        // FFMS2's indexing error.
        std::function<void(const std::string &ffmsError)> cannotOpen;
        std::function<void()> noTracks;
        // Answer with choose() or cancel().
        std::function<void(const std::vector<std::u8string> &labels)> chooseTrack;
        std::function<void()> reading;
        std::function<void(int percent)> progress;
        std::function<void(MatroskaLoaded loaded)> loaded;
        // Cancelled, failed or the helper lost: nothing was loaded.
        std::function<void(MatroskaFailure failure)> ended;
    };
    MatroskaSubtitleLoad(MatroskaPort &port, Hooks hooks) : m_port(port), m_hooks(std::move(hooks)) {}

    enum class State { Idle, Listing, Choosing, Reading };
    State state() const { return m_state; }

    void start(const std::string &mkvPath);
    // No selection is row 0.
    void choose(int row);
    void cancel();

private:
    void read(int track, std::u8string label);
    void finish(MatroskaFailure failure);

    MatroskaPort &m_port;
    Hooks m_hooks;
    State m_state = State::Idle;
    std::string m_path;
    std::vector<OfferedTrack> m_offered;
    std::uint64_t m_run = 0; // results of an earlier run are dropped
};

struct MatroskaFontTab {
    std::u16string video;
    enum class Status { NotMkv, CannotOpen, NoFonts, Fonts } status = Status::NotMkv;
    std::vector<MatroskaFont> fonts;
};
struct MatroskaFontsSaved {
    bool cannotCreateFolder = false;
    bool outputFailed = false; // the folder or the archive could not be opened: the tab stops
    std::vector<bool> saved;   // per font
    bool cancelled = false;
};
// Writes every tab's fonts into one output, opened at the first tab with fonts; a tab whose
// output fails is skipped. The output is committed when anything was opened.
std::vector<MatroskaFontsSaved> saveMatroskaFonts(const std::vector<MatroskaFontTab> &tabs, CollectorOutput &output,
                                                  const std::atomic<bool> *cancel = nullptr);

} // namespace hikari::application
