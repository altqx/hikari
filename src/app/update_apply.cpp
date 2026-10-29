#ifdef _WIN32

#include "hikari/app/update_apply.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <winhttp.h>

#include <QByteArrayView>
#include <QCryptographicHash>

#include <cwchar>
#include <cwctype>
#include <iterator>
#include <vector>

namespace hikari::app::update_apply {

namespace {
    constexpr long long kMaxInstallerBytes = 1LL << 30;

    struct Handle
    {
        HANDLE value = INVALID_HANDLE_VALUE;
        Handle() = default;
        explicit Handle(HANDLE handle) : value(handle) {}
        Handle(const Handle &) = delete;
        Handle &operator=(const Handle &) = delete;
        ~Handle() { Close(); }
        bool Ok() const { return value != INVALID_HANDLE_VALUE && value != nullptr; }
        void Close()
        {
            if (Ok())
                CloseHandle(value);
            value = INVALID_HANDLE_VALUE;
        }
    };

    struct InternetHandle
    {
        HINTERNET value = nullptr;
        explicit InternetHandle(HINTERNET handle) : value(handle) {}
        InternetHandle(const InternetHandle &) = delete;
        InternetHandle &operator=(const InternetHandle &) = delete;
        ~InternetHandle()
        {
            if (value)
                WinHttpCloseHandle(value);
        }
    };

    std::wstring Widen(const std::string &text)
    {
        if (text.empty())
            return std::wstring();
        int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
        if (size <= 0)
            return std::wstring();
        std::wstring wide(size_t(size), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), wide.data(), size);
        return wide;
    }

    std::string Narrow(const std::wstring &text)
    {
        if (text.empty())
            return std::string();
        int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0,
            nullptr, nullptr);
        if (size <= 0)
            return std::string();
        std::string narrow(size_t(size), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), narrow.data(), size,
            nullptr, nullptr);
        return narrow;
    }

    // Quoted as CommandLineToArgvW reads it back.
    std::wstring Quote(const std::wstring &argument)
    {
        std::wstring quoted = L"\"";
        size_t backslashes = 0;
        for (wchar_t c : argument) {
            if (c == L'\\') {
                backslashes++;
                continue;
            }
            if (c == L'"') {
                quoted.append(backslashes * 2 + 1, L'\\');
                quoted.push_back(L'"');
            }
            else {
                quoted.append(backslashes, L'\\');
                quoted.push_back(c);
            }
            backslashes = 0;
        }
        quoted.append(backslashes * 2, L'\\');
        quoted.push_back(L'"');
        return quoted;
    }

    std::wstring RandomHex()
    {
        unsigned char bytes[16];
        if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
            return std::wstring();
        static const wchar_t digits[] = L"0123456789abcdef";
        std::wstring hex;
        for (unsigned char b : bytes) {
            hex.push_back(digits[b >> 4]);
            hex.push_back(digits[b & 15]);
        }
        return hex;
    }

    // CreateDirectoryW fails on a name that exists, so the folder is always this process's own.
    std::wstring MakePrivateFolder(const std::wstring &parent)
    {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;OW)", SDDL_REVISION_1, &descriptor, nullptr))
        {
            return std::wstring();
        }
        SECURITY_ATTRIBUTES attributes = { sizeof(attributes), descriptor, FALSE };
        std::wstring folder;
        for (int attempt = 0; attempt < 8 && folder.empty(); attempt++) {
            std::wstring random = RandomHex();
            if (random.empty())
                break;
            std::wstring candidate = parent + L"\\HikariSub-update-" + random;
            if (CreateDirectoryW(candidate.c_str(), &attributes))
                folder = candidate;
            else if (GetLastError() != ERROR_ALREADY_EXISTS)
                break;
        }
        LocalFree(descriptor);
        return folder;
    }

    bool CopyInto(HANDLE source, const std::wstring &target)
    {
        Handle out(CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!out.Ok())
            return false;
        std::vector<char> buffer(1 << 20);
        long long total = 0;
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(source, buffer.data(), DWORD(buffer.size()), &read, nullptr))
                return false;
            if (read == 0)
                break;
            total += read;
            if (total > kMaxInstallerBytes)
                return false;
            DWORD written = 0;
            if (!WriteFile(out.value, buffer.data(), read, &written, nullptr) || written != read)
                return false;
        }
        return FlushFileBuffers(out.value) != 0;
    }

    std::optional<std::string> HashOf(HANDLE file)
    {
        LARGE_INTEGER zero = {};
        if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN))
            return std::nullopt;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        std::vector<char> buffer(1 << 20);
        long long total = 0;
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(file, buffer.data(), DWORD(buffer.size()), &read, nullptr))
                return std::nullopt;
            if (read == 0)
                break;
            total += read;
            if (total > kMaxInstallerBytes)
                return std::nullopt;
            hash.addData(QByteArrayView(buffer.data(), qsizetype(read)));
        }
        return hash.result().toHex().toStdString();
    }

    // FILE_SHARE_READ alone still lets the loader map the file to run it.
    HANDLE OpenLocked(const std::wstring &path)
    {
        return CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
    }

bool isReleaseHostUrl(const std::string &url)
{
    constexpr std::string_view scheme = "https://";
    if (url.compare(0, scheme.size(), scheme) != 0)
        return false;
    const std::string rest = url.substr(scheme.size());
    const std::string host = rest.substr(0, rest.find('/'));
    if (host.empty() || host.find_first_of(":@") != std::string::npos || host.size() == rest.size())
        return false;
    return application::isAllowedDownloadHost(host);
}

} // namespace

std::wstring commandLine(const application::ApplyRequest &request)
{
    return Widen(std::string(application::kApplyUpdateSwitch)) + L" " + Quote(Widen(request.version)) + L" " +
           Quote(Widen(request.file)) + L" " + std::to_wstring(request.pid) + L" " + (request.relaunch ? L"1" : L"0");
}

int apply(const application::ApplyRequest &request, const Environment &environment)
{
    if (application::checkInstallVersion(request.version, environment.installed) != application::InstallCheck::Ok)
        return application::kApplyBadArguments;
    const std::wstring file = Widen(request.file);
    if (!application::isPlainWindowsPath(request.file) || environment.installDir.empty() || !environment.fetchSums ||
        !environment.run)
    {
        return application::kApplyBadArguments;
    }
    if (environment.allUsers && environment.stagingParent.empty())
        return application::kApplyBadArguments;

    std::optional<std::string> sums = environment.fetchSums(request.version);
    if (!sums || sums->size() > application::kMaxSumsBytes)
        return application::kApplyNoSums;
    const std::string asset = application::assetNameFor("nsis", request.version);
    if (!application::listedSha256(*sums, asset))
        return application::kApplyMismatch;

    std::wstring installer = file;
    std::wstring folder;
    auto discard = [&] {
        if (folder.empty())
            return;
        DeleteFileW(installer.c_str());
        RemoveDirectoryW(folder.c_str());
    };
    if (environment.allUsers) {
        Handle source(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!source.Ok())
            return application::kApplyBadArguments;
        folder = MakePrivateFolder(environment.stagingParent);
        if (folder.empty())
            return application::kApplyFailed;
        installer = folder + L"\\" + Widen(asset);
        if (!CopyInto(source.value, installer)) {
            discard();
            return application::kApplyFailed;
        }
    }

    Handle locked(OpenLocked(installer));
    if (!locked.Ok()) {
        discard();
        return environment.allUsers ? application::kApplyFailed : application::kApplyBadArguments;
    }
    std::optional<std::string> actual = HashOf(locked.value);
    if (!actual || application::checkListedSha256(*sums, asset, *actual) != application::InstallCheck::Ok) {
        locked.Close();
        discard();
        return application::kApplyMismatch;
    }

    std::wstring arguments = Widen(application::nsisUpdateArguments(environment.allUsers, request.relaunch,
        (long long)request.pid, Narrow(environment.installDir)));
    bool started = environment.run(installer, arguments);
    locked.Close();
    if (!started) {
        discard();
        return application::kApplyFailed;
    }
    // The running installer holds its file; MOVEFILE_DELAY_UNTIL_REBOOT needs an administrator.
    if (!folder.empty()) {
        MoveFileExW(installer.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        MoveFileExW(folder.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }
    return application::kApplyOk;
}

namespace {
    // WinHTTP's own check also trusts the current user's root store, which the user can add to.
    bool trustedByMachine(HINTERNET request, const wchar_t *host)
    {
        PCCERT_CONTEXT certificate = nullptr;
        DWORD size = sizeof(certificate);
        if (!WinHttpQueryOption(request, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &certificate, &size) || !certificate)
            return false;
        LPSTR serverAuth[] = { const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH) };
        CERT_CHAIN_PARA chainPara = {};
        chainPara.cbSize = sizeof(chainPara);
        chainPara.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
        chainPara.RequestedUsage.Usage.cUsageIdentifier = 1;
        chainPara.RequestedUsage.Usage.rgpszUsageIdentifier = serverAuth;
        PCCERT_CHAIN_CONTEXT chain = nullptr;
        bool trusted = false;
        if (CertGetCertificateChain(HCCE_LOCAL_MACHINE, certificate, nullptr, certificate->hCertStore, &chainPara, 0,
                nullptr, &chain))
        {
            SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl = {};
            ssl.cbSize = sizeof(ssl);
            ssl.dwAuthType = AUTHTYPE_SERVER;
            ssl.pwszServerName = const_cast<wchar_t *>(host);
            CERT_CHAIN_POLICY_PARA policy = {};
            policy.cbSize = sizeof(policy);
            policy.pvExtraPolicyPara = &ssl;
            CERT_CHAIN_POLICY_STATUS status = {};
            status.cbSize = sizeof(status);
            trusted = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status) &&
                status.dwError == 0;
            CertFreeCertificateChain(chain);
        }
        CertFreeCertificateContext(certificate);
        return trusted;
    }
}

std::optional<std::string> fetchHttps(const std::string &url,
    const std::function<bool(const std::string &finalUrl)> &allowed, size_t limit)
{
    InternetHandle session(WinHttpOpen(L"HikariSub", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value)
        return std::nullopt;
    WinHttpSetTimeouts(session.value, 30000, 30000, 30000, 30000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    if (!WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }

    // Redirects WinHTTP follows itself would skip `allowed` and the certificate check.
    std::string next = url;
    for (int hop = 0; hop < 6; hop++) {
        std::wstring wide = Widen(next);
        // WinHttpCrackUrl's parts point into wide.
        URL_COMPONENTS parts = {};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = DWORD(-1);
        parts.dwUrlPathLength = DWORD(-1);
        parts.dwExtraInfoLength = DWORD(-1);
        if (wide.empty() || !WinHttpCrackUrl(wide.c_str(), DWORD(wide.size()), 0, &parts) || !parts.lpszHostName ||
            parts.dwHostNameLength == 0)
        {
            return std::nullopt;
        }
        bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
        // MUST KILL: plain http passes whenever `allowed` accepts it (test feeds on 127.0.0.1)
        if (!allowed(next) || (!secure && parts.nScheme != INTERNET_SCHEME_HTTP))
            return std::nullopt;
        std::wstring target = parts.lpszUrlPath && parts.dwUrlPathLength ?
            std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) : std::wstring(L"/");
        if (parts.lpszExtraInfo && parts.dwExtraInfoLength)
            target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        std::wstring hostName(parts.lpszHostName, parts.dwHostNameLength);

        InternetHandle connection(WinHttpConnect(session.value, hostName.c_str(), parts.nPort, 0));
        if (!connection.value)
            return std::nullopt;
        InternetHandle request(WinHttpOpenRequest(connection.value, L"GET", target.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
        if (!request.value)
            return std::nullopt;
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)))
            return std::nullopt;
        if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.value, nullptr))
        {
            return std::nullopt;
        }
        if (secure && !trustedByMachine(request.value, hostName.c_str()))
            return std::nullopt;
        DWORD status = 0, size = sizeof(status);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
        {
            return std::nullopt;
        }
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            DWORD bytes = 0;
            WinHttpQueryHeaders(request.value, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                WINHTTP_NO_OUTPUT_BUFFER, &bytes, WINHTTP_NO_HEADER_INDEX);
            if (bytes == 0 || bytes > 16384)
                return std::nullopt;
            std::wstring location(bytes / sizeof(wchar_t) + 1, L'\0');
            if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                    location.data(), &bytes, WINHTTP_NO_HEADER_INDEX))
            {
                return std::nullopt;
            }
            location.resize(wcsnlen(location.c_str(), location.size()));
            // GitHub's Location is absolute; a relative one is not followed.
            next = Narrow(location);
            if (next.empty())
                return std::nullopt;
            continue;
        }
        if (status != 200)
            return std::nullopt;

        std::string body;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.value, &available))
                return std::nullopt;
            if (available == 0)
                break;
            if (body.size() + available > limit)
                return std::nullopt;
            size_t at = body.size();
            body.resize(at + available);
            DWORD read = 0;
            if (!WinHttpReadData(request.value, body.data() + at, available, &read))
                return std::nullopt;
            body.resize(at + read);
        }
        return body;
    }
    return std::nullopt;
}

bool launch(const std::wstring &installer, const std::wstring &arguments)
{
    std::wstring commandLine = Quote(installer) + L" " + arguments;
    std::wstring folder = installer.substr(0, installer.find_last_of(L'\\'));
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(installer.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
            folder.c_str(), &startup, &process))
    {
        return false;
    }
    CloseHandle(process.hThread);
    for (int waited = 0; waited < 100 && !setupRunning(); ++waited) {
        if (WaitForSingleObject(process.hProcess, 100) != WAIT_TIMEOUT)
            break;
    }
    CloseHandle(process.hProcess);
    return true;
}

bool setupRunning()
{
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, kSetupMutex);
    if (mutex) {
        CloseHandle(mutex);
        return true;
    }
    return GetLastError() == ERROR_ACCESS_DENIED;
}

bool askStartAfterSetup()
{
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, kStartAfterSetup);
    if (!event)
        return false;
    const bool set = SetEvent(event) != FALSE;
    CloseHandle(event);
    return set;
}

std::wstring systemTempFolder()
{
    wchar_t windows[MAX_PATH];
    UINT length = GetSystemWindowsDirectoryW(windows, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return std::wstring();
    return std::wstring(windows, length) + L"\\Temp";
}

namespace {
    // On folders FILE_WRITE_DATA and FILE_APPEND_DATA are FILE_ADD_FILE and FILE_ADD_SUBDIRECTORY.
    constexpr unsigned long kChanges = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_DELETE_CHILD | DELETE |
        WRITE_DAC | WRITE_OWNER;
    constexpr unsigned long kReplaces = FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER;

    bool treeIsProtected(const std::wstring &folder, const RightsOf &rightsOf,
        const ListOf &list, int depth)
    {
        std::optional<unsigned long> rights = rightsOf(folder);
        if (!rights || (*rights & kChanges) || depth > 32)
            return false;
        std::optional<std::vector<Entry>> entries = list(folder);
        if (!entries)
            return false;
        for (const Entry &entry : *entries) {
            if (entry.reparse)
                return false;
            std::wstring path = folder + L"\\" + entry.name;
            if (entry.folder) {
                if (!treeIsProtected(path, rightsOf, list, depth + 1))
                    return false;
                continue;
            }
            std::optional<unsigned long> fileRights = rightsOf(path);
            if (!fileRights || (*fileRights & kChanges))
                return false;
        }
        return true;
    }
}

bool isProtected(const std::wstring &installDir, const RightsOf &rightsOf, const ListOf &list)
{
    std::wstring dir = installDir;
    while (dir.size() > 3 && dir.back() == L'\\')
        dir.pop_back();
    if (dir.size() < 4 || !iswalpha(dir[0]) || dir[1] != L':' || dir[2] != L'\\' ||
        dir.find(L'/') != std::wstring::npos || dir.find(L"\\\\") != std::wstring::npos)
        return false;
    for (size_t start = 3; start < dir.size();) {
        size_t end = dir.find(L'\\', start);
        std::wstring part = dir.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (part == L"." || part == L"..")
            return false;
        if (end == std::wstring::npos)
            break;
        start = end + 1;
    }
    for (size_t at = 2; at != std::wstring::npos && at < dir.size(); at = dir.find(L'\\', at + 1)) {
        std::wstring ancestor = dir.substr(0, at + (at == 2 ? 1 : 0));
        std::optional<unsigned long> rights = rightsOf(ancestor);
        if (!rights || (*rights & kReplaces))
            return false;
    }
    return treeIsProtected(dir, rightsOf, list, 0);
}

bool isProtectedFromUser(const std::wstring &installDir)
{
    HANDLE process = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &process))
        return false;
    Handle processToken(process);
    HANDLE duplicate = nullptr;
    if (!DuplicateToken(process, SecurityIdentification, &duplicate))
        return false;
    Handle token(duplicate);
    RightsOf rightsOf = [&token](const std::wstring &path) -> std::optional<unsigned long> {
        const SECURITY_INFORMATION wanted = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
            DACL_SECURITY_INFORMATION;
        DWORD needed = 0;
        GetFileSecurityW(path.c_str(), wanted, nullptr, 0, &needed);
        if (needed == 0)
            return std::nullopt;
        std::vector<unsigned char> descriptor(needed);
        if (!GetFileSecurityW(path.c_str(), wanted, descriptor.data(), needed, &needed))
            return std::nullopt;
        GENERIC_MAPPING mapping = { FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS };
        std::vector<unsigned char> privileges(1024);
        DWORD privilegesSize = DWORD(privileges.size());
        DWORD granted = 0;
        BOOL allowed = FALSE;
        if (!AccessCheck(descriptor.data(), token.value, MAXIMUM_ALLOWED, &mapping,
            reinterpret_cast<PPRIVILEGE_SET>(privileges.data()), &privilegesSize, &granted, &allowed))
            return std::nullopt;
        return allowed ? granted : 0;
    };
    ListOf list = [](const std::wstring &folder) -> std::optional<std::vector<Entry>> {
        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileExW((folder + L"\\*").c_str(), FindExInfoBasic, &data, FindExSearchNameMatch,
            nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE)
            return std::nullopt;
        std::vector<Entry> entries;
        do {
            std::wstring name = data.cFileName;
            if (name == L"." || name == L"..")
                continue;
            entries.push_back({ name, (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
                (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 });
        } while (FindNextFileW(find, &data));
        bool complete = GetLastError() == ERROR_NO_MORE_FILES;
        FindClose(find);
        if (!complete)
            return std::nullopt;
        return entries;
    };
    return isProtected(installDir, rightsOf, list);
}

bool isElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation = {};
    DWORD size = sizeof(elevation);
    bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
        elevation.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}


int runFromCommandLine(const std::string &installedVersion, bool allUsersInstall, const std::string &testFeed)
{
    int count = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!argv)
        return application::kApplyBadArguments;
    const bool isSwitch = count >= 2 && Narrow(argv[1]) == application::kApplyUpdateSwitch;
    std::vector<std::string> arguments;
    for (int i = 2; i < count; ++i)
        arguments.push_back(Narrow(argv[i]));
    LocalFree(argv);
    const auto request = isSwitch ? application::parseApplyArguments(arguments) : std::nullopt;
    if (!request)
        return application::kApplyBadArguments;

    Environment environment;
    environment.installed = installedVersion;
    std::vector<wchar_t> exe(32768);
    const DWORD length = GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size()));
    if (length == 0 || length >= exe.size())
        return application::kApplyFailed;
    const std::wstring path(exe.data(), length);
    environment.installDir = path.substr(0, path.find_last_of(L'\\'));
    environment.allUsers = allUsersInstall;
    if (allUsersInstall) {
        if (!isElevated())
            return application::kApplyBadArguments;
        environment.stagingParent = systemTempFolder();
    }
    std::string feed = testFeed;
    if (!feed.empty() && feed.back() != '/')
        feed += '/';
    environment.fetchSums = [feed](const std::string &version) -> std::optional<std::string> {
        if (!application::isReleaseVersion(version))
            return std::nullopt;
        const std::string url = feed.empty() ? application::sumsUrl(version)
                                             : feed + "download/v" + version + "/" + std::string(application::kSumsName);
        return fetchHttps(
            url,
            [feed](const std::string &reply) {
                return feed.empty() ? isReleaseHostUrl(reply) : reply.compare(0, feed.size(), feed) == 0;
            },
            application::kMaxSumsBytes);
    };
    environment.run = launch;
    return apply(*request, environment);
}

} // namespace hikari::app::update_apply

#endif
