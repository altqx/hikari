#pragma once

#include "hikari/api/json.h"
#include "hikari/application/ai_providers.h"

#include <QString>
#include <QUrl>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace hikari::backends::ai {

class AiSidecarHost;

/// The keychain entry with HikariSub's own ChatGPT sign-in for Codex.
inline constexpr const char *kCodexSecret = "codex-chatgpt";

/// HikariSub's ChatGPT sign-in for Codex, apart from the one in ~/.codex.
struct CodexSession {
    std::string access;
    std::string refresh;
    std::string idToken;
    std::string accountId;
    std::string email;
    /// Milliseconds since the epoch.
    std::int64_t expiresAt = 0;
    bool operator==(const CodexSession &) const = default;
};

api::Json sessionObject(const CodexSession &session);
std::string sessionJson(const CodexSession &session);
std::optional<CodexSession> sessionFromJson(std::string_view json);

/// The Codex CLI's own sign-in, read-only: `tokens` in its auth.json.
std::optional<CodexSession> sessionFromCodexAuth(const QString &authJsonPath);

enum class SignInFlow { CodexDevice, CodexBrowser, OpenRouter };

struct SignInProgress {
    /// "code" while a device code waits to be entered, "browser" while the browser has the sign-in.
    std::string step;
    std::string userCode;
    QUrl openUrl;
    std::int64_t expiresAt = 0;
    bool browserAvailable = true;
};

/// `kind` is the sidecar's (denied, codeExpired, codesOff, portBusy, network, expired, timedOut, invalid) or
/// cancelled, sidecar or keychain.
struct SignInFailure {
    std::string kind;
    std::string host;
    std::string detail;
};

struct SignedIn {
    std::string email;
    /// The OpenRouter key, for the provider being added; empty for Codex, whose session is in the keychain.
    std::string key;
};

/// Sign-ins run in the sidecar, one at a time. Tokens come back only through `_hikari/secret.store` under the name
/// the running sign-in expects, and reach the keychain only from here.
class AiSignIn {
public:
    /// Builds the sidecar if needed; null when none is installed.
    using HostFor = std::function<AiSidecarHost *()>;
    using Progress = std::function<void(const SignInProgress &)>;
    using Done = std::function<void(std::expected<SignedIn, SignInFailure>)>;

    AiSignIn(application::ai::KeyStore &keys, HostFor host);
    ~AiSignIn();
    AiSignIn(const AiSignIn &) = delete;
    AiSignIn &operator=(const AiSignIn &) = delete;

    /// Only https pages on auth.openai.com and openrouter.ai open from a sign-in.
    static bool openable(const QUrl &url);

    bool running() const { return m_running != nullptr; }
    /// Ends a running sign-in, if any, first. `done` comes once.
    void start(SignInFlow flow, Progress progress, Done done);
    /// `done` gets the cancelled failure.
    void cancel();

    void codexSession(std::function<void(std::optional<CodexSession>)> done);
    /// Trades the stored session's refresh token for new tokens; an `expired` failure means sign in again.
    void refreshCodex(Done done);
    /// Copies the Codex CLI's sign-in into HikariSub's; never writes `authJsonPath`.
    void importCodex(const QString &authJsonPath, Done done);
    /// Deletes HikariSub's ChatGPT sign-in; the Codex CLI's stays.
    void signOutCodex(std::function<void(std::optional<application::ai::KeyError>)> done);

private:
    struct Running;
    AiSidecarHost *connect();
    void run(const std::string &method, api::Json params, std::string secret, Done done);
    void store(const api::Json &params, std::function<void(std::optional<SignInFailure>)> respond);
    void finish(std::expected<SignedIn, SignInFailure> result);

    application::ai::KeyStore &m_keys;
    HostFor m_host;
    std::unique_ptr<Running> m_running;
    std::uint64_t m_serial = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

struct CodexCli {
    QString path;
    QString version;
};

/// `codex` on PATH and its `--version`; nullopt when it isn't there.
void findCodexCli(std::function<void(std::optional<CodexCli>)> done);

}
