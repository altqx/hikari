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
#include "ScriptAttachments.h"

#include "config.h"

#include "RendererVideo.h"
#include "OpennWrite.h"
#include "HikariSubFrame.h"
#include "DshowRenderer.h"
#include "CsriMod.h"
#include "CsriOverlay.h"
#include "UtilsWindows.h"



csri_rend *SubtitlesProvider::m_CsriRenderer = nullptr;
ASS_Renderer *SubtitlesProvider::m_Libass = nullptr;
ASS_Library *SubtitlesProvider::m_Library = nullptr;


void SubtitlesProvider::DestroySubtitlesProvider()
{
	if (m_Libass)
		ass_renderer_done(m_Libass);
	if (m_Library)
		ass_library_done(m_Library);

	if (m_CsriRenderer)
		csri_close_renderer(m_CsriRenderer);
}


void SubtitlesProvider::Prepare(wxString *text)
{
	std::lock_guard<std::mutex> lock(m_PrepareMutex);
	delete m_PrepareText;
	m_PrepareText = text;
	if (m_PrepareRunning || m_StopPrepare)
		return;
	// a finished thread has already let go of the lock for good
	if (m_PrepareThread.joinable())
		m_PrepareThread.join();
	m_PrepareRunning = true;
	m_PrepareThread = std::thread([this]() { RunPrepare(); });
}

void SubtitlesProvider::RunPrepare()
{
	SetThreadName(GetCurrentThreadId(), "SubtitlesPrepare");
	for (;;) {
		wxString *text;
		{
			std::lock_guard<std::mutex> lock(m_PrepareMutex);
			text = m_PrepareText;
			m_PrepareText = nullptr;
			if (!text || m_StopPrepare) {
				delete text;
				m_PrepareRunning = false;
				return;
			}
		}
		ParseAhead(text);
	}
}

void SubtitlesProvider::StopPreparing()
{
	{
		std::lock_guard<std::mutex> lock(m_PrepareMutex);
		m_StopPrepare = true;
		SAFE_DELETE(m_PrepareText);
	}
	if (m_PrepareThread.joinable())
		m_PrepareThread.join();
}

bool SubtitlesProvider::DrawOverlay(unsigned char* overlay, int time, wxRect* dirty)
{
	// without change detection the whole overlay is drawn again every time
	memset(overlay, 0, (size_t)m_VideoSize.x * m_VideoSize.y * 4);
	Draw(overlay, time);
	*dirty = wxRect(0, 0, m_VideoSize.x, m_VideoSize.y);
	return true;
}

bool SubtitlesVSFilter::DrawOverlay(unsigned char* overlay, int time, wxRect* dirty)
{
	std::lock_guard<std::recursive_mutex> lock(s_CsriMutex);
	if (!m_CsriFrame || m_CsriFrame->pixfmt != CSRI_F_BGR_ || m_Format != ARGB32)
		return SubtitlesProvider::DrawOverlay(overlay, time, dirty);
	// BGR_ (the fallback for a renderer that refuses BGRA) blends into the
	// frame and writes no alpha, so on a cleared overlay nothing would show;
	// the same instant drawn onto black and onto white gives each pixel's
	// colour and coverage
	size_t pixels = (size_t)m_VideoSize.x * m_VideoSize.y;
	memset(overlay, 0, pixels * 4);
	m_WhiteOverlay.assign(pixels * 4, 255);
	Draw(overlay, time);
	Draw(m_WhiteOverlay.data(), time);
	CsriOverlayFromBlackAndWhite(overlay, m_WhiteOverlay.data(), pixels);
	*dirty = wxRect(0, 0, m_VideoSize.x, m_VideoSize.y);
	return true;
}

std::recursive_mutex SubtitlesVSFilter::s_CsriMutex;

SubtitlesVSFilter::SubtitlesVSFilter()
{

}

SubtitlesVSFilter::~SubtitlesVSFilter()
{
	StopPreparing();
	std::lock_guard<std::recursive_mutex> lock(s_CsriMutex);
	SAFE_DELETE(m_CsriFrame);
	SAFE_DELETE(m_CsriFormat);
	m_CsriInstance = nullptr;
	m_Instances.Clear();
	

}
void SubtitlesVSFilter::Draw(unsigned char* buffer, int time)
{
	std::lock_guard<std::recursive_mutex> lock(s_CsriMutex);
	if (m_CsriInstance){
		SetCsriFramePlane(m_CsriFrame, buffer, m_CsriFormat->width, m_CsriFormat->height,
			m_BytesPerColor, m_IsSwapped);
		csri_render(m_CsriInstance, m_CsriFrame, double(time / 1000.0));
	}
	
};

bool SubtitlesVSFilter::Open(wxString *text)
{
	std::lock_guard<std::recursive_mutex> lock(s_CsriMutex);
	m_CsriInstance = nullptr;

	if (!text)
		return true;

	return OpenCached(text);
}

bool SubtitlesVSFilter::OpenString(wxString *text)
{
	std::lock_guard<std::recursive_mutex> lock(s_CsriMutex);
	m_CsriInstance = nullptr;

	if (!m_HasParameters){
		delete text;
		return false;
	}

	return OpenCached(text);
}

void SubtitlesVSFilter::ForgetParsedScripts()
{
	std::lock_guard<std::recursive_mutex> lock(s_CsriMutex);
	m_CsriInstance = nullptr;
	m_Instances.Clear();
}

void SubtitlesVSFilter::SetAttachments(std::shared_ptr<const ScriptAttachments> attachments)
{
	std::lock_guard<std::recursive_mutex> lock(s_CsriMutex);
	m_Attachments = std::move(attachments);
}

bool SubtitlesVSFilter::OpenCached(wxString *text)
{
	size_t hash = CsriScriptHash(ScriptHash(*text), m_Attachments.get());
	if (csri_inst *instance = m_Instances.Find(hash)) {
		// the video format may have changed since it was parsed; if so, parse again
		if (m_CsriFormat && !csri_request_fmt(instance, m_CsriFormat)) {
			delete text;
			m_CsriInstance = instance;
			return true;
		}
		m_Instances.Remove(instance);
	}
	if (!OpenInstance(text))
		return false;
	m_Instances.Add(hash, m_CsriInstance, m_CsriInstance);
	return true;
}

bool SubtitlesVSFilter::OpenInstance(wxString *text)
{
	m_CsriInstance = ParseInstance(text);
	return m_CsriInstance != nullptr;
}

// call with s_CsriMutex locked
csri_inst *SubtitlesVSFilter::ParseInstance(wxString *text)
{
	// Select renderer
	csri_rend *vobsub = GetVSFilter();
	if (!vobsub){
		delete text;
		HikariLogSilent(_("Cannot initialize CSRI."));
		return nullptr;
	}

	// mb_str() points into text's own conversion cache, so text must outlive it
	wxScopedCharBuffer buffer = text->mb_str(wxConvUTF8);
	csri_inst *instance;
	if (m_Attachments && !m_Attachments->Graphics().empty()) {
		// VSFilterMod's image tags draw the script's [Graphics]
		std::string script = CsriScriptText(std::string_view(buffer.data(), strlen(buffer)), m_Attachments.get());
		instance = csri_open_mem(vobsub, script.data(), script.size(), nullptr);
	}
	else {
		instance = csri_open_mem(vobsub, buffer, strlen(buffer), nullptr);
	}
	delete text;
	if (!instance){
		HikariLogSilent(_("Cannot create CSRI instance."));
		return nullptr;
	}
	if (!m_CsriFormat || !RequestCsriFormat(m_CsriFormat, m_CsriFrame,
		[instance](const csri_fmt *fmt) { return csri_request_fmt(instance, fmt); })) {
		HikariLogSilent(_("CSRI does not support this format."));
		csri_close(instance);
		return nullptr;
	}
	return instance;
}

void SubtitlesVSFilter::ParseAhead(wxString *text)
{
	size_t hash = ScriptHash(*text);
	std::lock_guard<std::recursive_mutex> lock(s_CsriMutex);
	hash = CsriScriptHash(hash, m_Attachments.get());
	if (!m_HasParameters || !m_CsriFormat || m_Instances.Find(hash)) {
		delete text;
		return;
	}
	if (csri_inst *instance = ParseInstance(text))
		m_Instances.Add(hash, instance, m_CsriInstance);
}

csri_rend *SubtitlesVSFilter::GetVSFilter()
{
	if (!m_CsriRenderer){
		m_CsriRenderer = csri_renderer_default();
		if (!m_CsriRenderer){
			return nullptr;
		}
		csri_info *info = csri_renderer_info(m_CsriRenderer);
		// not the stored name: the CSRI default may be VSFilterMod
		wxString name = SubtitlesProviderManager::ProviderInUse();
		if (!name.empty() && info){
			while (info->name != name){
				m_CsriRenderer = csri_renderer_next(m_CsriRenderer);
				if (!m_CsriRenderer)
					break;
				info = csri_renderer_info(m_CsriRenderer);
				if (!info)
					break;
			}
			if (!m_CsriRenderer)
				m_CsriRenderer = csri_renderer_default();
		}
	}
	return m_CsriRenderer;
}

void SubtitlesVSFilter::GetRendererNames(wxArrayString *names)
{
	std::lock_guard<std::recursive_mutex> lock(s_CsriMutex);
	for (csri_rend *filter = csri_renderer_default(); filter; filter = csri_renderer_next(filter)) {
		csri_info *info = csri_renderer_info(filter);
		if (!info) {
			// as before, a renderer without info ends the list when it is the default
			if (filter == csri_renderer_default())
				return;
			continue;
		}
		names->Add(info->name);
	}
}

void SubtitlesVSFilter::GetProviders(wxArrayString *providerList)
{
	// it closes the renderer a parse on the worker may be using
	std::lock_guard<std::recursive_mutex> lock(s_CsriMutex);
	if (!csri_renderer_default())
		return;
	GetRendererNames(providerList);
	csri_close_renderer(nullptr);
	//test if current renderer is removed
	//if yes than just set nullptr to renderer
	//without destroying it it makes memory leaks
	csri_close_renderer(m_CsriRenderer);
	m_CsriRenderer = 0;
}

void SubtitlesVSFilter::SetVideoParameters(const wxSize & size, unsigned char format, bool isSwapped)
{
	std::lock_guard<std::recursive_mutex> lock(s_CsriMutex);
	m_VideoSize = size;
	m_IsSwapped = isSwapped;
	m_Format = format;
	byte bytes = (m_Format == RGB32 || m_Format == ARGB32) ? 4 : (m_Format == YUY2) ? 2 : 1;
	m_BytesPerColor = bytes;
	m_HasParameters = true;

	if (!m_CsriFrame) {
		m_CsriFrame = new csri_frame;
		//we only uses first planes and strides rest can be reset just once
		for (int i = 1; i < 4; i++) {
			m_CsriFrame->planes[i] = 0;
			m_CsriFrame->strides[i] = 0;
		}
	}
	if (!m_CsriFormat) { m_CsriFormat = new csri_fmt; }
		
	m_CsriFrame->pixfmt = (m_Format == NV12) ? CSRI_F_YV12A : (m_Format == YV12) ? CSRI_F_YV12 :
		(m_Format == YUY2) ? CSRI_F_YUY2 : (m_Format == RGB32) ? CSRI_F_BGR_ : CSRI_F_BGRA;

	m_CsriFormat->width = size.GetWidth();
	m_CsriFormat->height = size.GetHeight();
	csri_inst *instance = m_CsriInstance;
	if (!RequestCsriFormat(m_CsriFormat, m_CsriFrame,
		[instance](const csri_fmt *fmt) { return csri_request_fmt(instance, fmt); })) {
		HikariLog(_("CSRI does not support this format."));
		m_Instances.Remove(m_CsriInstance);
		m_CsriInstance = nullptr;
	}
}


