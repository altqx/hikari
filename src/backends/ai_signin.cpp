#include "hikari/backends/ai_signin.h"

#include "hikari/backends/ai_sidecar_host.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace hikari::backends::ai {

namespace app = hikari::application::ai;
using api::Json;

namespace {

constexpr const char *kOpenRouterSecret = "openrouter-key";
constexpr int kVersionTimeoutMs = 5'000;

std::string stringAt(const Json &object, std::string_view key)
{
    const Json *value = object.find(key);
    return value && value->isString() ? value->asString() : std::string();
}

std::int64_t intAt(const Json &object, std::string_view key)
{
    const Json *value = object.find(key);
    return value && value->isNumber() ? static_cast<std::int64_t>(value->asNumber()) : 0;
}

QJsonObject claimsOf(const QString &jwt)
{
    const QStringList parts = jwt.split(QLatin1Char('.'));
    if (parts.size() < 2)
        return {};
    return QJsonDocument::fromJson(QByteArray::fromBase64(parts[1].toLatin1(), QByteArray::Base64UrlEncoding |
                                                                                    QByteArray::OmitTrailingEquals))
        .object();
}

const char *flowId(SignInFlow flow)
{
    switch (flow) {
    case SignInFlow::CodexDevice:
        return "codexDevice";
    case SignInFlow::CodexBrowser:
        return "codexBrowser";
    case SignInFlow::OpenRouter:
        return "openrouter";
    }
    return "codexDevice";
}

SignInFailure failureOf(const SidecarFailure &failure)
{
    if (failure.error == SidecarError::Cancelled)
        return {.kind = "cancelled"};
    if (failure.error == SidecarError::Remote && failure.code == rpc::kSignInFailed)
        return {.kind = stringAt(failure.data, "kind"), .host = stringAt(failure.data, "host"),
                .detail = stringAt(failure.data, "detail")};
    return {.kind = "sidecar", .detail = failure.message};
}

std::string keyErrorDetail(app::KeyError error)
{
    switch (error) {
    case app::KeyError::Unavailable:
        return "unavailable";
    case app::KeyError::Denied:
        return "denied";
    case app::KeyError::NotFound:
        return "notFound";
    case app::KeyError::Other:
        return "other";
    }
    return "other";
}

}

Json sessionObject(const CodexSession &s)
{
    return Json::object({{"access", s.access}, {"refresh", s.refresh}, {"idToken", s.idToken},
                         {"accountId", s.accountId}, {"email", s.email}, {"expiresAt", s.expiresAt}});
}

std::string sessionJson(const CodexSession &s)
{
    return sessionObject(s).dump();
}

std::optional<CodexSession> sessionFromJson(std::string_view json)
{
    const auto parsed = Json::parse(json);
    if (!parsed || !parsed->isObject())
        return std::nullopt;
    CodexSession s{.access = stringAt(*parsed, "access"), .refresh = stringAt(*parsed, "refresh"),
                   .idToken = stringAt(*parsed, "idToken"), .accountId = stringAt(*parsed, "accountId"),
                   .email = stringAt(*parsed, "email"), .expiresAt = intAt(*parsed, "expiresAt")};
    if (s.access.empty() || s.refresh.empty())
        return std::nullopt;
    return s;
}

std::optional<CodexSession> sessionFromCodexAuth(const QString &authJsonPath)
{
    QFile file(authJsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject tokens = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("tokens")).toObject();
    const QString idToken = tokens.value(QStringLiteral("id_token")).toString();
    const QString access = tokens.value(QStringLiteral("access_token")).toString();
    const QJsonObject identity = claimsOf(idToken);
    QString accountId = tokens.value(QStringLiteral("account_id")).toString();
    if (accountId.isEmpty())
        accountId = identity.value(QStringLiteral("https://api.openai.com/auth"))
                        .toObject()
                        .value(QStringLiteral("chatgpt_account_id"))
                        .toString();
    CodexSession s{.access = access.toStdString(),
                   .refresh = tokens.value(QStringLiteral("refresh_token")).toString().toStdString(),
                   .idToken = idToken.toStdString(),
                   .accountId = accountId.toStdString(),
                   .email = identity.value(QStringLiteral("email")).toString().toStdString(),
                   .expiresAt = static_cast<std::int64_t>(claimsOf(access).value(QStringLiteral("exp")).toDouble()) * 1000};
    if (s.access.empty() || s.refresh.empty())
        return std::nullopt;
    return s;
}

struct AiSignIn::Running {
    std::uint64_t serial = 0;
    std::string secret;
    Progress progress;
    Done done;
    QPointer<AiSidecarHost> host;
    std::uint64_t request = 0;
    std::string key;
    std::optional<SignInFailure> storeFailed;
};

AiSignIn::AiSignIn(app::KeyStore &keys, HostFor host) : m_keys(keys), m_host(std::move(host)) {}

AiSignIn::~AiSignIn()
{
    *m_alive = false;
    if (m_running && m_running->host)
        m_running->host->cancel(m_running->request);
}

bool AiSignIn::openable(const QUrl &url)
{
    return url.isValid() && url.scheme() == QLatin1String("https") && url.userInfo().isEmpty() && url.port() == -1 &&
           (url.host() == QLatin1String("auth.openai.com") || url.host() == QLatin1String("openrouter.ai"));
}

AiSidecarHost *AiSignIn::connect()
{
    AiSidecarHost *host = m_host();
    if (!host)
        return nullptr;
    host->onNotification("_hikari/signin.progress", [this, alive = std::weak_ptr(m_alive)](const Json &params) {
        if (!alive.lock() || !m_running || !m_running->progress)
            return;
        const Json *available = params.find("browserAvailable");
        QUrl url(QString::fromStdString(stringAt(params, "openUrl")));
        m_running->progress({.step = stringAt(params, "step"),
                             .userCode = stringAt(params, "userCode"),
                             .openUrl = openable(url) ? url : QUrl(),
                             .expiresAt = intAt(params, "expiresAt"),
                             .browserAvailable = !available || !available->isBool() || available->asBool()});
    });
    host->onRequest("_hikari/secret.store", [this, alive = std::weak_ptr(m_alive)](const Json &params,
                                                                                     AiSidecarHost::Respond respond) {
        if (!alive.lock()) {
            respond(std::unexpected(SidecarFailure{SidecarError::Remote, rpc::kInternalError, "gone", {}}));
            return;
        }
        store(params, [respond](std::optional<SignInFailure> failure) {
            if (failure)
                respond(std::unexpected(SidecarFailure{SidecarError::Remote, rpc::kInvalidParams, failure->detail, {}}));
            else
                respond(Json());
        });
    });
    return host;
}

void AiSignIn::store(const Json &params, std::function<void(std::optional<SignInFailure>)> respond)
{
    const std::string name = stringAt(params, "name");
    const std::string value = stringAt(params, "value");
    if (!m_running || name != m_running->secret || value.empty()) {
        respond(SignInFailure{.kind = "invalid", .detail = "no sign-in expects " + name});
        return;
    }
    if (name != kCodexSecret) {
        m_running->key = value;
        respond(std::nullopt);
        return;
    }
    if (!sessionFromJson(value)) {
        respond(SignInFailure{.kind = "invalid", .detail = "not a ChatGPT session"});
        return;
    }
    m_keys.write(kCodexSecret, value, [this, alive = std::weak_ptr(m_alive), respond](std::optional<app::KeyError> error) {
        if (!alive.lock())
            return;
        if (error && m_running)
            m_running->storeFailed = SignInFailure{.kind = "keychain", .detail = keyErrorDetail(*error)};
        respond(error ? std::optional(SignInFailure{.kind = "keychain", .detail = keyErrorDetail(*error)}) : std::nullopt);
    });
}

void AiSignIn::start(SignInFlow flow, Progress progress, Done done)
{
    if (m_running)
        cancel();
    const std::string secret = flow == SignInFlow::OpenRouter ? kOpenRouterSecret : kCodexSecret;
    m_running = std::make_unique<Running>(Running{.serial = ++m_serial, .secret = secret, .progress = std::move(progress)});
    run("_hikari/signin.start", Json::object({{"flow", flowId(flow)}, {"secret", secret}}), secret, std::move(done));
}

void AiSignIn::run(const std::string &method, Json params, std::string secret, Done done)
{
    if (!m_running)
        m_running = std::make_unique<Running>(Running{.serial = ++m_serial, .secret = std::move(secret)});
    m_running->done = std::move(done);
    AiSidecarHost *host = connect();
    if (!host) {
        finish(std::unexpected(SignInFailure{.kind = "sidecar", .detail = "the AI sidecar is not installed"}));
        return;
    }
    m_running->host = host;
    m_running->request = host->request(method, std::move(params), [this, alive = std::weak_ptr(m_alive),
                                                                    serial = m_running->serial](SidecarResult result) {
        if (!alive.lock() || !m_running || m_running->serial != serial)
            return;
        if (m_running->storeFailed)
            return finish(std::unexpected(*m_running->storeFailed));
        if (!result)
            return finish(std::unexpected(failureOf(result.error())));
        if (m_running->secret != kCodexSecret && m_running->key.empty())
            return finish(std::unexpected(SignInFailure{.kind = "invalid", .detail = "the sign-in sent no key"}));
        finish(SignedIn{.email = stringAt(*result, "email"), .key = std::move(m_running->key)});
    });
}

void AiSignIn::cancel()
{
    if (!m_running)
        return;
    if (m_running->host) {
        m_running->host->cancel(m_running->request);
        if (!m_running)
            return;
    }
    finish(std::unexpected(SignInFailure{.kind = "cancelled"}));
}

void AiSignIn::finish(std::expected<SignedIn, SignInFailure> result)
{
    const std::unique_ptr<Running> running = std::move(m_running);
    if (running && running->done)
        running->done(std::move(result));
}

void AiSignIn::codexSession(std::function<void(std::optional<CodexSession>)> done)
{
    m_keys.read(kCodexSecret, [alive = std::weak_ptr(m_alive), done = std::move(done)](app::KeyResult result) {
        if (!alive.lock())
            return;
        const auto *json = std::get_if<std::string>(&result);
        done(json ? sessionFromJson(*json) : std::nullopt);
    });
}

void AiSignIn::refreshCodex(Done done)
{
    if (m_running) {
        done(std::unexpected(SignInFailure{.kind = "invalid", .detail = "a sign-in is running"}));
        return;
    }
    codexSession([this, alive = std::weak_ptr(m_alive), done = std::move(done)](std::optional<CodexSession> session) mutable {
        if (!alive.lock())
            return;
        if (!session) {
            done(std::unexpected(SignInFailure{.kind = "expired"}));
            return;
        }
        auto params = Json::object({{"secret", kCodexSecret}, {"session", sessionObject(*session)}});
        run("_hikari/signin.refresh", std::move(params), kCodexSecret, std::move(done));
    });
}

void AiSignIn::importCodex(const QString &authJsonPath, Done done)
{
    const std::optional<CodexSession> session = sessionFromCodexAuth(authJsonPath);
    if (!session) {
        done(std::unexpected(SignInFailure{.kind = "import"}));
        return;
    }
    m_keys.write(kCodexSecret, sessionJson(*session),
                 [alive = std::weak_ptr(m_alive), email = session->email, done = std::move(done)](std::optional<app::KeyError> error) {
                     if (!alive.lock())
                         return;
                     if (error)
                         done(std::unexpected(SignInFailure{.kind = "keychain", .detail = keyErrorDetail(*error)}));
                     else
                         done(SignedIn{.email = email, .key = {}});
                 });
}

void AiSignIn::signOutCodex(std::function<void(std::optional<app::KeyError>)> done)
{
    m_keys.remove(kCodexSecret, [alive = std::weak_ptr(m_alive), done = std::move(done)](std::optional<app::KeyError> error) {
        if (alive.lock())
            done(error == app::KeyError::NotFound ? std::nullopt : error);
    });
}

void findCodexCli(std::function<void(std::optional<CodexCli>)> done)
{
    const QString path = QStandardPaths::findExecutable(QStringLiteral("codex"));
    if (path.isEmpty()) {
        done(std::nullopt);
        return;
    }
    auto *process = new QProcess;
    auto answer = std::make_shared<std::function<void(QString)>>([process, path, done = std::move(done)](QString version) mutable {
        auto once = std::move(done);
        done = nullptr;
        process->deleteLater();
        if (once)
            once(CodexCli{path, version});
    });
    QObject::connect(process, &QProcess::finished, process, [process, answer] {
        const QStringList words = QString::fromUtf8(process->readAllStandardOutput()).simplified().split(QLatin1Char(' '));
        (*answer)(process->exitCode() == 0 ? words.last() : QString());
    });
    QObject::connect(process, &QProcess::errorOccurred, process, [answer](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            (*answer)({});
    });
    QTimer::singleShot(kVersionTimeoutMs, process, [process] { process->kill(); });
    process->start(path, {QStringLiteral("--version")});
}

}
