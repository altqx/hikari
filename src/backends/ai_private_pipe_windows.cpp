#include "ai_private_pipe.h"

#include <QLocalSocket>
#include <QWinEventNotifier>

#include <vector>

#include <windows.h>
#include <sddl.h>

namespace hikari::backends::ai {

namespace {

QString userSid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    QString out;
    LPWSTR text = nullptr;
    if (size && GetTokenInformation(token, TokenUser, buffer.data(), size, &size) &&
        ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &text)) {
        out = QString::fromWCharArray(text);
        LocalFree(text);
    }
    CloseHandle(token);
    return out;
}

QString lastError(const char *what)
{
    return QStringLiteral("%1 failed (%2)").arg(QLatin1String(what)).arg(GetLastError());
}

}

struct PrivatePipe::Impl {
    Admits admits;
    Accepted accepted;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE event = nullptr;
    OVERLAPPED overlapped{};
    std::unique_ptr<QWinEventNotifier> notifier;

    ~Impl()
    {
        notifier.reset();
        if (pipe != INVALID_HANDLE_VALUE) {
            CancelIoEx(pipe, &overlapped);
            CloseHandle(pipe);
        }
        if (event)
            CloseHandle(event);
    }

    void await()
    {
        overlapped = {};
        ResetEvent(event);
        overlapped.hEvent = event;
        if (ConnectNamedPipe(pipe, &overlapped))
            return arrived();
        switch (GetLastError()) {
        case ERROR_IO_PENDING:
            notifier->setEnabled(true);
            return;
        case ERROR_PIPE_CONNECTED:
            return arrived();
        case ERROR_NO_DATA:
            DisconnectNamedPipe(pipe);
            return await();
        default:
            return;
        }
    }

    void arrived()
    {
        notifier->setEnabled(false);
        ULONG pid = 0;
        if (!GetNamedPipeClientProcessId(pipe, &pid) || !admits || !admits(pid)) {
            DisconnectNamedPipe(pipe);
            return await();
        }
        auto *socket = new QLocalSocket;
        if (!socket->setSocketDescriptor(reinterpret_cast<qintptr>(pipe), QLocalSocket::ConnectedState,
                                         QIODevice::ReadWrite)) {
            delete socket;
            DisconnectNamedPipe(pipe);
            return await();
        }
        pipe = INVALID_HANDLE_VALUE;
        notifier.release()->deleteLater();
        accepted(socket);
    }
};

PrivatePipe::PrivatePipe(Admits admits, Accepted accepted) : m(std::make_unique<Impl>())
{
    m->admits = std::move(admits);
    m->accepted = std::move(accepted);
}

PrivatePipe::~PrivatePipe() = default;

bool PrivatePipe::listen(const QString &name, const QString &containerSid, QString *error)
{
    const auto fail = [error](QString why) {
        if (error)
            *error = std::move(why);
        return false;
    };
    const QString user = userSid();
    if (user.isEmpty() || containerSid.isEmpty())
        return fail(QStringLiteral("the agent has no AppContainer to grant the pipe to"));
    const std::wstring sddl =
        QStringLiteral("D:P(A;;GA;;;%1)(A;;GRGW;;;%2)S:(ML;;NW;;;LW)").arg(user, containerSid).toStdWString();
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
        return fail(lastError("ConvertStringSecurityDescriptorToSecurityDescriptor"));
    SECURITY_ATTRIBUTES attributes{sizeof attributes, descriptor, FALSE};
    m->pipe = CreateNamedPipeW(name.toStdWString().c_str(),
                               PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                               PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                               64 * 1024, 64 * 1024, 0, &attributes);
    const QString created = m->pipe == INVALID_HANDLE_VALUE ? lastError("CreateNamedPipe") : QString();
    LocalFree(descriptor);
    if (!created.isEmpty())
        return fail(created);
    m->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m->event)
        return fail(lastError("CreateEvent"));
    m->notifier = std::make_unique<QWinEventNotifier>(m->event);
    m->notifier->setEnabled(false);
    QObject::connect(m->notifier.get(), &QWinEventNotifier::activated, m->notifier.get(), [impl = m.get()] {
        DWORD transferred = 0;
        GetOverlappedResult(impl->pipe, &impl->overlapped, &transferred, FALSE);
        impl->arrived();
    });
    m->await();
    return true;
}

}
