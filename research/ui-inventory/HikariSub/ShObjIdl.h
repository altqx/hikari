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

#pragma once
#ifndef _WIN32
#include "platform.h"
#ifndef HIKARISUB_GUID_STUB_DEFINED
#define HIKARISUB_GUID_STUB_DEFINED
struct GUID { unsigned long Data1{}; unsigned short Data2{}; unsigned short Data3{}; unsigned char Data4[8]{}; };
#endif
inline constexpr GUID CLSID_TaskbarList{};
constexpr int CLSCTX_INPROC_SERVER = 1;
constexpr int TBPF_NOPROGRESS = 0;
constexpr int TBPF_NORMAL = 2;
#define __uuidof(x) GUID{}
struct ITaskbarList3 {
    HRESULT SetProgressState(HWND, int) { return S_OK; }
    HRESULT SetProgressValue(HWND, ULONGLONG, ULONGLONG) { return S_OK; }
};
inline HRESULT CoCreateInstance(GUID, void*, int, GUID, void** out) { if (out) *out = new ITaskbarList3(); return S_OK; }
#endif
