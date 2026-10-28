#include "hikari/application/matroska.h"

#include "hikari/core/text_projection.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace hikari::application {

namespace {

std::u16string utf16(const std::u8string &text)
{
    return core::toUtf16(text);
}

// wxString(bytes, wxConvUTF8).
std::u8string decoded(const std::string &bytes)
{
    return core::legacy::utf8OrEmpty(bytes);
}

// wxString(bytes, wxConvISO8859_1): every byte is its own code point.
std::u16string latin1(const std::string &bytes)
{
    std::u16string out;
    for (const char c : bytes)
        out.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
    return out;
}

std::u16string asciiLower(std::u16string s)
{
    for (auto &c : s)
        if (c >= u'A' && c <= u'Z')
            c = static_cast<char16_t>(c - u'A' + u'a');
    return s;
}

bool isFontMimetype(const std::string &mimetype)
{
    return mimetype == "font/ttf" || mimetype == "font/otf" || mimetype == "application/x-truetype-font" ||
           mimetype == "application/vnd.ms-opentype";
}

// AttachmentName (Demux.cpp:188-208).
std::u16string attachmentName(const MatroskaAttachment &a)
{
    std::u16string name;
    if (a.hasFilename && !a.filename.empty()) {
        name = utf16(decoded(a.filename));
        if (name.empty())
            name = latin1(a.filename);
    }
    std::ranges::replace(name, u'\\', u'/');
    if (const auto slash = name.rfind(u'/'); slash != std::u16string::npos)
        name = name.substr(slash + 1);
    if (name == u"." || name == u"..")
        name.clear();
    if (name.empty()) {
        const bool opentype = a.mimetype == "font/otf" || a.mimetype == "application/vnd.ms-opentype";
        const std::string synthesized = "attachment_" + std::to_string(a.track) + (opentype ? ".otf" : ".ttf");
        name.assign(synthesized.begin(), synthesized.end());
    }
    return name;
}

} // namespace

std::vector<OfferedTrack> offeredTracks(const std::vector<MatroskaTrack> &tracks)
{
    std::vector<OfferedTrack> out;
    for (const auto &t : tracks) {
        const std::u8string codec = decoded(t.codec);
        if (codec != u8"ass" && codec != u8"ssa" && codec != u8"subrip" && codec != u8"srt" && codec != u8"text")
            continue;
        const std::string number = std::to_string(t.track) + " "; // "%i "
        std::u8string label(number.begin(), number.end());
        label += decoded(t.name) + u8" (" + decoded(t.language) + u8", " + codec + u8")";
        out.push_back({t.track, std::move(label), codec});
    }
    return out;
}

std::vector<MatroskaFont> matroskaFonts(const std::vector<MatroskaAttachment> &attachments)
{
    std::vector<MatroskaFont> out;
    for (const auto &a : attachments)
        if (isFontMimetype(a.mimetype))
            out.push_back({a.track, attachmentName(a), a.data});
    return out;
}

std::string matroskaSubtitlePath(const std::string &mkvPath, core::SubtitleFormat format)
{
    // wxString::BeforeLast('.'): empty without a dot.
    const auto dot = mkvPath.rfind('.');
    const std::string base = dot == std::string::npos ? std::string() : mkvPath.substr(0, dot);
    return base + (format == core::SubtitleFormat::Srt ? ".srt" : ".ass");
}

bool subtitlesFromMkvEnabled(const std::u16string &videoName)
{
    // Legacy's EndsWith was case-sensitive (Y9-mkv-case): any case here, as
    // the collector's check.
    const std::u16string lower = asciiLower(videoName);
    return lower.ends_with(u".mkv") || lower.ends_with(u".ogm");
}

bool fontsFromMkvEnabled(const std::u16string &videoPath)
{
    return asciiLower(videoPath).ends_with(u".mkv");
}

bool isMkvExtension(const std::u16string &videoPath)
{
    // AfterLast('.'): the whole path when it has no dot.
    const auto dot = videoPath.rfind(u'.');
    return asciiLower(dot == std::u16string::npos ? videoPath : videoPath.substr(dot + 1)) == u"mkv";
}

void MatroskaSubtitleLoad::start(const std::string &mkvPath)
{
    const std::uint64_t run = ++m_run;
    m_path = mkvPath;
    m_offered.clear();
    m_state = State::Listing;
    m_port.subtitleTracks(mkvPath, [this, run](std::expected<std::vector<MatroskaTrack>, MatroskaError> tracks) {
        if (run != m_run || m_state != State::Listing)
            return;
        if (!tracks) {
            // Demux::Open (Demux.cpp:44-47): logged, nothing else.
            if (tracks.error().failure == MatroskaFailure::CannotOpen && m_hooks.cannotOpen)
                m_hooks.cannotOpen(tracks.error().text);
            return finish(tracks.error().failure);
        }
        m_offered = offeredTracks(*tracks);
        if (m_offered.empty()) {
            if (m_hooks.noTracks)
                m_hooks.noTracks();
            return finish(MatroskaFailure::Failed);
        }
        if (m_offered.size() == 1)
            return read(m_offered.front().track, m_offered.front().label);
        m_state = State::Choosing;
        std::vector<std::u8string> labels;
        for (const auto &o : m_offered)
            labels.push_back(o.label);
        if (m_hooks.chooseTrack)
            m_hooks.chooseTrack(labels);
    });
}

void MatroskaSubtitleLoad::choose(int row)
{
    if (m_state != State::Choosing)
        return;
    row = std::clamp(row, 0, int(m_offered.size()) - 1);
    read(m_offered[std::size_t(row)].track, m_offered[std::size_t(row)].label);
}

void MatroskaSubtitleLoad::cancel()
{
    if (m_state == State::Idle)
        return;
    const bool running = m_state != State::Choosing;
    ++m_run; // a result already on its way, or the port's own Cancelled, is dropped
    if (running)
        m_port.cancel();
    finish(MatroskaFailure::Cancelled);
}

void MatroskaSubtitleLoad::read(int track, std::u8string label)
{
    const std::uint64_t run = m_run;
    m_state = State::Reading;
    std::u8string codecName;
    for (const auto &o : m_offered)
        if (o.track == track)
            codecName = o.codec;
    const core::MatroskaCodec codec = core::matroskaCodec(codecName);
    if (m_hooks.reading)
        m_hooks.reading();
    m_port.subtitles(
        m_path, track,
        [this, run](std::int64_t start, std::int64_t total) {
            if (run != m_run || !m_hooks.progress)
                return;
            // int prog = ((double(Start)) / double(Total)) * 100 (Demux.cpp:312).
            const double p = (double(start) / double(total)) * 100;
            m_hooks.progress(std::isfinite(p) ? int(std::clamp(p, 0.0, 100.0)) : 0);
        },
        [this, run, track, codec, label = std::move(label)](std::expected<MatroskaSubtitles, MatroskaError> read) {
            if (run != m_run || m_state != State::Reading)
                return;
            if (!read)
                return finish(read.error().failure);
            std::vector<std::u8string> lines;
            lines.reserve(read->packets.size());
            for (const auto &p : read->packets)
                lines.push_back(core::matroskaLine(p.start, p.duration, p.line, codec));
            MatroskaLoaded loaded;
            loaded.mkvPath = m_path;
            loaded.track = track;
            loaded.label = label;
            loaded.document = core::matroskaDocument(read->codecPrivate, lines, codec);
            loaded.subtitlePath = matroskaSubtitlePath(m_path, loaded.document.format());
            m_state = State::Idle;
            if (m_hooks.loaded)
                m_hooks.loaded(std::move(loaded));
        });
}

void MatroskaSubtitleLoad::finish(MatroskaFailure failure)
{
    m_state = State::Idle;
    if (m_hooks.ended)
        m_hooks.ended(failure);
}

std::vector<MatroskaFontsSaved> saveMatroskaFonts(const std::vector<MatroskaFontTab> &tabs, CollectorOutput &output,
                                                  const std::atomic<bool> *cancel)
{
    std::vector<MatroskaFontsSaved> out(tabs.size());
    bool opened = false;
    for (std::size_t t = 0; t < tabs.size(); ++t) {
        const auto &tab = tabs[t];
        auto &saved = out[t];
        if (tab.status != MatroskaFontTab::Status::Fonts || tab.fonts.empty())
            continue;
        // MakeDirectory(operation & AS_ZIP) once a tab has fonts (FontCollector.cpp:953-958).
        if (!opened) {
            if (!output.open()) {
                saved.outputFailed = true;
                saved.cannotCreateFolder = output.folderFailed();
                continue;
            }
            opened = true;
        }
        for (const auto &font : tab.fonts) {
            if (cancel && *cancel) {
                saved.cancelled = true;
                output.discard();
                return out;
            }
            // Demux::SaveFont (Demux.cpp:229-260): the attachment's bytes as they are.
            saved.saved.push_back(font.data && output.put(font.name, *font.data));
        }
    }
    if (opened)
        output.commit(); // CloseZip (FontCollector.cpp:928)
    return out;
}

} // namespace hikari::application
