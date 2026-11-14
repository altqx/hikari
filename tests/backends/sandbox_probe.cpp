#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

void report(const std::string &action, bool ok, long error)
{
    if (ok)
        std::printf("%s: ok\n", action.c_str());
    else
        std::printf("%s: denied %ld\n", action.c_str(), error);
    std::fflush(stdout);
}

#ifdef _WIN32
void openFile(const std::string &action, const char *path, bool write)
{
    HANDLE h = CreateFileA(path, write ? GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           write ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    report(action, h != INVALID_HANDLE_VALUE, static_cast<long>(GetLastError()));
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
}

void tcp(const std::string &action, const char *host, const char *port)
{
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return report(action, false, WSAGetLastError());
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(std::atoi(port)));
    inet_pton(AF_INET, host, &address.sin_addr);
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    connect(s, reinterpret_cast<sockaddr *>(&address), sizeof address);
    fd_set writable, failed;
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET(s, &writable);
    FD_SET(s, &failed);
    timeval wait{3, 0};
    const int ready = select(0, nullptr, &writable, &failed, &wait);
    report(action, ready > 0 && FD_ISSET(s, &writable), ready == 0 ? WSAETIMEDOUT : WSAGetLastError());
    closesocket(s);
}

void lowIntegrity(const std::string &action)
{
    HANDLE token = nullptr;
    BYTE buffer[TOKEN_INTEGRITY_LEVEL_MAX_SIZE]{};
    DWORD size = 0;
    const bool read = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) &&
                      GetTokenInformation(token, TokenIntegrityLevel, buffer, sizeof buffer, &size);
    if (token)
        CloseHandle(token);
    const PSID sid = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(buffer)->Label.Sid;
    const DWORD level = read ? *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1) : 0;
    report(action, read && level == SECURITY_MANDATORY_LOW_RID, static_cast<long>(level));
}

void list(const std::string &action, const char *folder)
{
    WIN32_FIND_DATAA found;
    const HANDLE h = FindFirstFileA((std::string(folder) + "\\*").c_str(), &found);
    report(action, h != INVALID_HANDLE_VALUE, static_cast<long>(GetLastError()));
    if (h != INVALID_HANDLE_VALUE)
        FindClose(h);
}

void ownDesktop(const std::string &action)
{
    wchar_t name[256]{};
    DWORD size = 0;
    GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, name, sizeof name, &size);
    report(action, std::wstring(name).starts_with(L"HikariSub.Agents."), static_cast<long>(GetLastError()));
}

int spawn(int first, int argc, char **argv)
{
    std::string command = std::string("\"") + argv[0] + "\"";
    for (int i = first; i < argc; ++i)
        command += std::string(" \"") + argv[i] + "\"";
    STARTUPINFOA si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        report("spawn", false, static_cast<long>(GetLastError()));
        return 0;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
#else
void openFile(const std::string &action, const char *path, bool write)
{
    const int fd = ::open(path, write ? O_WRONLY | O_CREAT | O_TRUNC : O_RDONLY, 0600);
    report(action, fd >= 0, errno);
    if (fd >= 0)
        ::close(fd);
}

void tcp(const std::string &action, const char *host, const char *port)
{
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return report(action, false, errno);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(std::atoi(port)));
    ::inet_pton(AF_INET, host, &address.sin_addr);
    report(action, ::connect(s, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0, errno);
    ::close(s);
}

void unixSocket(const std::string &action, const char *path)
{
    const int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0)
        return report(action, false, errno);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path, sizeof address.sun_path - 1);
    report(action, ::connect(s, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0, errno);
    ::close(s);
}

void uring(const std::string &action)
{
    unsigned char params[120]{};
    const long fd = ::syscall(SYS_io_uring_setup, 1, params);
    report(action, fd >= 0, errno);
    if (fd >= 0)
        ::close(static_cast<int>(fd));
}

void userNamespace(const std::string &action)
{
    report(action, ::unshare(CLONE_NEWUSER) == 0, errno);
}

int spawn(int first, int argc, char **argv)
{
    std::vector<char *> args{argv[0]};
    for (int i = first; i < argc; ++i)
        args.push_back(argv[i]);
    args.push_back(nullptr);
    const pid_t child = ::fork();
    if (child == 0) {
        ::execv(argv[0], args.data());
        std::printf("spawn: denied %d\n", errno);
        std::fflush(stdout);
        ::_exit(127);
    }
    int status = 0;
    ::waitpid(child, &status, 0);
    return 0;
}
#endif

}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        const std::string action = argv[i];
        if (action == "spawn")
            return spawn(i + 1, argc, argv);
        if ((action == "read" || action == "write") && i + 1 < argc) {
            openFile(action + " " + argv[i + 1], argv[i + 1], action == "write");
            ++i;
        } else if (action == "sleep" && i + 1 < argc) {
#ifdef _WIN32
            Sleep(static_cast<DWORD>(std::atoi(argv[++i])) * 1000);
#else
            ::sleep(static_cast<unsigned>(std::atoi(argv[++i])));
#endif
        } else if (action == "tcp" && i + 2 < argc) {
            tcp(action, argv[i + 1], argv[i + 2]);
            i += 2;
#ifdef _WIN32
        } else if (action == "low") {
            lowIntegrity(action);
        } else if (action == "desktop") {
            ownDesktop(action);
        } else if (action == "list" && i + 1 < argc) {
            list(action + " " + argv[i + 1], argv[i + 1]);
            ++i;
#else
        } else if (action == "unix" && i + 1 < argc) {
            unixSocket(action, argv[i + 1]);
            ++i;
        } else if (action == "uring") {
            uring(action);
        } else if (action == "userns") {
            userNamespace(action);
#endif
        } else {
            std::printf("%s: unknown\n", action.c_str());
            return 2;
        }
    }
    return 0;
}
