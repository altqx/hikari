#include "hikari/backends/sandbox.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <string>
#include <vector>

#include <windows.h>
#include <aclapi.h>
#include <userenv.h>

namespace hikari::backends::sandbox {

namespace {

struct Grant {
    std::wstring path;
    DWORD access = 0;
    bool folder = false;
};

bool hasAce(PACL acl, PSID sid, DWORD access)
{
    if (!acl)
        return false;
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void *ace = nullptr;
        if (!GetAce(acl, i, &ace))
            continue;
        const auto *header = static_cast<ACE_HEADER *>(ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
            continue;
        const auto *allowed = static_cast<ACCESS_ALLOWED_ACE *>(ace);
        if (EqualSid(const_cast<DWORD *>(&allowed->SidStart), sid) && (allowed->Mask & access) == access)
            return true;
    }
    return false;
}

/// Adds an allow ACE for the container, unless one already covers `access`: rewriting a folder's ACL walks its tree.
bool grant(PSID sid, const Grant &g)
{
    PACL current = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(g.path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &current,
                              nullptr, &descriptor) != ERROR_SUCCESS)
        return false;
    GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    DWORD mapped = g.access;
    MapGenericMask(&mapped, &mapping);
    bool ok = hasAce(current, sid, mapped);
    if (!ok) {
        EXPLICIT_ACCESSW entry{};
        entry.grfAccessPermissions = mapped;
        entry.grfAccessMode = GRANT_ACCESS;
        entry.grfInheritance = g.folder ? SUB_CONTAINERS_AND_OBJECTS_INHERIT : NO_INHERITANCE;
        entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        entry.Trustee.ptstrName = static_cast<LPWSTR>(sid);
        PACL updated = nullptr;
        ok = SetEntriesInAclW(1, &entry, current, &updated) == ERROR_SUCCESS &&
             SetNamedSecurityInfoW(const_cast<LPWSTR>(g.path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                   nullptr, nullptr, updated, nullptr) == ERROR_SUCCESS;
        LocalFree(updated);
    }
    LocalFree(descriptor);
    return ok;
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

Launcher::Launcher(std::optional<Profile> profile) : m(std::make_unique<Impl>())
{
    if (!profile) {
        m->coverage.missing = QStringLiteral("started without a sandbox");
        return;
    }
    const std::wstring name = profile->name.toStdWString();
    PSID sid = nullptr;
    HRESULT hr = CreateAppContainerProfile(name.c_str(), name.c_str(), name.c_str(), nullptr, 0, &sid);
    if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
        hr = DeriveAppContainerSidFromAppContainerName(name.c_str(), &sid);
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
        if (!grant(sid, g))
            refused << QString::fromStdWString(g.path);

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
