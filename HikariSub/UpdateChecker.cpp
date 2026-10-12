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

	// Sets the next automatic check and writes it out at once, so that a
	// crash or a kill does not leave the next start to check again.
	void SaveNextCheck(long long next)
	{
		Options.SetInt(UPDATER_NEXT_CHECK, static_cast<int>(next));
		Options.SaveOptions(true, false);
	}

	void RecordCheck(bool answered)
	{
		SaveNextCheck(update_policy::NextCheck(time(nullptr), Options.GetInt(UPDATER_NEXT_CHECK), answered));
	}

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
				Options.SetInt(UPDATER_NEXT_CHECK, static_cast<int>(time(nullptr) + update_policy::kWeek));
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
	// can be unbound with it. It deletes itself once the request has ended.
	// The frame is held weakly; a reply that comes while it is closing, or
	// after, is dropped without a message or a write to the options, which
	// have been saved for the last time by then; the hour's back-off that
	// Check() recorded stands.
	class UpdateRequest : public wxEvtHandler
	{
	public:
		UpdateRequest(wxWindow *parent, bool interactive)
			: parent(parent)
			, interactive(interactive)
		{
			Bind(wxEVT_WEBREQUEST_DATA, &UpdateRequest::OnData, this);
			Bind(wxEVT_WEBREQUEST_STATE, &UpdateRequest::OnState, this);
		}

		bool Start()
		{
			// The session is not opened when the platform's backend cannot
			// start, and CreateRequest() would then assert.
			wxWebSession &session = wxWebSession::GetDefault();
			if (!session.IsOpened())
				return false;
			request = session.CreateRequest(this, wxString::FromUTF8(update_policy::ReleasesApiUrl()));
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
			// A request that cannot start still ends in State_Failed,
			// which OnState handles.
			request.Start();
			return true;
		}

	private:
		void OnData(wxWebRequestEvent &evt)
		{
			if (tooLarge)
				return;
			size_t size = evt.GetDataSize();
			if (size > update_policy::kMaxResponseBytes - body.size()) {
				tooLarge = true;
				body.clear();
				body.shrink_to_fit();
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

			if (IsParentOpen())
				Finish(state == wxWebRequest::State_Completed && !tooLarge);
			// Last, as nothing of this object may be used after it.
			wxTheApp->ScheduleForDestruction(this);
		}

		bool IsParentOpen() const
		{
			wxWindow *window = parent;
			return window && !window->IsBeingDeleted() &&
				!wxTheApp->IsScheduledForDestruction(window);
		}

		void Fail()
		{
			RecordCheck(false);
			if (interactive)
				HikariMessageBox(_("Cannot check for updates"), _("Update"));
		}

		void Finish(bool received)
		{
			if (!received) {
				Fail();
				return;
			}

			JsonValue root = JsonValue::Parse(body);
			body.clear();
			body.shrink_to_fit();
			std::vector<update_policy::Release> releases;
			if (!ReadReleases(root, &releases)) {
				Fail();
				return;
			}
			RecordCheck(true);

			update_policy::Choice choice = update_policy::ChooseRelease(releases, VersionHikariSub,
				Options.GetBool(UPDATER_CHECK_FOR_STABLE));
			switch (update_policy::GetOutcome(choice)) {
			case update_policy::Outcome::Offer: {
				ReleaseInfo release = GetReleaseInfo(root.Items()[choice.offer]);
				if (!interactive && update_policy::WasDeclined(release.tag.utf8_string(),
					Options.GetString(UPDATER_DECLINED_VERSION).utf8_string()))
				{
					break;
				}
				UpdateAvailableDialog dialog(parent, release);
				dialog.SaveChoices(dialog.ShowModal());
				break;
			}
			case update_policy::Outcome::OnlyPrereleaseNewer:
				if (interactive)
					ShowNewerPrerelease(parent, GetReleaseInfo(root.Items()[choice.skippedPrerelease]));
				break;
			case update_policy::Outcome::UpToDate:
				if (interactive)
					HikariMessageBox(_("You already have the latest version"), _("Update"));
				break;
			}
		}

		wxWeakRef<wxWindow> parent;
		bool interactive;
		wxWebRequest request;
		std::string body;
		bool tooLarge = false;
	};

	void Check(wxWindow *parent, bool interactive)
	{
		// Recorded as failed first, so that no way out of the check skips
		// the back-off: no web session or request, a reply dropped as the
		// window closes, a kill. Fail() and Finish() record it again when
		// the request ends.
		SaveNextCheck(update_policy::NextCheckAtStart(time(nullptr), Options.GetInt(UPDATER_NEXT_CHECK)));
		UpdateRequest *check = new UpdateRequest(parent, interactive);
		if (!check->Start()) {
			delete check;
			if (interactive)
				HikariMessageBox(_("Cannot check for updates"), _("Update"));
		}
	}
}

void UpdateChecker::CheckOnStartup(wxWindow *parent)
{
	if (!Options.GetBool(UPDATER_AUTO_CHECK))
		return;
	if (!update_policy::IsCheckDue(time(nullptr), Options.GetInt(UPDATER_NEXT_CHECK)))
		return;

	Check(parent, false);
}

void UpdateChecker::CheckNow(wxWindow *parent)
{
	Check(parent, true);
}
