#include "hikari/backends/external_aligner.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace hikari::backends {

namespace {

constexpr qsizetype kMessageChars = 2000;

std::int64_t msOf(double seconds, std::int64_t offsetMs)
{
    return offsetMs + std::llround(seconds * 1000);
}

void putLe(QByteArray &out, std::uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        out.append(char((value >> (8 * i)) & 0xFF));
}

QString programName(AlignJob::Program program)
{
    return program == AlignJob::Program::WhisperX ? QStringLiteral("whisperx") : QStringLiteral("mfa");
}

QString shownName(AlignJob::Program program)
{
    return program == AlignJob::Program::WhisperX ? QStringLiteral("WhisperX")
                                                  : QStringLiteral("the Montreal Forced Aligner (mfa)");
}

}

std::vector<AlignedSpan> whisperxWords(const QByteArray &json, std::int64_t offsetMs)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    std::vector<AlignedSpan> out;
    const auto take = [&](const QJsonArray &words) {
        for (const QJsonValue &value : words) {
            const QJsonObject word = value.toObject();
            if (!word.contains(QStringLiteral("start")) || !word.contains(QStringLiteral("end")))
                continue;
            out.push_back({word.value(QStringLiteral("word")).toString().trimmed().toStdString(),
                           msOf(word.value(QStringLiteral("start")).toDouble(), offsetMs),
                           msOf(word.value(QStringLiteral("end")).toDouble(), offsetMs)});
        }
    };
    if (root.contains(QStringLiteral("word_segments"))) {
        take(root.value(QStringLiteral("word_segments")).toArray());
        return out;
    }
    for (const QJsonValue &segment : root.value(QStringLiteral("segments")).toArray())
        take(segment.toObject().value(QStringLiteral("words")).toArray());
    return out;
}

std::vector<AlignedSpan> mfaWords(const QByteArray &json, std::int64_t offsetMs)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonArray entries = root.value(QStringLiteral("tiers"))
                                   .toObject()
                                   .value(QStringLiteral("words"))
                                   .toObject()
                                   .value(QStringLiteral("entries"))
                                   .toArray();
    std::vector<AlignedSpan> out;
    for (const QJsonValue &value : entries) {
        const QJsonArray entry = value.toArray();
        if (entry.size() < 3)
            continue;
        const QString label = entry.at(2).toString().trimmed();
        if (label.isEmpty())
            continue;
        out.push_back({label.toStdString(), msOf(entry.at(0).toDouble(), offsetMs), msOf(entry.at(1).toDouble(), offsetMs)});
    }
    return out;
}

QByteArray wavBytes(const std::vector<std::int16_t> &samples, int sampleRate)
{
    const auto dataBytes = std::uint32_t(samples.size() * 2);
    QByteArray out;
    out.reserve(qsizetype(44 + dataBytes));
    out.append("RIFF");
    putLe(out, 36 + dataBytes, 4);
    out.append("WAVEfmt ");
    putLe(out, 16, 4);
    putLe(out, 1, 2);
    putLe(out, 1, 2);
    putLe(out, std::uint32_t(sampleRate), 4);
    putLe(out, std::uint32_t(sampleRate) * 2, 4);
    putLe(out, 2, 2);
    putLe(out, 16, 2);
    out.append("data");
    putLe(out, dataBytes, 4);
    for (const std::int16_t s : samples)
        putLe(out, std::uint16_t(s), 2);
    return out;
}

struct ExternalAligner::Job {
    QDir dir;
    QProcess *process = nullptr;
    std::function<void(AlignOutcome)> done;
};

ExternalAligner::ExternalAligner(QString workRoot, QObject *parent) : QObject(parent), m_workRoot(std::move(workRoot))
{
}

ExternalAligner::~ExternalAligner()
{
    for (auto &job : m_jobs) {
        if (job->process) {
            job->process->disconnect();
            job->process->kill();
            job->process->waitForFinished(2000);
        }
        job->dir.removeRecursively();
    }
}

void ExternalAligner::run(const AlignJob &job, const application::DisplayAudio *audio,
                          std::function<void(AlignOutcome)> done)
{
    const auto later = [this, done](AlignOutcome outcome) {
        QTimer::singleShot(0, this, [done, outcome = std::move(outcome)] { done(outcome); });
    };
    const QString name = programName(job.program);
    const QString program = m_lookup ? m_lookup(name) : QStandardPaths::findExecutable(name);
    if (program.isEmpty())
        return later({AlignOutcome::Kind::Missing,
                      (shownName(job.program) + QStringLiteral(" is not installed, or \"%1\" is not on the PATH. "
                                                               "Install it, then ask again.").arg(name)).toStdString(),
                      {}});
    if (!audio || audio->sampleRate() <= 0 || !audio->finished())
        return later({AlignOutcome::Kind::NoAudio, "No audio is open, or it is still loading.", {}});

    auto owned = std::make_unique<Job>();
    Job &j = *owned;
    j.dir = QDir(m_workRoot + QStringLiteral("/align-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(j.dir.path()))
        return later({AlignOutcome::Kind::Failed, "The aligner's work folder could not be made.", {}});
    const int rate = audio->sampleRate();
    const std::int64_t first = job.startMs * rate / 1000, count = std::max<std::int64_t>(0, (job.endMs - job.startMs) * rate / 1000);
    std::vector<std::int16_t> samples(static_cast<std::size_t>(count));
    audio->read(first, count, samples.data());
    QFile wav(j.dir.filePath(QStringLiteral("clip.wav"))), words(j.dir.filePath(QStringLiteral("clip.txt")));
    if (!wav.open(QIODevice::WriteOnly) || wav.write(wavBytes(samples, rate)) < 0 || !words.open(QIODevice::WriteOnly) ||
        words.write(QByteArray::fromStdString(job.text)) < 0) {
        j.dir.removeRecursively();
        return later({AlignOutcome::Kind::Failed, "The audio clip could not be written.", {}});
    }
    wav.close();
    words.close();

    QStringList arguments;
    QString output;
    if (job.program == AlignJob::Program::WhisperX) {
        arguments = {QStringLiteral("clip.wav"), QStringLiteral("--output_dir"), QStringLiteral("."),
                     QStringLiteral("--output_format"), QStringLiteral("json")};
        if (!job.language.empty())
            arguments << QStringLiteral("--language") << QString::fromStdString(job.language);
        output = QStringLiteral("clip.json");
    } else {
        arguments = {QStringLiteral("align_one"), QStringLiteral("clip.wav"), QStringLiteral("clip.txt"),
                     QString::fromStdString(job.dictionary), QString::fromStdString(job.acousticModel),
                     QStringLiteral("aligned.json"), QStringLiteral("--output_format"), QStringLiteral("json")};
        output = QStringLiteral("aligned.json");
    }

    j.done = std::move(done);
    j.process = new QProcess(this);
    j.process->setWorkingDirectory(j.dir.path());
    j.process->setProcessChannelMode(QProcess::MergedChannels);
    const auto finish = [this, raw = &j](AlignOutcome outcome) {
        auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [raw](const auto &p) { return p.get() == raw; });
        if (it == m_jobs.end())
            return;
        std::unique_ptr<Job> ended = std::move(*it);
        m_jobs.erase(it);
        ended->process->deleteLater();
        ended->dir.removeRecursively();
        ended->done(std::move(outcome));
    };
    const AlignJob::Program kind = job.program;
    const std::int64_t offset = job.startMs;
    connect(j.process, &QProcess::finished, this, [raw = &j, finish, output, kind, offset, name](int code, QProcess::ExitStatus status) {
        const QString log = QString::fromUtf8(raw->process->readAll()).right(kMessageChars);
        if (status != QProcess::NormalExit || code != 0)
            return finish({AlignOutcome::Kind::Failed,
                           (name + QStringLiteral(" stopped with exit code %1: ").arg(code) + log).toStdString(), {}});
        QFile result(raw->dir.filePath(output));
        if (!result.open(QIODevice::ReadOnly))
            return finish({AlignOutcome::Kind::Failed, (name + QStringLiteral(" wrote no ") + output).toStdString(), {}});
        const QByteArray json = result.readAll();
        auto words = kind == AlignJob::Program::WhisperX ? whisperxWords(json, offset) : mfaWords(json, offset);
        if (words.empty())
            return finish({AlignOutcome::Kind::Failed, (name + QStringLiteral(" found no words.")).toStdString(), {}});
        finish({AlignOutcome::Kind::Aligned, {}, std::move(words)});
    });
    connect(j.process, &QProcess::errorOccurred, this, [raw = &j, finish, name](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish({AlignOutcome::Kind::Failed, (name + QStringLiteral(" could not be started: ") +
                                                 raw->process->errorString()).toStdString(), {}});
    });
    m_jobs.push_back(std::move(owned));
    j.process->start(program, arguments);
}

}
