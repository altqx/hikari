#include "hikari/backends/sandbox.h"

#include "integrity_label_windows.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>

namespace hikari::backends::sandbox {

namespace {

struct Grant {
    std::wstring path;
    DWORD access = 0;
    bool folder = false;
    /// Granted to ALL APPLICATION PACKAGES, as Program Files is: an entry naming one container shuts every Low
    /// process out of the folder, HikariSub's own bridge included.
    bool anyPackage = false;
};

// Two processes creating the same new profile at once: the second can fail with ERROR_PATH_NOT_FOUND.
HRESULT appContainerSid(const std::wstring &name, PSID *sid)
{
    const HANDLE creating = CreateMutexW(nullptr, FALSE, (L"Local\\" + name + L".profile").c_str());
    const DWORD waited = creating ? WaitForSingleObject(creating, 30'000) : WAIT_FAILED;
    const bool locked = waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED;
    HRESULT hr = CreateAppContainerProfile(name.c_str(), name.c_str(), name.c_str(), nullptr, 0, sid);
    if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
        hr = DeriveAppContainerSidFromAppContainerName(name.c_str(), sid);
    if (locked)
        ReleaseMutex(creating);
    if (creating)
        CloseHandle(creating);
    return hr;
}

DWORD fileAccess(DWORD access)
{
    GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    MapGenericMask(&access, &mapping);
    return access;
}

/// Whether `acl` already gives an AppContainer `access` through its own SID, or through ALL APPLICATION PACKAGES when
/// `anyPackageCounts`.
bool covered(PACL acl, PSID sid, DWORD access, bool anyPackageCounts)
{
    if (!acl)
        return true;
    BYTE anyPackage[SECURITY_MAX_SID_SIZE]{};
    DWORD size = sizeof anyPackage;
    const bool haveAnyPackage =
        anyPackageCounts && CreateWellKnownSid(WinBuiltinAnyPackageSid, nullptr, anyPackage, &size);
    DWORD granted = 0, denied = 0;
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void *ace = nullptr;
        if (!GetAce(acl, i, &ace))
            continue;
        const auto *header = static_cast<ACE_HEADER *>(ace);
        const bool allows = header->AceType == ACCESS_ALLOWED_ACE_TYPE;
        if ((!allows && header->AceType != ACCESS_DENIED_ACE_TYPE) || (header->AceFlags & INHERIT_ONLY_ACE))
            continue;
        const auto *entry = static_cast<ACCESS_ALLOWED_ACE *>(ace);
        auto *aceSid = const_cast<DWORD *>(&entry->SidStart);
        if (!EqualSid(aceSid, sid) && !(haveAnyPackage && EqualSid(aceSid, anyPackage)))
            continue;
        const DWORD mask = fileAccess(entry->Mask);
        if (allows)
            granted |= mask & ~denied;
        else
            denied |= mask & ~granted;
    }
    return (granted & access) == access;
}

/// Adds an allow ACE for the container, or ALL APPLICATION PACKAGES, unless it already has `access`: rewriting a
/// folder's ACL walks its tree.
DWORD grant(PSID container, const Grant &g)
{
    BYTE anyPackage[SECURITY_MAX_SID_SIZE]{};
    DWORD anyPackageSize = sizeof anyPackage;
    if (g.anyPackage && !CreateWellKnownSid(WinBuiltinAnyPackageSid, nullptr, anyPackage, &anyPackageSize))
        return GetLastError();
    const PSID sid = g.anyPackage ? static_cast<PSID>(anyPackage) : container;
    PACL current = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (const DWORD read = GetNamedSecurityInfoW(g.path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                                                 nullptr, &current, nullptr, &descriptor);
        read != ERROR_SUCCESS)
        return read;
    const DWORD mapped = fileAccess(g.access);
    DWORD result = ERROR_SUCCESS;
    const bool writes = g.access & (GENERIC_ALL | GENERIC_WRITE);
    if (!covered(current, sid, mapped, !writes)) {
        EXPLICIT_ACCESSW entry{};
        entry.grfAccessPermissions = mapped;
        entry.grfAccessMode = GRANT_ACCESS;
        entry.grfInheritance = g.folder ? SUB_CONTAINERS_AND_OBJECTS_INHERIT : NO_INHERITANCE;
        entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        entry.Trustee.ptstrName = static_cast<LPWSTR>(sid);
        PACL updated = nullptr;
        result = SetEntriesInAclW(1, &entry, current, &updated);
        if (result == ERROR_SUCCESS)
            result = SetNamedSecurityInfoW(const_cast<LPWSTR>(g.path.c_str()), SE_FILE_OBJECT,
                                           DACL_SECURITY_INFORMATION, nullptr, nullptr, updated, nullptr);
        LocalFree(updated);
    }
    LocalFree(descriptor);
    return result;
}

QString refusal(const Grant &g, DWORD error)
{
    const QString path = QString::fromStdWString(g.path);
    return error == ERROR_ACCESS_DENIED ? QStringLiteral("%1 (this user can't change its permissions)").arg(path)
                                        : QStringLiteral("%1 (error %2)").arg(path).arg(error);
}

void collect(std::vector<Grant> &grants, const QStringList &paths, DWORD access, bool anyPackage = false)
{
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (info.exists())
            grants.push_back(
                {QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString(), access, info.isDir(), anyPackage});
    }
}

QString labelRefusal(const QString &path, DWORD error)
{
    return error == ERROR_ACCESS_DENIED ? QStringLiteral("%1 (this user can't label it)").arg(path)
                                        : QStringLiteral("%1 (error %2)").arg(path).arg(error);
}

std::wstring quoted(const QString &path)
{
    return L"\"" + QDir::toNativeSeparators(path).toStdWString() + L"\"";
}

HDESK privateDesktop(std::wstring *name)
{
    static std::atomic<unsigned> made = 0;
    *name = L"HikariSub.Agents." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++made);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;OW)S:(ML;;NW;;;LW)", SDDL_REVISION_1,
                                                              &descriptor, nullptr))
        return nullptr;
    SECURITY_ATTRIBUTES attributes{sizeof attributes, descriptor, FALSE};
    const HDESK desktop = CreateDesktopW(name->c_str(), nullptr, nullptr, 0, GENERIC_ALL, &attributes);
    LocalFree(descriptor);
    wchar_t station[256]{};
    DWORD size = 0;
    if (desktop && GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, station, sizeof station, &size))
        *name = std::wstring(station) + L"\\" + *name;
    return desktop;
}

class LowRightsFolderUsers {
public:
    DWORD open(const std::wstring &path)
    {
        const std::lock_guard lock(m_mutex);
        int &users = m_users[path];
        if (users == 0)
            if (const DWORD error = setIntegrityLabel(path, IntegrityLabel::LowRightsMayWrite); error != ERROR_SUCCESS) {
                m_users.erase(path);
                return error;
            }
        ++users;
        return ERROR_SUCCESS;
    }

    void close(const std::wstring &path)
    {
        const std::lock_guard lock(m_mutex);
        const auto it = m_users.find(path);
        if (it == m_users.end() || --it->second > 0)
            return;
        m_users.erase(it);
        setIntegrityLabel(path, IntegrityLabel::LowRightsMayNotOpen);
    }

private:
    std::mutex m_mutex;
    std::map<std::wstring, int> m_users;
};

LowRightsFolderUsers &lowRightsFolderUsers()
{
    static LowRightsFolderUsers users;
    return users;
}

HANDLE agentJob(bool lowRights)
{
    const HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (lowRights)
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    if (lowRights) {
        JOBOBJECT_BASIC_UI_RESTRICTIONS ui{JOB_OBJECT_UILIMIT_DESKTOP | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS |
                                           JOB_OBJECT_UILIMIT_EXITWINDOWS | JOB_OBJECT_UILIMIT_GLOBALATOMS |
                                           JOB_OBJECT_UILIMIT_HANDLES | JOB_OBJECT_UILIMIT_READCLIPBOARD |
                                           JOB_OBJECT_UILIMIT_WRITECLIPBOARD | JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS};
        SetInformationJobObject(job, JobObjectBasicUIRestrictions, &ui, sizeof ui);
    }
    return job;
}

}

struct Launcher::Impl {
    Coverage coverage;
    PSID container = nullptr;
    std::optional<QString> lowRightsHelper;
    std::vector<std::wstring> writable;
    HDESK desktop = nullptr;
    std::wstring desktopName;
    std::wstring commandLine;
    BYTE internetSid[SECURITY_MAX_SID_SIZE]{};
    SID_AND_ATTRIBUTES capability{};
    SECURITY_CAPABILITIES security{};
    std::vector<HANDLE> jobs;
    HANDLE job = nullptr;
    std::vector<HANDLE> inherited;
    std::vector<BYTE> attributes;
    STARTUPINFOEXW startup{};

    ~Impl()
    {
        if (container)
            FreeSid(container);
        for (const std::wstring &path : writable)
            lowRightsFolderUsers().close(path);
        if (desktop)
            CloseDesktop(desktop);
    }

    void startLowRights(const Profile &profile)
    {
        const QString helper = QDir::toNativeSeparators(*profile.lowRightsHelper);
        lowRightsHelper = helper;
        if (!QFileInfo(helper).isFile()) {
            coverage.missing = QStringLiteral("HikariSub's low-rights helper is missing (%1)").arg(helper);
            return;
        }
        QStringList refused;
        for (const QString &path : profile.readWrite) {
            const QFileInfo info(path);
            if (!info.exists())
                continue;
            const std::wstring native = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
            if (const DWORD error = lowRightsFolderUsers().open(native); error != ERROR_SUCCESS)
                refused << labelRefusal(QString::fromStdWString(native), error);
            else
                writable.push_back(native);
        }
        desktop = privateDesktop(&desktopName);
        if (!desktop)
            refused << QStringLiteral("a desktop of its own (error %1)").arg(GetLastError());
        coverage.files = true;
        coverage.network = true;
        if (!refused.isEmpty())
            coverage.missing = QStringLiteral("low rights couldn't be set up for %1").arg(refused.join(QStringLiteral(", ")));
    }

    void modify(QProcess::CreateProcessArguments *args)
    {
        job = agentJob(lowRightsHelper.has_value());
        if (job)
            jobs.push_back(job);
        if (lowRightsHelper) {
            commandLine = quoted(*lowRightsHelper) + L" " + args->arguments;
            args->arguments = commandLine.data();
            args->applicationName = nullptr;
        }
        inherited.clear();
        for (HANDLE h : {args->startupInfo->hStdInput, args->startupInfo->hStdOutput, args->startupInfo->hStdError}) {
            DWORD flags = 0;
            if (h && h != INVALID_HANDLE_VALUE && GetHandleInformation(h, &flags) && (flags & HANDLE_FLAG_INHERIT) &&
                std::find(inherited.begin(), inherited.end(), h) == inherited.end())
                inherited.push_back(h);
        }
        const DWORD count = (container ? 1 : 0) + (job ? 1 : 0) + (inherited.empty() ? 0 : 1);
        if (count == 0)
            return;
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, count, 0, &size);
        attributes.assign(size, 0);
        auto *list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if (!InitializeProcThreadAttributeList(list, count, 0, &size))
            return;
        if (container)
            UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &security, sizeof security,
                                      nullptr, nullptr);
        if (job)
            UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job, sizeof job, nullptr, nullptr);
        if (!inherited.empty())
            UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(),
                                      inherited.size() * sizeof(HANDLE), nullptr, nullptr);
        startup = {};
        startup.StartupInfo = *reinterpret_cast<STARTUPINFOW *>(args->startupInfo);
        startup.StartupInfo.cb = sizeof startup;
        if (desktop)
            startup.StartupInfo.lpDesktop = desktopName.data();
        startup.lpAttributeList = list;
        args->startupInfo = reinterpret_cast<decltype(args->startupInfo)>(&startup.StartupInfo);
        args->flags |= EXTENDED_STARTUPINFO_PRESENT;
    }
};

Coverage systemCoverage(Network)
{
    return {.files = true, .network = true, .missing = {}};
}

Launcher::Launcher(std::optional<Profile> profile) : m(std::make_unique<Impl>())
{
    if (!profile) {
        m->coverage.missing = QStringLiteral("started without a sandbox");
        return;
    }
    if (profile->lowRightsHelper) {
        m->startLowRights(*profile);
        return;
    }
    const std::wstring name = profile->name.toStdWString();
    PSID sid = nullptr;
    const HRESULT hr = appContainerSid(name, &sid);
    if (FAILED(hr) || !sid) {
        m->coverage.missing =
            QStringLiteral("the AppContainer couldn't be created (0x%1)").arg(static_cast<quint32>(hr), 8, 16,
                                                                              QLatin1Char('0'));
        return;
    }
    m->container = sid;

    std::vector<Grant> grants;
    collect(grants, profile->readExecute, GENERIC_READ | GENERIC_EXECUTE, true);
    collect(grants, profile->read, GENERIC_READ);
    collect(grants, profile->readWrite, GENERIC_ALL);
    QStringList refused;
    for (const Grant &g : grants)
        if (const DWORD error = grant(sid, g); error != ERROR_SUCCESS)
            refused << refusal(g, error);

    m->security.AppContainerSid = sid;
    if (profile->network == Network::Any) {
        DWORD size = sizeof m->internetSid;
        if (CreateWellKnownSid(WinCapabilityInternetClientSid, nullptr, m->internetSid, &size)) {
            m->capability = {m->internetSid, SE_GROUP_ENABLED};
            m->security.Capabilities = &m->capability;
            m->security.CapabilityCount = 1;
        }
    }
    m->coverage.files = true;
    m->coverage.network = true;
    if (!refused.isEmpty())
        m->coverage.missing = QStringLiteral("access couldn't be granted to %1").arg(refused.join(QStringLiteral(", ")));
}

Launcher::~Launcher()
{
    killTree();
}

const Coverage &Launcher::coverage() const
{
    return m->coverage;
}

QString Launcher::containerSid() const
{
    LPWSTR text = nullptr;
    if (!m->container || !ConvertSidToStringSidW(m->container, &text))
        return {};
    const QString sid = QString::fromWCharArray(text);
    LocalFree(text);
    return sid;
}

bool Launcher::runs(qint64 pid) const
{
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return false;
    const bool inside = std::ranges::any_of(m->jobs, [process](HANDLE job) {
        BOOL in = FALSE;
        return IsProcessInJob(process, job, &in) && in;
    });
    CloseHandle(process);
    return inside;
}

void Launcher::prepare(QProcess &process, void (*)())
{
    process.setCreateProcessArgumentsModifier([impl = m.get()](QProcess::CreateProcessArguments *args) {
        impl->modify(args);
    });
}

void Launcher::killTree()
{
    for (HANDLE job : m->jobs) {
        TerminateJobObject(job, 1);
        CloseHandle(job);
    }
    m->jobs.clear();
    m->job = nullptr;
}

}
