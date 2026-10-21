//  Copyright (c) 2018 - 2026, Marcin Drob
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

// Checks GitHub for a newer release and offers it in the way this copy's
// package kind allows (installers spec, sections 2 to 4): an installer,
// AppImage, deb or rpm copy downloads and installs it (UpdateInstaller); an
// AUR copy is told to update with its package manager; a portable copy, or
// one with "Install updates automatically" off, is shown the release page.
// A rewrite release can also carry an opt-in offer to try the rewrite.

#pragma once

#include <wx/string.h>

class wxWindow;

class UpdateChecker
{
public:
	// Honours the auto-check option and the next-check time, and stays silent
	// on error, when already current, and about a version the user closed
	// the dialog on. Safe to call with no network.
	static void CheckOnStartup(wxWindow *parent);

	// Menu entry: checks whatever the options say, and reports being up to
	// date and any failure. While a check runs, it has that one report
	// instead of starting another.
	static void CheckNow(wxWindow *parent);

	// Help, "Try the new HikariSub...": checks for the rewrite's offer
	// (ADR-0021) and shows it, whatever was chosen of it before, or says
	// there is none.
	static void OfferNow(wxWindow *parent);
};
