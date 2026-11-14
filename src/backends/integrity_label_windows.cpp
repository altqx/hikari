#include "integrity_label_windows.h"

#include <aclapi.h>
#include <sddl.h>

namespace hikari::backends {

DWORD setIntegrityLabel(const std::wstring &path, IntegrityLabel label)
{
    const wchar_t *sddl =
        label == IntegrityLabel::LowRightsMayWrite ? L"S:(ML;OICI;NW;;;LW)" : L"S:(ML;OICI;NRNW;;;ME)";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, nullptr))
        return GetLastError();
    BOOL present = FALSE, defaulted = FALSE;
    PACL sacl = nullptr;
    GetSecurityDescriptorSacl(descriptor, &present, &sacl, &defaulted);
    const DWORD result = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION,
                                               nullptr, nullptr, nullptr, sacl);
    LocalFree(descriptor);
    return result;
}

}
