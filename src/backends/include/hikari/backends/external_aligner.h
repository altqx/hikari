#pragma once

#include "hikari/application/audio_display.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class QProcess;

namespace hikari::backends {

// A forced-alignment job for a program the user installed: WhisperX or the Montreal Forced Aligner.
struct AlignJob {
    enum class Program { WhisperX, Mfa };
    Program program = Program::WhisperX;
    std::int64_t startMs = 0, endMs = 0;
    std::string text; // the words sung, one Line a row
    std::string language;
    std::string dictionary, acousticModel;
};

struct AlignedSpan {
    std::string text;
    std::int64_t startMs = 0, endMs = 0;
};

struct AlignOutcome {
    enum class Kind { Aligned, Missing, NoAudio, Failed };
    Kind kind = Kind::Failed;
    std::string message;
    std::vector<AlignedSpan> words; // in ms from the audio's start
};

// WhisperX's JSON: word_segments, else each segment's words; times in seconds from the clip's start.
std::vector<AlignedSpan> whisperxWords(const QByteArray &json, std::int64_t offsetMs);
// MFA's JSON: tiers.words.entries as [start, end, label]; empty labels are silence.
std::vector<AlignedSpan> mfaWords(const QByteArray &json, std::int64_t offsetMs);
// A mono 16-bit PCM WAV file's bytes.
QByteArray wavBytes(const std::vector<std::int16_t> &samples, int sampleRate);

// Writes the clip and the words to a folder of its own, runs the program there and reads its JSON.
class ExternalAligner : public QObject {
public:
    // `workRoot`: where each job's folder goes; it is removed when the job ends.
    explicit ExternalAligner(QString workRoot, QObject *parent = nullptr);
    ~ExternalAligner() override;

    // Empty: not installed. QStandardPaths::findExecutable when unset.
    void setLookup(std::function<QString(const QString &name)> lookup) { m_lookup = std::move(lookup); }

    // `done` runs once, later, on this object's thread.
    void run(const AlignJob &job, const application::DisplayAudio *audio, std::function<void(AlignOutcome)> done);

private:
    struct Job;
    std::function<QString(const QString &)> m_lookup;
    QString m_workRoot;
    std::vector<std::unique_ptr<Job>> m_jobs;
};

}
