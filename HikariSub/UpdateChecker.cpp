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

#include "UpdateChecker.h"
#include "JsonValue.h"
#include "HikariCheckBox.h"
#include "HikariDialog.h"
#include "HikariMessageBox.h"
#include "HikariStaticText.h"
#include "HikariTextCtrl.h"
#include "MappedButton.h"
#include "PackageKind.h"
#include "SemVer.h"
#include "UpdateInstaller.h"
#include "UpdateManifest.h"
#include "UpdatePolicy.h"
#include "VersionHikariSub.h"
#include "config.h"
#include <wx/app.h>
#include <wx/sizer.h>
#include <wx/timer.h>
#include <wx/utils.h>
#include <wx/weakref.h>
#include <wx/webrequest.h>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

namespace
{
	constexpr std::optional<SemVer> kVersion = ParseSemVer(VersionHikariSub);
	constexpr long long kNumVersion[] = { NumVersionHikariSub };
	static_assert(kVersion, "VersionHikariSub is not a semantic version");
	static_assert(kNumVersion[0] == kVersion->major && kNumVersion[1] == kVersion->minor &&
		kNumVersion[2] == kVersion->patch && kNumVersion[3] == 0,
		"NumVersionHikariSub does not match VersionHikariSub");

	constexpr int kOpenId = 20001;
	constexpr int kRemindId = 20002;
	constexpr int kInstallId = 20003;
	// The dialog's result once its download has verified.
	constexpr int kInstalledId = 20004;

	struct ReleaseInfo
	{
		wxString tag;
		wxString name;
		wxString url;
		wxString notes;
	};

	// The list endpoint is used rather than /releases/latest because latest
	// hides prereleases outright, which would leave the "stable only" option
	// with nothing to do. False when the reply is not a release list. Of
	// each asset, the name, size and digest, which stand in for an update
	// manifest when a release has none.
	bool ReadReleases(const JsonValue &root, std::vector<update_policy::Release> *releases)
	{
		if (root.GetType() != JsonValue::Type::Array)
			return false;

		for (const JsonValue &release : root.Items()) {
			update_policy::Release entry;
			entry.tag = release.GetString("tag_name").utf8_string();
			entry.draft = release.GetBool("draft");
			entry.prerelease = release.GetBool("prerelease");
			if (const JsonValue *assets = release.Find("assets")) {
				for (const JsonValue &asset : assets->Items()) {
					update_policy::ReleaseAsset file;
					const JsonValue *name = asset.Find("name");
					const JsonValue *size = asset.Find("size");
					const JsonValue *digest = asset.Find("digest");
					if (!name || name->GetType() != JsonValue::Type::String)
						continue;
					file.name = name->Utf8();
					if (size && size->GetType() == JsonValue::Type::Number && size->Number() >= 0 &&
						size->Number() <= double(update_manifest::kMaxPackageBytes))
						file.size = static_cast<long long>(size->Number());
					if (digest && digest->GetType() == JsonValue::Type::String)
						file.digest = digest->Utf8();
					entry.assets.push_back(file);
				}
			}
			releases->push_back(entry);
		}
		return true;
	}

	// Only the releases that may be shown are read in full. The page is
	// built from the tag; the reply's html_url is not used.
	ReleaseInfo GetReleaseInfo(const JsonValue &release)
	{
		ReleaseInfo info;
		info.tag = release.GetString("tag_name");
		info.name = release.GetString("name", info.tag);
		info.url = update_installer::ReleasePage(info.tag);
		info.notes = release.GetString("body");
		return info;
	}

	// The release list's URL, or the test feed's.
	std::string ReleasesUrl(int page)
	{
		std::string feed = update_installer::FeedBase();
		return feed.empty() ? update_policy::ReleasesApiUrl(page) : update_manifest::feed::ReleasesUrl(feed);
	}

	bool IsReleasesReplyUrl(const std::string &url)
	{
		std::string feed = update_installer::FeedBase();
		return feed.empty() ? update_policy::IsApiReplyUrl(url) : update_manifest::feed::IsFeedUrl(feed, url);
	}

	void OpenReleasePage(const wxString &url)
	{
		if (!url.empty())
			wxLaunchDefaultBrowser(url);
	}

	long long Now()
	{
		return static_cast<long long>(time(nullptr));
	}

	long long LoadNextCheck()
	{
		return update_policy::ParseNextCheck(Options.GetString(UPDATER_NEXT_CHECK).utf8_string());
	}

	void SetNextCheck(long long next)
	{
		Options.SetString(UPDATER_NEXT_CHECK, wxString::FromUTF8(update_policy::FormatNextCheck(next)));
	}

	// Sets the next automatic check and writes it out at once, so that a
	// crash or a kill does not leave the next start to check again.
	void SaveNextCheck(long long next)
	{
		SetNextCheck(next);
		Options.SaveOptions(true, false);
	}

	class OptionsNextCheck : public update_policy::NextCheckStore
	{
	public:
		long long Load() const override { return LoadNextCheck(); }
		void Save(long long nextCheck) override { SaveNextCheck(nextCheck); }
	};

	OptionsNextCheck optionsNextCheck;
	update_policy::CheckTracker tracker(optionsNextCheck);


	// Says that only a prerelease is newer, and offers to open its page.
	void ShowNewerPrerelease(wxWindow *parent, const ReleaseInfo &release)
	{
		HikariMessageDialog message(parent,
			wxString::Format(_("You have the latest stable version. A newer prerelease, %s, is available."),
				release.tag), _("Update"), wxYES_NO);
		message.SetYesLabel(_("Open the download page"));
		message.SetNoLabel(_("Close"));
		if (message.ShowModal() & wxYES)
			OpenReleasePage(release.url);
	}

	// "Remind me in a week" puts the next automatic check off a week.
	// Closing an offer any other way declines its version: the startup
	// check keeps quiet about it, and does not download it, and offers the
	// next one; a check from the menu still offers it.
	void SaveOfferChoice(int result, const wxString &tag)
	{
		if (result == kRemindId) {
			SetNextCheck(Now() + update_policy::kWeek);
			Options.SetString(UPDATER_DECLINED_VERSION, wxString());
		}
		else {
			Options.SetString(UPDATER_DECLINED_VERSION, tag);
		}
	}

	class UpdateAvailableDialog : public HikariDialog
	{
	public:
		// With an offer of type Install, the first button downloads and
		// installs the release, and a progress line takes its place while
		// it downloads (installers spec, 4.1).
		UpdateAvailableDialog(wxWindow *parent, const ReleaseInfo &release, const update_manifest::Offer &offer,
			const std::string &version)
			: HikariDialog(parent, -1, _("A new version is available"))
			, tag(release.tag)
			, frame(parent)
			, target{ version, offer.package }
		{
			wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

			HikariStaticText *title = new HikariStaticText(this, -1, release.name);
			title->SetFont(title->GetFont().Bold());
			sizer->Add(title, 0, wxEXPAND | wxALL, 4);

			sizer->Add(new HikariStaticText(this, -1,
				wxString::Format(_("You have version %s; version %s is available"),
					VersionHikariSub, release.tag)),
				0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
			if (offer.type == update_manifest::OfferType::NotifyRequirement) {
				sizer->Add(new HikariStaticText(this, -1,
					wxString::Format(_("It needs %s or newer, so it cannot be installed here."),
						wxString::FromUTF8(offer.requirement.c_str()))),
					0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
			}

			HikariTextCtrl *notes = new HikariTextCtrl(this, -1, release.notes,
				wxDefaultPosition, wxSize(460, 220),
				wxTE_MULTILINE | wxTE_READONLY | wxTE_BESTWRAP);
			sizer->Add(notes, 1, wxEXPAND | wxALL, 4);

			sizer->Add(new HikariStaticText(this, -1, release.url),
				0, wxEXPAND | wxLEFT | wxRIGHT, 4);

			autoCheck = new HikariCheckBox(this, -1, _("Check for updates automatically"));
			autoCheck->SetValue(Options.GetBool(UPDATER_AUTO_CHECK));
			sizer->Add(autoCheck, 0, wxEXPAND | wxALL, 4);

			stableOnly = new HikariCheckBox(this, -1, _("Stable versions only"));
			stableOnly->SetValue(Options.GetBool(UPDATER_CHECK_FOR_STABLE));
			sizer->Add(stableOnly, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

			progress = new HikariStaticText(this, -1, wxString());
			sizer->Add(progress, 0, wxEXPAND | wxLEFT | wxRIGHT, 4);
			progress->Hide();

			wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
			// A MappedButton rather than wxHyperlinkCtrl: nothing else in the
			// UI is a stock control, so a stock one ignores the theme.
			bool install = offer.type == update_manifest::OfferType::Install;
			primary = new MappedButton(this, install ? kInstallId : kOpenId,
				install ? _("Download and install") : _("Open the download page"));
			MappedButton *later = new MappedButton(this, kRemindId, _("Remind me in a week"));
			MappedButton *close = new MappedButton(this, wxID_CLOSE, _("Close"));
			buttons->Add(primary, 1, wxALL, 2);
			buttons->Add(later, 1, wxALL, 2);
			buttons->Add(close, 1, wxALL, 2);
			sizer->Add(buttons, 0, wxEXPAND | wxALL, 2);

			wxString url = release.url;
			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [url](wxCommandEvent &) {
				OpenReleasePage(url);
			}, kOpenId);
			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent &) {
				StartDownload();
			}, kInstallId);
			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent &) {
				EndModal(kRemindId);
			}, kRemindId);
			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent &) {
				EndModal(wxID_CLOSE);
			}, wxID_CLOSE);

			SetEscapeId(wxID_CLOSE);
			SetSizerAndFit(sizer);
		}

		~UpdateAvailableDialog()
		{
			if (listenerId)
				update_installer::Detach(listenerId);
		}

		// A download asked for here is not a decline, however the dialog
		// closes: it goes on, and "Update ready" follows.
		void SaveChoices(int result)
		{
			if (downloading || result == kInstalledId)
				Options.SetString(UPDATER_DECLINED_VERSION, wxString());
			else
				SaveOfferChoice(result, tag);
			Options.SetBool(UPDATER_AUTO_CHECK, autoCheck->GetValue());
			Options.SetBool(UPDATER_CHECK_FOR_STABLE, stableOnly->GetValue());
			Options.SaveOptions(true, false);
		}

	private:
		void StartDownload()
		{
			downloading = true;
			primary->Hide();
			progress->SetLabelText(_("Downloading..."));
			progress->Show();
			Layout();
			update_installer::Listener listener;
			listener.progress = [this](long long received, long long total) {
				if (total > 0)
					progress->SetLabelText(wxString::Format(_("Downloading... %d%%"),
						static_cast<int>(received * 100 / total)));
			};
			listener.done = [this](bool ok, const wxString &error) {
				listenerId = 0;
				if (ok) {
					EndModal(kInstalledId);
					return;
				}
				downloading = false;
				progress->SetLabelText(error.empty() ? _("Cannot download the update") : error);
				primary->Show();
				Layout();
			};
			listenerId = update_installer::Download(frame, target, listener);
		}

		HikariCheckBox *autoCheck;
		HikariCheckBox *stableOnly;
		HikariStaticText *progress;
		MappedButton *primary;
		wxString tag;
		wxWindow *frame;
		update_installer::Target target;
		int listenerId = 0;
		bool downloading = false;
	};

	// The notice of an AUR install (installers spec, 4.5): it is updated
	// with the package manager, so nothing is downloaded or opened but the
	// release's page.
	class AurNoticeDialog : public HikariDialog
	{
	public:
		AurNoticeDialog(wxWindow *parent, const ReleaseInfo &release, bool prerelease)
			: HikariDialog(parent, -1, _("A new version is available"))
			, tag(release.tag)
		{
			wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
			HikariStaticText *title = new HikariStaticText(this, -1, _("A new version is available"));
			title->SetFont(title->GetFont().Bold());
			sizer->Add(title, 0, wxEXPAND | wxALL, 4);
			wxString version = release.tag.Mid(1);
			wxString body = prerelease ?
				wxString::Format(_("HikariSub %s is a prerelease. The AUR package carries stable versions only. To try it, use the AppImage from the release page."),
					version) :
				wxString::Format(_("HikariSub %s is available; you have %s. You installed HikariSub from the AUR, so update it with your package manager or AUR helper, for example yay -Syu or paru -Syu."),
					version, VersionHikariSub);
			sizer->Add(new HikariStaticText(this, -1, body), 0, wxEXPAND | wxALL, 4);

			wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
			buttons->Add(new MappedButton(this, kOpenId, _("What's new")), 1, wxALL, 2);
			buttons->Add(new MappedButton(this, kRemindId, _("Remind me in a week")), 1, wxALL, 2);
			buttons->Add(new MappedButton(this, wxID_CLOSE, _("Close")), 1, wxALL, 2);
			sizer->Add(buttons, 0, wxEXPAND | wxALL, 2);

			wxString url = release.url;
			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [url](wxCommandEvent &) { OpenReleasePage(url); }, kOpenId);
			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent &) { EndModal(kRemindId); }, kRemindId);
			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent &) { EndModal(wxID_CLOSE); }, wxID_CLOSE);
			SetEscapeId(wxID_CLOSE);
			SetSizerAndFit(sizer);
		}

		void SaveChoices(int result)
		{
			SaveOfferChoice(result, tag);
			Options.SaveOptions(true, false);
		}

	private:
		wxString tag;
	};

	// One check: the request's handler, separate from the frame so that it
	// can be unbound with it. It asks for a page of the release list at a
	// time, keeps the details of only the candidates so far, then reads the
	// update manifest of each candidate that has one, highest first, until
	// one is for this codebase (installers spec, 2.4). It deletes itself
	// once the last request has ended. Its timer, started with the first
	// page, cancels the request in flight at kCheckDeadlineSeconds, for
	// the pages and the manifests alike, and CheckRun decides how the
	// check ends. The frame is held weakly. A reply that comes while it is
	// closing, or after, is still recorded, as the options outlive the
	// frame and the event loop that brings the reply is still running;
	// only what it found is not shown.
	class UpdateRequest : public wxEvtHandler
	{
	public:
		explicit UpdateRequest(wxWindow *parent)
			: parent(parent)
			, pages(VersionHikariSub, Options.GetBool(UPDATER_CHECK_FOR_STABLE))
			, deadline(this)
		{
			Bind(wxEVT_WEBREQUEST_DATA, &UpdateRequest::OnData, this);
			Bind(wxEVT_WEBREQUEST_STATE, &UpdateRequest::OnState, this);
			Bind(wxEVT_TIMER, &UpdateRequest::OnDeadline, this, deadline.GetId());
		}

		// Asks for a page of the release list, from 1.
		bool Start(int page = 1)
		{
			if (!StartRequest(ReleasesUrl(page), L"application/vnd.github+json"))
				return false;
			// Once for the check, not again for each page.
			if (page == 1)
				deadline.StartOnce(static_cast<int>(update_policy::kCheckDeadlineSeconds * 1000));
			request.Start();
			return true;
		}

	private:
		enum class Phase
		{
			Pages,
			Manifests,
		};

		bool StartRequest(const std::string &url, const wxString &accept)
		{
			// The session is not opened when the platform's backend cannot
			// start, and CreateRequest() would then assert.
			wxWebSession &session = wxWebSession::GetDefault();
			if (url.empty() || !session.IsOpened())
				return false;
			body.clear();
			request = session.CreateRequest(this, wxString::FromUTF8(url));
			if (!request.IsOk())
				return false;

			request.SetHeader(L"Accept", accept);
			request.SetHeader(L"X-GitHub-Api-Version", L"2022-11-28");
			// GitHub refuses a request without one. The version is left
			// out: the server has no use for it.
			request.SetHeader(L"User-Agent", L"HikariSub");
			// The reply comes in pieces through OnData, so that it can be
			// cut off at kMaxResponseBytes, or a manifest's at
			// kMaxManifestBytes.
			request.SetStorage(wxWebRequest::Storage_None);
			request.SetTimeouts(update_policy::kConnectTimeoutMs, update_policy::kDataTimeoutMs);
			// A request that cannot start still ends in State_Failed,
			// which OnState handles.
			return true;
		}

		// The backends' timeouts do not bound the whole check, as
		// WinHTTP's data timeout is a read's, so the check does. A cancel
		// ends the request in State_Cancelled, and OnState() ends the check
		// as TimedOut: curl's backend sets it at once, and WinHTTP's when
		// the read in flight is aborted. wx 3.3.3's WinHTTP backend sends
		// nothing if the cancel comes while it takes in a piece of the
		// reply; the check then ends on the timer's second firing, and the
		// handler is kept, as such a request may still send it an event,
		// until that event comes (CheckRun::LateEventReleases()).
		// curl's backend changes the state on the main thread, where it is
		// read here; WinHTTP's on its own, and a cancel of a request that
		// has just ended there only closes its handle.
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
				// The handler waits for the request's last event; what it
				// took in of the reply is not kept meanwhile.
				body.clear();
				body.shrink_to_fit();
				if (Finish(update_policy::Ending::TimedOut))
					Release();
				break;
			}
		}

		void OnData(wxWebRequestEvent &evt)
		{
			if (!run.TakesData())
				return;
			size_t size = evt.GetDataSize();
			bool fits = phase == Phase::Pages ? update_policy::FitsReply(body.size(), size) :
				body.size() <= update_manifest::kMaxManifestBytes &&
				size <= update_manifest::kMaxManifestBytes - body.size();
			if (!fits) {
				body.clear();
				body.shrink_to_fit();
				if (run.CutOff(request.GetState() == wxWebRequest::State_Active))
					request.Cancel();
				return;
			}
			body.append(static_cast<const char *>(evt.GetDataBuffer()), size);
		}

		void OnState(wxWebRequestEvent &evt)
		{
			wxWebRequest::State state = evt.GetState();
			if (state == wxWebRequest::State_Active || state == wxWebRequest::State_Idle)
				return;
			std::optional<update_policy::Ending> pageEnding = run.PageEnded(
				state == wxWebRequest::State_Completed ? update_policy::RequestEnd::Completed :
				state == wxWebRequest::State_Cancelled ? update_policy::RequestEnd::Cancelled :
				update_policy::RequestEnd::Failed);
			if (!pageEnding) {
				// The last event of a check that has ended without it.
				if (run.LateEventReleases())
					Release();
				return;
			}

			update_policy::Ending ending = *pageEnding;
			if (phase == Phase::Manifests) {
				if (ending == update_policy::Ending::TimedOut) {
					run.Finish();
					Finish(ending);
					Release();
					return;
				}
				// A manifest that did not come whole, from GitHub's
				// release files, rules its release out; a cut-off one too.
				std::string url = ReplyUrl(evt);
				bool came = ending == update_policy::Ending::Answered && evt.GetResponse().GetStatus() == 200 &&
					update_installer::IsAssetReplyUrl(url);
				const update_policy::Release &release = pages.Releases()[walk->Current()];
				Consider(update_manifest::Admit(release, came ? &body : nullptr));
				body.clear();
				body.shrink_to_fit();
				if (Walk())
					return;
				run.Finish();
				Finish(timedOut ? update_policy::Ending::TimedOut : update_policy::Ending::Answered);
				Release();
				return;
			}

			if (ending == update_policy::Ending::Answered)
				ending = ReadPage(ReplyUrl(evt));
			if (ending == update_policy::Ending::Answered) {
				// The next page only while the window is open, as nothing
				// would be shown; a check that so stops short of the end
				// of the list is no answer (GetEnding()). A page that
				// cannot be asked for, past the deadline too, fails the
				// check, as one that fails to come does, rather than have
				// it choose from part of the list.
				if (pages.ReadsNextPage() && IsParentOpen()) {
					if (!run.AsksNextPage())
						ending = update_policy::Ending::TimedOut;
					else if (Start(pages.NextPage()))
						return;
					else
						ending = update_policy::Ending::NoRequest;
				}
				else {
					ending = pages.GetEnding();
				}
			}
			if (ending == update_policy::Ending::Answered) {
				// The list is read: on to the candidates' manifests.
				phase = Phase::Manifests;
				walk.emplace(update_policy::ListCandidates(pages.Releases(), pages.Installed(), pages.StableOnly()));
				if (Walk())
					return;
				if (timedOut)
					ending = update_policy::Ending::TimedOut;
			}
			run.Finish();
			Finish(ending);
			// Last, as nothing of this object may be used after it.
			Release();
		}

		// What the current candidate offers, or nothing, and on to the
		// next.
		void Consider(std::optional<update_manifest::Manifest> manifest)
		{
			if (!manifest) {
				walk->Skip();
				return;
			}
			found = walk->Current();
			foundSkipped = walk->CurrentIsSkipped();
			foundManifest = std::move(*manifest);
			walk->Take();
		}

		// Looks at the candidates in turn. True while a manifest is being
		// asked for; false once the walk is over, timedOut set when the
		// deadline stopped it.
		bool Walk()
		{
			while (walk->Current() >= 0) {
				const update_policy::Release &release = pages.Releases()[walk->Current()];
				if (!update_manifest::HasManifest(release)) {
					Consider(update_manifest::Admit(release, nullptr));
					continue;
				}
				// Nothing would be shown.
				if (!IsParentOpen())
					return false;
				if (!run.AsksNextPage()) {
					timedOut = true;
					return false;
				}
				std::string version = update_policy::CandidateVersion(release.tag);
				run.NextRequest();
				if (StartRequest(update_installer::AssetUrl(version, std::string(update_manifest::kManifestName)),
					L"application/json"))
				{
					request.Start();
					return true;
				}
				walk->Skip();
			}
			return false;
		}

		// Records how the check ended, and shows it. The timer is stopped
		// before Report(), whose dialogs run the event loop, so that it
		// does not fire while one shows; a state event that comes then is
		// ignored, as the check has ended (CheckRun). Whether the request's
		// last event came while the result showed.
		bool Finish(update_policy::Ending ending)
		{
			deadline.Stop();
			tracker.End(Now(), ending);
			run.ReportBegins();
			if (IsParentOpen())
				Report(ending);
			tracker.Done();
			return run.ReportEnds();
		}

		// Deletes this handler once the event loop is idle, once only.
		void Release()
		{
			if (released)
				return;
			released = true;
			wxTheApp->ScheduleForDestruction(this);
		}

		static std::string ReplyUrl(const wxWebRequestEvent &evt)
		{
			const wxWebResponse &response = evt.GetResponse();
			if (evt.GetState() != wxWebRequest::State_Completed || !response.IsOk())
				return std::string();
			return response.GetURL().utf8_string();
		}

		// Reads the page of a request that completed into pages, and the
		// details of any candidate on it.
		update_policy::Ending ReadPage(const std::string &url)
		{
			if (!IsReleasesReplyUrl(url))
				return update_policy::Ending::NotFromApi;

			JsonValue root = JsonValue::Parse(body);
			body.clear();
			body.shrink_to_fit();
			std::vector<update_policy::Release> releases;
			if (!ReadReleases(root, &releases))
				return update_policy::Ending::NotAList;
			// ReadReleases() reads one entry for each of root's items, so
			// an index on the page is one into root.Items().
			pages.Add(releases);
			details.Keep(pages, [&root](size_t index) {
				return GetReleaseInfo(root.Items()[index]);
			});
			return update_policy::Ending::Answered;
		}

		bool IsParentOpen() const
		{
			wxWindow *window = parent;
			return window && !window->IsBeingDeleted() &&
				!wxTheApp->IsScheduledForDestruction(window);
		}

		void Report(update_policy::Ending ending)
		{
			bool interactive = tracker.Reports();
			if (ending != update_policy::Ending::Answered) {
				if (interactive)
					HikariMessageBox(_("Cannot check for updates"), _("Update"));
				return;
			}

			const ReleaseInfo *info = found >= 0 ? details.Find(found) : nullptr;
			if (!info) {
				if (interactive)
					HikariMessageBox(_("You already have the latest version"), _("Update"));
				return;
			}
			if (foundSkipped) {
				if (interactive)
					ShowNewerPrerelease(parent, *info);
				return;
			}
			if (!interactive && update_policy::WasDeclined(info->tag.utf8_string(),
				Options.GetString(UPDATER_DECLINED_VERSION).utf8_string()))
			{
				return;
			}

			const update_policy::Release &release = pages.Releases()[found];
			std::string version = update_policy::CandidateVersion(release.tag);
			package_kind::Kind kind = update_installer::Kind();
			bool autoInstall = Options.GetBool(UPDATER_AUTO_INSTALL) && update_installer::CanInstall();
			update_manifest::Offer offer = update_manifest::Decide(kind, autoInstall, foundManifest,
				update_installer::GetSystemInfo());
			switch (offer.type) {
			case update_manifest::OfferType::AurNotice: {
				AurNoticeDialog dialog(parent, *info, update_manifest::IsPrerelease(release));
				dialog.SaveChoices(dialog.ShowModal());
				return;
			}
			case update_manifest::OfferType::Install:
				// Already downloaded and waiting: its notice shows at
				// each start, and at the next quit it is installed.
				if (update_installer::IsPending(version) && !interactive)
					return;
				if (!interactive) {
					update_installer::Download(parent, update_installer::Target{ version, offer.package });
					return;
				}
				break;
			case update_manifest::OfferType::Notify:
			case update_manifest::OfferType::NotifyRequirement:
				break;
			}
			UpdateAvailableDialog dialog(parent, *info, offer, version);
			dialog.SaveChoices(dialog.ShowModal());
		}

		wxWeakRef<wxWindow> parent;
		wxWebRequest request;
		std::string body;
		update_policy::CheckRun run;
		update_policy::ReleasePages pages;
		update_policy::CandidateDetails<ReleaseInfo> details;
		Phase phase = Phase::Pages;
		std::optional<update_manifest::CandidateWalk> walk;
		int found = -1;
		bool foundSkipped = false;
		update_manifest::Manifest foundManifest;
		bool timedOut = false;
		// Stopped by its destructor, so it never fires at a deleted check.
		wxTimer deadline;
		bool released = false;
	};

	void Check(wxWindow *parent, bool interactive)
	{
		if (tracker.Begin(Now(), interactive) != update_policy::Start::Started)
			return;

		UpdateRequest *check = new UpdateRequest(parent);
		if (!check->Start()) {
			delete check;
			tracker.End(Now(), update_policy::Ending::NoRequest);
			if (tracker.Reports())
				HikariMessageBox(_("Cannot check for updates"), _("Update"));
			tracker.Done();
		}
	}
}

void UpdateChecker::CheckOnStartup(wxWindow *parent)
{
	if (!Options.GetBool(UPDATER_AUTO_CHECK))
		return;
	if (!update_policy::IsCheckDue(Now(), LoadNextCheck()))
		return;

	Check(parent, false);
}

void UpdateChecker::CheckNow(wxWindow *parent)
{
	Check(parent, true);
}
