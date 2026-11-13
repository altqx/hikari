#include "hikari/backends/sandbox.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
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

/// Whether `acl` already gives an AppContainer `access`, through its own SID or ALL APPLICATION PACKAGES.
bool covered(PACL acl, PSID sid, DWORD access)
{
    if (!acl)
        return true;
    BYTE anyPackage[SECURITY_MAX_SID_SIZE]{};
    DWORD size = sizeof anyPackage;
    const bool haveAnyPackage = CreateWellKnownSid(WinBuiltinAnyPackageSid, nullptr, anyPackage, &size);
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

/// Adds an allow ACE for the container unless it already has `access`: rewriting a folder's ACL walks its tree.
DWORD grant(PSID sid, const Grant &g)
{
    PACL current = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (const DWORD read = GetNamedSecurityInfoW(g.path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                                                 nullptr, &current, nullptr, &descriptor);
        read != ERROR_SUCCESS)
        return read;
    const DWORD mapped = fileAccess(g.access);
    DWORD result = ERROR_SUCCESS;
    if (!covered(current, sid, mapped)) {
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

void collect(std::vector<Grant> &grants, const QStringList &paths, DWORD access)
{
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (info.exists())
            grants.push_back({QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString(), access, info.isDir()});
    }
}

}

struct Launcher::Impl {
    Coverage coverage;
    PSID container = nullptr;
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
    }

    void modify(QProcess::CreateProcessArguments *args)
    {
        job = CreateJobObjectW(nullptr, nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
            jobs.push_back(job);
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
    collect(grants, profile->readExecute, GENERIC_READ | GENERIC_EXECUTE);
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
