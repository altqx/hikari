#pragma once

#include <string>

#include <windows.h>

namespace hikari::backends {

enum class IntegrityLabel {
    /// Low processes may write it, and so may every other Low process of the user.
    LowRightsMayWrite,
    /// Medium, refusing Low processes reading as well as writing.
    LowRightsMayNotOpen,
};

/// Labels `path` and, for a folder, what it holds and will hold. The user needs WRITE_OWNER on it.
DWORD setIntegrityLabel(const std::wstring &path, IntegrityLabel label);

}
