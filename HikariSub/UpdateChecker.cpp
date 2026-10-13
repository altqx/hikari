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
#include "SemVer.h"
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

	struct ReleaseInfo
	{
		wxString tag;
		wxString name;
		wxString url;
		wxString notes;
	};

	// The list endpoint is used rather than /releases/latest because latest
	// hides prereleases outright, which would leave the "stable only" option
	// with nothing to do. False when the reply is not a release list.
	bool ReadReleases(const JsonValue &root, std::vector<update_policy::Release> *releases)
	{
		if (root.GetType() != JsonValue::Type::Array)
			return false;

		for (const JsonValue &release : root.Items()) {
			update_policy::Release entry;
			entry.tag = release.GetString("tag_name").utf8_string();
			entry.draft = release.GetBool("draft");
			entry.prerelease = release.GetBool("prerelease");
			releases->push_back(entry);
		}
		return true;
	}

	// Only the release that is shown is read in full. The page is built
	// from the tag; the reply's html_url is not used.
	ReleaseInfo GetReleaseInfo(const JsonValue &release)
	{
		ReleaseInfo info;
		info.tag = release.GetString("tag_name");
		info.name = release.GetString("name", info.tag);
		info.url = wxString::FromUTF8(update_policy::ReleasePageUrl(info.tag.utf8_string()));
		info.notes = release.GetString("body");
		return info;
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

	class UpdateAvailableDialog : public HikariDialog
	{
	public:
		UpdateAvailableDialog(wxWindow *parent, const ReleaseInfo &release)
			: HikariDialog(parent, -1, _("A new version is available"))
			, tag(release.tag)
		{
			wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

			HikariStaticText *title = new HikariStaticText(this, -1, release.name);
			title->SetFont(title->GetFont().Bold());
			sizer->Add(title, 0, wxEXPAND | wxALL, 4);

			sizer->Add(new HikariStaticText(this, -1,
				wxString::Format(_("You have version %s; version %s is available"),
					VersionHikariSub, release.tag)),
				0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

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

			wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
			// A MappedButton rather than wxHyperlinkCtrl: nothing else in the
			// UI is a stock control, so a stock one ignores the theme.
			MappedButton *open = new MappedButton(this, kOpenId, _("Open the download page"));
			MappedButton *later = new MappedButton(this, kRemindId, _("Remind me in a week"));
			MappedButton *close = new MappedButton(this, wxID_CLOSE, _("Close"));
			buttons->Add(open, 1, wxALL, 2);
			buttons->Add(later, 1, wxALL, 2);
			buttons->Add(close, 1, wxALL, 2);
			sizer->Add(buttons, 0, wxEXPAND | wxALL, 2);

			wxString url = release.url;
			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [url](wxCommandEvent &) {
				OpenReleasePage(url);
			}, kOpenId);
			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent &) {
				EndModal(kRemindId);
			}, kRemindId);
			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent &) {
				EndModal(wxID_CLOSE);
			}, wxID_CLOSE);

			SetEscapeId(wxID_CLOSE);
			SetSizerAndFit(sizer);
		}

		// "Remind me in a week" puts the next automatic check off a week.
		// Closing the dialog any other way declines this version: the
		// startup check keeps quiet about it and offers the next one, and a
		// check from the menu still offers it.
		void SaveChoices(int result)
		{
			if (result == kRemindId) {
				SetNextCheck(Now() + update_policy::kWeek);
				Options.SetString(UPDATER_DECLINED_VERSION, wxString());
			}
			else {
				Options.SetString(UPDATER_DECLINED_VERSION, tag);
			}
			Options.SetBool(UPDATER_AUTO_CHECK, autoCheck->GetValue());
			Options.SetBool(UPDATER_CHECK_FOR_STABLE, stableOnly->GetValue());
			Options.SaveOptions(true, false);
		}

	private:
		HikariCheckBox *autoCheck;
		HikariCheckBox *stableOnly;
		wxString tag;
	};

	// One check: the request's handler, separate from the frame so that it
	// can be unbound with it. It asks for a page of the release list at a
	// time, keeps the details of only the releases chosen so far, and
	// deletes itself once the last request has ended. Its timer, started
	// with the first page, cancels the request in flight at
	// kCheckDeadlineSeconds, and CheckRun decides how the check ends.
	// The frame is held weakly. A reply that comes while it is closing, or
	// after, is still recorded, as the options outlive the frame and the
	// event loop that brings the reply is still running; only what it
	// found is not shown.
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
			// The session is not opened when the platform's backend cannot
			// start, and CreateRequest() would then assert.
			wxWebSession &session = wxWebSession::GetDefault();
			if (!session.IsOpened())
				return false;
			body.clear();
			request = session.CreateRequest(this, wxString::FromUTF8(update_policy::ReleasesApiUrl(page)));
			if (!request.IsOk())
				return false;

			request.SetHeader(L"Accept", L"application/vnd.github+json");
			request.SetHeader(L"X-GitHub-Api-Version", L"2022-11-28");
			// GitHub refuses a request without one. The version is left
			// out: the server has no use for it.
			request.SetHeader(L"User-Agent", L"HikariSub");
			// The reply comes in pieces through OnData, so that it can be
			// cut off at kMaxResponseBytes.
			request.SetStorage(wxWebRequest::Storage_None);
			request.SetTimeouts(update_policy::kConnectTimeoutMs, update_policy::kDataTimeoutMs);
			// Once for the check, not again for each page.
			if (page == 1)
				deadline.StartOnce(static_cast<int>(update_policy::kCheckDeadlineSeconds * 1000));
			// A request that cannot start still ends in State_Failed,
			// which OnState handles.
			request.Start();
			return true;
		}

	private:
		// The backends' timeouts do not bound the whole check, as
		// WinHTTP's data timeout is a read's, so the check does. A cancel
		// ends the request in State_Cancelled, and OnState() ends the check
		// as TimedOut: curl's backend sets it at once, and WinHTTP's when
		// the read in flight is aborted. wx 3.3.4's WinHTTP backend sends
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
			if (!update_policy::FitsReply(body.size(), size)) {
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
			run.Finish();
			Finish(ending);
			// Last, as nothing of this object may be used after it.
			Release();
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
		// details of any release chosen from it.
		update_policy::Ending ReadPage(const std::string &url)
		{
			if (!update_policy::IsApiReplyUrl(url))
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
			chosen.Keep(pages, [&root](size_t index) {
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

			switch (update_policy::GetOutcome(pages.GetChoice())) {
			case update_policy::Outcome::Offer: {
				if (!interactive && update_policy::WasDeclined(chosen.offer.tag.utf8_string(),
					Options.GetString(UPDATER_DECLINED_VERSION).utf8_string()))
				{
					break;
				}
				UpdateAvailableDialog dialog(parent, chosen.offer);
				dialog.SaveChoices(dialog.ShowModal());
				break;
			}
			case update_policy::Outcome::OnlyPrereleaseNewer:
				if (interactive)
					ShowNewerPrerelease(parent, chosen.skippedPrerelease);
				break;
			case update_policy::Outcome::UpToDate:
				if (interactive)
					HikariMessageBox(_("You already have the latest version"), _("Update"));
				break;
			}
		}

		wxWeakRef<wxWindow> parent;
		wxWebRequest request;
		std::string body;
		update_policy::CheckRun run;
		update_policy::ReleasePages pages;
		update_policy::ChosenDetails<ReleaseInfo> chosen;
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
