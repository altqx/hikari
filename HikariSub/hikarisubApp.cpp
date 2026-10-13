/***************************************************************
 * Copyright (c) 2012 - 2026, Marcin Drob
 * Copyright (c) 2026, altqx
 * Name:      hikarisubApp.cpp
 * Purpose:   Subtitles editor and player
 * Author:    Bjakja (bjakja@op.pl)
 * Created:   2012-04-23
 * License:
 * HikariSub is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * HikariSub is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with HikariSub.  If not, see <http://www.gnu.org/licenses/>.
 **************************************************************/


 
#include "hikarisubApp.h"
#include "Menu.h"
#include "ListControls.h"
#include "OpennWrite.h"
#include "Hotkeys.h"
#include "HikariMessageBox.h"
#include "FontEnumerator.h"
#include "SubsGrid.h"
#include "VideoBox.h"
#include "Notebook.h"
#include "PackageKind.h"
#include "UpdateApply.h"
#include "UpdateInstaller.h"
#include "VersionHikariSub.h"
#include <wx/image.h>
#include <wx/ipc.h>
#include <wx/utils.h>
#include <wx/intl.h>
#include <wx/dcscreen.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/ffile.h>
#include <wx/log.h>
#include <wx/weakref.h>
#include <wx/translation.h>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#ifndef _WIN32
#include "PrivateFolder.h"
#include <gst/gst.h>
#include <unistd.h>
#include <clocale>
#include <cstring>
#include <cstdlib>
#include <wx/tokenzr.h>
#include <wx/tooltip.h>
#endif
#include "LogHandler.h"

#include "UtilsWindows.h"
#include <versionhelpers.h>
//#include <boost/stacktrace.hpp>
#ifdef _WIN32
#include <dbghelp.h>
#endif

namespace {
#ifndef _WIN32
wxString JoinOpenPaths(const wxArrayString& openPaths)
{
	wxString joined;
	for (size_t i = 0; i < openPaths.GetCount(); ++i){
		if (i){ joined += L"|"; }
		joined += openPaths[i];
	}
	return joined;
}

void QueueOpenPaths(const wxString& packedPaths)
{
	hikarisubApp *Hikari = (hikarisubApp *)wxTheApp;
	if (!Hikari){ return; }
	wxStringTokenizer tkn(packedPaths, L"|");
	while (tkn.HasMoreTokens()){
		wxString path = tkn.NextToken();
		if (!path.empty()){
			Hikari->paths.Add(path);
		}
	}
	if (!Hikari->paths.empty()){
		Hikari->openTimer.Start(400, true);
	}
}

const wxString HikariSubIpcTopic()
{
	return L"HikariSubOpenFiles";
}

// The socket a second instance passes the files to open to the first
// through, in a folder only the user may open that no other user can rename
// or replace, see UserRuntimeSocketPath: in the shared temporary folder,
// another user could take its name first and be sent the paths, or connect
// to it and have files opened. Found once, so the socket removed at the end
// is the one made at the start. Empty when there is no such folder; files
// are then not passed on.
wxString HikariSubIpcServiceName()
{
	static const wxString name = []() {
		std::string socket = UserRuntimeSocketPath("hikarisub.ipc");
		if (socket.empty()){
			HikariLogSilent(L"No folder only this user can open for the socket that passes files to a running instance");
			return wxString();
		}
		return wxString(socket.c_str(), *wxConvFileName);
	}();
	return name;
}

void RemoveHikariSubIpcSocket()
{
	wxString name = HikariSubIpcServiceName();
	if (name.empty()){
		return;
	}
	wxLogNull noLog;
	wxRemoveFile(name);
}

class HikariSubIpcConnection : public wxConnection
{
public:
	bool OnExec(const wxString& topic, const wxString& data) override
	{
		if (topic != HikariSubIpcTopic()){
			return false;
		}
		QueueOpenPaths(data);
		HikariSubFrame *frame = HikariSubFrame::Get();
		if (frame){
			if (frame->IsIconized()){
				frame->Iconize(false);
			}
			frame->Raise();
		}
		return true;
	}
};

class HikariSubIpcServer : public wxServer
{
public:
	wxConnectionBase *OnAcceptConnection(const wxString& topic) override
	{
		if (topic == HikariSubIpcTopic()){
			return new HikariSubIpcConnection();
		}
		return nullptr;
	}
};

bool SendOpenPathsToRunningInstance(const wxString& packedPaths)
{
	wxString service = HikariSubIpcServiceName();
	if (packedPaths.empty() || service.empty()){
		return false;
	}
	wxClient client;
	for (int attempt = 0; attempt < 100; ++attempt){
		//the socket must still be the user's own in a private folder, so the
		//paths are sent to no other user's socket
		wxConnectionBase *connection = IsUserRuntimeSocket(std::string(service.fn_str().data())) ?
			client.MakeConnection(L"localhost", service, HikariSubIpcTopic()) : nullptr;
		if (connection){
			const bool sent = connection->Execute(packedPaths);
			connection->Disconnect();
			return sent;
		}
		wxMilliSleep(40);
	}
	return false;
}
#endif
}

#ifdef _WIN32
typedef enum MONITOR_DPI_TYPE {
	MDT_EFFECTIVE_DPI = 0,
	MDT_ANGULAR_DPI = 1,
	MDT_RAW_DPI = 2,
	MDT_DEFAULT = MDT_EFFECTIVE_DPI
} MONITOR_DPI_TYPE;

STDAPI GetDpiForMonitor(
	_In_ HMONITOR hmonitor,
	_In_ MONITOR_DPI_TYPE dpiType,
	_Out_ UINT* dpiX,
	_Out_ UINT* dpiY);
#endif


#ifdef _WIN32
LONG __stdcall MyCustomFilter(EXCEPTION_POINTERS* pep)
{
	// The data root: an installed copy cannot write beside the exe.
	wxString exePath = Options.dataPath + L"/MiniDump.dmp";
	if (Options.dataPath.empty()){
		wxStandardPathsBase& paths = wxStandardPaths::Get();
		exePath = wxFileName(paths.GetExecutablePath()).GetPath() + L"/MiniDump.dmp";
	}
	HANDLE hFile = CreateFileW(exePath.wc_str(), GENERIC_READ | GENERIC_WRITE,
		0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if ((hFile != NULL) && (hFile != INVALID_HANDLE_VALUE))
	{
		// Create the minidump 

		MINIDUMP_EXCEPTION_INFORMATION mdei;

		mdei.ThreadId = GetCurrentThreadId();
		mdei.ExceptionPointers = pep;
		mdei.ClientPointers = FALSE;

		MINIDUMP_TYPE mdt = MiniDumpNormal;

		BOOL rv = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
			hFile, mdt, (pep != 0) ? &mdei : 0, 0, 0);

		/*if (!rv)
			_tprintf(_T("MiniDumpWriteDump failed. Error: %u \n"), GetLastError());
		else
			_tprintf(_T("Minidump created.\n"));*/

		// Close the file 

		CloseHandle(hFile);

	}
	return EXCEPTION_EXECUTE_HANDLER;
}
#endif



void hikarisubApp::OnOutofMemory()
{
	TabPanel *tab = Notebook::GetTab();

	if (tab->grid->file->HistorySize() > 4){
		tab->grid->file->DropOldestHistory(2);
		HikariLog(_("Not enough RAM. History partially deleted"));
		return;
	}
	else if (Notebook::GetTabs()->Size() > 1){
		for (size_t i = 0; i < Notebook::GetTabs()->Size(); i++)
		{
			if (i != Notebook::GetTabs()->GetSelection()){
				if (Notebook::GetTabs()->Page(i)->grid->file->HistorySize() > 4){
					Notebook::GetTabs()->Page(i)->grid->file->DropOldestHistory(2);
					HikariLog(_("Not enough RAM. History partially deleted"));
					return;
				}
			}
		}
	}

	std::exit(1);
}

IMPLEMENT_APP(hikarisubApp);

namespace
{
	// The kind this copy runs as, for --version, read before the options
	// are: package-kind.txt beside the executable and $APPIMAGE.
	std::string_view KindTokenAtStart()
	{
#ifdef _WIN32
		wchar_t exe[32768];
		DWORD length = GetModuleFileNameW(nullptr, exe, 32768);
		wxString dir = wxString(exe, length).BeforeLast(L'\\');
#else
		char exe[4096];
		ssize_t length = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
		wxString dir = length > 0 ? wxString::FromUTF8(exe, length).BeforeLast(L'/') : wxString();
#endif
		std::optional<package_kind::Kind> marker;
#ifdef HIKARISUB_UPDATE_TEST_FEED
		if (const char *forced = std::getenv("HIKARISUB_PACKAGE_KIND"))
			marker = package_kind::ParseToken(forced);
		else
#endif
		{
			// fopen, not wxFFile, which logs a missing file before wx can
			// show it.
			std::string path = (dir + wxFileName::GetPathSeparator() + L"package-kind.txt").utf8_string();
#ifdef _WIN32
			FILE *file = _wfopen(wxString::FromUTF8(path.c_str()).wc_str(), L"rb");
#else
			FILE *file = std::fopen(path.c_str(), "rb");
#endif
			char content[64];
			size_t read = file ? std::fread(content, 1, sizeof(content), file) : 0;
			if (file)
				std::fclose(file);
			if (read > 0 && read < sizeof(content))
				marker = package_kind::ParseMarker(std::string_view(content, read));
		}
#ifdef _WIN32
		return package_kind::Token(package_kind::Resolve(marker, true, false));
#else
		const char *appImage = std::getenv("APPIMAGE");
		return package_kind::Token(package_kind::Resolve(marker, false, appImage && wxFileName::FileExists(appImage)));
#endif
	}

#ifndef _WIN32
	// --hikari-gst-check[=<file>] (installers spec, 8.1): creates the
	// GStreamer elements the player uses and, given a file, decodes it to
	// fakesinks. Prints what is missing; exits 0 when nothing is, 1 when an
	// element is missing and 2 when the file does not decode, as the
	// packaging tests' gst-check does.
	int GstCheck(const char *file)
	{
		GError *error = nullptr;
		if (!gst_init_check(nullptr, nullptr, &error)) {
			std::printf("GStreamer cannot start: %s\n", error ? error->message : "unknown error");
			return 1;
		}
		int missing = 0;
		for (const char *factory : { "playbin", "appsrc", "appsink", "audioconvert", "audioresample",
			"autoaudiosink", "videoconvert", "fakesink" })
		{
			GstElement *element = gst_element_factory_make(factory, nullptr);
			if (!element) {
				std::printf("missing element: %s\n", factory);
				missing++;
				continue;
			}
			gst_object_unref(element);
		}
		if (missing)
			return 1;
		if (!file || !*file) {
			std::printf("elements: ok\n");
			return 0;
		}

		gchar *uri = gst_filename_to_uri(file, nullptr);
		GstElement *playbin = gst_element_factory_make("playbin", nullptr);
		GstElement *videoSink = gst_element_factory_make("fakesink", nullptr);
		GstElement *audioSink = gst_element_factory_make("fakesink", nullptr);
		g_object_set(playbin, "uri", uri, "video-sink", videoSink, "audio-sink", audioSink, nullptr);
		g_free(uri);
		int result = 2;
		if (gst_element_set_state(playbin, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE) {
			GstBus *bus = gst_element_get_bus(playbin);
			GstMessage *message = gst_bus_timed_pop_filtered(bus, 60 * GST_SECOND,
				GstMessageType(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
			if (!message) {
				std::printf("decode: timed out\n");
			}
			else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
				GError *decodeError = nullptr;
				gst_message_parse_error(message, &decodeError, nullptr);
				std::printf("decode: %s\n", decodeError ? decodeError->message : "error");
				if (decodeError)
					g_error_free(decodeError);
			}
			else {
				std::printf("decode: ok\n");
				result = 0;
			}
			if (message)
				gst_message_unref(message);
			gst_object_unref(bus);
		}
		else {
			std::printf("decode: the pipeline does not start\n");
		}
		gst_element_set_state(playbin, GST_STATE_NULL);
		gst_object_unref(playbin);
		return result;
	}
#endif
}

bool hikarisubApp::Initialize(int& argc, wxChar** argv)
{
#ifdef _WIN32
	// The installing side of an update (installers spec, 4.2): nothing of
	// the app starts, no settings are read and no window opens.
	if (argc >= 2 && wxString(argv[1]) == update_apply::kSwitch)
		std::exit(update_installer::ApplyUpdate(KindTokenAtStart() == package_kind::Token(package_kind::Kind::NsisMachine)));
#endif
	for (int i = 1; i < argc; i++) {
		wxString argument = argv[i];
		if (argument == L"--version") {
			std::printf("%s (%s)\n", VersionHikariSub, std::string(KindTokenAtStart()).c_str());
			std::fflush(stdout);
			std::exit(0);
		}
		if (argument == L"--hikari-gst-check" || argument.StartsWith(L"--hikari-gst-check=")) {
#ifdef _WIN32
			std::exit(2);
#else
			std::string file = argument.AfterFirst(L'=').utf8_string();
			int result = GstCheck(file.c_str());
			std::fflush(stdout);
			std::exit(result);
#endif
		}
	}
	return wxApp::Initialize(argc, argv);
}

bool hikarisubApp::OnInit()
{

#ifndef _WIN32
	m_ipcServer = nullptr;
	// An AppImage update an ordinary quit left: installed now, watched,
	// before the lock, so the new image starts as the only instance.
	if (update_installer::InstallAppImageAtStart())
		return false;
#endif
	m_checker = new wxSingleInstanceChecker();

	// The updater's switches are not files to open.
	wxArrayString arguments;
	{
		wxString healthFile, updatedFrom;
		for (int i = 1; i < argc; i++) {
			wxString argument = argv[i];
			if (argument.StartsWith(L"--hikari-health=", &healthFile) ||
				argument.StartsWith(L"--hikari-updated-from=", &updatedFrom))
				continue;
			arguments.Add(argument);
		}
		update_installer::SetCommandLine(healthFile, updatedFrom);
	}

	//bool wxsOK = true;

	if (!m_checker->IsAnotherRunning())
	{
#ifdef _WIN32
		// Held until the process ends; a failure is ignored.
		runningMutex = CreateMutexW(nullptr, FALSE, L"Global\\io.github.altqx.HikariSub.Running");
#endif

#ifndef _WIN32
		RemoveHikariSubIpcSocket();
		m_ipcServer = new HikariSubIpcServer();
		wxString service = HikariSubIpcServiceName();
		//wxServer makes the socket under umask 077; it is used only when it
		//is then the user's socket, 0600, in the folder that was checked
		if (service.empty() || !m_ipcServer->Create(service) ||
			!NarrowUserRuntimeSocket(std::string(service.fn_str().data()))){
			delete m_ipcServer;
			m_ipcServer = nullptr;
			HikariLogSilent(L"Cannot create HikariSub IPC server for opening files in a running instance");
		}
#endif

#ifndef _WIN32
		// Seed the program-font DPI from the real display; the GetDeviceCaps shim
		// only reports 96 and GetFontPixelHeight() needs the real resolution.
		// Safe here (wx GUI is initialised).
		{
			int ydpi = wxScreenDC().GetPPI().y;
			if (ydpi > 0)
				Options.FontsRescale(ydpi);
		}
		// Point portable Linux runs at a usable gdk-pixbuf loader cache.
		if (!std::getenv("GDK_PIXBUF_MODULE_FILE")){
			const wxString pixbufCaches[] = {
				L"/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders.cache",
				L"/usr/lib64/gdk-pixbuf-2.0/2.10.0/loaders.cache",
				L"/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache"
			};
			for (const auto& pixbufCache : pixbufCaches){
				if (wxFileExists(pixbufCache)){
					setenv("GDK_PIXBUF_MODULE_FILE", pixbufCache.mb_str().data(), 0);
					wxString pixbufModuleDir = pixbufCache.BeforeLast(wxFileName::GetPathSeparator()) + wxFileName::GetPathSeparator() + L"loaders";
					if (!std::getenv("GDK_PIXBUF_MODULEDIR") && wxDirExists(pixbufModuleDir))
						setenv("GDK_PIXBUF_MODULEDIR", pixbufModuleDir.mb_str().data(), 0);
					break;
				}
			}
		}
		if (!std::getenv("XDG_DATA_DIRS"))
			setenv("XDG_DATA_DIRS", "/usr/local/share:/usr/share", 0);

		// Disable native wxGTK tooltip popups; HikariSub uses tooltip text for status help.
		wxToolTip::Enable(false);
#endif

		wxImage::AddHandler(new wxPNGHandler);
		wxImage::AddHandler(new wxICOHandler);
		wxImage::AddHandler(new wxCURHandler);

		//if (wxsOK)
		//{
		//do not load here float options cause here is polish or another default locale with ',' instead of '.'
		//wxHandleFatalExceptions(true);
		//0 - failed, 1 - succeeded, 2 - no config
		int isGood = Options.LoadOptions();
		if (!isGood){ HikariMessageBox(_("Cannot read settings.\nThe program will be terminated."), _("Warning")); return false; }
		//0x0415 	Polish (pl) 	0x15 	LANG_POLISH 	Poland (PL) 	0x01 	SUBLANG_POLISH_POLAND
		if (isGood == 2 && GetSystemDefaultUILanguage() == 0x415){
			Options.SetString(PROGRAM_LANGUAGE, L"pl");
			Options.SetString(DICTIONARY_LANGUAGE, L"pl");
		}
			
		locale = nullptr;
		wxString lang = Options.GetString(PROGRAM_LANGUAGE);
		if (lang == L"0"){
			lang = emptyString; Options.SetString(PROGRAM_LANGUAGE, lang);
		}
		if (lang == L"1"){
			lang = emptyString; Options.SetString(PROGRAM_LANGUAGE, lang);
		}
		// Register the catalogue directory and make sure a wxTranslations exists
		// even when no catalogue is loaded, so the options dialog can still list
		// the available languages.  wxLocale keeps an existing one.
		wxFileTranslationsLoader::AddCatalogLookupPathPrefix(Options.GetLocalePath());
		if (!wxTranslations::Get())
			wxTranslations::Set(new wxTranslations());

		// English is the source language: its strings are in the binary already.
		if (lang != emptyString && lang != L"en"){
			locale = new wxLocale();
			const  wxLanguageInfo * li = locale->FindLanguageInfo(lang);
			if (!li){
				HikariMessageBox(_("Cannot find language, language change failed"));
			}
			else{
#ifdef _WIN32
				if (!locale->Init(li->Language, wxLOCALE_DONT_LOAD_DEFAULT)){
					HikariMessageBox(_("wxLocale cannot initialize, language change failed"));
				}
#else
				{
					// On Unix, wxLocale::Init() may fail when the requested OS locale
					// (for example th_TH.UTF-8) has not been generated.  The message
					// catalogs can still be loaded after Init() sets wxLocale's internal
					// language state, so silence wxWidgets' warning dialog and continue.
					wxLogNull suppressLocaleWarning;
					locale->Init(li->Language, wxLOCALE_DONT_LOAD_DEFAULT);
				}
#endif
				if (!locale->AddCatalog(HIKARISUB_CATALOG_DOMAIN, wxLANGUAGE_ENGLISH, L"UTF-8")){
					HikariMessageBox(_("Cannot find translation, language change failed"));
				}
			}
		}
		
#ifdef _WIN32
		SetUnhandledExceptionFilter(MyCustomFilter);
#endif

		//on x64 it makes not working unicode toupper tolower conversion
		//setlocale(LC_CTYPE, "C");
		//without a wxLocale (English, or when it failed) LC_CTYPE stays "C",
		//then case-insensitive matching folds only A-Z
		const char *ctype = setlocale(LC_CTYPE, nullptr);
		if (!ctype || !strcmp(ctype, "C") || !strcmp(ctype, "POSIX")){
			ctype = setlocale(LC_CTYPE, "");
#ifndef _WIN32
			if (!ctype || !strcmp(ctype, "C") || !strcmp(ctype, "POSIX"))
				setlocale(LC_CTYPE, "C.UTF-8");
#endif
		}
		//locale numbers changes here cause of it is set with wxlocale, I have to change it back
		setlocale(LC_NUMERIC, "C");

		if (!Hkeys.LoadHkeys()){
			HikariMessageBox(_("Cannot read hotkeys.\nThe program will be terminated."), _("Warning"));
			wxDELETE(locale); return false;
		}

		for (const wxString &argument : arguments) { paths.Add(argument); }

		int posx, posy, sizex, sizey, msizex, msizey, sposx, sposy;
		Options.GetCoords(WINDOW_POSITION, &posx, &posy);
		Options.GetCoords(WINDOW_SIZE, &sizex, &sizey);
		Options.GetCoords(MONITOR_SIZE, &msizex, &msizey);
		Options.GetCoords(STYLE_MANAGER_POSITION, &sposx, &sposy);
		//wxRect(posx, posy, sizex, sizey)
#ifdef _WIN32
		wxPoint posOnScreen = wxGetMousePosition();
#else
		// Avoid wxGetMousePosition() on Wayland before a keyboard-capable seat exists.
		wxPoint posOnScreen(posx, posy);
#endif
		if (msizex && msizey) {
			if (IsWindows8OrGreater()) {
#ifdef _WIN32
				HMONITOR hmon = MonitorFromPoint(POINT(posOnScreen.x, posOnScreen.y), MONITOR_DEFAULTTONEAREST);
				unsigned int dpiy = 96, dpix = 96;
				HRESULT hr = GetDpiForMonitor(hmon, MDT_EFFECTIVE_DPI, &dpix, &dpiy);
				if (hr == S_OK)
					Options.FontsRescale(dpiy);
#endif
			}
			wxRect rt = GetMonitorRect1(0/*-1*/, nullptr, wxRect(posOnScreen.x, posOnScreen.y, 1, 1));
			if (rt.width != msizex || rt.height != msizey) {
				int mposx, mposy, vsizex, vsizey;
				Options.GetCoords(MONITOR_POSITION, &mposx, &mposy);
				Options.GetCoords(VIDEO_WINDOW_SIZE, &vsizex, &vsizey);
				int audioHeight = Options.GetInt(AUDIO_BOX_HEIGHT);
				HikariLogSilent(wxString::Format(L"Audio Height %d", audioHeight));
				float scalex = (float)rt.width / (float)msizex;
				float scaley = (float)rt.height / (float)msizey;
				//program position
				posx -= mposx;
				posy -= mposy;
				posx *= scalex;
				posy *= scaley;
				posx += rt.x;
				posy += rt.y;
				//style manager
				sposx -= mposx;
				sposy -= mposy;
				sposx *= scalex;
				sposy *= scaley;
				sposx += rt.x;
				sposy += rt.y;
				//program size
				sizex *= scalex;
				sizey *= scaley;
				//video size
				vsizex *= scalex;
				vsizey *= scaley;
				audioHeight *= scaley;
				Options.SetCoords(VIDEO_WINDOW_SIZE, vsizex, vsizey);
				Options.SetCoords(MONITOR_SIZE, rt.width, rt.height);
				Options.SetCoords(MONITOR_POSITION, rt.x, rt.y);
				Options.SetInt(AUDIO_BOX_HEIGHT, audioHeight);
				HikariLogSilent(wxString::Format(L"Audio Height scaled %d", audioHeight));
			}
			if (sizex > rt.width) {
				sizex = rt.width;
			}
			if (sizey > rt.height) {
				sizey = rt.height - 100;
			}
			//check position of spellchecker
			if (!rt.Contains(wxRect(sposx, sposy, 400, 800))) {
				sposx = rt.x + ((float)(rt.width - 400) / 2.f);
				sposy = rt.y + ((float)(rt.height - 800) / 2.f);
			}
			//check position of main window
			if (!rt.Contains(wxRect(posx, posy, sizex, sizey))) {
				posx = rt.x + ((float)(rt.width - sizex) / 2.f),
				posy = rt.y + ((float)(rt.height - sizey) / 2.f);
			}
		}
		if (sizex < 1000 || sizey < 700) {
			sizex = 1000; sizey = 700;
		}

		Options.SetCoords(WINDOW_POSITION, posx, posy);
		Options.SetCoords(WINDOW_SIZE, sizex, sizey);
		Options.SetCoords(STYLE_MANAGER_POSITION, sposx, sposy);

		Frame = nullptr;
		Frame = new HikariSubFrame(wxPoint(posx, posy), wxSize(sizex, sizey));

		//handler for out of memory in new
		std::set_new_handler(OnOutofMemory);
		//start listen to font folders notifications
		FontEnum.StartListening();
		
		if (isGood == 2)
			Frame->CenterOnScreen();

		bool opevent = false;
		bool hasPaths = paths.GetCount() > 0;
		if (hasPaths){
			if (Options.GetBool(VIDEO_FULL_SCREEN_ON_START)){
				Frame->OpenFiles(paths, false, true);
				Frame->GetTab()->video->Layout();
			}
			else{
				opevent = true;

			}
		}


		Frame->Show();
		SetTopWindow(Frame);
		openTimer.SetOwner(this, 1199);
		Bind(wxEVT_TIMER, &hikarisubApp::OnOpen, this, 1199);
		if (opevent){
			openTimer.Start(500, true);
		}
		bool loadCrashSession = false;
#if _DEBUG
		bool loadSession = true;
#else
		int session = Options.GetInt(LAST_SESSION_CONFIG);
		bool loadSession = session == 2 && !hasPaths;
		if (session == 1 && !hasPaths){
			if (HikariMessageBox(_("Load last session?"), _("Prompt"), wxYES_NO, Frame) == wxYES){
				loadSession = true;
			}
		}
		//Check if program was bad close or crashed
		if (!hasPaths && !loadSession && Notebook::CheckLastSession() == 2) {
			if (HikariMessageBox(_("The program crashed or was closed improperly.\nLoad the last session with the latest autosaved subtitles?"), _("Prompt"), wxYES_NO, Frame) == wxYES) {
				loadCrashSession = loadSession = true;
			}
		}
#endif
		if (loadSession){
			debugtimer.SetOwner(this, 2299);
			debugtimer.Start(100, true);
			Bind(wxEVT_TIMER, [=, this](wxTimerEvent &evt){
				Frame->Tabs->LoadLastSession(loadCrashSession);
			}, 2299);
		}

#ifndef _WIN32
		if (const char* autoExitMsText = std::getenv("HIKARISUB_AUTOMATION_EXIT_MS")){
			long autoExitMs = std::strtol(autoExitMsText, nullptr, 10);
			if (autoExitMs < 1)
				autoExitMs = 1000;
			automationExitTimer.SetOwner(this, 3299);
			Bind(wxEVT_TIMER, [this](wxTimerEvent &evt){
				for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst(); node; node = node->GetNext()){
					wxWindow* window = node->GetData();
					HikariDialog* hikariDialog = (window && window->IsKindOf(CLASSINFO(HikariDialog))) ? static_cast<HikariDialog*>(window) : nullptr;
					if (hikariDialog && hikariDialog->IsShown() && hikariDialog->IsModal()){
						int escapeId = hikariDialog->GetEscapeId();
						hikariDialog->EndModal(escapeId != wxID_NONE ? escapeId : wxID_CANCEL);
						automationExitTimer.Start(250, true);
						return;
					}
					wxDialog* wxDialogWindow = (window && window->IsKindOf(CLASSINFO(wxDialog))) ? static_cast<wxDialog*>(window) : nullptr;
					if (wxDialogWindow && wxDialogWindow->IsShown() && wxDialogWindow->IsModal()){
						int escapeId = wxDialogWindow->GetEscapeId();
						wxDialogWindow->EndModal(escapeId != wxID_NONE ? escapeId : wxID_CANCEL);
						automationExitTimer.Start(250, true);
						return;
					}
				}
				if (Frame){
					// Frame->Close() can synchronously open a modal "save changes?" dialog.
					// Re-arm the automation timer before closing so the modal loop has a
					// chance to receive the next tick and dismiss the dialog.
					automationExitTimer.Start(250, true);
					Frame->Close();
				}
			}, 3299);
			automationExitTimer.Start(autoExitMs, true);
		}
#endif

		wxString path = Options.GetString(EXTERNAL_FONTS_DIRECTORY);
		if(!path.empty())
			FontEnum.LoadExternalFontsToProcessFromThread(path);

	}
	else{
#ifndef _WIN32
		wxString subs = JoinOpenPaths(arguments);
#else
		wxString subs;
		for (size_t i = 0; i < arguments.size(); i++) {
			subs.Append(arguments[i]);
			if (i + 1 != arguments.size()){ subs += L"|"; }
		}
#endif

		delete m_checker; // OnExit() won't be called if we return false
		m_checker = nullptr;
		if (subs.empty())
			return false;
#ifndef _WIN32
		if (!SendOpenPathsToRunningInstance(subs)){
			HikariLogSilent(wxString::Format(L"Cannot pass files to an already running instance on this platform: %s", subs));
		}
		return false;
#else
		//damn wxwidgets, why class name is not customizable?    
		int count = 0;
		HWND hWnd = nullptr;
		while (!hWnd){
			//prevent to total dedlock, when main HikariSub is crashed or closed
			if (count > 100){
				HikariLogSilent(wxString::Format(L"Cannot open: %s", subs));
				return false;
			}

			hWnd = FindWindow(L"HikariSub_main_windowNR", 0);
			//wait to can find it next time
			Sleep(40);
			count++;
		}
		//hwnd here must exist
		const wchar_t *text = subs.wc_str();
		COPYDATASTRUCT cds;
		cds.cbData = (subs.length() + 1) * sizeof(wchar_t);
		cds.lpData = (void *)text;
		SendMessage(hWnd, WM_COPYDATA, 0, (LPARAM)&cds);
		
		return false;
#endif
	}

	return true;

}

int hikarisubApp::OnExit()
{
#ifndef _WIN32
	if (m_ipcServer){ delete m_ipcServer; m_ipcServer = nullptr; }
	RemoveHikariSubIpcSocket();
#endif
	if (m_checker){ delete m_checker; m_checker = nullptr; }
	wxDELETE(locale);
	// The AppImage watchdog and the relaunch after a package install, once
	// the windows, the lock and the socket are gone.
	update_installer::AfterExit();
	return 0;
}

int hikarisubApp::FilterEvent(wxEvent& event)
{
#ifndef _WIN32
	const wxEventType type = event.GetEventType();
	if (type == wxEVT_ENTER_WINDOW || type == wxEVT_MOTION || type == wxEVT_LEAVE_WINDOW){
		static wxWeakRef<wxWindow> lastTooltipWindow = nullptr;
		wxWindow *eventWindow = wxDynamicCast(event.GetEventObject(), wxWindow);
		HikariSubFrame *frame = HikariSubFrame::Get();
		if (eventWindow && frame){
			wxString tip = eventWindow->GetToolTipText();
			if (tip != emptyString){
				if (type == wxEVT_LEAVE_WINDOW){
					if (lastTooltipWindow == eventWindow){ lastTooltipWindow = nullptr; }
					frame->SetStatusText(emptyString, 0);
					// Keep wxGTK native tooltip popups disabled for owner-drawn controls.
					wxToolTip::Enable(false);
				}
				else{
					lastTooltipWindow = eventWindow;
					tip = tip.BeforeFirst(L'\n');
					frame->SetStatusText(tip, 0);
				}
			}
			else if (type == wxEVT_MOTION && lastTooltipWindow){
				wxWindow *top = wxGetTopLevelParent(lastTooltipWindow);
				const wxPoint mouse = wxGetMousePosition();
				if (!top || !wxRect(top->GetScreenPosition(), top->GetSize()).Contains(mouse)){
					lastTooltipWindow = nullptr;
					frame->SetStatusText(emptyString, 0);
					wxToolTip::Enable(false);
				}
			}
		}
	}
	if (type == wxEVT_LEFT_DOWN || type == wxEVT_RIGHT_DOWN || type == wxEVT_MIDDLE_DOWN ||
		type == wxEVT_LEFT_UP || type == wxEVT_RIGHT_UP || type == wxEVT_MIDDLE_UP){
		wxWindow *eventWindow = wxDynamicCast(event.GetEventObject(), wxWindow);
		if (PopupList::DismissOnExternalClick(eventWindow)){ return 1; }
		if (MenuDialog::DismissOnExternalClick(eventWindow)){ return 1; }
	}
	if (type == wxEVT_KEY_DOWN || type == wxEVT_CHAR_HOOK) {
		wxKeyEvent* keyEvent = dynamic_cast<wxKeyEvent*>(&event);
		if (keyEvent) {
			if (MenuBar::HandleNavKeyIfOpen(*keyEvent, type == wxEVT_CHAR_HOOK)) { return 1; }
			int key = keyEvent->GetKeyCode();
			if (key == WXK_ESCAPE || key == L'B' || key == L'b') {
				TabPanel* tab = Notebook::GetTab();
				if (tab && tab->video && tab->video->IsFullScreen()) {
					tab->video->SetFullscreen(false);
					return 1;
				}
			}
		}
	}
#endif
	return wxApp::FilterEvent(event);
}

//void hikarisubApp::OnUnhandledException()
//{
//	//wxString recover;
//	//for(size_t i=0;i<Frame->Tabs->Size();i++)
//		//{
//		//recover<<"Sub"<<i<<": "<<Frame->Tabs->Page(i)->SubsPath<<"\r\n"
//			//<<"Vid"<<i<<": "<<Frame->Tabs->Page(i)->VideoPath<<"\r\n";
//		//}
//	//recover<<Options.GetString("Subs Recent")<<Options.GetString("video Recent");
//	//OpenWrite op;
//	//op.FileWrite(Options.pathfull+"\\recover.txt",Options.pathfull+"\\recover.txt");
//	//Options.SaveOptions();
//
//	wLogStatus(_T("Ups, HikariSub się skraszował w przyszłości będzie można wznowić sesję po tym kraszu"), "Krasz", wxOK | wxICON_ERROR);
//}

void hikarisubApp::OnFatalException()
{
	//wxString recover;
	//for(size_t i=0;i<Frame->Tabs->Size();i++)
	//{
	//recover<<"Sub"<<i<<": "<<Frame->Tabs->Page(i)->SubsPath<<"\r\n"
	//<<"Vid"<<i<<": "<<Frame->Tabs->Page(i)->VideoPath<<"\r\n";
	//}
	//recover<<Options.GetString("Subs Recent")<<"\r\n"<<Options.GetString("video Recent");
	//OpenWrite op;
	//op.FileWrite(Options.pathfull+"\\recover.txt",recover);
	//Options.SaveOptions();

	//HikariMessageBox(_T("Ups, HikariSub się skraszował w przyszłości będzie można wznowić sesję po tym kraszu"), "Krasz", wxOK | wxICON_ERROR);
}
void hikarisubApp::OnOpen(wxTimerEvent &evt)
{
	if (!IsBusy()){
		if (Frame->IsIconized()){ Frame->Iconize(false); }
		Frame->Raise();
		Frame->OpenFiles(paths, false);
		paths.Clear();
	}
	else{
		openTimer.Start(100, true);
	}
}

bool hikarisubApp::IsBusy()
{
	wxWindowList children = Frame->GetChildren();
	for (wxWindowList::compatibility_iterator node = children.GetFirst(); node; node = node->GetNext()) {
		wxWindow *current = (wxWindow *)node->GetData();
		if ((current->IsKindOf(CLASSINFO(HikariDialog)) && ((HikariDialog*)current)->IsModal()) ||
			(current->IsKindOf(CLASSINFO(wxDialog)) && ((wxDialog*)current)->IsModal()) || current->GetId() == 31555)
		{
			return true;
		}

	}
	return false;
}

