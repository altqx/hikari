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

#include "UpdateInstaller.h"
#include "HikariDialog.h"
#include "HikariMessageBox.h"
#include "HikariStaticText.h"
#include "JsonValue.h"
#include "MappedButton.h"
#include "Sha256.h"
#include "UpdatePolicy.h"
#include "UpdateApply.h"
#include "UpdateVerify.h"
#include "VersionHikariSub.h"
#include "config.h"
#include <wx/app.h>
#include <wx/clipbrd.h>
#include <wx/datetime.h>
#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/process.h>
#include <wx/sizer.h>
#include <wx/timer.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>
#include <wx/weakref.h>
#include <wx/webrequest.h>
#include <ctime>
#include <map>
#include <optional>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <gnu/libc-version.h>
#include <cerrno>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

using update_manifest::Pending;
using update_manifest::LastResult;

namespace
{
	const wxChar kSep = wxFileName::GetPathSeparator();

	wxString FromUtf8(const std::string &text) { return wxString::FromUTF8(text.c_str()); }
	std::string ToUtf8(const wxString &text) { return text.utf8_string(); }

	// The update cache (4.1): %LOCALAPPDATA%\HikariSub\Updates or
	// ${XDG_CACHE_HOME:-$HOME/.cache}/HikariSub/Updates. Empty when the
	// environment gives none.
	wxString CacheDir()
	{
		wxString base;
#ifdef _WIN32
		if (!wxGetEnv(L"LOCALAPPDATA", &base) || base.empty())
			return wxString();
#else
		if (!wxGetEnv(L"XDG_CACHE_HOME", &base) || !base.StartsWith(L"/")){
			wxString home;
			if (!wxGetEnv(L"HOME", &home) || !home.StartsWith(L"/"))
				return wxString();
			base = home + L"/.cache";
		}
#endif
		return base + kSep + L"HikariSub" + kSep + L"Updates";
	}

	// wx logs a file it cannot remove or rename, a missing one included,
	// as an error dialog; the updater says what failed itself.
	void RemoveFile(const wxString &path)
	{
		if (!wxFileExists(path))
			return;
		wxLogNull quiet;
		wxRemoveFile(path);
	}

	bool RenameFile(const wxString &from, const wxString &to)
	{
		wxLogNull quiet;
		return wxRenameFile(from, to, true);
	}

	wxString AppImagePath();

	// This copy's name in the shared cache (CopyKey): copies of other
	// kinds, and AppImages at other paths, keep records of their own.
	// InstallAppImageAtStart names the kind itself: it runs before the
	// options have read the marker that Kind() needs.
	wxString CopyKey(package_kind::Kind kind)
	{
		return FromUtf8(update_manifest::CopyKey(package_kind::Token(kind), ToUtf8(AppImagePath())));
	}
	wxString CopyKey() { return CopyKey(update_installer::Kind()); }

	wxString PendingPath(package_kind::Kind kind) { return CacheDir() + kSep + L"pending-" + CopyKey(kind) + L".json"; }
	wxString PendingPath() { return PendingPath(update_installer::Kind()); }
	// How an update of this copy that was handed off ended; the NSIS
	// installer writes it under its scope's key.
	wxString LastResultPath(package_kind::Kind kind) { return CacheDir() + kSep + L"last-result-" + CopyKey(kind) + L".json"; }
	wxString LastResultPath() { return LastResultPath(update_installer::Kind()); }
	// The version of the AppImage update an ordinary quit left for the
	// next start.
	wxString AppImageAtStartPath() { return CacheDir() + kSep + L"at-start-" + CopyKey(package_kind::Kind::AppImage); }
	// The version of the NSIS update handed to its installer at the last
	// quit. The installer writes a failure to last-result.json in the
	// profile of the account that runs it, which for all users is the
	// administrator who approved UAC, maybe not this user: the next start
	// tells this user from this file when it is still not that version.
	wxString InstallingPath() { return CacheDir() + kSep + L"installing-" + CopyKey(); }

	bool ReadSmallFile(const wxString &path, std::string *content, size_t limit = 64 * 1024)
	{
		// wx logs a file that cannot be opened, as an error dialog.
		if (!wxFileExists(path))
			return false;
		wxFFile file(path, L"rb");
		if (!file.IsOpened())
			return false;
		std::vector<char> buffer(limit + 1);
		size_t read = file.Read(buffer.data(), buffer.size());
		if (read > limit)
			return false;
		content->assign(buffer.data(), read);
		return true;
	}

	// Written to a temporary file beside it, then renamed over it.
	bool WriteAtomically(const wxString &path, const std::string &content)
	{
		wxFileName::Mkdir(path.BeforeLast(kSep), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
		wxString temporary = path + L".tmp";
		wxLogNull quiet;
		{
			wxFFile file(temporary, L"wb");
			if (!file.IsOpened() || !file.Write(content.data(), content.size()) || !file.Flush())
				return false;
		}
		return RenameFile(temporary, path);
	}

	std::optional<Pending> LoadPending(const wxString &path = PendingPath())
	{
		std::string text;
		if (!ReadSmallFile(path, &text))
			return std::nullopt;
		return update_manifest::ReadPending(JsonValue::Parse(text));
	}

	// kind: the AppImage's paths name it, as they run before Kind() can be read.
	void WriteLastResult(const std::string &version, bool ok, int code, const wxString &message,
		package_kind::Kind kind)
	{
		LastResult result{ version, ok, code, ToUtf8(message) };
		WriteAtomically(LastResultPath(kind), update_manifest::FormatLastResult(result));
	}
	void WriteLastResult(const std::string &version, bool ok, int code, const wxString &message)
	{
		WriteLastResult(version, ok, code, message, update_installer::Kind());
	}

	std::string NowRfc3339()
	{
		return ToUtf8(wxDateTime::Now().ToUTC().Format(L"%Y-%m-%dT%H:%M:%SZ"));
	}

	// Whether the file is the package: its size, then its SHA-256.
	bool VerifyFile(const wxString &path, long long size, const std::string &sha256)
	{
		if (!wxFileExists(path))
			return false;
		wxFFile file(path, L"rb");
		if (!file.IsOpened() || file.Length() != size)
			return false;
		Sha256 hash;
		std::vector<char> buffer(1 << 20);
		while (!file.Eof()) {
			size_t read = file.Read(buffer.data(), buffer.size());
			if (read == 0)
				break;
			hash.Update(buffer.data(), read);
		}
		return hash.HexDigest() == update_manifest::Lowercase(sha256);
	}

	wxString AppImagePath()
	{
		wxString path;
		if (!wxGetEnv(L"APPIMAGE", &path))
			return wxString();
		return path;
	}

	// A program in PATH, or empty.
	wxString FindInPath(const wxString &name)
	{
		wxString path;
		if (!wxGetEnv(L"PATH", &path))
			return wxString();
		wxStringTokenizer dirs(path, L":");
		while (dirs.HasMoreTokens()) {
			wxString candidate = dirs.GetNextToken() + L"/" + name;
			if (wxFileExists(candidate) && wxFileName::IsFileExecutable(candidate))
				return candidate;
		}
		return wxString();
	}

	bool IsNativeKind(package_kind::Kind kind)
	{
		return kind == package_kind::Kind::Deb || kind == package_kind::Kind::Rpm;
	}

	// Session state of the hand-off.
	wxString healthFile;
	wxString updatedFrom;
	bool relaunch = false;
	bool handedOff = false;
	bool nsisAttempted = false;
	bool waitingForInstaller = false;
	// What AfterExit() does.
	bool runAppImageWatchdog = false;
	bool execAfterExit = false;
	std::optional<Pending> handedOffPending;

	void ShowInFolder(const wxString &file)
	{
#ifdef _WIN32
		wxExecute(L"explorer.exe /select,\"" + file + L"\"", wxEXEC_ASYNC);
#else
		wxString uri = L"file://" + file;
		const wxString command[] = { L"dbus-send", L"--session", L"--dest=org.freedesktop.FileManager1",
			L"--type=method_call", L"/org/freedesktop/FileManager1", L"org.freedesktop.FileManager1.ShowItems",
			L"array:string:" + uri, L"string:" };
		std::vector<const wxChar *> argv;
		for (const wxString &arg : command)
			argv.push_back(arg.wc_str());
		argv.push_back(nullptr);
		if (FindInPath(L"dbus-send").empty() || wxExecute(argv.data(), wxEXEC_SYNC) != 0)
			wxLaunchDefaultApplication(file.BeforeLast(kSep));
#endif
	}

	// "Update downloaded" for a deb or rpm where there is no pkexec, so the
	// user installs it (4.4).
	void ShowManualInstall(wxWindow *frame, const Pending &pending)
	{
		HikariDialog *dialog = new HikariDialog(frame, -1, _("Update"));
		wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
		wxString file = FromUtf8(pending.file);
		sizer->Add(new HikariStaticText(dialog, -1,
			wxString::Format(_("HikariSub %s has been downloaded, but this system has no pkexec to ask for your password, so it cannot be installed from here.\nUpdate downloaded: %s"),
				FromUtf8(pending.version), file)), 0, wxEXPAND | wxALL, 6);
		wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
		MappedButton *show = new MappedButton(dialog, wxID_OPEN, _("Show in folder"));
		MappedButton *copy = new MappedButton(dialog, wxID_COPY, _("Copy install command"));
		MappedButton *close = new MappedButton(dialog, wxID_CLOSE, _("Close"));
		buttons->Add(show, 1, wxALL, 2);
		buttons->Add(copy, 1, wxALL, 2);
		buttons->Add(close, 1, wxALL, 2);
		sizer->Add(buttons, 0, wxEXPAND | wxALL, 2);
		std::string command = update_manifest::InstallCommand(pending.file, pending.version);
		dialog->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [file](wxCommandEvent &) { ShowInFolder(file); }, wxID_OPEN);
		dialog->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [command](wxCommandEvent &) {
			if (wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(FromUtf8(command)));
				wxTheClipboard->Flush();
				wxTheClipboard->Close();
			}
		}, wxID_COPY);
		dialog->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [dialog](wxCommandEvent &) { dialog->Destroy(); }, wxID_CLOSE);
		dialog->SetEscapeId(wxID_CLOSE);
		dialog->Bind(wxEVT_SHOW, [dialog](wxShowEvent &evt) {
			if (!evt.IsShown())
				dialog->CallAfter([dialog] { dialog->Destroy(); });
			evt.Skip();
		});
		dialog->SetSizerAndFit(sizer);
		dialog->CenterOnParent();
		dialog->Show();
	}

	// "HikariSub <V> is ready to install" (4.1): Restart now, Later, and
	// the release's notes. Any way of closing it but Restart now is Later.
	void ShowUpdateReady(wxWindow *frame, const Pending &pending)
	{
		if (!frame || frame->IsBeingDeleted())
			return;
		if (IsNativeKind(update_installer::Kind()) && FindInPath(L"pkexec").empty()) {
			ShowManualInstall(frame, pending);
			return;
		}
		constexpr int kRestartId = 20101;
		constexpr int kNotesId = 20102;
		wxString version = FromUtf8(pending.version);
		HikariDialog *dialog = new HikariDialog(frame, -1, _("Update ready"));
		wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
		HikariStaticText *title = new HikariStaticText(dialog, -1,
			wxString::Format(_("HikariSub %s is ready to install"), version));
		title->SetFont(title->GetFont().Bold());
		sizer->Add(title, 0, wxEXPAND | wxALL, 6);
		sizer->Add(new HikariStaticText(dialog, -1,
			_("Restart now to install it, or it is installed when you next quit HikariSub.")),
			0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
		wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
		buttons->Add(new MappedButton(dialog, kRestartId, _("Restart now")), 1, wxALL, 2);
		buttons->Add(new MappedButton(dialog, wxID_CLOSE, _("Later")), 1, wxALL, 2);
		buttons->Add(new MappedButton(dialog, kNotesId, _("What's new")), 1, wxALL, 2);
		sizer->Add(buttons, 0, wxEXPAND | wxALL, 2);
		wxString page = update_installer::ReleasePage(L"v" + version);
		wxWeakRef<wxWindow> owner(frame);
		dialog->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [dialog, owner](wxCommandEvent &) {
			dialog->Destroy();
			if (owner)
				update_installer::RestartNow(owner);
		}, kRestartId);
		dialog->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [page](wxCommandEvent &) {
			if (!page.empty())
				wxLaunchDefaultBrowser(page);
		}, kNotesId);
		dialog->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [dialog](wxCommandEvent &) { dialog->Destroy(); }, wxID_CLOSE);
		dialog->SetEscapeId(wxID_CLOSE);
		dialog->Bind(wxEVT_SHOW, [dialog](wxShowEvent &evt) {
			if (!evt.IsShown())
				dialog->CallAfter([dialog] { dialog->Destroy(); });
			evt.Skip();
		});
		dialog->SetSizerAndFit(sizer);
		dialog->CenterOnParent();
		dialog->Show();
	}

	// One download at a time (4.1). It writes <name>.part while hashing
	// it, verifies the size and the hash, renames it, records it pending,
	// and shows "Update ready". The whole download has a deadline, and a
	// stall of kStallSeconds without a byte fails it; CheckRun decides the
	// ending as it does for the check, a cancel whose event is lost
	// included. It deletes itself once its request's last event has come.
	class PackageDownload : public wxEvtHandler
	{
	public:
		PackageDownload(wxWindow *frame, const update_installer::Target &target)
			: frame(frame)
			, target(target)
			, deadline(this)
			, stall(this)
		{
			Bind(wxEVT_WEBREQUEST_DATA, &PackageDownload::OnData, this);
			Bind(wxEVT_WEBREQUEST_STATE, &PackageDownload::OnState, this);
			Bind(wxEVT_TIMER, &PackageDownload::OnDeadline, this, deadline.GetId());
			Bind(wxEVT_TIMER, &PackageDownload::OnStall, this, stall.GetId());
		}

		const std::string &Version() const { return target.version; }

		int AddListener(const update_installer::Listener &listener)
		{
			int id = ++lastId;
			if (listener.progress || listener.done)
				listeners[id] = listener;
			return id;
		}

		void RemoveListener(int id) { listeners.erase(id); }

		// False when nothing could be started; Fail() has then been
		// called.
		bool Start()
		{
			if (!PreparePaths()) {
				Fail(_("The update cannot be saved: there is no writable folder for it."));
				return false;
			}
			if (VerifyFile(finalPath, target.package.size, target.package.sha256)) {
				// Already downloaded and still whole.
				Complete();
				return true;
			}
			RemoveFile(finalPath);
			bool opened;
			{
				wxLogNull quiet;
				opened = file.Open(partPath, wxFile::write);
			}
			if (!opened) {
				Fail(_("The update cannot be saved: the folder is not writable."));
				return false;
			}
			std::string url = update_installer::AssetUrl(target.version, target.package.name);
			wxWebSession &session = wxWebSession::GetDefault();
			if (url.empty() || !session.IsOpened()) {
				Fail(_("Cannot download the update"));
				return false;
			}
			request = session.CreateRequest(this, FromUtf8(url));
			if (!request.IsOk()) {
				Fail(_("Cannot download the update"));
				return false;
			}
			request.SetHeader(L"User-Agent", L"HikariSub");
			request.SetHeader(L"Accept", L"application/octet-stream");
			request.SetStorage(wxWebRequest::Storage_None);
			request.SetTimeouts(update_policy::kConnectTimeoutMs, update_policy::kDataTimeoutMs);
			deadline.StartOnce(static_cast<int>(
				update_manifest::DownloadDeadlineSeconds(target.package.size) * 1000));
			lastByte = time(nullptr);
			stall.Start(5000);
			request.Start();
			return true;
		}

		void Cancel()
		{
			cancelledByNewer = true;
			listeners.clear();
			Detached();
			if (request.IsOk() && request.GetState() == wxWebRequest::State_Active)
				request.Cancel();
		}

	private:
		bool PreparePaths()
		{
			wxString name = FromUtf8(target.package.name);
			if (update_installer::Kind() == package_kind::Kind::AppImage) {
				finalPath = FromUtf8(update_manifest::AppImageStagingPath(ToUtf8(AppImagePath()), target.version));
				if (finalPath.empty())
					return false;
			}
			else {
				wxString cache = CacheDir();
				if (cache.empty())
					return false;
				wxString dir = cache + kSep + CopyKey() + kSep + FromUtf8(target.version);
				if (!wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL))
					return false;
				finalPath = dir + kSep + name;
			}
			partPath = finalPath + L".part";
			return true;
		}

		void OnStall(wxTimerEvent &)
		{
			if (time(nullptr) - lastByte < update_manifest::kStallSeconds)
				return;
			stall.Stop();
			if (request.IsOk() && request.GetState() == wxWebRequest::State_Active)
				request.Cancel();
		}

		void OnDeadline(wxTimerEvent &)
		{
			bool active = request.IsOk() && request.GetState() == wxWebRequest::State_Active;
			switch (run.Fire(active)) {
			case update_policy::CheckRun::Fired::Nothing:
				break;
			case update_policy::CheckRun::Fired::Cancel:
				request.Cancel();
				wxFALLTHROUGH;
			case update_policy::CheckRun::Fired::Wait:
				deadline.StartOnce(static_cast<int>(update_policy::kCancelWaitSeconds * 1000));
				break;
			case update_policy::CheckRun::Fired::GiveUp:
				Fail(_("The update took too long to download"));
				if (lastEventCame)
					Release();
				break;
			}
		}

		void OnData(wxWebRequestEvent &evt)
		{
			if (!run.TakesData() || ended)
				return;
			if (!checkedUrl) {
				checkedUrl = true;
				const wxWebResponse &response = evt.GetResponse();
				if (!response.IsOk() || !update_installer::IsAssetReplyUrl(ToUtf8(response.GetURL()))) {
					badUrl = true;
					if (run.CutOff(request.GetState() == wxWebRequest::State_Active))
						request.Cancel();
					return;
				}
			}
			size_t size = evt.GetDataSize();
			if (received + static_cast<long long>(size) > target.package.size) {
				if (run.CutOff(request.GetState() == wxWebRequest::State_Active))
					request.Cancel();
				return;
			}
			if (file.Write(evt.GetDataBuffer(), size) != size) {
				writeFailed = true;
				if (run.CutOff(request.GetState() == wxWebRequest::State_Active))
					request.Cancel();
				return;
			}
			hash.Update(evt.GetDataBuffer(), size);
			received += static_cast<long long>(size);
			lastByte = time(nullptr);
			for (auto &entry : listeners) {
				if (entry.second.progress)
					entry.second.progress(received, target.package.size);
			}
		}

		void OnState(wxWebRequestEvent &evt)
		{
			wxWebRequest::State state = evt.GetState();
			if (state == wxWebRequest::State_Active || state == wxWebRequest::State_Idle)
				return;
			lastEventCame = true;
			std::optional<update_policy::Ending> ending = run.PageEnded(
				state == wxWebRequest::State_Completed ? update_policy::RequestEnd::Completed :
				state == wxWebRequest::State_Cancelled ? update_policy::RequestEnd::Cancelled :
				update_policy::RequestEnd::Failed);
			if (!ending || ended) {
				Release();
				return;
			}
			run.Finish();
			const wxWebResponse &response = evt.GetResponse();
			bool fromAssets = state == wxWebRequest::State_Completed && response.IsOk() &&
				update_installer::IsAssetReplyUrl(ToUtf8(response.GetURL()));
			if (*ending == update_policy::Ending::Answered && fromAssets && !badUrl && !writeFailed &&
				response.GetStatus() == 200)
			{
				Finish();
			}
			else if (cancelledByNewer) {
				Fail(wxString());
			}
			else if (*ending == update_policy::Ending::TimedOut) {
				Fail(_("The update took too long to download"));
			}
			else if (writeFailed) {
				Fail(_("The update cannot be saved: the disk may be full."));
			}
			else if (badUrl || (*ending == update_policy::Ending::Answered && !fromAssets)) {
				Fail(_("The update came from somewhere other than GitHub, and was refused."));
			}
			else if (*ending == update_policy::Ending::CutOff) {
				Fail(_("The update is larger than its release says, and was refused."));
			}
			else {
				Fail(_("Cannot download the update"));
			}
			Release();
		}

		// The body came whole: verify it, then keep it.
		void Finish()
		{
			deadline.Stop();
			stall.Stop();
			file.Close();
			std::string digest = hash.HexDigest();
			if (received != target.package.size || digest != update_manifest::Lowercase(target.package.sha256)) {
				Fail(_("The downloaded update does not match its release, and was deleted."));
				return;
			}
#ifndef _WIN32
			if (update_installer::Kind() == package_kind::Kind::AppImage)
				chmod(partPath.fn_str(), 0755);
#endif
			if (!RenameFile(partPath, finalPath)) {
				Fail(_("The update cannot be saved: the folder is not writable."));
				return;
			}
			Complete();
		}

		// The verified file is at finalPath: record it and say so.
		void Complete()
		{
			if (ended)
				return;
			ended = true;
			deadline.Stop();
			stall.Stop();
			Pending pending{ target.version, std::string(package_kind::Token(update_installer::Kind())),
				ToUtf8(finalPath), update_manifest::Lowercase(target.package.sha256), target.package.size,
				NowRfc3339() };
			if (!WriteAtomically(PendingPath(), update_manifest::FormatPending(pending))) {
				ended = false;
				Fail(_("The update cannot be saved: the folder is not writable."));
				return;
			}
			std::map<int, update_installer::Listener> waiting;
			waiting.swap(listeners);
			for (auto &entry : waiting) {
				if (entry.second.done)
					entry.second.done(true, wxString());
			}
			wxWindow *window = frame;
			Detached();
			if (window && !window->IsBeingDeleted() && !wxTheApp->IsScheduledForDestruction(window))
				ShowUpdateReady(window, pending);
			// A download that verified from the cache has no request.
			if (!request.IsOk())
				Release();
		}

		void Fail(const wxString &error)
		{
			if (ended)
				return;
			ended = true;
			deadline.Stop();
			stall.Stop();
			if (file.IsOpened())
				file.Close();
			RemoveFile(partPath);
			std::map<int, update_installer::Listener> waiting;
			waiting.swap(listeners);
			for (auto &entry : waiting) {
				if (entry.second.done)
					entry.second.done(false, error);
			}
			Detached();
			if (!request.IsOk())
				Release();
		}

		// It is no longer the download that a new one joins.
		void Detached();

		void Release()
		{
			if (released)
				return;
			released = true;
			Detached();
			wxTheApp->ScheduleForDestruction(this);
		}

		wxWeakRef<wxWindow> frame;
		update_installer::Target target;
		wxString finalPath;
		wxString partPath;
		wxFile file;
		Sha256 hash;
		long long received = 0;
		time_t lastByte = 0;
		wxWebRequest request;
		update_policy::CheckRun run;
		wxTimer deadline;
		wxTimer stall;
		std::map<int, update_installer::Listener> listeners;
		int lastId = 0;
		bool checkedUrl = false;
		bool badUrl = false;
		bool writeFailed = false;
		bool cancelledByNewer = false;
		bool ended = false;
		bool lastEventCame = false;
		bool released = false;
	};

	PackageDownload *current = nullptr;

	void PackageDownload::Detached()
	{
		if (current == this)
			current = nullptr;
	}

	// Deletes the pending record, and its file when it is still there.
	// A pending update of another copy of HikariSub that shares this
	// user's cache: one of another kind (an AppImage beside a deb, an
	// installed copy beside a portable one), or an AppImage update that
	// is for another AppImage (IsStagedBeside). Each copy has a record of
	// its own (CopyKey), so this is a second guard. It is neither
	// offered, removed nor installed here. A record whose kind cannot be
	// read is no copy's, and is dropped as before.
	bool ForAnotherCopy(const Pending &pending)
	{
		std::optional<package_kind::Kind> kind = package_kind::ParseToken(pending.kind);
		if (!kind)
			return false;
		if (*kind != update_installer::Kind())
			return true;
#ifdef _WIN32
		return false;
#else
		return *kind == package_kind::Kind::AppImage &&
			!update_manifest::IsStagedBeside(ToUtf8(AppImagePath()), pending.file, pending.version);
#endif
	}

	void DropPending(const Pending &pending)
	{
		RemoveFile(FromUtf8(pending.file));
		RemoveFile(PendingPath());
	}

	// Removes this copy's Updates/<copy>/<V> folders, and the AppImage's
	// staged files, of versions that are not newer than this one (4.1).
	// Other copies' downloads are theirs to clear: a deb copy's pending
	// 0.0.2 is not done with because a portable 0.0.3 started.
	void ClearOldVersions()
	{
		wxString cache = CacheDir();
		if (!cache.empty())
			cache += kSep + CopyKey();
		if (!cache.empty() && wxDirExists(cache)) {
			wxDir dir(cache);
			wxString name;
			std::vector<wxString> old;
			for (bool more = dir.GetFirst(&name, wxEmptyString, wxDIR_DIRS); more; more = dir.GetNext(&name)) {
				if (ParseSemVer(ToUtf8(name)) && !update_manifest::IsNewer(ToUtf8(name), VersionHikariSub))
					old.push_back(cache + kSep + name);
			}
			for (const wxString &folder : old)
				wxFileName::Rmdir(folder, wxPATH_RMDIR_RECURSIVE);
		}
		if (update_installer::Kind() != package_kind::Kind::AppImage)
			return;
		wxString appImage = AppImagePath();
		if (appImage.empty() || !wxDirExists(appImage.BeforeLast(kSep)))
			return;
		wxDir appDir(appImage.BeforeLast(kSep));
		if (!appDir.IsOpened())
			return;
		wxString name;
		std::vector<wxString> stale;
		const std::string imageName = ToUtf8(appImage.AfterLast(kSep));
		for (bool more = appDir.GetFirst(&name, wxEmptyString, wxDIR_FILES | wxDIR_HIDDEN); more;
			more = appDir.GetNext(&name))
		{
			// This image's .<name>.<V>.update, and its .part; another
			// image's are its own to clear.
			std::string version = update_manifest::StagedVersion(imageName, ToUtf8(name));
			if (!version.empty() && !update_manifest::IsNewer(version, VersionHikariSub))
				stale.push_back(appImage.BeforeLast(kSep) + kSep + name);
		}
		for (const wxString &file : stale)
			RemoveFile(file);
	}

#ifdef _WIN32
	std::wstring Wide(const wxString &text) { return std::wstring(text.wc_str()); }

	// Unprotected: an all-users copy the user can change, never elevated.
	enum class Launch { Started, Declined, Failed, Unprotected };

	// Starts the installing side (UpdateApply.h): this installed
	// hikarisub.exe with --hikari-apply-update, through one UAC prompt for
	// all users' scope, unelevated for the user's own. The downloaded
	// installer is never what UAC is asked to run: the installing side
	// checks it against the release's SHA256SUMS first (4.2).
	Launch StartApply(const Pending &pending, bool withRelaunch, HANDLE *process)
	{
		bool allUsers = update_installer::Kind() == package_kind::Kind::NsisMachine;
		std::vector<wchar_t> exe(32768);
		DWORD length = GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size()));
		if (length == 0 || length >= exe.size())
			return Launch::Failed;
		// UAC is asked to run this program only when the user cannot have
		// changed it: package-kind.txt says "all users" in a per-user copy
		// as easily as in Program Files, and a DLL put beside the program
		// would run elevated before any check (UpdateApply.h).
		std::wstring installDir(exe.data(), length);
		size_t slash = installDir.find_last_of(L'\\');
		installDir.erase(slash == std::wstring::npos ? 0 : slash);
		if (allUsers && !update_apply::IsElevated() && !update_apply::IsProtectedFromUser(installDir))
			return Launch::Unprotected;
		update_apply::Request request;
		request.version = pending.version;
		request.file = Wide(FromUtf8(pending.file));
		request.pid = GetCurrentProcessId();
		request.relaunch = withRelaunch;
		std::wstring parameters = update_apply::CommandLine(request);
		SHELLEXECUTEINFOW info = {};
		info.cbSize = sizeof(info);
		info.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
		info.lpVerb = allUsers ? L"runas" : nullptr;
		info.lpFile = exe.data();
		info.lpParameters = parameters.c_str();
		info.nShow = SW_SHOWNORMAL;
		if (!ShellExecuteExW(&info))
			return GetLastError() == ERROR_CANCELLED ? Launch::Declined : Launch::Failed;
		if (!info.hProcess)
			return Launch::Failed;
		*process = info.hProcess;
		return Launch::Started;
	}

	// What the installing side's exit code means for the user.
	wxString ApplyMessage(int code, const Pending &pending)
	{
		switch (code) {
		case update_verify::kMismatch:
			return _("The update was refused: the installer does not match its release.");
		case update_verify::kNoSums:
			return wxString::Format(_("HikariSub %s could not be checked against its release on GitHub, so it was not installed. It is tried again at the next quit."),
				FromUtf8(pending.version));
		case update_verify::kBadArguments:
			return _("The update could not be handed to the installer.");
		default:
			return wxString::Format(_("The installer of HikariSub %s could not be started. You can run it yourself: %s"),
				FromUtf8(pending.version), FromUtf8(pending.file));
		}
	}

	// Waits, with the event loop running and the frame hidden, for the
	// installing side to check the installer and start it. Then the frame
	// is closed, and the installer waits for this process to end; or, when
	// the installing side refused, the user is told why, and a Restart
	// now keeps the app running.
	class ApplyWait : public wxTimer
	{
	public:
		ApplyWait(wxWindow *frame, HANDLE process, const Pending &pending, bool withRelaunch)
			: owner(frame), process(process), pending(pending), withRelaunch(withRelaunch)
		{
			busy = new HikariDialog(nullptr, -1, _("Update"));
			wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
			sizer->Add(new HikariStaticText(busy, -1, _("Checking the update...")), 0, wxEXPAND | wxALL, 12);
			busy->SetSizerAndFit(sizer);
			busy->CenterOnScreen();
			busy->Show();
			frame->Hide();
			Start(200);
		}

		void Notify() override
		{
			if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0)
				return;
			Stop();
			DWORD code = update_verify::kFailed;
			if (!GetExitCodeProcess(process, &code))
				code = update_verify::kFailed;
			CloseHandle(process);
			waitingForInstaller = false;
			if (busy)
				busy->Destroy();
			if (code != update_verify::kOk) {
				// A file that is not the release's, or a version that is
				// not newer, will not become right: it is dropped.
				if (code == update_verify::kMismatch || code == update_verify::kBadArguments)
					DropPending(pending);
				HikariMessageBox(ApplyMessage(int(code), pending), _("Update"), wxOK, nullptr);
				handedOff = false;
				if (withRelaunch && owner) {
					relaunch = false;
					owner->Show();
					owner->Raise();
					wxTheApp->CallAfter([this] { delete this; });
					return;
				}
			}
			else {
				// The installer runs once this process has exited.
				WriteAtomically(InstallingPath(), pending.version);
			}
			if (owner)
				owner->Destroy();
			wxTheApp->CallAfter([this] { delete this; });
		}

	private:
		wxWeakRef<wxWindow> owner;
		HANDLE process;
		Pending pending;
		bool withRelaunch;
		wxWeakRef<HikariDialog> busy;
	};
#else
	// Steps 1 and 2 of the AppImage hand-off (4.3): keep the running file
	// as $APPIMAGE.old, then rename the new one over $APPIMAGE.
	bool StageAppImage(const Pending &pending)
	{
		std::string appImage = ToUtf8(AppImagePath());
		std::string old = appImage + ".old";
		if (appImage.empty())
			return false;
		unlink(old.c_str());
		if (link(appImage.c_str(), old.c_str()) != 0) {
			wxLogNull quiet;
			if (rename(appImage.c_str(), old.c_str()) != 0 &&
				!wxCopyFile(FromUtf8(appImage), FromUtf8(old), true))
			{
				return false;
			}
		}
		if (rename(pending.file.c_str(), appImage.c_str()) != 0) {
			// Put the old file back if it was moved away.
			if (access(appImage.c_str(), F_OK) != 0)
				rename(old.c_str(), appImage.c_str());
			return false;
		}
		return true;
	}

	pid_t Spawn(const std::vector<std::string> &arguments)
	{
		std::vector<char *> argv;
		for (const std::string &argument : arguments)
			argv.push_back(const_cast<char *>(argument.c_str()));
		argv.push_back(nullptr);
		posix_spawnattr_t attributes;
		posix_spawnattr_init(&attributes);
		posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID);
		pid_t pid = -1;
		if (posix_spawn(&pid, argv[0], nullptr, &attributes, argv.data(), environ) != 0)
			pid = -1;
		posix_spawnattr_destroy(&attributes);
		return pid;
	}

	// The command line of this start, without the updater's own switches,
	// for a relaunch.
	std::vector<std::string> RelaunchArguments(const std::string &program)
	{
		std::vector<std::string> arguments = { program };
		for (int i = 1; i < wxTheApp->argc; i++) {
			std::string argument = ToUtf8(wxTheApp->argv[i]);
			if (argument.rfind("--hikari-", 0) == 0)
				continue;
			arguments.push_back(argument);
		}
		arguments.push_back(std::string(update_manifest::kUpdatedFromSwitch) + VersionHikariSub);
		return arguments;
	}

	// Stops a started image that did not report its window: its whole
	// process group (Spawn starts it in a session of its own, so the
	// AppImage runtime and the app it runs go together), with SIGTERM, then
	// SIGKILL after a grace, and waits for it, so that the image put back
	// does not find it still holding the single-instance lock.
	void StopImage(pid_t child, bool reaped)
	{
		if (child <= 0)
			return;
		auto gone = [&](int tenths) {
			for (int i = 0; i < tenths; i++) {
				if (!reaped && waitpid(child, nullptr, WNOHANG) == child)
					reaped = true;
				if (reaped && kill(-child, 0) != 0 && errno == ESRCH)
					return true;
				usleep(100 * 1000);
			}
			return false;
		};
		kill(-child, SIGTERM);
		if (gone(50))
			return;
		kill(-child, SIGKILL);
		gone(50);
	}

	// The AppImage watchdog (4.3, steps 3 and 4): start the new file, and
	// restore the old one if it does not show its window within 60 s.
	void AppImageWatchdog(const Pending &pending)
	{
		std::string appImage = ToUtf8(AppImagePath());
		// Named for this image too: another AppImage's watchdog, waiting on
		// the same version, must not take this one's report for its own.
		std::string health = ToUtf8(CacheDir() + kSep + L"health-" + CopyKey(package_kind::Kind::AppImage) + L"-" +
			FromUtf8(pending.version));
		unlink(health.c_str());
		std::vector<std::string> arguments = RelaunchArguments(appImage);
		arguments.push_back(std::string(update_manifest::kHealthSwitch) + health);
		pid_t child = Spawn(arguments);
		bool healthy = false;
		bool reaped = false;
		for (int waited = 0; child > 0 && waited < 60 * 5; waited++) {
			if (access(health.c_str(), F_OK) == 0) {
				healthy = true;
				break;
			}
			int status = 0;
			if (waitpid(child, &status, WNOHANG) == child) {
				reaped = true;
				break;
			}
			usleep(200 * 1000);
		}
		if (healthy) {
			unlink(health.c_str());
			return;
		}
		StopImage(child, reaped);
		std::string old = appImage + ".old";
		bool restored = rename(old.c_str(), appImage.c_str()) == 0;
		WriteLastResult(pending.version, false, 1, restored ?
			wxString::Format(_("HikariSub %s did not start, so HikariSub %s was put back."),
				FromUtf8(pending.version), VersionHikariSub) :
			wxString::Format(_("HikariSub %s did not start. The previous version is %s; rename it back to use it."),
				FromUtf8(pending.version), FromUtf8(old)), package_kind::Kind::AppImage);
		std::vector<std::string> restart = { appImage };
		Spawn(restart);
	}

	// What the helper's exit code means for the user (4.4).
	wxString HelperMessage(int code, const wxString &file)
	{
		switch (code) {
		case update_manifest::kHelperMismatch:
			return _("The update was refused: the package does not match its release.");
		case update_manifest::kHelperPackageManagerFailed:
			return wxString::Format(_("The package manager could not install the update. You can install it yourself: %s"),
				file);
		case update_manifest::kHelperLocked:
			return _("The package manager was busy with another installation. The update is installed at the next quit.");
		case update_manifest::kHelperBadArguments:
			return _("The update could not be handed to the installer.");
		case update_manifest::kHelperNoSums:
			return wxString::Format(_("The update could not be checked against its release on GitHub, so it was not installed. You can install it yourself: %s"),
				file);
		default:
			return wxString::Format(_("The update could not be installed (code %d)."), code);
		}
	}

	// The deb or rpm hand-off (4.4): pkexec runs the helper while the app
	// waits with a small notice and the event loop running, then the
	// frame is closed. Relaunch, of the new version or of the old one
	// when the install did not happen, waits for AfterExit().
	void RunPkexec(wxWindow *frame, const Pending &pending)
	{
		HikariDialog *busy = new HikariDialog(nullptr, -1, _("Update"));
		wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(new HikariStaticText(busy, -1, _("Installing update...")), 0, wxEXPAND | wxALL, 12);
		busy->SetSizerAndFit(sizer);
		busy->CenterOnScreen();
		busy->Show();
		frame->Hide();
		waitingForInstaller = true;

		wxProcess *process = new wxProcess();
		wxWeakRef<wxWindow> owner(frame);
		time_t started = time(nullptr);
		Pending record = pending;
		bool withRelaunch = relaunch;
		process->Bind(wxEVT_END_PROCESS, [busy, owner, started, record, withRelaunch, process](wxProcessEvent &evt) {
			int code = evt.GetExitCode();
			wxString file = FromUtf8(record.file);
			if (code == update_manifest::kHelperOk) {
				execAfterExit = withRelaunch;
			}
			else if (code == update_manifest::kPkexecDismissed || code == update_manifest::kPkexecNotAuthorised) {
				// Dismissed, or not authorised: the record stays. No
				// polkit agent answers at once; that is told at the next
				// start, with the command to install it by hand.
				if (code == update_manifest::kPkexecNotAuthorised && time(nullptr) - started < 2) {
					WriteLastResult(record.version, false, code,
						wxString::Format(_("No password prompt could be shown to install HikariSub %s. Install it yourself: %s"),
							FromUtf8(record.version), FromUtf8(update_manifest::InstallCommand(record.file, record.version))));
				}
				execAfterExit = withRelaunch;
			}
			else {
				WriteLastResult(record.version, false, code, HelperMessage(code, file));
				execAfterExit = withRelaunch;
			}
			waitingForInstaller = false;
			busy->Destroy();
			if (owner)
				owner->Destroy();
			wxTheApp->CallAfter([process] { delete process; });
		});
		// The file and the version only: the helper checks the file
		// against the release's own SHA256SUMS, never a digest of ours.
		wxString helper = FromUtf8(std::string(update_manifest::kUpdateHelper));
		wxString file = FromUtf8(record.file);
		wxString version = FromUtf8(record.version);
		const wxChar *argv[] = { L"pkexec", helper.wc_str(), L"install", file.wc_str(), version.wc_str(), nullptr };
		if (wxExecute(argv, wxEXEC_ASYNC, process) == 0) {
			// pkexec could not start: as if it was not there.
			wxProcessEvent failed(0, 0, update_manifest::kPkexecNotAuthorised);
			process->ProcessEvent(failed);
		}
	}
#endif
}

#ifdef _WIN32
int update_installer::ApplyUpdate(bool allUsersInstall)
{
	int count = 0;
	LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &count);
	if (!argv)
		return update_verify::kBadArguments;
	std::vector<std::wstring> arguments;
	bool isSwitch = count >= 2 && std::wstring(argv[1]) == update_apply::kSwitch;
	for (int i = 2; i < count; i++)
		arguments.push_back(argv[i]);
	LocalFree(argv);
	std::optional<update_apply::Request> request = isSwitch ? update_apply::Parse(arguments) : std::nullopt;
	if (!request)
		return update_verify::kBadArguments;

	update_apply::Environment environment;
	environment.installed = VersionHikariSub;
	std::vector<wchar_t> exe(32768);
	DWORD length = GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size()));
	if (length == 0 || length >= exe.size())
		return update_verify::kFailed;
	std::wstring path(exe.data(), length);
	environment.installDir = path.substr(0, path.find_last_of(L'\\'));
	environment.allUsers = allUsersInstall;
	if (allUsersInstall) {
		// Program Files is only an administrator's to change, and so is
		// the folder the installer is copied to.
		if (!update_apply::IsElevated())
			return update_verify::kBadArguments;
		environment.stagingParent = update_apply::SystemTempFolder();
	}
	std::string feed = FeedBase();
	environment.fetchSums = [feed](const std::string &version) -> std::optional<std::string> {
		std::string url = feed.empty() ? update_verify::SumsUrl(version) :
			update_manifest::feed::DownloadUrl(feed, version, update_verify::kSumsName);
		if (url.empty())
			return std::nullopt;
		return update_apply::FetchHttps(url, [feed](const std::string &reply) {
			return feed.empty() ? update_manifest::IsDownloadUrl(reply) : update_manifest::feed::IsFeedUrl(feed, reply);
		}, update_verify::kMaxSumsBytes);
	};
	environment.run = update_apply::Launch;
	return update_apply::Apply(*request, environment);
}
#endif

package_kind::Kind update_installer::Kind()
{
	static std::optional<package_kind::Kind> kind;
	if (!kind) {
#ifdef _WIN32
		kind = package_kind::Resolve(package_kind::ParseToken(ToUtf8(Options.packageMarker)), true, false);
#else
		wxString appImage = AppImagePath();
		kind = package_kind::Resolve(package_kind::ParseToken(ToUtf8(Options.packageMarker)), false,
			!appImage.empty() && wxFileName::FileExists(appImage));
#endif
	}
	return *kind;
}

update_manifest::SystemInfo update_installer::GetSystemInfo()
{
	update_manifest::SystemInfo info;
#ifdef _WIN32
	typedef LONG(WINAPI *RtlGetVersionFunction)(OSVERSIONINFOW *);
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	RtlGetVersionFunction getVersion = ntdll ?
		reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
	OSVERSIONINFOW version = {};
	version.dwOSVersionInfoSize = sizeof(version);
	if (getVersion && getVersion(&version) == 0) {
		info.windowsVersion = std::to_string(version.dwMajorVersion) + "." + std::to_string(version.dwMinorVersion) +
			"." + std::to_string(version.dwBuildNumber);
	}
#else
	info.glibcVersion = gnu_get_libc_version();
#endif
	return info;
}

std::string update_installer::FeedBase()
{
#ifdef HIKARISUB_UPDATE_TEST_FEED
	static std::string base = [] {
		wxString value;
		if (!wxGetEnv(L"HIKARISUB_UPDATE_FEED", &value))
			return std::string();
		std::string text = ToUtf8(value);
		return update_manifest::feed::IsBase(text) ? text : std::string();
	}();
	return base;
#else
	return std::string();
#endif
}

wxString update_installer::ReleasePage(const wxString &tag)
{
	std::string base = FeedBase();
	if (!base.empty())
		return FromUtf8(update_manifest::feed::PageUrl(base, ToUtf8(tag)));
	return FromUtf8(update_policy::ReleasePageUrl(ToUtf8(tag)));
}

std::string update_installer::AssetUrl(const std::string &version, const std::string &name)
{
	std::string base = FeedBase();
	if (!base.empty())
		return update_manifest::feed::DownloadUrl(base, version, name);
	return update_manifest::DownloadUrl(version, name);
}

bool update_installer::IsAssetReplyUrl(const std::string &url)
{
	std::string base = FeedBase();
	if (!base.empty())
		return update_manifest::feed::IsFeedUrl(base, url);
	return update_manifest::IsDownloadUrl(url);
}

bool update_installer::CanInstall()
{
	package_kind::Kind kind = Kind();
	if (package_kind::FlowOf(kind) != package_kind::Flow::Install)
		return false;
	if (kind == package_kind::Kind::AppImage) {
		wxString appImage = AppImagePath();
		return wxFileName::IsFileWritable(appImage) && wxFileName::IsDirWritable(appImage.BeforeLast(kSep));
	}
	return !CacheDir().empty();
}

int update_installer::Download(wxWindow *frame, const Target &target, const Listener &listener)
{
	if (current && current->Version() == target.version)
		return current->AddListener(listener);
	if (current) {
		current->Cancel();
		current = nullptr;
	}
	PackageDownload *download = new PackageDownload(frame, target);
	current = download;
	int id = download->AddListener(listener);
	if (!download->Start())
		return 0;
	return id;
}

void update_installer::Detach(int id)
{
	if (current)
		current->RemoveListener(id);
}

bool update_installer::IsPending(const std::string &version)
{
	if (current && current->Version() == version)
		return true;
	std::optional<Pending> pending = LoadPending();
	return pending && pending->version == version && !ForAnotherCopy(*pending) &&
		wxFileExists(FromUtf8(pending->file));
}

void update_installer::SetCommandLine(const wxString &health, const wxString &from)
{
	healthFile = health;
	updatedFrom = from;
}

void update_installer::OnStartup(wxWindow *frame)
{
	// The new version's window is up: say so to the watchdog, and drop
	// the old file, which is kept until a start of the new one shows its
	// window (4.3). Only once the watchdog has been told: if the report
	// cannot be written, it puts the old file back, and needs it.
	bool reported = true;
	if (!healthFile.empty()) {
		wxLogNull quiet;
		wxFFile health(healthFile, L"wb");
		reported = health.IsOpened() && health.Close();
	}
#ifndef _WIN32
	if (reported && Kind() == package_kind::Kind::AppImage) {
		wxString old = AppImagePath() + L".old";
		if (wxFileExists(old))
			RemoveFile(old);
	}
#endif

	std::string text;
	std::string installing;
	if (ReadSmallFile(InstallingPath(), &installing, 64))
		RemoveFile(InstallingPath());
	if (ReadSmallFile(LastResultPath(), &text)) {
		installing.clear();
		RemoveFile(LastResultPath());
		std::optional<LastResult> result = update_manifest::ReadLastResult(JsonValue::Parse(text));
		if (result && !result->ok) {
			wxString message = FromUtf8(result->message);
			if (message.empty())
				message = wxString::Format(_("HikariSub %s could not be installed (code %d)."),
					FromUtf8(result->version), result->code);
			HikariMessageBox(message, _("Update"), wxOK, frame);
		}
	}
	// Handed to the installer, no result here, and still not installed:
	// the installer failed, and told the account that ran it, if anyone.
	if (!installing.empty() && installing.back() == '\n')
		installing.pop_back();
	if (update_verify::IsReleaseVersion(installing) && update_manifest::IsNewer(installing, VersionHikariSub)) {
		HikariMessageBox(wxString::Format(_("HikariSub %s was not installed: its installer did not finish. If another administrator account approved it, that account was told why."),
			FromUtf8(installing)), _("Update"), wxOK, frame);
	}

	std::optional<Pending> pending = LoadPending();
	if (pending && ForAnotherCopy(*pending)) {
		// Another copy's update: left to it.
	}
	else if (pending) {
		std::optional<package_kind::Kind> kind = package_kind::ParseToken(pending->kind);
		bool usable = kind && *kind == Kind() && update_manifest::IsNewer(pending->version, VersionHikariSub) &&
			package_kind::FlowOf(Kind()) == package_kind::Flow::Install && wxFileExists(FromUtf8(pending->file));
		if (!usable)
			DropPending(*pending);
		else
			ShowUpdateReady(frame, *pending);
	}
	else if (wxFileExists(PendingPath())) {
		RemoveFile(PendingPath());
	}
	ClearOldVersions();
}

void update_installer::RestartNow(wxWindow *frame)
{
	relaunch = true;
	frame->Close();
	// A cancelled save prompt keeps the frame, and nothing restarts.
	if (!frame->IsBeingDeleted() && !waitingForInstaller)
		relaunch = false;
}

update_installer::Quit update_installer::OnQuit(wxWindow *frame)
{
	if (handedOff || waitingForInstaller)
		return Quit::Continue;
	std::optional<Pending> pending = LoadPending();
	if (!pending || ForAnotherCopy(*pending))
		return Quit::Continue;
	std::optional<package_kind::Kind> kind = package_kind::ParseToken(pending->kind);
	if (!kind || *kind != Kind() || package_kind::FlowOf(*kind) != package_kind::Flow::Install ||
		!update_manifest::IsNewer(pending->version, VersionHikariSub))
	{
		DropPending(*pending);
		return Quit::Continue;
	}
	// The file is checked again: it sat in the user's folder since. This
	// only weeds out a broken download: the record is the user's to
	// change, so what installs checks the file against the release itself.
	if (!VerifyFile(FromUtf8(pending->file), pending->size, pending->sha256)) {
		DropPending(*pending);
		return Quit::Continue;
	}

#ifdef _WIN32
	if (nsisAttempted && !relaunch)
		return Quit::Continue;
	nsisAttempted = true;
	HANDLE process = nullptr;
	switch (StartApply(*pending, relaunch, &process)) {
	case Launch::Started:
		handedOff = true;
		waitingForInstaller = true;
		new ApplyWait(frame, process, *pending, relaunch);
		return Quit::Wait;
	case Launch::Declined:
		// The record stays; a Restart now keeps the app running.
		if (relaunch) {
			relaunch = false;
			return Quit::Veto;
		}
		return Quit::Continue;
	case Launch::Unprotected:
		// The download stays for the user to run; the update is not tried again.
		RemoveFile(PendingPath());
		WriteLastResult(pending->version, false, 4,
			wxString::Format(_("HikariSub is installed for all users in a folder that can be changed without administrator rights, so HikariSub %s was not installed automatically. You can run its installer yourself: %s"),
				FromUtf8(pending->version), FromUtf8(pending->file)));
		relaunch = false;
		return Quit::Continue;
	case Launch::Failed:
		break;
	}
	WriteLastResult(pending->version, false, 2,
		wxString::Format(_("The installer of HikariSub %s could not be started. You can run it yourself: %s"),
			FromUtf8(pending->version), FromUtf8(pending->file)));
	relaunch = false;
	return Quit::Continue;
#else
	if (*kind == package_kind::Kind::AppImage) {
		if (!relaunch) {
			// An ordinary quit: the next start installs it, watched.
			WriteAtomically(AppImageAtStartPath(), pending->version);
			return Quit::Continue;
		}
		if (!StageAppImage(*pending)) {
			WriteLastResult(pending->version, false, 2,
				wxString::Format(_("HikariSub %s could not replace the AppImage. The download is at %s."),
					FromUtf8(pending->version), FromUtf8(pending->file)));
			relaunch = false;
			return Quit::Continue;
		}
		handedOff = true;
		RemoveFile(PendingPath());
		runAppImageWatchdog = relaunch;
		handedOffPending = *pending;
		return Quit::Continue;
	}
	if (FindInPath(L"pkexec").empty()) {
		if (relaunch) {
			relaunch = false;
			ShowManualInstall(frame, *pending);
			return Quit::Veto;
		}
		return Quit::Continue;
	}
	handedOff = true;
	handedOffPending = *pending;
	RunPkexec(frame, *pending);
	return Quit::Wait;
#endif
}

bool update_installer::InstallAppImageAtStart(bool isAppImage)
{
#ifndef _WIN32
	// Not Kind(): it would keep this kind before the options read the marker.
	if (!isAppImage || AppImagePath().empty())
		return false;
	std::string named;
	if (!ReadSmallFile(AppImageAtStartPath(), &named, 64))
		return false;
	const wxString record = PendingPath(package_kind::Kind::AppImage);
	std::optional<Pending> pending = LoadPending(record);
	// Another AppImage copy's quit left it: that copy's next start installs
	// it. (Kind() is not read yet, so the record's kind decides.)
	if (pending && package_kind::ParseToken(pending->kind) == package_kind::Kind::AppImage &&
		!update_manifest::IsStagedBeside(ToUtf8(AppImagePath()), pending->file, pending->version))
	{
		return false;
	}
	// Once: what fails below leaves the update to OnStartup().
	RemoveFile(AppImageAtStartPath());
	if (!pending || !update_manifest::IsAtStartVersion(named, pending->version) ||
		package_kind::ParseToken(pending->kind) != package_kind::Kind::AppImage ||
		!update_manifest::IsNewer(pending->version, VersionHikariSub) ||
		!VerifyFile(FromUtf8(pending->file), pending->size, pending->sha256))
	{
		return false;
	}
	if (!StageAppImage(*pending)) {
		WriteLastResult(pending->version, false, 2,
			wxString::Format(_("HikariSub %s could not replace the AppImage. The download is at %s."),
				FromUtf8(pending->version), FromUtf8(pending->file)), package_kind::Kind::AppImage);
		return false;
	}
	RemoveFile(record);
	AppImageWatchdog(*pending);
	return true;
#else
	(void)isAppImage;
	return false;
#endif
}

void update_installer::AfterExit()
{
#ifndef _WIN32
	if (runAppImageWatchdog && handedOffPending) {
		runAppImageWatchdog = false;
		AppImageWatchdog(*handedOffPending);
		return;
	}
	if (execAfterExit) {
		execAfterExit = false;
		std::string launcher(update_manifest::kNativeLauncher);
		std::vector<std::string> arguments = RelaunchArguments(launcher);
		std::vector<char *> argv;
		for (std::string &argument : arguments)
			argv.push_back(argument.data());
		argv.push_back(nullptr);
		execv(launcher.c_str(), argv.data());
	}
#endif
}
