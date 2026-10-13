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

// Installs an update for the kinds that can: downloads the package in the
// background, verifies its size and SHA-256, keeps it pending, shows
// "Update ready", and hands it to the kind's installer at Restart now or at
// the next quit: the NSIS installer, the AppImage replacing itself, or the
// deb/rpm helper through pkexec (installers spec, section 4). The AUR and
// portable copies never get here: they are told about a release only.

#pragma once

#include "PackageKind.h"
#include "UpdateManifest.h"

#include <wx/string.h>
#include <functional>
#include <string>

class wxWindow;

namespace update_installer
{
	// This copy's kind, resolved once from the marker and $APPIMAGE.
	package_kind::Kind Kind();

	update_manifest::SystemInfo GetSystemInfo();

	// A test build's local update feed, from HIKARISUB_UPDATE_FEED, or
	// empty. Always empty in a release build.
	std::string FeedBase();

	// The page of a release, built from its tag (2.2).
	wxString ReleasePage(const wxString &tag);

	// The URL of a release's file, built from the version and name.
	std::string AssetUrl(const std::string &version, const std::string &name);

	// Whether a download's final URL, after redirects, is one of GitHub's
	// release-file hosts, or the test feed.
	bool IsAssetReplyUrl(const std::string &url);

	// Whether this copy can install a package itself now: a kind that
	// installs, and, for an AppImage, a writable file and folder. When it
	// cannot, it is told about the release only (4.3).
	bool CanInstall();

	struct Target
	{
		std::string version;
		update_manifest::Package package;
	};

	// The download of a check from the menu, shown in its dialog.
	struct Listener
	{
		// Bytes received and the package's size.
		std::function<void(long long received, long long total)> progress;
		// Whether the package came and verified, or why not.
		std::function<void(bool ok, const wxString &error)> done;
	};

	// Downloads target in the background and, once it verifies, keeps it
	// pending and shows "Update ready". A download of it already running
	// is joined; one of another version is cancelled. A file of the
	// version already in the cache that verifies is used as it is. The id
	// detaches the listener, as its dialog closes.
	int Download(wxWindow *frame, const Target &target, const Listener &listener = Listener());
	void Detach(int id);

	// Whether that version is downloaded, verified and waiting to be
	// installed, or downloading now.
	bool IsPending(const std::string &version);

	// The switches of a start that an update made (4.3).
	void SetCommandLine(const wxString &healthFile, const wxString &updatedFrom);

	// The main window is shown: what a start does for the updater. The
	// result of the last install, once; the pending update's notice; the
	// cache of versions that are not newer cleared; the AppImage's health
	// file written and its old file removed.
	void OnStartup(wxWindow *frame);

	enum class Quit
	{
		// Close as usual.
		Continue,
		// Keep running: the update could not be handed off, and the user
		// asked to restart into it.
		Veto,
		// The installer runs, and the frame is closed when it ends.
		Wait,
	};

	// The frame is closing and every save prompt has passed: hands the
	// pending update off, with relaunch when Restart now asked for it.
	Quit OnQuit(wxWindow *frame);

	// "Restart now": the normal quit, then the new version.
	void RestartNow(wxWindow *frame);

#ifdef _WIN32
	// hikarisub.exe --hikari-apply-update <version> <file> <pid> <relaunch>,
	// from wxApp::Initialize before anything else: checks the installer
	// against the release's SHA256SUMS and starts it (UpdateApply.h).
	// Returns the exit code. allUsersInstall: this copy is an all-users
	// install, which runs it elevated.
	int ApplyUpdate(bool allUsersInstall);
#endif

	// From wxApp::OnExit, once the windows are gone and the single-instance
	// lock is released: the AppImage watchdog and the relaunch after a
	// deb or rpm install.
	void AfterExit();

	// From wxApp::OnInit, before the single-instance lock: the AppImage
	// update that an ordinary quit left installs now, replacing the image
	// and starting the new one with this start's arguments under the
	// watchdog, as after Restart now. An ordinary quit cannot do that
	// itself: nothing would watch the new image's first start, and an
	// image that cannot start would stay. True when another process took
	// over (the new version, or the old one put back), so this one exits.
	// isAppImage: this start's kind, read from the marker and $APPIMAGE
	// (Kind() would cache a kind before the options have read the marker).
	bool InstallAppImageAtStart(bool isAppImage);
}
