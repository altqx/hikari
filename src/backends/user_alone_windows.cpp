#include "hikari/backends/user_alone.h"

#include "hikari/backends/sandbox.h"
#include "integrity_label_windows.h"

#include <QDir>
#include <QFileInfo>

#include <windows.h>

#include <aclapi.h>
#include <userenv.h>

#include <algorithm>
#include <string>
#include <vector>

namespace hikari::backends {

namespace {

std::vector<BYTE> currentUserSid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<char> user(size);
    const bool read = size != 0 && GetTokenInformation(token, TokenUser, user.data(), size, &size) != FALSE;
    CloseHandle(token);
    if (!read)
        return {};
    PSID sid = reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid;
    std::vector<BYTE> copy(GetLengthSid(sid));
    if (!CopySid(static_cast<DWORD>(copy.size()), copy.data(), sid))
        return {};
    return copy;
}

std::wstring native(const QString &path)
{
    return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toStdWString();
}

bool ownContainer(PSID sid)
{
    static const std::vector<std::vector<BYTE>> own = [] {
        std::vector<std::vector<BYTE>> sids;
        for (const QString &name : sandbox::ownProfileNames()) {
            PSID derived = nullptr;
            if (SUCCEEDED(DeriveAppContainerSidFromAppContainerName(reinterpret_cast<LPCWSTR>(name.utf16()), &derived)) &&
                derived) {
                const auto *bytes = static_cast<const BYTE *>(derived);
                sids.emplace_back(bytes, bytes + GetLengthSid(derived));
                FreeSid(derived);
            }
        }
        return sids;
    }();
    return std::ranges::any_of(own, [sid](const std::vector<BYTE> &o) {
        return EqualSid(const_cast<BYTE *>(o.data()), sid) != FALSE;
    });
}

bool othersAllowed(PACL acl, PSID user)
{
    if (!acl)
        return true;
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void *ace = nullptr;
        if (!GetAce(acl, i, &ace))
            return true;
        const BYTE type = static_cast<ACE_HEADER *>(ace)->AceType;
        if (type == ACCESS_DENIED_ACE_TYPE)
            continue;
        if (type != ACCESS_ALLOWED_ACE_TYPE)
            return true;
        const auto *allowed = static_cast<ACCESS_ALLOWED_ACE *>(ace);
        PSID sid = const_cast<DWORD *>(&allowed->SidStart);
        if (allowed->Mask != 0 && !EqualSid(sid, user) && !ownContainer(sid))
            return true;
    }
    return false;
}

}

bool isForUserAlone(const QString &path)
{
    std::vector<BYTE> user = currentUserSid();
    if (user.empty() || !QFileInfo::exists(path))
        return false;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(native(path).c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl,
                              nullptr, &descriptor) != ERROR_SUCCESS)
        return false;
    const bool alone = !othersAllowed(acl, user.data());
    LocalFree(descriptor);
    return alone;
}

bool keepForUserAlone(const QString &path)
{
    std::vector<BYTE> user = currentUserSid();
    const QFileInfo info(path);
    if (user.empty() || !info.exists() || info.isSymLink())
        return false;
    EXPLICIT_ACCESSW entry{};
    entry.grfAccessPermissions = FILE_ALL_ACCESS;
    entry.grfAccessMode = SET_ACCESS;
    entry.grfInheritance = info.isDir() ? SUB_CONTAINERS_AND_OBJECTS_INHERIT : NO_INHERITANCE;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entry.Trustee.TrusteeType = TRUSTEE_IS_USER;
    entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(user.data());
    PACL acl = nullptr;
    if (SetEntriesInAclW(1, &entry, nullptr, &acl) != ERROR_SUCCESS)
        return false;
    std::wstring name = native(path);
    const bool set = SetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT,
                                           DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
                                           nullptr, acl, nullptr) == ERROR_SUCCESS;
    LocalFree(acl);
    return set && setIntegrityLabel(name, IntegrityLabel::LowRightsMayNotOpen) == ERROR_SUCCESS;
}

}
