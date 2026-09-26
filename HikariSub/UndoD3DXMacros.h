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

// No include guard: must run again after every D3DX include.
//
// d3dx9core.h puts back the DrawText macro that wx/msw/winundef.h undefined.
// wx/defs.h re-applies winundef.h outside its own guard, so the next wx header
// defines that inline function twice and MSVC reports C2084.

#ifdef DrawText
#undef DrawText
#endif
