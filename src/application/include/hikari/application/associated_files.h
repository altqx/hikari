#pragma once

#include "hikari/application/media_association.h"

#include <filesystem>
#include <optional>
#include <string>

namespace hikari::application {

// Searches files beside `file` named "<stem>.*" (no subfolders, no hidden files), the file itself
// included; nullopt with fewer than two. Returns the first with a video extension when `video`,
// else a subtitle one (case-sensitive).
std::optional<std::filesystem::path> findSameNamedFile(const std::filesystem::path &file, bool video);

// An association equal to what the tab already has open is not offered again.
struct TabMediaPaths {
    std::string video, audio, keyframes, timecodes;
};

struct AssociationOffer {
    std::string video;     // empty: not listed
    std::string audio;
    std::string keyframes;
    std::string timecodes;
    // Listed, but loads with the video, not on its own.
    bool audioIsVideo = false;
    std::string directoryVideo; // full path
    bool associated() const { return !video.empty() || !audio.empty() || !keyframes.empty() || !timecodes.empty(); }
    bool asks() const { return associated() || !directoryVideo.empty(); }
};

// Offers nothing when the folder's video is already the tab's video.
AssociationOffer associationOffer(const MediaAssociations &associations, const std::string &directoryVideo,
                                  const TabMediaPaths &tab);

enum class AssociationAnswer { LoadAssociated, LoadFromDirectory, No };

struct AssociationLoad {
    std::string video;      // empty: no video
    bool videoAudio = true; // the video's own audio opens with it
    std::string audio;      // associated or dummy audio, empty: none
    std::string keyframes;  // empty: none
    std::string timecodes;  // empty: none; loads after the video, which drops a tab's file
    bool loadsAnything() const { return !video.empty() || !audio.empty() || !keyframes.empty() || !timecodes.empty(); }
};
AssociationLoad associationLoad(const AssociationOffer &offer, AssociationAnswer answer);

}
