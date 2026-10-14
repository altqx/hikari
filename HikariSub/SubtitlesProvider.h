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

#pragma  once
#include "SubtitlesProviderManager.h"
#include "CsriMod.h"
#include "WinUndef.h"
#include <wx/window.h>
#include <wx/arrstr.h>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <thread>
#include "ParsedScripts.h"
#include <memory>
#include <vector>

extern "C" {
#ifdef __WXMSW__
#include <libass/ass.h> // the submodule keeps its headers under libass/
#else
#include <ass/ass.h>
#endif
}
// Both builds use the altqx fork in Thirdparty/libass. Its API macros tell
// it from upstream, which steps LIBASS_VERSION the same way.
#if !defined(LIBASS_HAS_FRAME_ATLAS) || !defined(LIBASS_HAS_TRACK_EVENTS_MODIFIED) || LIBASS_VERSION < 0x01705010
#error "HikariSub builds against the altqx libass fork in Thirdparty/libass"
#endif

class ScriptAttachments;

inline size_t ScriptHash(const wxString &text)
{
	return ScriptTextHash(std::wstring_view(text.wc_str(), text.length()));
}

class SubtitlesProvider
{
public:
	virtual ~SubtitlesProvider(){};
	SubtitlesProvider(){};
	virtual void Draw(unsigned char* buffer, int time){};
	// Draws into a transparent ARGB overlay the size of the video, which the
	// caller keeps between calls. Returns false when the subtitles look the
	// same as last time; otherwise dirty covers everything that changed.
	virtual bool DrawOverlay(unsigned char* overlay, int time, wxRect* dirty);
	// takes the text; nullptr closes the subtitles
	virtual bool Open(wxString *text){ return false; };
	//for styles preview
	virtual bool OpenString(wxString *text){ return false; };
	virtual void SetVideoParameters(const wxSize& size, unsigned char format, bool isSwapped) {};
	virtual void ReloadLibraries(bool destroyExisted = false) { };
	// drops every script parsed so far, the one shown too, so that the next
	// Open parses again; the caller opens the subtitles again
	virtual void ForgetParsedScripts() {}
	virtual bool IsLibass() { return false; }
	// Parses text on a worker thread so a later Open of the same text finds
	// it ready; takes the text. Only providers that can do it say so.
	virtual bool CanPrepare() { return false; }
	void Prepare(wxString *text);
	// the attachments of the script the next texts come from; nullptr for none
	virtual void SetAttachments(std::shared_ptr<const ScriptAttachments> attachments) {}
	//implementation in subtitlesVsfilter
	static void DestroySubtitlesProvider();
	static ASS_Renderer *m_Libass;
	// style previews draw with their own renderer, so their sizes do not
	// empty the caches of the one the video uses; guarded by openMutex
	static ASS_Renderer *m_PreviewLibass;
	static ASS_Library *m_Library;
private:
	SubtitlesProvider(const SubtitlesProvider &copy) = delete;
	void RunPrepare();
	std::thread m_PrepareThread;
	std::mutex m_PrepareMutex;
	// the latest text to prepare; an older one still waiting is dropped
	wxString *m_PrepareText = nullptr;
	bool m_PrepareRunning = false;
	bool m_StopPrepare = false;
protected:
	// on the worker thread; takes the text
	virtual void ParseAhead(wxString *text) { delete text; }
	// derived destructors call it first, as the worker calls into them
	void StopPreparing();
	wxSize m_VideoSize;
	unsigned char m_Format = 0;
	bool m_IsSwapped = false;
	bool m_HasParameters = false;
	char m_BytesPerColor = 4;
	static csri_rend *m_CsriRenderer;
};


class SubtitlesVSFilter : public SubtitlesProvider
{
public:
	SubtitlesVSFilter();
	virtual ~SubtitlesVSFilter();
	void Draw(unsigned char* buffer, int time);
	bool DrawOverlay(unsigned char* overlay, int time, wxRect* dirty) override;
	bool Open(wxString *text);
	bool OpenString(wxString *text);
	static void GetProviders(wxArrayString *providerList);
	// the renderers that loaded, the default one first; unlike GetProviders
	// it keeps them open
	static void GetRendererNames(wxArrayString *names);
	void SetVideoParameters(const wxSize& size, unsigned char format, bool isSwapped);
	bool CanPrepare() override { return true; }
	void ForgetParsedScripts() override;
	void SetAttachments(std::shared_ptr<const ScriptAttachments> attachments) override;
protected:
	void ParseAhead(wxString *text) override;
private:
	// the script's [Graphics] go to the renderer after the events of each text
	std::shared_ptr<const ScriptAttachments> m_Attachments;
	// VSFilter is not safe to call from two threads at once, so every call takes this
	static std::recursive_mutex s_CsriMutex;
	// a parsed instance that accepts the video format, or nullptr; takes the text
	csri_inst *ParseInstance(wxString *text);
	bool OpenInstance(wxString *text);
	bool OpenCached(wxString *text);
	csri_frame *m_CsriFrame = nullptr;
	csri_fmt *m_CsriFormat = nullptr;
	// owned by m_Instances
	csri_inst *m_CsriInstance = nullptr;
	ParsedScripts<csri_inst> m_Instances{ csri_close };
	// the second drawing of a BGR_ overlay, onto white
	std::vector<unsigned char> m_WhiteOverlay;
	csri_rend *GetVSFilter();
};

class SubtitlesLibass : public SubtitlesProvider
{
public:
	SubtitlesLibass();
	virtual ~SubtitlesLibass();
	void Draw(unsigned char* buffer, int time);
	bool DrawOverlay(unsigned char* overlay, int time, wxRect* dirty) override;
	bool Open(wxString *text);
	bool OpenString(wxString *text);
	void SetVideoParameters(const wxSize& size, unsigned char format, bool isSwapped);
	void ReloadLibraries(bool destroyExisted = false) override;
	bool IsLibass() { return true; }
	bool CanPrepare() override { return true; }
protected:
	void ParseAhead(wxString *text) override;
public:
	ASS_Track *m_AssTrack = nullptr;
	static std::atomic<bool> m_IsReady;
	HANDLE thread = nullptr;
	wxSize m_VideoSize;
	volatile bool m_SubsSkipped = false;
	static wxMutex openMutex;
	// Parses run outside openMutex holding this shared, so the library they
	// read stays; ReloadLibraries takes it exclusively to replace the
	// library. Taken before openMutex.
	static std::shared_mutex s_LibraryMutex;
private:
	// Blends a libass image list onto an ARGB (premultiplied) overlay buffer.
	void BlendImages(ASS_Image* img, unsigned char* buffer);
	ASS_Image* RenderFrame(int time, int* change);
	// call with s_LibraryMutex shared and openMutex not locked; takes the
	// text. The track shown so far stays until the new one is parsed.
	bool ReadTrack(wxString* text);
	// call with openMutex locked, libass ready: adds the attached fonts the
	// library lacks
	void AddAttachedFonts();
	// how many of AttachedFonts the library has; guarded by openMutex
	static size_t s_AttachedFontsAdded;
	// owns m_AssTrack
	ParsedScripts<ASS_Track> m_Tracks{ ass_free_track };
	bool m_HasRendered = false;
	// a style preview's, which OpenString opens, rendered by m_PreviewLibass
	bool m_IsPreview = false;
	// what the last DrawOverlay drew, cleared before the next one draws
	wxRect m_OverlayDrawn;
	// all tabs share m_Libass, whose change detection compares with whatever it rendered last
	static SubtitlesLibass* m_LastRenderer;

	// every tab's tracks belong to the shared library; guarded by openMutex
	static std::vector<SubtitlesLibass*> s_Instances;
	void ForgetTracks();

};

