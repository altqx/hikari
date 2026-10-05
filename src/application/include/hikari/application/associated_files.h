#pragma once

// P9: associated-file discovery when subtitles or a video open (legacy
// HikariSubFrame::OpenFile and FindFile, Notebook::LoadVideo with loadPrompt,
// at 20d647c4).
//
// Two sources are offered:
//  - the Document's own Script Info "Video File", "Audio File" and
//    "Keyframes File" (resolved by resolveMediaAssociations, I1; approved
//    C05-audio-association: never another tab's), listed under "Associated
//    files:";
//  - a video beside the subtitles with the same name ("Video from
//    directory:"), or, when a video opens, subtitles beside it with the same
//    name ("Load subtitles named ...?"), found as FindFile finds them.
// Nothing is substituted silently: every file is named in the question
// before it loads.

#include "hikari/application/media_association.h"

#include <filesystem>
#include <optional>
#include <string>

namespace hikari::application {

// HikariSubFrame::FindFile (HikariSubFrame.cpp:1704-1729): the files beside
// `file` named "<its name without the last extension>.*" (wxDir::GetAllFiles
// with wxDIR_FILES, so no subfolders and no hidden files, in the platform's
// listing order, legacy_dir), the file itself included; with fewer than two
// nothing is found. Otherwise the first whose extension (after the last '.',
// compared case sensitively) is a video's (avi mp4 mkv ogm wmv asf rmvb rm 3gp
// ts m2ts m4v flv) when `video`, else a subtitle file's (ass txt sub srt ssa).
std::optional<std::filesystem::path> findSameNamedFile(const std::filesystem::path &file, bool video);

// What the tab already has open (legacy TabPanel VideoPath, AudioPath and
// KeyframesPath): an association equal to it is not offered again.
struct TabMediaPaths {
    std::string video, audio, keyframes;
};

// Notebook::LoadVideo's question for subtitles just opened (Notebook.cpp:
// 1155-1290, loadPrompt true). Each listed path is as legacy shows it: the
// Script Info value when it is an existing absolute path, else the subtitle
// folder joined with it (resolveMediaAssociations); an "Audio File" starting
// with "dummy" is dummy audio and listed as written.
struct AssociationOffer {
    std::string video;     // "Video: " (empty: not listed)
    std::string audio;     // "Audio: "
    std::string keyframes; // "Keyframes: "
    // The Script Info audio is the associated video's file (legacy
    // sameAudioPath): listed, but it comes with the video, not on its own.
    bool audioIsVideo = false;
    std::string directoryVideo; // "Video from directory:" (full path; the question shows its name)
    // Associated files are listed: the OK button ("Load associated" beside
    // "Load from directory", else "Yes").
    bool associated() const { return !video.empty() || !audio.empty() || !keyframes.empty(); }
    // There is a question at all (legacy: otherwise wxNO, nothing loads).
    bool asks() const { return associated() || !directoryVideo.empty(); }
};

// The offer for `associations` and the folder's same-named video, `tab`
// being what the tab already shows. Legacy returned early, offering nothing
// at all, when the folder's video is the tab's video already.
AssociationOffer associationOffer(const MediaAssociations &associations, const std::string &directoryVideo,
                                  const TabMediaPaths &tab);

// The answers: legacy's wxOK, wxYES and wxNO.
enum class AssociationAnswer { LoadAssociated, LoadFromDirectory, No };

// What an answer loads (Notebook.cpp:1225-1281).
struct AssociationLoad {
    std::string video;      // empty: no video
    bool videoAudio = true; // the video's own audio opens with it (no associated audio of its own)
    std::string audio;      // the associated audio file (or dummy audio), empty: none
    std::string keyframes;  // empty: none
};
AssociationLoad associationLoad(const AssociationOffer &offer, AssociationAnswer answer);

} // namespace hikari::application
