#include "hikari/application/associated_files.h"

#include "hikari/application/legacy_dir.h"

#include <array>
#include <string_view>

namespace hikari::application {

namespace {

constexpr std::array<std::u16string_view, 5> kSubtitles{u"ass", u"txt", u"sub", u"srt", u"ssa"};
// HikariSubFrame.cpp:1718-1720 (avs left out there too).
constexpr std::array<std::u16string_view, 13> kVideos{u"avi", u"mp4",  u"mkv", u"ogm", u"wmv", u"asf", u"rmvb",
                                                      u"rm",  u"3gp",  u"ts",  u"m2ts", u"m4v", u"flv"};

// wxString::AfterLast: the whole string when the character is missing.
std::u16string afterLast(const std::u16string &s, char16_t c)
{
    const auto at = s.rfind(c);
    return at == std::u16string::npos ? s : s.substr(at + 1);
}

// wxString::BeforeLast: empty when the character is missing.
std::u16string beforeLast(const std::u16string &s, char16_t c)
{
    const auto at = s.rfind(c);
    return at == std::u16string::npos ? std::u16string() : s.substr(0, at);
}

bool startsWith(std::string_view s, std::string_view prefix)
{
    return s.substr(0, prefix.size()) == prefix;
}

} // namespace

std::optional<std::filesystem::path> findSameNamedFile(const std::filesystem::path &file, bool video)
{
    // wxFileName(fn).GetPath() / GetFullName(), then GetAllFiles(path, &files,
    // filespec.BeforeLast('.') + ".*", wxDIR_FILES).
    const std::u16string spec = beforeLast(file.filename().u16string(), u'.') + u".*";
    const auto files = legacy_dir::entries(file.parent_path(), spec, legacy_dir::Files);
    if (!files || files->size() < 2)
        return std::nullopt;
    for (const auto &found : *files) {
        // files[i].AfterLast('.'), on the full path, not lowered.
        const std::u16string ext = afterLast(found.u16string(), u'.');
        bool matches = false;
        if (video) {
            for (const auto e : kVideos)
                matches = matches || ext == e;
        } else {
            for (const auto e : kSubtitles)
                matches = matches || ext == e;
        }
        if (matches)
            return found;
    }
    return std::nullopt;
}

AssociationOffer associationOffer(const MediaAssociations &associations, const std::string &directoryVideo,
                                  const TabMediaPaths &tab)
{
    AssociationOffer offer;
    // hasVideoPath / hasAudioPath / hasKeyframePath (Notebook.cpp:1174-1182):
    // an existing file, or "dummy" audio (audiopath.StartsWith("dummy")).
    const std::string video = associations.video && associations.video->resolved ? *associations.video->resolved : "";
    std::string audio = associations.audio && associations.audio->resolved ? *associations.audio->resolved : "";
    if (associations.audio && startsWith(associations.audio->authored, "dummy"))
        audio = associations.audio->authored;
    const std::string keyframes =
        associations.keyframes && associations.keyframes->resolved ? *associations.keyframes->resolved : "";
    // sameAudioPath: the audio is the video's file (1187).
    offer.audioIsVideo = !audio.empty() && audio == video;
    // An association the tab already has is not listed (1195-1200).
    if (!video.empty() && tab.video != video)
        offer.video = video;
    if (!audio.empty() && tab.audio != audio)
        offer.audio = audio;
    if (!keyframes.empty() && tab.keyframes != keyframes)
        offer.keyframes = keyframes;
    if (!directoryVideo.empty()) {
        if (tab.video == directoryVideo)
            return {}; // return -1: nothing is asked or loaded (1208-1209)
        offer.directoryVideo = directoryVideo;
    }
    return offer;
}

AssociationLoad associationLoad(const AssociationOffer &offer, AssociationAnswer answer)
{
    AssociationLoad load;
    if (!offer.asks() || answer == AssociationAnswer::No)
        return load; // result & wxNO: return -1
    if (answer == AssociationAnswer::LoadFromDirectory) {
        // wxYES: nothing associated, only the folder's video (with its audio).
        load.video = offer.directoryVideo;
        return load;
    }
    // wxOK: the associated video, else the folder's (found = !path.empty()).
    load.video = !offer.video.empty() ? offer.video : offer.directoryVideo;
    const bool ownAudio = !offer.audio.empty() && !offer.audioIsVideo;
    if (ownAudio)
        load.audio = offer.audio;
    load.videoAudio = !ownAudio; // LoadVideo(..., !hasAudioPath)
    load.keyframes = offer.keyframes;
    return load;
}

} // namespace hikari::application
