#include "hikari/app/ai_karaoke.h"

#include "audio_controller.h"

#include "hikari/application/vocal_onsets.h"

#include <QTimer>

#include <atomic>
#include <utility>

namespace hikari::app {

class AiKaraoke::Templater final : public application::KaraokeTemplaterPort {
public:
    explicit Templater(AiKaraoke &owner) : m_owner(owner) {}

    bool available() const override
    {
        const auto *port = shared();
        return port && port->available();
    }

    // MUST KILL: the shared templater drops a superseded preview's done, so a timer answers it.
    void preview(const application::MacroSnapshot &snapshot, PreviewDone done) override
    {
        auto pending = std::make_shared<PreviewDone>(std::move(done));
        const auto once = [pending](Preview preview) {
            if (!*pending)
                return;
            PreviewDone run = std::move(*pending);
            *pending = nullptr;
            run(std::move(preview));
        };
        QTimer::singleShot(m_owner.m_wiring.templaterTimeout, &m_owner, [once] {
            once({std::nullopt, "it did not answer: the preview was replaced or took too long"});
        });
        if (auto *port = shared())
            port->preview(snapshot, once);
        else
            once({std::nullopt, "no templater script is loaded"});
    }

    void cancelPreview() override
    {
        if (auto *port = shared())
            port->cancelPreview();
    }

    bool apply(bool selectedOnly) override
    {
        auto *port = shared();
        return port && port->apply(selectedOnly);
    }

private:
    application::KaraokeTemplaterPort *shared() const
    {
        return m_owner.m_wiring.templater ? m_owner.m_wiring.templater() : nullptr;
    }

    AiKaraoke &m_owner;
};

struct AiKaraoke::Onsets {
    std::weak_ptr<const application::DisplayAudio> audio;
    std::shared_ptr<const std::vector<int>> found;
    std::vector<std::function<void(std::optional<std::vector<int>>)>> waiting;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::thread worker;

    void stop()
    {
        if (cancel)
            cancel->store(true);
        if (worker.joinable())
            worker.join();
        cancel.reset();
    }

    void answer(std::optional<std::vector<int>> onsets)
    {
        for (auto &done : std::exchange(waiting, {}))
            done(onsets);
    }
};

AiKaraoke::AiKaraoke(Wiring wiring, QObject *parent)
    : QObject(parent), m_wiring(std::move(wiring)), m_templater(std::make_unique<Templater>(*this)),
      m_onsets(std::make_unique<Onsets>()), m_aligner(m_wiring.workRoot)
{
}

AiKaraoke::~AiKaraoke()
{
    m_onsets->stop();
}

application::KaraokeCharClass AiKaraoke::classes() const
{
    return ui::karaokeCharClass();
}

const application::Hyphenation *AiKaraoke::hyphenation() const
{
    return &ui::karaokeHyphenation();
}

application::ReadingAnalyzer AiKaraoke::readings() const
{
    return m_wiring.readings ? m_wiring.readings() : application::ReadingAnalyzer{};
}

void AiKaraoke::onsets(std::function<void(std::optional<std::vector<int>>)> done)
{
    std::shared_ptr<const application::DisplayAudio> audio = m_wiring.audio ? m_wiring.audio() : nullptr;
    if (!audio || !audio->finished() || audio->sampleRate() <= 0)
        return done(std::nullopt);
    Onsets &o = *m_onsets;
    const bool same = o.audio.lock() == audio;
    if (same && o.found)
        return done(*o.found);
    o.waiting.push_back(std::move(done));
    if (same && o.cancel)
        return;
    o.stop();
    o.audio = audio;
    o.found.reset();
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    o.cancel = cancel;
    o.worker = std::thread([this, audio, cancel] {
        std::optional<std::vector<int>> found;
        try {
            found = application::detectVocalOnsets(*audio, cancel.get());
        } catch (const std::exception &) {
            found.emplace();
        }
        QMetaObject::invokeMethod(
            this,
            [this, cancel, found = std::move(found)] {
                Onsets &o = *m_onsets;
                if (cancel->load() || o.cancel != cancel)
                    return;
                o.worker.join();
                o.cancel.reset();
                if (found)
                    o.found = std::make_shared<const std::vector<int>>(*found);
                else
                    o.audio.reset();
                o.answer(found);
            },
            Qt::QueuedConnection);
    });
}

application::KaraokeTemplaterPort *AiKaraoke::templater()
{
    return m_templater.get();
}

void AiKaraoke::align(const api::AlignRequest &request, std::function<void(api::AlignResult)> done)
{
    backends::AlignJob job;
    job.program = request.program == "mfa" ? backends::AlignJob::Program::Mfa : backends::AlignJob::Program::WhisperX;
    job.startMs = request.startMs;
    job.endMs = request.endMs;
    job.text = request.text;
    job.language = request.language;
    job.dictionary = request.dictionary;
    job.acousticModel = request.acousticModel;
    std::shared_ptr<const application::DisplayAudio> audio = m_wiring.audio ? m_wiring.audio() : nullptr;
    m_aligner.run(job, audio.get(), [audio, done = std::move(done)](backends::AlignOutcome outcome) {
        api::AlignResult result;
        using Kind = backends::AlignOutcome::Kind;
        result.outcome = outcome.kind == Kind::Aligned   ? "aligned"
                         : outcome.kind == Kind::Missing ? "missing"
                         : outcome.kind == Kind::NoAudio ? "no_audio"
                                                         : "failed";
        result.message = std::move(outcome.message);
        for (backends::AlignedSpan &word : outcome.words)
            result.words.push_back({std::move(word.text), word.startMs, word.endMs});
        done(std::move(result));
    });
}

}
