//  Copyright (c) 2026, altqx

//  HikariSub is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

//  HikariSub is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
//  along with HikariSub.  If not, see <http://www.gnu.org/licenses/>.

#ifdef _WIN32

#include "UpdateApply.h"
#include "Sha256.h"
#include "UpdateVerify.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <wincrypt.h>
#include <winhttp.h>

#include <cwchar>
#include <cwctype>
#include <memory>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")

namespace
{
	// The largest installer taken: the release's own limit for a package.
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

	// An absolute path on a drive: "X:\..." with no "..", no wildcard and
	// no device or stream syntax, and no name that ends in a dot or a space,
	// which Windows drops ("a\.. \b" is "b").
	bool IsPlainAbsolutePath(const std::wstring &path)
	{
		if (path.size() < 4 || path.size() > 32000 || !iswalpha(path[0]) || path[1] != L':' || path[2] != L'\\')
			return false;
		for (size_t i = 4; i <= path.size(); i++) {
			if ((i == path.size() || path[i] == L'\\') && (path[i - 1] == L'.' || path[i - 1] == L' '))
				return false;
		}
		for (size_t i = 2; i < path.size(); i++) {
			wchar_t c = path[i];
			if (c < 32 || c == L'"' || c == L'*' || c == L'?' || c == L'<' || c == L'>' || c == L'|' || c == L':')
				return false;
		}
		return true;
	}

	// One argument, quoted as CommandLineToArgvW reads it back.
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

	// A new folder under parent that only SYSTEM, administrators and its
	// owner (an administrator, when elevated) may open or change, and
	// that inherits nothing. CreateDirectoryW fails on a name that exists,
	// so the folder is always this process's own.
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

	// Copies source into the new file target, at most kMaxInstallerBytes.
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

	// The SHA-256 of what the handle reads, from its start.
	std::optional<std::string> HashOf(HANDLE file)
	{
		LARGE_INTEGER zero = {};
		if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN))
			return std::nullopt;
		Sha256 hash;
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
			hash.Update(buffer.data(), read);
		}
		return hash.HexDigest();
	}

	// A file opened to read, that no one may write, delete or rename
	// while the handle is open; the loader may still map it to run it.
	HANDLE OpenLocked(const std::wstring &path)
	{
		return CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, nullptr);
	}
}

std::optional<update_apply::Request> update_apply::Parse(const std::vector<std::wstring> &arguments)
{
	if (arguments.size() != 4)
		return std::nullopt;
	Request request;
	request.version = Narrow(arguments[0]);
	if (!update_verify::IsReleaseVersion(request.version))
		return std::nullopt;
	request.file = arguments[1];
	if (!IsPlainAbsolutePath(request.file))
		return std::nullopt;
	const std::wstring &pid = arguments[2];
	if (pid.empty() || pid.size() > 10)
		return std::nullopt;
	unsigned long long value = 0;
	for (wchar_t c : pid) {
		if (c < L'0' || c > L'9')
			return std::nullopt;
		value = value * 10 + unsigned(c - L'0');
	}
	if (value == 0 || value > 0xffffffffull)
		return std::nullopt;
	request.pid = static_cast<unsigned long>(value);
	if (arguments[3] != L"0" && arguments[3] != L"1")
		return std::nullopt;
	request.relaunch = arguments[3] == L"1";
	return request;
}

std::wstring update_apply::CommandLine(const Request &request)
{
	return std::wstring(kSwitch) + L" " + Quote(Widen(request.version)) + L" " + Quote(request.file) + L" " +
		std::to_wstring(request.pid) + L" " + (request.relaunch ? L"1" : L"0");
}

int update_apply::Apply(const Request &request, const Environment &environment)
{
	using namespace update_verify;
	if (CheckVersion(request.version, environment.installed) != Verdict::Ok)
		return kBadArguments;
	if (!IsPlainAbsolutePath(request.file) || environment.installDir.empty() || !environment.fetchSums ||
		!environment.run)
	{
		return kBadArguments;
	}
	if (environment.allUsers && environment.stagingParent.empty())
		return kBadArguments;

	std::optional<std::string> sums = environment.fetchSums(request.version);
	if (!sums || sums->size() > kMaxSumsBytes)
		return kNoSums;
	std::string asset = NsisAssetName(request.version);
	if (!FindSum(*sums, asset))
		return kMismatch;

	// The file to run: a private copy for all users, the download itself
	// for the user's own install.
	std::wstring installer = request.file;
	std::wstring folder;
	auto discard = [&] {
		if (folder.empty())
			return;
		DeleteFileW(installer.c_str());
		RemoveDirectoryW(folder.c_str());
	};
	if (environment.allUsers) {
		Handle source(CreateFileW(request.file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, nullptr));
		if (!source.Ok())
			return kBadArguments;
		folder = MakePrivateFolder(environment.stagingParent);
		if (folder.empty())
			return kFailed;
		installer = folder + L"\\" + Widen(asset);
		if (!CopyInto(source.value, installer)) {
			discard();
			return kFailed;
		}
	}

	Handle locked(OpenLocked(installer));
	if (!locked.Ok()) {
		discard();
		return environment.allUsers ? kFailed : kBadArguments;
	}
	std::optional<std::string> actual = HashOf(locked.value);
	if (!actual || CheckDigest(*sums, asset, *actual) != Verdict::Ok) {
		locked.Close();
		discard();
		return kMismatch;
	}

	std::wstring arguments = Widen(NsisArguments(request.pid, request.relaunch, environment.allUsers,
		Narrow(environment.installDir)));
	bool started = environment.run(installer, arguments);
	locked.Close();
	if (!started) {
		discard();
		return kFailed;
	}
	// The running installer holds its file; the copy and its folder go at
	// the next restart (only an administrator may ask for that).
	if (!folder.empty()) {
		MoveFileExW(installer.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
		MoveFileExW(folder.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
	}
	return kOk;
}

namespace
{
	// Whether the server's certificate chains to a root that this
	// computer trusts (the local machine's stores only) and names host.
	// WinHTTP's own check also takes the current user's root store, which
	// an unelevated process of the same user can add a root to; with a
	// proxy of its choosing, it could then answer for GitHub here.
	bool TrustedByMachine(HINTERNET request, const wchar_t *host)
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

std::optional<std::string> update_apply::FetchHttps(const std::string &url,
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

	// Redirects are followed here, not by WinHTTP, so that every URL on
	// the way passes allowed and every server's certificate is checked.
	std::string next = url;
	for (int hop = 0; hop < 6; hop++) {
		std::wstring wide = Widen(next);
		// The parts point into wide; the path and the query are sent.
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
		// Plain http only for a test build's feed on 127.0.0.1, which
		// allowed decides.
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
		if (secure && !TrustedByMachine(request.value, hostName.c_str()))
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
			// GitHub's are absolute; anything else is not followed.
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

bool update_apply::Launch(const std::wstring &installer, const std::wstring &arguments)
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
	// The app this hands off for exits next: a start of it after that
	// must find the setup's mutex. The setup takes it first thing.
	for (int waited = 0; waited < 100 && !SetupRunning(); ++waited) {
		if (WaitForSingleObject(process.hProcess, 100) != WAIT_TIMEOUT)
			break;
	}
	CloseHandle(process.hProcess);
	return true;
}

bool update_apply::SetupRunning()
{
	HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, kSetupMutex);
	if (mutex) {
		CloseHandle(mutex);
		return true;
	}
	return GetLastError() == ERROR_ACCESS_DENIED;
}

bool update_apply::AskStartAfterSetup()
{
	HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, kStartAfterSetup);
	if (!event)
		return false;
	bool set = SetEvent(event) != FALSE;
	CloseHandle(event);
	return set;
}

std::wstring update_apply::SystemTempFolder()
{
	wchar_t windows[MAX_PATH];
	UINT length = GetSystemWindowsDirectoryW(windows, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return std::wstring();
	return std::wstring(windows, length) + L"\\Temp";
}

namespace
{
	// What lets the user change a file or a folder (FILE_WRITE_DATA and
	// FILE_APPEND_DATA are FILE_ADD_FILE and FILE_ADD_SUBDIRECTORY).
	constexpr unsigned long kChanges = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_DELETE_CHILD | DELETE |
		WRITE_DAC | WRITE_OWNER;
	// What lets the user replace a folder on the way: delete or rename it,
	// or a child of it, or give itself that right.
	constexpr unsigned long kReplaces = FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER;

	bool TreeIsProtected(const std::wstring &folder, const update_apply::RightsOf &rightsOf,
		const update_apply::ListOf &list, int depth)
	{
		std::optional<unsigned long> rights = rightsOf(folder);
		if (!rights || (*rights & kChanges) || depth > 32)
			return false;
		std::optional<std::vector<update_apply::Entry>> entries = list(folder);
		if (!entries)
			return false;
		for (const update_apply::Entry &entry : *entries) {
			if (entry.reparse)
				return false;
			std::wstring path = folder + L"\\" + entry.name;
			if (entry.folder) {
				if (!TreeIsProtected(path, rightsOf, list, depth + 1))
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

bool update_apply::IsProtected(const std::wstring &installDir, const RightsOf &rightsOf, const ListOf &list)
{
	std::wstring dir = installDir;
	while (dir.size() > 3 && dir.back() == L'\\')
		dir.pop_back();
	// A plain local path below a drive's root: no UNC, device or relative
	// path, and no "." or ".." to walk.
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
	// The folders on the way, the drive's root first.
	for (size_t at = 2; at != std::wstring::npos && at < dir.size(); at = dir.find(L'\\', at + 1)) {
		std::wstring ancestor = dir.substr(0, at + (at == 2 ? 1 : 0));
		std::optional<unsigned long> rights = rightsOf(ancestor);
		if (!rights || (*rights & kReplaces))
			return false;
	}
	return TreeIsProtected(dir, rightsOf, list, 0);
}

bool update_apply::IsProtectedFromUser(const std::wstring &installDir)
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
	return IsProtected(installDir, rightsOf, list);
}

bool update_apply::IsElevated()
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

#endif
