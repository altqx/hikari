#include "fake_host.h"
#include "script_client.h"

#include "hikari/api/nodes.h"

#include <gtest/gtest.h>

using namespace hikari::api;
using namespace hikari::api::testing;

namespace {

const std::filesystem::path kScripts = HIKARI_API_SCRIPTS_DIR;

class MediaAndFiles : public ::testing::Test {
protected:
    FakeHost host;
    Api api{host.host()};
    ScriptClient client{api};

    std::string error(std::string_view operation, const char *params = "{}")
    {
        const Result result = client.call(aiCaller(), operation, *Json::parse(params));
        return result ? std::string("ok") : result.error().code;
    }
};

TEST_F(MediaAndFiles, Media)
{
    EXPECT_GT(client.run(kScripts / "media.jsonl"), 0);
}

TEST_F(MediaAndFiles, Files)
{
    host.files.contents = {{"/home/u/notes.txt", "h\xC3\xA9llo"}, {"/bin/blob", std::string("\xFF\0", 2)}};
    EXPECT_EQ(api.grantFile("test.ai", "/home/u/notes.txt", false), "f1");
    EXPECT_EQ(api.grantFile("test.ai", "/subs/out.ass", true), "f2");
    api.grantFile("test.ai", "/gone/missing.txt", false);
    api.grantFile("test.ai", "/bin/blob", false);
    api.grantFile("test.ai", "/locked/x.txt", true);
    EXPECT_GT(client.run(kScripts / "files.jsonl"), 0);
    EXPECT_EQ(host.files.contents.at("/subs/out.ass"), "hi");
}

TEST_F(MediaAndFiles, GrantingAgainChangesAccessAndRevokingEndsIt)
{
    host.files.contents["/a.txt"] = "a";
    const std::string handle = api.grantFile("test.ai", "/a.txt", false);
    EXPECT_EQ(error("files.write", R"({"file": "f1", "data": "b"})"), "not_granted");
    EXPECT_EQ(api.grantFile("test.ai", "/a.txt", true), handle);
    EXPECT_EQ(error("files.write", R"({"file": "f1", "data": "b"})"), "ok");
    api.revokeFiles("test.ai");
    EXPECT_EQ(error("files.read", R"({"file": "f1"})"), "not_granted");
}

TEST_F(MediaAndFiles, AFileOver16MiBIsRefused)
{
    host.files.contents["/big.bin"] = std::string(16 * 1024 * 1024 + 1, 'a');
    api.grantFile("test.ai", "/big.bin", true);
    const Result read = client.call(aiCaller(), "files.read", *Json::parse(R"({"file": "f1"})"));
    ASSERT_FALSE(read);
    EXPECT_EQ(*read.error().data.find("reason"), Json("too_large"));
    host.files.contents["/big.bin"].pop_back();
    EXPECT_EQ(error("files.read", R"({"file": "f1"})"), "ok");
}

TEST_F(MediaAndFiles, NoVideoNoAudio)
{
    host.media.videoOpen = false;
    host.media.audioOpen = false;
    EXPECT_EQ(*client.call(aiCaller(), "video.info", Json()), *Json::parse(R"({"open": false})"));
    EXPECT_EQ(error("video.frame_at_time", R"({"time": 0})"), "unavailable");
    EXPECT_EQ(error("video.time_of_frame", R"({"frame": 0})"), "unavailable");
    EXPECT_EQ(error("video.frame"), "unavailable");
    EXPECT_EQ(*client.call(aiCaller(), "audio.info", Json()), *Json::parse(R"({"open": false})"));
    EXPECT_EQ(error("audio.peaks", R"({"start": 0, "end": 10})"), "unavailable");
}

TEST_F(MediaAndFiles, LoadingAudioIsNotReady)
{
    host.media.audioLoading = true;
    EXPECT_EQ(*client.call(aiCaller(), "audio.info", Json()), *Json::parse(R"({"open": true, "ready": false})"));
    const Result peaks = client.call(aiCaller(), "audio.peaks", *Json::parse(R"({"start": 0, "end": 10})"));
    ASSERT_FALSE(peaks);
    EXPECT_EQ(peaks.error().code, errors::kRefused);
    EXPECT_EQ(*peaks.error().data.find("reason"), Json("loading"));
}

TEST_F(MediaAndFiles, AFrameHasSubtitlesUnlessAskedNotTo)
{
    ASSERT_TRUE(client.call(aiCaller(), "video.frame", Json()));
    EXPECT_TRUE(host.media.lastWithSubtitles);
    ASSERT_TRUE(client.call(aiCaller(), "video.frame", *Json::parse(R"({"subtitles": false})")));
    EXPECT_FALSE(host.media.lastWithSubtitles);
}

TEST_F(MediaAndFiles, WithoutPortsEverythingIsUnavailable)
{
    Api bare{Host{}};
    ScriptClient none{bare};
    EXPECT_EQ(none.call(aiCaller(), "files.read", *Json::parse(R"({"file": "f1"})")).error().code, errors::kUnavailable);
    EXPECT_EQ(none.call(aiCaller(), "video.frame", Json()).error().code, errors::kUnavailable);
    EXPECT_EQ(*none.call(aiCaller(), "files.list", Json()), *Json::parse(R"({"files": []})"));
}

TEST_F(MediaAndFiles, RegisteredNodeTypesAreCatalogued)
{
    NodeType split{"test.split", "test", "Splits text.", {{"text", "string"}}, {{"parts", "string[]"}},
                   *Json::parse(R"({"type": "object", "properties": {"by": {"type": "string"}}})"), 1};
    nodeCatalogue().add(split);
    EXPECT_FALSE(nodeCatalogue().add(split));
    const Result listed = client.call(aiCaller(), "nodes.catalogue", *Json::parse(R"({"category": "test"})"));
    ASSERT_TRUE(listed);
    EXPECT_EQ(listed->dump(), R"({"nodes":[{"id":"test.split","category":"test","summary":"Splits text.",)"
                              R"("inputs":[{"name":"text","type":"string"}],"outputs":[{"name":"parts","type":"string[]"}],)"
                              R"("params":{"type":"object","properties":{"by":{"type":"string"}}},"since":1}]})");
    EXPECT_EQ(*client.call(aiCaller(), "nodes.catalogue", *Json::parse(R"({"category": "none"})")),
              *Json::parse(R"({"nodes": []})"));
}

TEST_F(MediaAndFiles, TheEnginesNodesAreCataloguedAndGraphsValidated)
{
    const Result units = client.call(aiCaller(), "nodes.catalogue", *Json::parse(R"({"category": "units"})"));
    ASSERT_TRUE(units);
    EXPECT_EQ(units->find("nodes")->asArray().front().find("id")->asString(), "units.for_each");
    const Result fine = client.call(aiCaller(), "nodes.validate", *Json::parse(R"({"graph": {"format": 1, "nodes": [
        {"id": "syl", "type": "units.for_each"}, {"id": "emit", "type": "output.emit"}],
        "wires": [{"from": "syl.units", "to": "emit.units"}]}})"));
    ASSERT_TRUE(fine);
    EXPECT_EQ(*fine, *Json::parse(R"({"valid": true, "problems": []})"));
    const Result bad = client.call(aiCaller(), "nodes.validate", *Json::parse(R"({"graph": {"format": 1, "nodes": [
        {"id": "syl", "type": "units.for_each", "fields": {"unit": "word"}}, {"id": "emit", "type": "output.emit"}],
        "wires": [{"from": "syl.units", "to": "emit.units"}]}})"));
    ASSERT_TRUE(bad);
    EXPECT_EQ(bad->dump(), Json::parse(R"({"valid": false, "problems": [
        {"node": "syl", "port": "unit", "message": "is not one of its choices"}]})")->dump());
}

}
