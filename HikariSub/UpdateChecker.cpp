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
#include <wx/sizer.h>
#include <wx/utils.h>
#include <wx/webrequest.h>
#include <ctime>
#include <vector>

namespace
{
	constexpr std::optional<SemVer> kVersion = ParseSemVer(VersionHikariSub);
	constexpr long long kNumVersion[] = { NumVersionHikariSub };
	static_assert(kVersion, "VersionHikariSub is not a semantic version");
	static_assert(kNumVersion[0] == kVersion->major && kNumVersion[1] == kVersion->minor &&
		kNumVersion[2] == kVersion->patch && kNumVersion[3] == 0,
		"NumVersionHikariSub does not match VersionHikariSub");

	const wxString kReleasesUrl = wxString::FromUTF8(update_policy::ReleasesApiUrl());

	constexpr time_t kDay  = 24 * 60 * 60;
	constexpr time_t kWeek = 7 * kDay;

	struct ReleaseInfo
	{
		wxString tag;
		wxString name;
		wxString url;
		wxString notes;
	};

	// The list endpoint is used rather than /releases/latest because latest
	// hides prereleases outright, which would leave the "stable only" option
	// with nothing to do. False when the body is not a release list.
	bool ReadReleases(const wxString &body, std::vector<update_policy::Release> *releases,
		std::vector<ReleaseInfo> *infos)
	{
		JsonValue root = JsonValue::Parse(std::string(body.utf8_str()));
		if (root.GetType() != JsonValue::Type::Array)
			return false;

		for (const JsonValue &release : root.Items()) {
			wxString tag = release.GetString("tag_name");
			update_policy::Release entry;
			entry.tag = tag.utf8_string();
			entry.draft = release.GetBool("draft");
			entry.prerelease = release.GetBool("prerelease");
			releases->push_back(entry);

			ReleaseInfo info;
			info.tag = tag;
			info.name = release.GetString("name", tag);
			// Built from the tag; the reply's html_url is not used.
			info.url = wxString::FromUTF8(update_policy::ReleasePageUrl(tag.utf8_string()));
			info.notes = release.GetString("body");
			infos->push_back(info);
		}
		return true;
	}

	// Says that only a prerelease is newer, and offers to open its page.
	void ShowNewerPrerelease(wxWindow *parent, const ReleaseInfo &release)
	{
		HikariMessageDialog message(parent,
			wxString::Format(_("You have the latest stable version. A newer prerelease, %s, is available."),
				release.tag), _("Update"), wxYES_NO);
		message.SetYesLabel(_("Open the download page"));
		message.SetNoLabel(_("Close"));
		if ((message.ShowModal() & wxYES) && !release.url.empty())
			wxLaunchDefaultBrowser(release.url);
	}

	class UpdateAvailableDialog : public HikariDialog
	{
	public:
		UpdateAvailableDialog(wxWindow *parent, const ReleaseInfo &release)
			: HikariDialog(parent, -1, _("A new version is available"))
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
			MappedButton *open = new MappedButton(this, 20001, _("Open the download page"));
			MappedButton *later = new MappedButton(this, 20002, _("Remind me in a week"));
			MappedButton *close = new MappedButton(this, wxID_CLOSE, _("Close"));
			buttons->Add(open, 1, wxALL, 2);
			buttons->Add(later, 1, wxALL, 2);
			buttons->Add(close, 1, wxALL, 2);
			sizer->Add(buttons, 0, wxEXPAND | wxALL, 2);

			url = release.url;
			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent &) {
				if (!url.empty())
					wxLaunchDefaultBrowser(url);
			}, 20001);
			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent &) {
				Options.SetInt(UPDATER_NEXT_CHECK, (int)(time(nullptr) + kWeek));
				EndModal(wxID_CLOSE);
			}, 20002);
			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent &) {
				EndModal(wxID_CLOSE);
			}, wxID_CLOSE);

			SetEscapeId(wxID_CLOSE);
			SetSizerAndFit(sizer);
		}

		void SaveChoices()
		{
			Options.SetBool(UPDATER_AUTO_CHECK, autoCheck->GetValue());
			Options.SetBool(UPDATER_CHECK_FOR_STABLE, stableOnly->GetValue());
			Options.SaveOptions(true, false);
		}

	private:
		HikariCheckBox *autoCheck;
		HikariCheckBox *stableOnly;
		wxString url;
	};

	void Check(wxWindow *parent, bool interactive)
	{
		// Each check's handler stays bound to the parent, and the one bound
		// last takes every event it matches without passing it on. A request
		// id of its own keeps a reply to its own check: with wxID_ANY, a
		// startup check still waiting when the menu check starts was answered
		// as the menu check, with its messages.
		const int id = wxWindow::NewControlId();
		wxWebRequest request = wxWebSession::GetDefault().CreateRequest(parent, kReleasesUrl, id);
		if (!request.IsOk()) {
			if (interactive)
				HikariMessageBox(_("Cannot check for updates"), _("Update"));
			return;
		}

		request.SetHeader(L"Accept", L"application/vnd.github+json");
		request.SetHeader(L"X-GitHub-Api-Version", L"2022-11-28");
		request.SetHeader(L"User-Agent", wxString::Format(L"HikariSub/%s", VersionHikariSub));

		parent->Bind(wxEVT_WEBREQUEST_STATE, [parent, interactive](wxWebRequestEvent &evt) {
			if (evt.GetState() == wxWebRequest::State_Active ||
				evt.GetState() == wxWebRequest::State_Idle)
			{
				return;
			}

			// An automatic check backs off a day whatever happened, so a
			// server that is down does not mean a request on every start.
			Options.SetInt(UPDATER_NEXT_CHECK, (int)(time(nullptr) + kDay));

			if (evt.GetState() != wxWebRequest::State_Completed) {
				if (interactive)
					HikariMessageBox(_("Cannot check for updates"), _("Update"));
				return;
			}

			std::vector<update_policy::Release> releases;
			std::vector<ReleaseInfo> infos;
			if (!ReadReleases(evt.GetResponse().AsString(), &releases, &infos)) {
				if (interactive)
					HikariMessageBox(_("Cannot check for updates"), _("Update"));
				return;
			}

			update_policy::Choice choice = update_policy::ChooseRelease(releases, VersionHikariSub,
				Options.GetBool(UPDATER_CHECK_FOR_STABLE));
			switch (update_policy::GetOutcome(choice)) {
			case update_policy::Outcome::Offer: {
				UpdateAvailableDialog dialog(parent, infos[choice.offer]);
				dialog.ShowModal();
				dialog.SaveChoices();
				break;
			}
			case update_policy::Outcome::OnlyPrereleaseNewer:
				if (interactive)
					ShowNewerPrerelease(parent, infos[choice.skippedPrerelease]);
				break;
			case update_policy::Outcome::UpToDate:
				if (interactive)
					HikariMessageBox(_("You already have the latest version"), _("Update"));
				break;
			}
		}, id);

		request.Start();
	}
}

void UpdateChecker::CheckOnStartup(wxWindow *parent)
{
	if (!Options.GetBool(UPDATER_AUTO_CHECK))
		return;
	if (time(nullptr) < (time_t)Options.GetInt(UPDATER_NEXT_CHECK))
		return;

	Check(parent, false);
}

void UpdateChecker::CheckNow(wxWindow *parent)
{
	Check(parent, true);
}
