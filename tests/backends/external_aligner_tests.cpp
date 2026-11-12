#include "hikari/backends/external_aligner.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <optional>

using namespace hikari;
using namespace hikari::backends;

namespace {

class ExternalAlignerTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_external_aligner_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    std::optional<AlignOutcome> run(const AlignJob &job, const application::DisplayAudio *audio)
    {
        std::optional<AlignOutcome> outcome;
        aligner.run(job, audio, [&](AlignOutcome o) { outcome = std::move(o); });
        EXPECT_FALSE(outcome) << "done runs later";
        QElapsedTimer timer;
        timer.start();
        while (!outcome && timer.elapsed() < 20000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return outcome;
    }

    QString program(const QByteArray &script)
    {
        const QString path = bin.filePath(QStringLiteral("aligner"));
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\n" + script);
        file.close();
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
        return path;
    }

    QTemporaryDir bin, work;
    ExternalAligner aligner{work.path()};
    application::DisplayAudio audio = application::DisplayAudio::silence(16000, 16000 * 4);
};

TEST(ExternalAlignerJson, WhisperXWordSegmentsComeFirst)
{
    const auto words = whisperxWords(R"({"segments": [{"words": [{"word": "no", "start": 9, "end": 9.5}]}],
        "word_segments": [{"word": " twin", "start": 0.25, "end": 0.5}, {"word": "kle"}, {"word": "star", "start": 1, "end": 1.75}]})",
                                     1000);
    ASSERT_EQ(words.size(), 2u);
    EXPECT_EQ(words[0].text, "twin");
    EXPECT_EQ(words[0].startMs, 1250);
    EXPECT_EQ(words[0].endMs, 1500);
    EXPECT_EQ(words[1].endMs, 2750);
}

TEST(ExternalAlignerJson, WhisperXFallsBackToEachSegmentsWords)
{
    const auto words = whisperxWords(R"({"segments": [{"words": [{"word": "a", "start": 0, "end": 0.1}]},
        {"words": [{"word": "b", "start": 0.2, "end": 0.3}]}]})", 0);
    ASSERT_EQ(words.size(), 2u);
    EXPECT_EQ(words[1].text, "b");
    EXPECT_EQ(words[1].startMs, 200);
}

TEST(ExternalAlignerJson, MfaSkipsSilence)
{
    const auto words = mfaWords(R"({"tiers": {"words": {"type": "interval", "entries": [[0, 0.5, ""], [0.5, 0.9, "twinkle"],
        [0.9, 1.4, "star"]]}, "phones": {"entries": [[0, 1, "t"]]}}})", 2000);
    ASSERT_EQ(words.size(), 2u);
    EXPECT_EQ(words[0].text, "twinkle");
    EXPECT_EQ(words[0].startMs, 2500);
    EXPECT_EQ(words[1].endMs, 3400);
}

TEST(ExternalAlignerJson, TheClipIsAMono16BitWav)
{
    const QByteArray wav = wavBytes({1, -1}, 16000);
    ASSERT_EQ(wav.size(), 48);
    EXPECT_EQ(wav.left(4), "RIFF");
    EXPECT_EQ(wav.mid(8, 8), "WAVEfmt ");
    EXPECT_EQ(wav.mid(36, 4), "data");
    EXPECT_EQ(quint8(wav[22]), 1);
    EXPECT_EQ(quint8(wav[24]) | quint8(wav[25]) << 8, 16000);
    EXPECT_EQ(quint8(wav[46]), 0xFF);
}

TEST_F(ExternalAlignerTest, AMissingProgramIsNamed)
{
    aligner.setLookup([](const QString &) { return QString(); });
    const auto outcome = run({AlignJob::Program::WhisperX, 0, 1000, "twinkle", {}, {}, {}}, &audio);
    ASSERT_TRUE(outcome);
    EXPECT_EQ(outcome->kind, AlignOutcome::Kind::Missing);
    EXPECT_NE(outcome->message.find("WhisperX"), std::string::npos);
}

TEST_F(ExternalAlignerTest, NoAudioIsNamed)
{
    aligner.setLookup([](const QString &name) { return name; });
    const auto outcome = run({AlignJob::Program::WhisperX, 0, 1000, "twinkle", {}, {}, {}}, nullptr);
    ASSERT_TRUE(outcome);
    EXPECT_EQ(outcome->kind, AlignOutcome::Kind::NoAudio);
}

#ifndef _WIN32
TEST_F(ExternalAlignerTest, WhisperXRunsInItsOwnFolderAndItsWordsAreRead)
{
    const QString path = program(R"sh(
[ "$1" = clip.wav ] && [ -s clip.wav ] && [ "$(cat clip.txt)" = "twinkle star" ] || exit 3
[ "$6" = --language ] && [ "$7" = en ] || exit 4
echo '{"word_segments": [{"word": "twinkle", "start": 0.1, "end": 0.6}, {"word": "star", "start": 0.7, "end": 1.2}]}' > clip.json
)sh");
    aligner.setLookup([path](const QString &name) { return name == QStringLiteral("whisperx") ? path : QString(); });
    const auto outcome = run({AlignJob::Program::WhisperX, 1000, 3000, "twinkle star", "en", {}, {}}, &audio);
    ASSERT_TRUE(outcome);
    ASSERT_EQ(outcome->kind, AlignOutcome::Kind::Aligned) << outcome->message;
    ASSERT_EQ(outcome->words.size(), 2u);
    EXPECT_EQ(outcome->words[0].startMs, 1100);
    EXPECT_EQ(outcome->words[1].endMs, 2200);
    EXPECT_TRUE(QDir(work.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
}

TEST_F(ExternalAlignerTest, MfaGetsTheModelsAndAFailureKeepsItsOutput)
{
    const QString path = program(R"sh(
echo "args: $1 $4 $5" ; exit 2
)sh");
    aligner.setLookup([path](const QString &name) { return name == QStringLiteral("mfa") ? path : QString(); });
    const auto outcome = run({AlignJob::Program::Mfa, 0, 1000, "star", {}, "english_mfa", "english_us_arpa"}, &audio);
    ASSERT_TRUE(outcome);
    EXPECT_EQ(outcome->kind, AlignOutcome::Kind::Failed);
    EXPECT_NE(outcome->message.find("exit code 2"), std::string::npos) << outcome->message;
    EXPECT_NE(outcome->message.find("args: align_one english_mfa english_us_arpa"), std::string::npos) << outcome->message;
}
#endif

}
