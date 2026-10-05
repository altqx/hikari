// P9: associated-file discovery (legacy HikariSubFrame::FindFile,
// HikariSubFrame.cpp:1704-1729, and Notebook::LoadVideo's question,
// Notebook.cpp:1155-1290, at 20d647c4). The Script Info fixtures are in
// tests/fixtures/associations (C05: a Document's own associations only).

#include "hikari/application/associated_files.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <random>
#include <set>
#include <string_view>

using namespace hikari;
using namespace hikari::application;
namespace fs = std::filesystem;

namespace {

core::Document parse(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

std::vector<std::byte> readFile(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

class SameNamedFile : public ::testing::Test {
protected:
    void SetUp() override
    {
        std::random_device rd;
        folder = fs::temp_directory_path() / ("hikari-associated-" + std::to_string(rd()));
        fs::create_directories(folder);
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(folder, ec);
    }
    fs::path touch(const std::string &name)
    {
        std::ofstream(folder / name) << "x";
        return folder / name;
    }
    fs::path folder;
};

} // namespace

// FindFile: "<name>.*" beside the file, the file itself counted; the first
// with a video (or subtitle) extension, compared case sensitively.
TEST_F(SameNamedFile, FindsTheVideoBesideTheSubtitles)
{
    const auto subs = touch("ep01.ass");
    EXPECT_FALSE(findSameNamedFile(subs, true)); // fewer than two files
    touch("ep01.txt");
    EXPECT_FALSE(findSameNamedFile(subs, true)); // no video among them
    touch("ep01.mkv");
    touch("ep02.mkv");             // another name
    touch("ep01.extra.mkv");       // "ep01.*" matches, but its name differs before the last '.'
    fs::create_directories(folder / "ep01.dir");
    const auto found = findSameNamedFile(subs, true);
    ASSERT_TRUE(found);
    // Both "ep01.mkv" and "ep01.extra.mkv" match "ep01.*"; which comes first
    // is the folder's listing order, as wxDir's.
    EXPECT_TRUE(found->filename() == "ep01.mkv" || found->filename() == "ep01.extra.mkv");
}

TEST_F(SameNamedFile, FindsTheSubtitlesBesideAVideo)
{
    const auto video = touch("movie.mkv");
    touch("movie.srt");
    const auto found = findSameNamedFile(video, false);
    ASSERT_TRUE(found);
    EXPECT_EQ(found->filename(), "movie.srt");
}

#ifndef _WIN32
// The extension compare is case sensitive (AfterLast('.') without Lower):
// "EP.MKV" is no video. On Linux wxMatchWild is case sensitive too.
TEST_F(SameNamedFile, ExtensionsAndNamesAreCaseSensitive)
{
    const auto subs = touch("ep.ass");
    touch("ep.MKV");
    EXPECT_FALSE(findSameNamedFile(subs, true));
    touch("EP.mkv"); // another name on Linux
    EXPECT_FALSE(findSameNamedFile(subs, true));
    touch(".hidden.mkv");
    touch("ep.m2ts");
    EXPECT_EQ(findSameNamedFile(subs, true)->filename(), "ep.m2ts");
}
#endif

// The legacy question's lines: only resolved associations the tab does not
// have yet; the audio equal to the video listed but loaded with it.
TEST(AssociationOffer, ListsWhatTheTabDoesNotHave)
{
    const auto doc = parse("[Script Info]\nVideo File: ep1.mkv\nAudio File: ep1.mkv\nKeyframes File: kf.txt\n");
    const std::set<std::string> files{"/subs/ep1.mkv", "/subs/kf.txt"};
    const auto a = resolveMediaAssociations(doc, "/subs/ep1.ass", [&](const std::string &p) { return files.contains(p); }, false);
    auto offer = associationOffer(a, "", {});
    EXPECT_EQ(offer.video, "/subs/ep1.mkv");
    EXPECT_EQ(offer.audio, "/subs/ep1.mkv"); // listed (Notebook.cpp:1197)
    EXPECT_TRUE(offer.audioIsVideo);
    EXPECT_EQ(offer.keyframes, "/subs/kf.txt");
    EXPECT_TRUE(offer.associated());
    auto load = associationLoad(offer, AssociationAnswer::LoadAssociated);
    EXPECT_EQ(load.video, "/subs/ep1.mkv");
    EXPECT_TRUE(load.videoAudio); // sameAudioPath: hasAudioPath = false
    EXPECT_EQ(load.audio, "");
    EXPECT_EQ(load.keyframes, "/subs/kf.txt");
    // The tab shows that video already: not listed, nothing reloads.
    offer = associationOffer(a, "", {"/subs/ep1.mkv", "", "/subs/kf.txt"});
    EXPECT_EQ(offer.video, "");
    EXPECT_EQ(offer.keyframes, "");
    EXPECT_TRUE(offer.associated()); // the audio line remains
    load = associationLoad(offer, AssociationAnswer::LoadAssociated);
    EXPECT_EQ(load.video, "");
    EXPECT_EQ(load.audio, "");
    // No loads nothing.
    load = associationLoad(associationOffer(a, "", {}), AssociationAnswer::No);
    EXPECT_EQ(load.video, "");
    EXPECT_EQ(load.keyframes, "");
}

// C05: the Script Info "Audio File" association loads that audio and the
// video then opens without its own.
TEST(AssociationOffer, TheAudioFileAssociationOpensItsOwnAudio)
{
    const auto doc = parse("[Script Info]\nVideo File: ep1.mkv\nAudio File: audio/ep1.flac\n");
    const std::set<std::string> files{"/subs/ep1.mkv", "/subs/audio/ep1.flac"};
    const auto a = resolveMediaAssociations(doc, "/subs/ep1.ass", [&](const std::string &p) { return files.contains(p); }, false);
    const auto offer = associationOffer(a, "", {});
    EXPECT_FALSE(offer.audioIsVideo);
    const auto load = associationLoad(offer, AssociationAnswer::LoadAssociated);
    EXPECT_EQ(load.video, "/subs/ep1.mkv");
    EXPECT_FALSE(load.videoAudio); // LoadVideo(..., !hasAudioPath)
    EXPECT_EQ(load.audio, "/subs/audio/ep1.flac");
    // Only the audio: no video at all.
    const auto audioOnly = parse("[Script Info]\nAudio File: audio/ep1.flac\n");
    const auto b = resolveMediaAssociations(audioOnly, "/subs/x.ass", [&](const std::string &p) { return files.contains(p); }, false);
    const auto l = associationLoad(associationOffer(b, "", {}), AssociationAnswer::LoadAssociated);
    EXPECT_EQ(l.video, "");
    EXPECT_EQ(l.audio, "/subs/audio/ep1.flac");
    // Dummy audio (audiopath.StartsWith("dummy")) is offered as written.
    const auto dummy = parse("[Script Info]\nAudio File: dummy-audio:silence?sr=44100&bd=16&ch=1&ln=396900000\n");
    const auto c = resolveMediaAssociations(dummy, "/subs/x.ass", [](const std::string &) { return false; }, false);
    const auto o = associationOffer(c, "", {});
    EXPECT_EQ(o.audio, "dummy-audio:silence?sr=44100&bd=16&ch=1&ln=396900000");
    EXPECT_EQ(associationLoad(o, AssociationAnswer::LoadAssociated).audio, o.audio);
}

// "Video from directory": the folder's same-named video; Load associated
// takes it when there is no associated video, Load from directory takes it
// with nothing associated; the tab's own video already means no question.
TEST(AssociationOffer, TheFoldersVideo)
{
    const auto doc = parse("[Script Info]\nAudio File: ep1.wav\n");
    const std::set<std::string> files{"/subs/ep1.wav"};
    const auto a = resolveMediaAssociations(doc, "/subs/ep1.ass", [&](const std::string &p) { return files.contains(p); }, false);
    const auto offer = associationOffer(a, "/subs/ep1.mkv", {});
    EXPECT_TRUE(offer.associated());
    EXPECT_EQ(offer.directoryVideo, "/subs/ep1.mkv");
    auto load = associationLoad(offer, AssociationAnswer::LoadAssociated);
    EXPECT_EQ(load.video, "/subs/ep1.mkv"); // found = !path.empty()
    EXPECT_EQ(load.audio, "/subs/ep1.wav");
    EXPECT_FALSE(load.videoAudio);
    load = associationLoad(offer, AssociationAnswer::LoadFromDirectory);
    EXPECT_EQ(load.video, "/subs/ep1.mkv");
    EXPECT_TRUE(load.videoAudio);
    EXPECT_EQ(load.audio, "");
    // The tab shows the folder's video: return -1, nothing asked even for the audio.
    const auto none = associationOffer(a, "/subs/ep1.mkv", {"/subs/ep1.mkv", "", ""});
    EXPECT_FALSE(none.asks());
    EXPECT_EQ(associationLoad(none, AssociationAnswer::LoadAssociated).audio, "");
    // Nothing associated, nothing beside: no question.
    EXPECT_FALSE(associationOffer(resolveMediaAssociations(parse("[Script Info]\n"), "/subs/b.ass",
                                                           [](const std::string &) { return false; }, false),
                                  "", {})
                     .asks());
}

// The fixture files of tests/fixtures/associations: ep01 names its video,
// audio and keyframes relative to itself; ep02's associations are missing
// and only the folder's same-named video is offered.
TEST(AssociationOffer, Fixtures)
{
    const fs::path dir = HIKARI_ASSOCIATION_FIXTURES;
#ifdef _WIN32
    constexpr bool windows = true;
#else
    constexpr bool windows = false;
#endif
    const auto exists = [](const std::string &p) { return fs::is_regular_file(fs::u8path(p)); };
    const auto ep01 = core::loadAss(readFile(dir / "ep01.ass")).document;
    const auto a = resolveMediaAssociations(ep01, (dir / "ep01.ass").string(), exists, windows);
    const auto offer = associationOffer(a, findSameNamedFile(dir / "ep01.ass", true).value_or(fs::path()).string(), {});
    EXPECT_EQ(fs::path(offer.video).filename(), "ep01.mkv");
    EXPECT_EQ(fs::path(offer.audio).filename(), "ep01.flac");
    EXPECT_EQ(fs::path(offer.keyframes).filename(), "ep01_keyframes.txt");
    EXPECT_EQ(fs::path(offer.directoryVideo).filename(), "ep01.mkv");
    const auto ep02 = core::loadAss(readFile(dir / "ep02.ass")).document;
    const auto b = resolveMediaAssociations(ep02, (dir / "ep02.ass").string(), exists, windows);
    ASSERT_TRUE(b.video && b.audio);
    EXPECT_FALSE(b.video->resolved);
    EXPECT_FALSE(b.audio->resolved);
    const auto offer2 = associationOffer(b, findSameNamedFile(dir / "ep02.ass", true).value_or(fs::path()).string(), {});
    EXPECT_FALSE(offer2.associated());
    EXPECT_EQ(fs::path(offer2.directoryVideo).filename(), "ep02.mp4");
}
