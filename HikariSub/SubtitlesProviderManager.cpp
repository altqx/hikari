//  Copyright (c) 2020 - 2026, Marcin Drob
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

#include "SubtitlesProvider.h"
#include "SubtitlesProviderChoice.h"
#include "config.h"


std::vector< SubtitlesProviderManager*> SubtitlesProviderManager::gs_Base;

SubtitlesProviderManager::~SubtitlesProviderManager()
{
	SAFE_DELETE(SP);
}

SubtitlesProvider *SubtitlesProviderManager::GetProvider()
{
	if (!SP){
		if (ProviderInUse() == LIBASS_PROVIDER)
			SP = new SubtitlesLibass();
		else
			SP = new SubtitlesVSFilter();
		m_IsLibass = SP->IsLibass() ? 1 : 0;
	}
	return SP;
}

wxString SubtitlesProviderManager::ProviderInUse()
{
	std::wstring stored = Options.GetString(VSFILTER_INSTANCE).ToStdWstring();
	std::vector<std::wstring> csri;
#ifdef _WIN32
	// Linux loads no CSRI renderers; libass needs none of them loaded
	wxArrayString names;
	if (stored != LIBASS_PROVIDER)
		SubtitlesVSFilter::GetRendererNames(&names);
	for (const wxString &name : names)
		csri.push_back(name.ToStdWstring());
#endif
	return wxString(ResolveSubtitlesProvider(stored, csri));
}

void SubtitlesProviderManager::GetProviders(wxArrayString *providerList)
{
	wxArrayString names;
	SubtitlesVSFilter::GetProviders(&names);
	std::vector<std::wstring> csri;
	for (const wxString &name : names)
		csri.push_back(name.ToStdWstring());
	for (const std::wstring &name : OrderSubtitlesProviders(csri))
		providerList->Add(wxString(name));
}

wxString SubtitlesProviderManager::ProviderLabel(const wxString &name)
{
	if (name == XY_VSFILTER_PROVIDER)
		return L"xy-VSFilter";
	if (name == VSFILTERMOD_PROVIDER)
		return _("VSFilterMod (slow, only for VSFilterMod tags)");
	return name;
}

void SubtitlesProviderManager::DestroyProviders()
{
	for (auto *spm : gs_Base) {
		std::lock_guard<std::mutex> lock(spm->m_ProviderMutex);
		SAFE_DELETE(spm->SP)
	}
}


void SubtitlesProviderManager::DestroySubsProvider()
{
	SubtitlesProvider::DestroySubtitlesProvider();
	//if there is some providers I may destroy it here
	for (auto *spm : gs_Base) {
		SAFE_DELETE(spm)
	}
	gs_Base.clear();
}

SubtitlesProviderManager *SubtitlesProviderManager::Get()
{
	auto * spm = new SubtitlesProviderManager();
	gs_Base.push_back(spm);
	return spm;
}

void SubtitlesProviderManager::Release()
{

	for (size_t i = 0; i < gs_Base.size(); i++) {
		auto *spm = gs_Base[i];
		if (spm == this) {
			delete spm;
			gs_Base.erase(gs_Base.begin() + i);
			return;
		}
	}
	//if it goes here it means that some alocation was outside base = memory leaks
	return;
}

void SubtitlesProviderManager::Draw(unsigned char* buffer, int time)
{
	std::lock_guard<std::mutex> lock(m_ProviderMutex);
	GetProvider()->Draw(buffer, time);
}

bool SubtitlesProviderManager::DrawOverlay(unsigned char* overlay, int time, wxRect* dirty)
{
	std::lock_guard<std::mutex> lock(m_ProviderMutex);
	return GetProvider()->DrawOverlay(overlay, time, dirty);
}

bool SubtitlesProviderManager::Open(int flag, wxString *text)
{
	std::lock_guard<std::mutex> lock(m_ProviderMutex);
	bool result = GetProvider()->Open(text);
	m_ShowsWholeSubtitles = result && flag == OPEN_WHOLE_SUBTITLES;
	return result;
}
//for styles preview
bool SubtitlesProviderManager::OpenString(wxString *text)
{
	std::lock_guard<std::mutex> lock(m_ProviderMutex);
	return GetProvider()->OpenString(text);
}

void SubtitlesProviderManager::SetVideoParameters(const wxSize& size, unsigned char format, bool isSwapped)
{
	std::lock_guard<std::mutex> lock(m_ProviderMutex);
	GetProvider()->SetVideoParameters(size, format, isSwapped);
}

bool SubtitlesProviderManager::IsLibass()
{
	// a renderer changed in Options is created again at once, by the
	// reopen that follows or the next frame drawn
	int known = m_IsLibass.load();
	if (known >= 0)
		return known == 1;
	std::lock_guard<std::mutex> lock(m_ProviderMutex);
	return GetProvider()->IsLibass();
}

bool SubtitlesProviderManager::CanPrepare()
{
	std::lock_guard<std::mutex> lock(m_ProviderMutex);
	return GetProvider()->CanPrepare();
}

void SubtitlesProviderManager::Prepare(wxString *text)
{
	std::lock_guard<std::mutex> lock(m_ProviderMutex);
	GetProvider()->Prepare(text);
}

bool SubtitlesProviderManager::ReloadLibraries()
{
	if (ProviderInUse() == LIBASS_PROVIDER) {
		if (gs_Base.size() > 0) {
			std::lock_guard<std::mutex> lock(gs_Base[0]->m_ProviderMutex);
			gs_Base[0]->GetProvider()->ReloadLibraries(true);
		}
		return true;
	}
	return false;
}
