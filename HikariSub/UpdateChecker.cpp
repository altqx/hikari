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
	// The offer of the rewrite's buttons, and its results.
	constexpr int kTryId = 20005;
	constexpr int kRemindLaterId = 20006;
	constexpr int kNoOfferId = 20007;
	// Tried: downloaded, or its page opened.
	constexpr int kTriedId = 20008;
	// Tried by an AUR install, which is told to use its package manager.
	constexpr int kTriedAurId = 20009;

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

	// "Remind me later" of the rewrite's offer: when it shows again, or 0.
	long long LoadOfferRemind()
	{
		return update_policy::ParseNextCheck(Options.GetString(UPDATER_OFFER_REMIND).utf8_string());
	}

	void SetOfferRemind(long long remindAt)
	{
		Options.SetString(UPDATER_OFFER_REMIND,
			remindAt > 0 ? wxString::FromUTF8(update_policy::FormatNextCheck(remindAt)) : wxString());
	}

	// What "Don't offer again" of the rewrite's offer declined.
	update_manifest::DeclinedOffer LoadOfferDeclined()
	{
		return { Options.GetString(UPDATER_OFFER_DECLINED).utf8_string(),
			update_policy::ReadOption(Options.GetString(UPDATER_OFFER_DECLINED_ENDED).utf8_string(), false) };
	}

	void SetOfferDeclined(const update_manifest::DeclinedOffer &declined)
	{
		Options.SetString(UPDATER_OFFER_DECLINED, wxString::FromUTF8(declined.version));
		Options.SetString(UPDATER_OFFER_DECLINED_ENDED, declined.ended ? L"true" : L"false");
	}

	wxString FromUtf8(const std::string &text)
	{
		return wxString::FromUTF8(text.c_str());
	}

	// Plain text broken at spaces into lines of about width characters,
	// as HikariStaticText does not wrap.
	wxString Wrap(const wxString &text, size_t width = 64)
	{
		wxString wrapped;
		size_t line = 0;
		wxString word;
		auto flush = [&] {
			if (word.empty())
				return;
			if (line > 0 && line + 1 + word.length() > width) {
				wrapped += L"\n";
				line = 0;
			}
			else if (line > 0) {
				wrapped += L" ";
				line++;
			}
			wrapped += word;
			line += word.length();
			word.clear();
		};
		for (wxUniChar c : text) {
			if (c == L' ') {
				flush();
			}
			else if (c == L'\n') {
				flush();
				wrapped += L"\n";
				line = 0;
			}
			else {
				word += c;
			}
		}
		flush();
		return wrapped;
	}


	// Says that only a prerelease is newer, and offers to open its page.
	void ShowNewerPrerelease(wxWindow *parent, const ReleaseInfo &release)
	{
		HikariMessageDialog message(parent,
			wxString::Format(_("You have the latest stable version. A newer prerelease, %s, is available."),
				release.tag), _("Update"), wxYES_NO);
		message.SetYesLabel(_("Open the download page"));
		message.SetNoLabel(_("Close"));
		update_installer::Present(&message, parent);
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
		// notCarried: a prerelease that the install's AUR package does not
		// carry (only hikarisub-beta-bin carries prereleases). rewrite: the
		// rewrite that the user chose to try, which has no reminder.
		AurNoticeDialog(wxWindow *parent, const ReleaseInfo &release, bool notCarried, bool rewrite = false)
			: HikariDialog(parent, -1, _("A new version is available"))
			, tag(release.tag)
		{
			wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
			HikariStaticText *title = new HikariStaticText(this, -1, _("A new version is available"));
			title->SetFont(title->GetFont().Bold());
			sizer->Add(title, 0, wxEXPAND | wxALL, 4);
			wxString version = release.tag.Mid(1);
			wxString body = notCarried ?
				wxString::Format(_("HikariSub %s is a prerelease. The AUR package carries stable versions only. To try it, use the AppImage from the release page."),
					version) :
				wxString::Format(_("HikariSub %s is available; you have %s. You installed HikariSub from the AUR, so update it with your package manager or AUR helper, for example yay -Syu or paru -Syu."),
					version, VersionHikariSub);
			sizer->Add(new HikariStaticText(this, -1, Wrap(body)), 0, wxEXPAND | wxALL, 4);

			wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
			buttons->Add(new MappedButton(this, kOpenId, _("What's new")), 1, wxALL, 2);
			if (!rewrite)
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


	// The rewrite's offer (ADR-0021): its pitch, in the interface language
	// or English, and Try it, Remind me later and Don't offer again. Closing
	// it otherwise is Remind me later. Nothing is downloaded before Try
	// it; then, as for an update of this kind, the package is downloaded
	// with its progress here and checked, and "Update ready" follows, or
	// the release page opens, or an AUR install is told to use its
	// package manager.
	class OfferDialog : public HikariDialog
	{
	public:
		OfferDialog(wxWindow *parent, const update_manifest::Manifest &manifest)
			: HikariDialog(parent, -1, _("The new HikariSub"))
			, frame(parent)
			, manifest(manifest)
		{
			const update_manifest::Pitch &pitch = update_manifest::PitchFor(*manifest.offer,
				Options.GetString(PROGRAM_LANGUAGE).utf8_string());
			wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

			HikariStaticText *title = new HikariStaticText(this, -1, Wrap(FromUtf8(pitch.title), 48));
			title->SetFont(title->GetFont().Bold().Larger());
			sizer->Add(title, 0, wxEXPAND | wxALL, 6);
			sizer->Add(new HikariStaticText(this, -1,
				wxString::Format(_("Version %s. You have HikariSub %s."), FromUtf8(manifest.version),
					VersionHikariSub)), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
			sizer->AddSpacer(6);
			for (const std::string &highlight : pitch.highlights) {
				sizer->Add(new HikariStaticText(this, -1, Wrap(L"\u2022 " + FromUtf8(highlight))), 0,
					wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
			}
			if (!pitch.note.empty()) {
				sizer->AddSpacer(6);
				sizer->Add(new HikariStaticText(this, -1, Wrap(FromUtf8(pitch.note))), 0,
					wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
			}
			if (manifest.offer->legacySupportEnded) {
				sizer->AddSpacer(6);
				HikariStaticText *ended = new HikariStaticText(this, -1,
					Wrap(_("This version of HikariSub gets no further updates. It keeps working, and moving to the new HikariSub is up to you.")));
				ended->SetFont(ended->GetFont().Bold());
				sizer->Add(ended, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
			}

			progress = new HikariStaticText(this, -1, wxString());
			sizer->Add(progress, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
			progress->Hide();

			wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
			tryIt = new MappedButton(this, kTryId, _("Try it"));
			buttons->Add(tryIt, 1, wxALL, 2);
			remindLater = new MappedButton(this, kRemindLaterId, _("Remind me later"));
			buttons->Add(remindLater, 1, wxALL, 2);
			noOffer = new MappedButton(this, kNoOfferId, _("Don't offer again"));
			buttons->Add(noOffer, 1, wxALL, 2);
			sizer->AddSpacer(6);
			sizer->Add(buttons, 0, wxEXPAND | wxALL, 2);
			wxBoxSizer *margin = new wxBoxSizer(wxVERTICAL);
			margin->Add(sizer, 1, wxEXPAND | wxALL, 8);

			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent &) { TryIt(); }, kTryId);
			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent &) { EndModal(kRemindLaterId); },
				kRemindLaterId);
			Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent &) { EndModal(kNoOfferId); }, kNoOfferId);
			SetEscapeId(kRemindLaterId);
			SetSizerAndFit(margin);
		}

		~OfferDialog()
		{
			if (listenerId)
				update_installer::Detach(listenerId);
		}

		// A download asked for here goes on however the window closes, and
		// counts as tried.
		void SaveChoices(int result)
		{
			if (downloading)
				result = kTriedId;
			bool ended = manifest.offer->legacySupportEnded;
			switch (result) {
			case kTriedId:
			case kTriedAurId:
				SetOfferDeclined(update_manifest::TriedAfter(manifest.version, ended, LoadOfferDeclined()));
				SetOfferRemind(0);
				break;
			case kNoOfferId:
				SetOfferDeclined({ manifest.version, ended });
				SetOfferRemind(0);
				break;
			default:
				SetOfferRemind(update_manifest::OfferRemindAt(Now()));
				break;
			}
			Options.SaveOptions(true, false);
		}

	private:
		void TryIt()
		{
			bool autoInstall = Options.GetBool(UPDATER_AUTO_INSTALL) && update_installer::CanInstall();
			update_manifest::Offer offer = update_manifest::Decide(update_installer::Kind(), autoInstall, manifest,
				update_installer::GetSystemInfo());
			switch (offer.type) {
			case update_manifest::OfferType::Install:
				StartDownload(offer.package);
				return;
			case update_manifest::OfferType::Notify:
				OpenReleasePage(update_installer::ReleasePage(L"v" + FromUtf8(manifest.version)));
				EndModal(kTriedId);
				return;
			case update_manifest::OfferType::NotifyRequirement:
				tryIt->Hide();
				ShowProgress(wxString::Format(_("It needs %s or newer, so it cannot be installed here."),
					FromUtf8(offer.requirement)));
				return;
			case update_manifest::OfferType::AurNotice:
				EndModal(kTriedAurId);
				return;
			}
		}

		void ShowProgress(const wxString &text)
		{
			progress->SetLabelText(text);
			progress->Show();
			Fit();
			Layout();
		}

		void StartDownload(const update_manifest::Package &package)
		{
			// It counts as tried from here, so the other choices are not
			// offered while it lasts.
			SetDownloading(true);
			ShowProgress(_("Downloading..."));
			update_installer::Listener listener;
			listener.progress = [this](long long received, long long total) {
				if (total > 0)
					progress->SetLabelText(wxString::Format(_("Downloading... %d%%"),
						static_cast<int>(received * 100 / total)));
			};
			listener.done = [this](bool ok, const wxString &error) {
				listenerId = 0;
				if (ok) {
					EndModal(kTriedId);
					return;
				}
				SetDownloading(false);
				progress->SetLabelText(error.empty() ? _("Cannot download the update") : error);
				Layout();
			};
			listenerId = update_installer::Download(frame, update_installer::Target{ manifest.version, package },
				listener);
		}

		void SetDownloading(bool on)
		{
			downloading = on;
			tryIt->Show(!on);
			remindLater->Enable(!on);
			noOffer->Enable(!on);
		}

		wxWindow *frame;
		update_manifest::Manifest manifest;
		HikariStaticText *progress;
		MappedButton *tryIt;
		MappedButton *remindLater;
		MappedButton *noOffer;
		int listenerId = 0;
		bool downloading = false;
	};

	// Shows the offer, and saves the user's choice. An AUR install that
	// tries it is told about the package manager, as for an update.
	void ShowOffer(wxWindow *parent, const update_manifest::Manifest &manifest)
	{
		OfferDialog dialog(parent, manifest);
		update_installer::Present(&dialog, parent);
		int result = dialog.ShowModal();
		dialog.SaveChoices(result);
		if (result != kTriedAurId)
			return;
		ReleaseInfo info;
		info.tag = L"v" + FromUtf8(manifest.version);
		info.name = info.tag;
		info.url = update_installer::ReleasePage(info.tag);
		std::optional<SemVer> version = ParseSemVer(manifest.version);
		bool prerelease = version && !version->prerelease.empty();
		AurNoticeDialog notice(parent, info,
			!package_kind::AurCarries(prerelease, update_installer::AurChannel()), true);
		update_installer::Present(&notice, parent);
		notice.ShowModal();
	}

	// What a check was asked for from the menu: the updates, or the offer
	// of the rewrite. One at startup asks for neither, and shows only what
	// it finds to show.
	enum class Ask
	{
		Nothing,
		Updates,
		Offer,
	};

	class UpdateRequest;
	// The check that runs, which a check asked for meanwhile joins.
	UpdateRequest *runningCheck = nullptr;

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
			, pages(VersionHikariSub, update_installer::StableOnly())
			, deadline(this)
		{
			Bind(wxEVT_WEBREQUEST_DATA, &UpdateRequest::OnData, this);
			Bind(wxEVT_WEBREQUEST_STATE, &UpdateRequest::OnState, this);
			Bind(wxEVT_TIMER, &UpdateRequest::OnDeadline, this, deadline.GetId());
		}

		~UpdateRequest()
		{
			if (runningCheck == this)
				runningCheck = nullptr;
		}

		// What it reports. A check that starts asking for nothing is the
		// startup check, which shows only what it finds to show, still so
		// when a check from the menu joins it.
		void Asks(Ask ask)
		{
			if (ask == Ask::Updates)
				asksUpdates = true;
			else if (ask == Ask::Offer)
				asksOffer = true;
			else
				atStartup = true;
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
				Consider(update_manifest::Consider(release, came ? &body : nullptr, SeesPrereleases()));
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
				walk.emplace(pages.Releases(),
					update_policy::ListCandidates(pages.Releases(), pages.Installed(), pages.StableOnly()));
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

		bool SeesPrereleases() const
		{
			return update_manifest::SeesPrereleases(pages.StableOnly(), VersionHikariSub);
		}

		// What the current candidate offers, or nothing, and on to the
		// next. The first update is the one; an offer of the rewrite is
		// kept when it is the highest so far.
		void Consider(update_manifest::Considered considered)
		{
			offers.Take(std::move(considered.offer));
			if (!considered.update || walk->Found()) {
				walk->Skip();
				return;
			}
			found = walk->Current();
			foundSkipped = walk->CurrentIsSkipped();
			foundManifest = std::move(*considered.update);
			walk->Take();
		}

		// Looks at the candidates in turn. True while a manifest is being
		// asked for; false once the walk is over, timedOut set when the
		// deadline stopped it.
		bool Walk()
		{
			while (walk->Current() >= 0) {
				const update_policy::Release &release = pages.Releases()[walk->Current()];
				// Past the update, only the rewrite's releases are left,
				// and only a manifest can hold more: an offer.
				if (walk->Found() && !update_manifest::HasManifest(release)) {
					walk->Skip();
					continue;
				}
				if (!update_manifest::HasManifest(release)) {
					Consider(update_manifest::Consider(release, nullptr, SeesPrereleases()));
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
			// No check joins it from here (CheckTracker::Begin()).
			if (runningCheck == this)
				runningCheck = nullptr;
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
			if (asksOffer)
				ReportOffer(ending);
			if (!asksUpdates && !atStartup)
				return;
			bool shown = ReportUpdate(ending, asksUpdates);
			if (!atStartup || ending != update_policy::Ending::Answered)
				return;
			// A startup check that answered: a reminder that has come is
			// done with, whether the offer shows now or not.
			long long remindAt = LoadOfferRemind();
			if (update_manifest::OfferReminderDue(remindAt, Now())) {
				SetOfferRemind(0);
				Options.SaveOptions(true, false);
			}
			// Asked for from the menu, it has had its answer.
			if (asksUpdates || asksOffer)
				return;
			// A legacy update comes first, the one shown now or the one
			// waiting to be installed; the offer waits for a later start.
			const update_manifest::Manifest *offer = offers.Get();
			if (shown || !offer || !update_installer::PendingVersion().empty())
				return;
			if (!update_manifest::ShowsOfferAtStart(offer->version, offer->offer->legacySupportEnded,
				LoadOfferDeclined(), remindAt, Now()))
			{
				return;
			}
			ShowOffer(parent, *offer);
		}

		// "Try the new HikariSub...": the current offer, whatever was
		// chosen of it before, or that there is none.
		void ReportOffer(update_policy::Ending ending)
		{
			if (ending != update_policy::Ending::Answered) {
				update_installer::Message(parent, _("Cannot check for updates"));
				return;
			}
			if (const update_manifest::Manifest *offer = offers.Get())
				ShowOffer(parent, *offer);
			else
				update_installer::Message(parent, _("There is no new HikariSub to try at the moment."));
		}

		// Whether it showed an update, or started or found one waiting.
		bool ReportUpdate(update_policy::Ending ending, bool interactive)
		{
			if (ending != update_policy::Ending::Answered) {
				if (interactive)
					update_installer::Message(parent, _("Cannot check for updates"));
				return false;
			}

			const ReleaseInfo *info = found >= 0 ? details.Find(found) : nullptr;
			if (!info) {
				if (interactive)
					update_installer::Message(parent, _("You already have the latest version"));
				return false;
			}
			if (foundSkipped) {
				if (interactive)
					ShowNewerPrerelease(parent, *info);
				return interactive;
			}
			if (!interactive && update_policy::WasDeclined(info->tag.utf8_string(),
				Options.GetString(UPDATER_DECLINED_VERSION).utf8_string()))
			{
				return false;
			}

			const update_policy::Release &release = pages.Releases()[found];
			std::string version = update_policy::CandidateVersion(release.tag);
			package_kind::Kind kind = update_installer::Kind();
			bool autoInstall = Options.GetBool(UPDATER_AUTO_INSTALL) && update_installer::CanInstall();
			update_manifest::Offer offer = update_manifest::Decide(kind, autoInstall, foundManifest,
				update_installer::GetSystemInfo());
			switch (offer.type) {
			case update_manifest::OfferType::AurNotice: {
				AurNoticeDialog dialog(parent, *info,
					!package_kind::AurCarries(update_manifest::IsPrerelease(release), update_installer::AurChannel()));
				update_installer::Present(&dialog, parent);
				dialog.SaveChoices(dialog.ShowModal());
				return true;
			}
			case update_manifest::OfferType::Install:
				// Already downloaded and waiting: its notice shows at
				// each start, and at the next quit it is installed.
				if (update_installer::IsPending(version) && !interactive)
					return true;
				if (!interactive) {
					// A higher version the user chose to try waits to be
					// installed, and a lower one does not replace it.
					if (update_manifest::IsNewer(update_installer::PendingVersion(), version))
						return true;
					update_installer::Download(parent, update_installer::Target{ version, offer.package });
					return true;
				}
				break;
			case update_manifest::OfferType::Notify:
			case update_manifest::OfferType::NotifyRequirement:
				break;
			}
			UpdateAvailableDialog dialog(parent, *info, offer, version);
			update_installer::Present(&dialog, parent);
			dialog.SaveChoices(dialog.ShowModal());
			return true;
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
		update_manifest::OfferChoice offers;
		bool asksUpdates = false;
		bool asksOffer = false;
		bool atStartup = false;
		bool timedOut = false;
		// Stopped by its destructor, so it never fires at a deleted check.
		wxTimer deadline;
		bool released = false;
	};

	void Check(wxWindow *parent, Ask ask)
	{
		switch (tracker.Begin(Now(), ask != Ask::Nothing)) {
		case update_policy::Start::Ignored:
			return;
		case update_policy::Start::Joined:
			if (runningCheck)
				runningCheck->Asks(ask);
			return;
		case update_policy::Start::Started:
			break;
		}

		UpdateRequest *check = new UpdateRequest(parent);
		check->Asks(ask);
		if (!check->Start()) {
			delete check;
			tracker.End(Now(), update_policy::Ending::NoRequest);
			if (tracker.Reports())
				update_installer::Message(parent, _("Cannot check for updates"));
			tracker.Done();
			return;
		}
		runningCheck = check;
	}
}

void UpdateChecker::CheckOnStartup(wxWindow *parent)
{
	if (!Options.GetBool(UPDATER_AUTO_CHECK))
		return;
	// A reminder of the offer that has come shows it at this start, even
	// within a day of the last check.
	if (!update_policy::IsCheckDue(Now(), LoadNextCheck()) &&
		!update_manifest::OfferReminderDue(LoadOfferRemind(), Now()))
	{
		return;
	}

	Check(parent, Ask::Nothing);
}

void UpdateChecker::CheckNow(wxWindow *parent)
{
	Check(parent, Ask::Updates);
}

void UpdateChecker::OfferNow(wxWindow *parent)
{
	Check(parent, Ask::Offer);
}
