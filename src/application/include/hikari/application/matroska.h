#pragma once

// Y9: MKV extraction (legacy Demux, Demux.cpp at 20d647c4, and its callers
// SubsGrid::OnMkvSubs, SubsGrid.cpp:1134-1194, and FontCollector::
// CopyMKVFonts, FontCollector.cpp:913-982). The media helper reads the file
// with the project's FFMS2 extensions (MatroskaPort); this layer keeps
// legacy's choices: which tracks are offered and how they are named, the
// Document built from a track, the font attachments and their file names,
// and where the loaded subtitles would be saved. Reading never touches a
// Document: the loaded one is staged until the caller applies it, so a
// cancelled read, a failure or the helper's loss leaves every Document as
// it was.

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

// What the helper reads. Strings are FFMS2's bytes, not decoded.
struct MatroskaTrack {
    int track = 0; // the container's stream index
    std::string name, language, codec; // FFMS_GetTrackName/Language, FFMS_GetSubtitleFormat
};
struct MatroskaPacket {
    std::int64_t start = 0, duration = 0; // as FFMS_GetSubtitles' callback gets them
    std::string line;
};
struct MatroskaSubtitles {
    std::string codecPrivate; // FFMS_GetSubtitleExtradata
    std::vector<MatroskaPacket> packets; // in the callback's order
};
struct MatroskaAttachment {
    int track = 0;
    bool hasFilename = false;
    std::string filename, mimetype;
    std::shared_ptr<const std::vector<std::byte>> data;
};

enum class MatroskaFailure {
    CannotOpen, // FFMS_CreateIndexer failed; text is FFMS2's error
    Cancelled,
    HelperLost, // the helper ended or could not start
    Failed,
};
struct MatroskaError {
    MatroskaFailure failure = MatroskaFailure::Failed;
    std::string text;
};

// The media helper's MKV requests (backends::FfmsMatroska). One request runs
// at a time; each resolves exactly once.
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

// Demux::GetSubtitles' list (Demux.cpp:68-83): the tracks whose codec is
// ass, ssa, subrip, srt or text (bitmap tracks are left out), each named
// "<track> <name> (<language>, <codec>)".
struct OfferedTrack {
    int track = 0;
    std::u8string label;
    std::u8string codec;
};
std::vector<OfferedTrack> offeredTracks(const std::vector<MatroskaTrack> &tracks);

// Demux::GetFontList (Demux.cpp:210-227) and AttachmentName (188-208): the
// attachments whose MIME type is font/ttf, font/otf,
// application/x-truetype-font or application/vnd.ms-opentype, in order,
// named by their file name without any directory (UTF-8, or Latin-1 when
// it is not UTF-8), or "attachment_<track>.otf/.ttf" when that leaves none.
struct MatroskaFont {
    int track = 0;
    std::u16string name;
    std::shared_ptr<const std::vector<std::byte>> data;
};
std::vector<MatroskaFont> matroskaFonts(const std::vector<MatroskaAttachment> &attachments);

// OnMkvSubs (SubsGrid.cpp:1162-1169): the file the loaded subtitles are
// named after, mkvpath.BeforeLast('.') + ".ass" (".srt" for SRT).
std::string matroskaSubtitlePath(const std::string &mkvPath, core::SubtitleFormat format);

// The menus' checks: SubsGrid's GRID_SUBS_FROM_MKV (VideoName ends with
// ".mkv" or ".ogm", SubsGrid.cpp:289, 879-881) and the font collector's
// "Demux fonts from loaded MKV file" (VideoPath.Lower() ends with ".mkv",
// FontCollector.cpp:182, 549); CopyMKVFontsFromTab's own check
// (AfterLast('.').Lower() == "mkv", FontCollector.cpp:935-937).
bool subtitlesFromMkvEnabled(const std::u16string &videoName);
bool fontsFromMkvEnabled(const std::u16string &videoPath);
bool isMkvExtension(const std::u16string &videoPath);

// A loaded track, staged until applied.
struct MatroskaLoaded {
    std::string mkvPath;
    int track = 0;
    std::u8string label;
    core::Document document;
    std::string subtitlePath; // matroskaSubtitlePath
};

// Demux::GetSubtitles' flow: the tracks, legacy's choice (none: the message;
// one: that track; several: the chooser), then the read with its progress
// and cancel. Runs on the port's thread.
class MatroskaSubtitleLoad {
public:
    struct Hooks {
        // Demux::Open's failure: HikariLog("Indexing error occurred: %s").
        std::function<void(const std::string &ffmsError)> cannotOpen;
        // "The file does not contain any subtitle tracks." (Demux.cpp:85-89).
        std::function<void()> noTracks;
        // HikariListBox "Choose subtitle track" (Demux.cpp:96-105): answer
        // with choose() or cancel().
        std::function<void(const std::vector<std::u8string> &labels)> chooseTrack;
        // ProgressSink "Loading subtitles from Matroska." opens, then shows
        // percents ((Start / Total) * 100, Demux.cpp:312-313).
        std::function<void()> reading;
        std::function<void(int percent)> progress;
        // The staged result.
        std::function<void(MatroskaLoaded loaded)> loaded;
        // Cancelled, failed or the helper lost: nothing was loaded.
        std::function<void(MatroskaFailure failure)> ended;
    };
    MatroskaSubtitleLoad(MatroskaPort &port, Hooks hooks) : m_port(port), m_hooks(std::move(hooks)) {}

    enum class State { Idle, Listing, Choosing, Reading };
    State state() const { return m_state; }

    void start(const std::string &mkvPath);
    // OK or a double click on a row (OnOKClick: no selection is row 0).
    void choose(int row);
    // The chooser's Cancel, or the progress dialog's.
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

// CopyMKVFonts' per-tab outcome (FontCollector.cpp:913-982).
struct MatroskaFontTab {
    std::u16string video;            // the tab's VideoPath
    enum class Status { NotMkv, CannotOpen, NoFonts, Fonts } status = Status::NotMkv;
    std::vector<MatroskaFont> fonts;
};
struct MatroskaFontsSaved {
    bool cannotCreateFolder = false; // MakeDirectory's "Cannot create folder."
    bool outputFailed = false;       // the folder or the archive could not be opened: the tab stops
    std::vector<bool> saved;         // per font, SaveFont's answer
    bool cancelled = false;
};
// Writes every tab's fonts into one output (the folder, or the archive
// created at the first tab with fonts), as CopyMKVFontsFromTab: the output
// is opened for a tab with fonts; a tab whose output fails is skipped. The
// output is committed (CloseZip) when anything was opened.
std::vector<MatroskaFontsSaved> saveMatroskaFonts(const std::vector<MatroskaFontTab> &tabs, CollectorOutput &output,
                                                  const std::atomic<bool> *cancel = nullptr);

} // namespace hikari::application
