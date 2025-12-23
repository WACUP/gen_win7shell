#define PLUGIN_VERSION L"4.13.2"

#define NR_BUTTONS 15

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <gdiplus.h>
#include <process.h>

#include "gen_win7shell.h"
#include <loader/loader/utils.h>
#include <loader/loader/runtime_helper.h>
#include <loader/hook/squash.h>
#include <loader/hook/plugins.h>
#include <sdk/winamp/wa_cup.h>
#include <sdk/winamp/wa_msgids.h>
#include <sdk/Agave/Language/lang.h>
#include <sdk/winamp/ipc_pe.h>
#include <common/wa_prefs.h>
#include "resource.h"
#include "api.h"
#include "tools.h"
#include "metadata.h"
#include "jumplist.h"
#include "settings.h"
#include "taskbar.h"
#include "renderer.h"

// TODO add to lang.h
// Taskbar Integration plugin (gen_win7shell.dll)
// {0B1E9802-CA15-4939-8445-FD800E8BFF9A}
static const GUID GenWin7PlusShellLangGUID = 
{ 0xb1e9802, 0xca15, 0x4939, { 0x84, 0x45, 0xfd, 0x80, 0xe, 0x8b, 0xff, 0x9a } };

SETUP_API_LNG_VARS;

UINT WM_TASKBARBUTTONCREATED = (UINT)-1;
//std::wstring AppID;	// this is updated on loading to what the
					// running WACUP install has generated as
					// it otherwise makes multiple instances
					// tricky to work with independently

bool thumbshowing = false, no_uninstall = true, classicSkin = true,
	 windowShade = false, modernSUI = false, modernFix = false,
	 finishedLoad = false, running = false, closing = false;
HWND ratewnd = 0, dialogParent = 0;
int pladv = 1, repeat = 0;
#ifdef USE_MOUSE
HHOOK hMouseHook = NULL;
#endif
SettingsManager *SManager = NULL;
sSettings Settings = { 0 };
std::vector<int> TButtons;
iTaskBar *itaskbar = NULL;
MetaData *metadata = NULL;
renderer *thumbnaildrawer = NULL;
HANDLE updatethread = NULL, setupthread = NULL;
CRITICAL_SECTION g_cs[4] = { 0 };
HIMAGELIST theicons = NULL, overlayicons = NULL;

api_albumart *WASABI_API_ALBUMART = 0;
api_playlists *WASABI_API_PLAYLISTS = 0;
//api_explorerfindfile *WASABI_API_EXPLORERFINDFILE = 0;

// CALLBACKS
VOID CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
LRESULT CALLBACK rateWndProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);
#ifdef USE_MOUSE
LRESULT CALLBACK KeyboardEvent(int nCode, WPARAM wParam, LPARAM lParam);
#endif

WA_UTILS_API HBITMAP GetMainWindowBmp(const bool done);

#ifndef _WIN64
WA_UTILS_API const bool IsWasabiWindow(HWND hwnd);
WA_UTILS_API const bool IsModernSkinActive(const wchar_t** skin);
#endif

extern "C" __declspec(dllexport) LRESULT CALLBACK TabHandler_Taskbar(HWND, UINT, WPARAM, LPARAM);
extern "C" __declspec(dllexport) LRESULT CALLBACK TabHandler_Thumbnail(HWND, UINT, WPARAM, LPARAM);
extern "C" __declspec(dllexport) LRESULT CALLBACK TabHandler_ThumbnailImage(HWND, UINT, WPARAM, LPARAM);

HIMAGELIST GetThumbnailIcons(const bool force_refresh);
void updateToolbar(HIMAGELIST ImageList = NULL);
void SetThumbnailTimer(void);
void AddStringtoList(HWND window, const int control_ID);

// Winamp EVENTS
int init(void);
void config(void);
void quit(void);

void __cdecl MessageProc(HWND hWnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam);

// this structure contains plugin information, version, name...
winampGeneralPurposePlugin plugin =
{
	(char*)L"Taskbar Integration",
	GPPHDR_VER_WACUP,
	init, config, quit,
	GEN_INIT_WACUP_HAS_MESSAGES
};

DWORD WINAPI SetupJumpListThread(LPVOID lp)
{
	if (!closing && SUCCEEDED(CreateCOM()))
	{
		JumpList *jl = new JumpList(true);
		if (jl != NULL)
		{
			if (Settings.JLbms || Settings.JLfrequent ||
				Settings.JLpl || Settings.JLrecent || Settings.JLtasks)
			{
				static wchar_t *pluginPath, *shortLoaderPath, *tmp1, *tmp2, *tmp3, *tmp4;

				// to ensure things work reliably we
				// need an 8.3 style filepath for us
				if (!pluginPath)
				{
					wchar_t path[MAX_PATH]/* = { 0 }*/;
					if (GetModuleFileName(plugin.hDllInstance, path, ARRAYSIZE(path)))
					{
						const DWORD len = GetShortPathName(path, path, ARRAYSIZE(path));
						pluginPath = SafeWideDupN(path, len);
					}
				}

				if (!shortLoaderPath)
				{
					wchar_t path[MAX_PATH]/* = { 0 }*/;
					const DWORD len = GetShortPathName(GetPaths()->wacup_loader_exe,
															 path, ARRAYSIZE(path));
					shortLoaderPath = SafeWideDupN(path, len);
				}

				if (!tmp1)
				{
					tmp1 = LngStringDup(IDS_WINAMP_PREFERENCES);
				}

				if (!tmp2)
				{
					tmp2 = LngStringDup(IDS_OPEN_FILE);
				}

				if (!tmp3)
				{
					tmp3 = LngStringDup(IDS_BOOKMARKS);
				}

				if (!tmp4)
				{
					tmp4 = LngStringDup(IDS_PLAYLISTS);
				}

				__try
				{
					// based on testing, this & things in the
					// CreateShellLink() sometimes fails :'(
					// I can't find any reason for it. due to
					// that it is necessary to try & catch it
					// so we don't take down the entire thing
					jl->CreateJumpList(pluginPath, shortLoaderPath, tmp1, tmp2, tmp3, tmp4,
									   Settings.JLrecent, Settings.JLfrequent,
									   Settings.JLtasks, Settings.JLbms,
									   Settings.JLpl, closing);
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
				}
			}
			delete jl;
		}

		// Create the taskbar interface
		itaskbar = new iTaskBar(Settings);
		if ((itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX)) && itaskbar->Reset())
		{
			updateToolbar(GetThumbnailIcons(false));
		}

		if (Settings.VuMeter)
		{
			SetTimer(plugin.hwndParent, 6668, 66, TimerProc);
		}

		CloseCOM();
	}

	if (setupthread != NULL)
	{
		CloseHandle(setupthread);
		setupthread = NULL;
	}
	return 0;
}

void StartSetupJumpList(void)
{
	if (!closing && (!CheckThreadHandleIsValid(&setupthread)))
	{
		setupthread = StartThread(SetupJumpListThread, 0, THREAD_PRIORITY_NORMAL, 0, NULL);
	}
}

MetaData* get_metadata(void)
{
	if (metadata == NULL)
	{
		metadata = new MetaData();
	}
	return metadata;
}

bool CreateThumbnailDrawer(const bool always_create = true)
{
	if ((thumbnaildrawer == NULL) && always_create)
	{
		// Create thumbnail renderer
		thumbnaildrawer = new renderer(Settings, *get_metadata());
	}

	return (thumbnaildrawer != NULL);
}

#if 0
const bool GenerateAppIDFromFolder(const wchar_t *search_path, wchar_t *app_id)
{
	CreateCOM();

	IKnownFolderManager* pkfm = NULL;
	HRESULT hr = CreateCOMInProc(CLSID_KnownFolderManager,
				 __uuidof(IKnownFolderManager), (LPVOID*)&pkfm);
	if (SUCCEEDED(hr))
	{
		IKnownFolder* pFolder = NULL;
		// use FFFP_NEARESTPARENTMATCH instead of FFFP_EXACTMATCH so
		// it'll better cope with the possible variations in location
		if (SUCCEEDED(pkfm->FindFolderFromPath(search_path, FFFP_NEARESTPARENTMATCH, &pFolder)))
		{
			wchar_t *path = 0;
			if (SUCCEEDED(pFolder->GetPath(0, &path)))
			{
				// we now check that things are a match
				const size_t len = wcslen(path);
				if (SameStrN(search_path, path, len))
				{
					// and if they are then we'll merge
					// the two together to get the final
					// version of the string for an appid
					KNOWNFOLDERID pkfid = {0};
					if (SUCCEEDED(pFolder->GetId(&pkfid)))
					{
						wchar_t szGuid[40] = {0};
						StringFromGUID2(pkfid, szGuid, ARRAYSIZE(szGuid));
						PrintfCch(app_id, MAX_PATH, L"%s%s", szGuid, &search_path[len]);
					}
				}
			}
			MemFreeCOM(path);
			pFolder->Release();
			return (!!app_id[0]);
		}

		pkfm->Release();
	}
	return false;
}

LPCWSTR GetAppID(void)
{
	// we do this to make sure we can group things correctly
	// especially if used in a plug-in in WACUP so that the
	// taskbar handling will be correct vs existing pinnings
	// under WACUP, we do a few things so winamp.original is
	// instead re-mapped to wacup.exe so the load is called
	if (AppID.empty())
	{
		LPWSTR id = NULL;
		GetCurrentProcessExplicitAppUserModelID(&id);
		if (!id)
		{
			wchar_t self_path[MAX_PATH]/* = { 0 }*/;
			if (GetModuleFileName(NULL, self_path, ARRAYSIZE(self_path)))
			{
				wchar_t app_id[MAX_PATH] = { 0 };
				if (!GenerateAppIDFromFolder(self_path, app_id))
				{
					CopyCchStr(app_id, ARRAYSIZE(app_id), self_path);
				}

				RenameExtension(app_id, L".exe");

				// TODO: auto-pin icon (?)
				if (SetCurrentProcessExplicitAppUserModelID(app_id) != S_OK)
				{
					MessageBox(plugin.hwndParent, LangString(IDS_ERROR_SETTING_APPID),
										  (LPWSTR)plugin.description, MB_ICONWARNING);
				}
				else
				{
					AppID = app_id;
				}
			}
		}
		else
		{
			AppID = id;
			MemFreeCOM(id);
		}

		if (AppID.empty())
		{
			AppID = L"WACUP";
		}
	}
	return AppID.c_str();
}
#endif

// event functions follow
int init(void)
{
	/************************************************************************/
	/* Winamp services                                                      */
	/************************************************************************/
	//ServiceBuild(plugin.service, WASABI_API_MEMMGR, memMgrApiServiceGuid);
	//ServiceBuild(plugin.service, WASABI_API_ALBUMART, albumArtGUID);
	WASABI_API_ALBUMART = plugin.albumart;
	//ServiceBuild(plugin.service, WASABI_API_PLAYLISTS, api_playlistsGUID);
	WASABI_API_PLAYLISTS = plugin.playlists;
	//ServiceBuild(plugin.service, WASABI_API_LNG, languageApiGUID);

	StartPluginLangWithDesc(plugin.hDllInstance, GenWin7PlusShellLangGUID,
					IDS_PLUGIN_NAME, PLUGIN_VERSION, &plugin.description);

	for (int i = 0; i < ARRAYSIZE(g_cs); i++)
	{
		InitializeCriticalSectionEx(&g_cs[i], 400, CRITICAL_SECTION_NO_DEBUG_INFO);
	}

	return GEN_INIT_SUCCESS;/*/
	return GEN_INIT_FAILURE;/**/
}

void config(void)
{
	HMENU popup = CreatePopupMenu();

	AddItemToMenu(popup, 128, (LPWSTR)plugin.description);
	EnableMenuItem(popup, 128, MF_BYCOMMAND | MF_GRAYED | MF_DISABLED);
	AddItemToMenu(popup, (UINT)-1, 0);
	AddItemToMenu(popup, 2, LangString(IDS_OPEN_TASKBAR_PREFS));
	AddItemToMenu(popup, (UINT)-1, 0);
	AddItemToMenu(popup, 1, LangString(IDS_ABOUT));

	POINT pt = { 0 };
	HWND list = GetPrefsListPos(&pt);
	switch (TrackPopupMenu(popup, TPM_RETURNCMD, pt.x, pt.y, 0, list, NULL))
	{
		case 1:
		{
			wchar_t text[1024]/* = { 0 }*/;

			const unsigned char* output = DecompressResourceText(plugin.hDllInstance,
												  plugin.hDllInstance, IDR_ABOUT_GZ);

			PrintfCch(text, ARRAYSIZE(text), (LPCWSTR)output, WACUP_Author(),
										  WACUP_Copyright(), TEXT(__DATE__));

			DecompressResourceFree(output);

			AboutMessageBox(list, text, (LPWSTR)plugin.description);
			break;
		}
		case 2:
		{
			// leave the WACUP core to handle opening
			// to the 'taskbar' preferences node
			OpenPrefsPage((WPARAM)-667);
			break;
		}
	}

	DestroyMenu(popup);
}

void quit(void)
{
	closing = true;
	running = false;

	KillTimer(plugin.hwndParent, 6667);
	KillTimer(plugin.hwndParent, 6668);
	KillTimer(plugin.hwndParent, 6670);
	KillTimer(plugin.hwndParent, 6671);
	KillTimer(plugin.hwndParent, 6672);
	KillTimer(plugin.hwndParent, 6673);

	WaitForThreadToClose(&updatethread, INFINITE);
	WaitForThreadToClose(&setupthread, INFINITE);

#ifdef USE_MOUSE
	if (hMouseHook != NULL)
	{
		UnhookWindowsHookEx(hMouseHook);
	}
#endif

	if (itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX))
	{
		delete itaskbar;
		itaskbar = NULL;
	}

	if (thumbnaildrawer != NULL)
	{
		delete thumbnaildrawer;
		thumbnaildrawer = NULL;
	}

	//ServiceRelease(plugin.service, WASABI_API_MEMMGR, memMgrApiServiceGuid);
	//ServiceRelease(plugin.service, WASABI_API_ALBUMART, albumArtGUID);
	//ServiceRelease(plugin.service, WASABI_API_PLAYLISTS, api_playlistsGUID);
	//ServiceRelease(plugin.service, WASABI_API_LNG, languageApiGUID);
	//ServiceRelease(plugin.service, WASABI_API_EXPLORERFINDFILE, ExplorerFindFileApiGUID);

	for (int i = 0; i < ARRAYSIZE(g_cs); i++)
	{
		DeleteCriticalSection(&g_cs[i]);
	}
}

void updateToolbar(HIMAGELIST ImageList)
{
	if ((itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX)) &&
				 Settings.Thumbnailbuttons && plugin.messages)
	{
		const size_t count = TButtons.size();
		std::vector<THUMBBUTTON> thbButtons(count);
		for (size_t i = 0; i < count; ++i)
		{
			THUMBBUTTON& button = thbButtons[i];

			button.dwMask = THB_BITMAP | THB_TOOLTIP;
			button.iId = (UINT)TButtons[i];
			button.iBitmap = 0;
			button.hIcon = NULL;
			button.szTip[0] = 0;
			button.dwFlags = THBF_ENABLED;

			if (button.iId == TB_RATE || button.iId == TB_STOPAFTER ||
				button.iId == TB_DELETE || button.iId == TB_JTFE ||
				button.iId == TB_OPENEXPLORER)
			{
				button.dwMask = button.dwMask | THB_FLAGS;
				button.dwFlags = THBF_DISMISSONCLICK;
			}
			else if (button.iId == TB_PLAYPAUSE)
			{
				button.iBitmap = tools::getBitmap(button.iId, !!(Settings.play_state == PLAYSTATE_PLAYING));
				tools::getToolTip(TB_PLAYPAUSE, button.szTip, ARRAYSIZE(button.szTip), Settings.play_state);
			}
			else if (button.iId == TB_REPEAT)
			{
				button.iBitmap = tools::getBitmap(button.iId, Settings.state_repeat);
				tools::getToolTip(TB_REPEAT, button.szTip, ARRAYSIZE(button.szTip), Settings.state_repeat);
			}
			else if (button.iId == TB_SHUFFLE)
			{
				button.iBitmap = tools::getBitmap(button.iId, Settings.play_state == Settings.state_shuffle);
				tools::getToolTip(TB_SHUFFLE, button.szTip, ARRAYSIZE(button.szTip), Settings.state_shuffle);
			}

			if (!button.iBitmap)
			{
				button.iBitmap = tools::getBitmap(button.iId, 0);
			}

			if (!button.szTip[0])
			{
				tools::getToolTip(button.iId, button.szTip, ARRAYSIZE(button.szTip), 0);
			}
		}

		if (itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX))
		{
			__try
			{
				itaskbar->ThumbBarUpdateButtons(thbButtons, ImageList);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
			}
		}
	}
}

BOOL CALLBACK checkSkinProc(HWND hwnd, LPARAM lParam)
{
#ifndef _WIN64
	if (IsWasabiWindow(hwnd))
	{
		// if any of these are a child window of
		// the current skin being used then it's
		// very likely it's a SUI modern skin...
		// BaseWindow_RootWnd -> BaseWindow_RootWnd -> Winamp *
		HWND child = GetWindow(hwnd, GW_CHILD);
		if (IsWindow(child))
		{
			wchar_t cl[16]/* = { 0 }*/;
			if (GetClassName(child, cl, ARRAYSIZE(cl)) &&
				(SameStrN(cl, L"Winamp EQ", 9) ||
				 SameStrN(cl, L"Winamp PE", 9) ||
				 SameStrN(cl, L"Winamp Gen", 10) ||
				 SameStrN(cl, L"Winamp Video", 12)))
			{
				modernSUI = true;
				return FALSE;
			}
		}
	}
#endif
	(void)lParam;
	return TRUE;
}

void updateRepeatButton(void)
{
	// update repeat state
	int current_repeat_state = repeat;
	if (current_repeat_state == 1 && pladv == 1)
	{
		current_repeat_state = 2;
	}

	if (current_repeat_state != Settings.state_repeat)
	{
		Settings.state_repeat = current_repeat_state;
		updateToolbar();
	}
}

void UpdateLivePreview(void)
{
	static bool processing = false;
	if (!processing)
	{
		processing = true;

		// incase this is updating then we'll try to
		// ensure we're "live" updating to make this
		// look more like the default OS handling...
		if (running && classicSkin && (IsMainMinimised()/*/
			IsIconic(plugin.hwndParent)/**/ || (Settings.
					  Thumbnailbackground != BG_WINAMP)))
		{
			// the wacup core since 1.99.41 will cache
			// this for us which reduces the impact of
			// creating & updating it especially where
			// the user is running with a high refresh
			// rate screen to reduce the cpu involved.
			const HBITMAP main_window_bmp = GetMainWindowBmp(false);
			if (main_window_bmp != NULL)
			{
				// afaict this does not respect being
				// given a bitmap where alpha is set!
				// as the WACUP core will for classic
				// skins try to set the faux alpha as
				// needed which isn't an issue for it
				// as it uses regions to do clipping
				// but will cause those skin areas to
				// appear as black when drawn here...
				DwmSetIconicLivePreviewBitmap(plugin.hwndParent, main_window_bmp, NULL, 0);

				//DeleteObject(main_window_bmp);
			}
		}

		processing = false;
	}
}

DWORD WINAPI UpdateThread(LPVOID lp)
{
	(void)CreateCOM();

	while (running && CreateThumbnailDrawer())
	{
		const HBITMAP thumbnail = (running && (thumbnaildrawer != NULL) ?
								   thumbnaildrawer->GetThumbnail(false, false) : NULL);
		if (thumbnail != NULL)
		{
			HRESULT hr = S_OK;
			__try
			{
				hr = DwmSetIconicThumbnail(plugin.hwndParent, thumbnail, 0);

				UpdateLivePreview();
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
			}

			DeleteObject(thumbnail);

			if (!running || FAILED(hr))
			{
				running = false;
				SetThumbnailTimer();
				break;
			}
		}

		SleepEx((running ? (Settings.Thumbnailbackground == BG_WINAMP) ?
				(!Settings.LowFrameRate ? Settings.MFT : Settings.MST) :
				(!Settings.LowFrameRate ? Settings.TFT : Settings.TST) : 1000), TRUE);
	}

	// we're done so give the core a hint that
	// the cached preview image can be removed
	GetMainWindowBmp(true);

	CloseCOM();

	if (updatethread != NULL)
	{
		CloseHandle(updatethread);
		updatethread = NULL;
	}
	return 0;
}

void SetThumbnailTimer(void)
{
	SetTimer(plugin.hwndParent, 6670, (running ? (Settings.Thumbnailbackground == BG_WINAMP) ?
			 (!Settings.LowFrameRate ? Settings.MFT : Settings.MST) :
			 (!Settings.LowFrameRate ? Settings.TFT : Settings.TST) : 1000), TimerProc);

	KillTimer(plugin.hwndParent, 6671);

	if (!CheckThreadHandleIsValid(&updatethread))
	{
		updatethread = StartThread(UpdateThread, 0, THREAD_PRIORITY_NORMAL, 0, NULL);
	}

	SetTimer(plugin.hwndParent, 6671, 60000, TimerProc);
}

void ResetThumbnail(void)
{
	if (CreateThumbnailDrawer(false))
	{
		thumbnaildrawer->ClearAlbumart();
		thumbnaildrawer->ClearBackground(false);
		thumbnaildrawer->ClearCustomBackground();
		thumbnaildrawer->ClearFonts();
		thumbnaildrawer->ThumbnailPopup();
	}
}

HIMAGELIST GetThumbnailIcons(const bool force_refresh)
{
	EnterCriticalSection(&thumbnai_icons_cs);

	if (!theicons || force_refresh)
	{
		if (theicons)
		{
			ImageListDestroy(theicons);
			theicons = NULL;
		}

		theicons = tools::prepareIcons();
	}

	LeaveCriticalSection(&thumbnai_icons_cs);
	return theicons;
}

HIMAGELIST GetOverlayIcons(const bool force_refresh)
{
	EnterCriticalSection(&overlay_icons_cs);

	if (!overlayicons || force_refresh)
	{
		if (overlayicons)
		{
			ImageListDestroy(overlayicons);
			overlayicons = NULL;
		}

		overlayicons = tools::prepareOverlayIcons();
	}

	LeaveCriticalSection(&overlay_icons_cs);
	return overlayicons;
}

void UpdateOverlyStatus(const bool force_refresh)
{
	Settings.play_state = GetPlayingState();

	// ensure we're either updating the full imagelist when needed or that
	// we're going to be able to correctly show toggled play/paused states
	updateToolbar((force_refresh ? GetThumbnailIcons(force_refresh) : NULL));

	if (Settings.Overlay)
	{
		static wchar_t* paused_str = LngStringDup(IDS_PAUSED);
		HICON icon = NULL;
		switch (Settings.play_state)
		{
			case PLAYSTATE_PLAYING:
			case PLAYSTATE_PAUSED:
			{
				if (itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX))
				{
					const bool paused = (Settings.play_state == PLAYSTATE_PAUSED);
					const int index = tools::getBitmap(TB_PLAYPAUSE, paused);
					if ((index >= 0) && (index < tools::getBitmapCount()))
					{
						icon = ImageListGetIcon(GetOverlayIcons(force_refresh), (index - 1), 0);
						if (icon == NULL)
						{
							icon = ImageListGetIcon(GetThumbnailIcons(false), index, 0);
						}

						if (itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX))
						{
							static wchar_t* playing_str;
							itaskbar->SetIconOverlay(icon, (!paused ? (playing_str ? playing_str :
										(playing_str = LngStringDup(IDS_PLAYING))) : paused_str));
						}
					}
				}
				break;
			}
			default:
			{
				if (itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX))
				{
					const int index = tools::getBitmap(TB_STOP, 1);
					if ((index >= 0) && (index < tools::getBitmapCount()))
					{
						icon = ImageListGetIcon(GetOverlayIcons(force_refresh), index, 0);
						if (icon == NULL)
						{
							icon = ImageListGetIcon(GetThumbnailIcons(false/*force_refresh*/), index, 0);
						}

						if (itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX))
						{
							itaskbar->SetIconOverlay(icon, paused_str);
						}
					}
				}
				break;
			}
		}

		if (icon != NULL)
		{
			DestroyIcon(icon);
		}
	}
}

MetaData* reset_metadata(LPCWSTR filename, const bool force = false)
{
	MetaData* meta_data = get_metadata();
	if (meta_data != NULL)
	{
		meta_data->reset(filename, force);
	}
	return meta_data;
}

void __cdecl MessageProc(HWND hWnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
{
	if (uMsg == WM_DWMSENDICONICTHUMBNAIL)
	{
		if (CreateThumbnailDrawer())
		{
			// just update the dimensions and let the timer
			// process the rendering later on as is needed.
			thumbnaildrawer->SetDimensions(HIWORD(lParam), LOWORD(lParam));
			running = true;

			SetThumbnailTimer();
			DwmInvalidateIconicBitmaps(plugin.hwndParent);
		}
	}
	else if (uMsg == WM_DWMSENDICONICLIVEPREVIEWBITMAP)
	{
		UpdateLivePreview();
	}
	else if (uMsg == WM_WA_IPC)
	{
		switch (lParam)
		{
			case IPC_PLAYING_FILEW:
			{
				Settings.play_playlistpos = GetPlaylistPosition();

				EnterCriticalSection(&metadata_cs);

				std::wstring filename((wParam ? (wchar_t*)wParam : L""));
				if (filename.empty())
				{
					filename.resize(FILENAME_SIZE);
					size_t filename_len = 0;
					GetPlayingFilename(1, NULL, &filename[0], FILENAME_SIZE, &filename_len);
					filename.resize(filename_len);
				}

				MetaData* meta_data = reset_metadata(filename.c_str());

				LeaveCriticalSection(&metadata_cs);

				Settings.play_total = GetCurrentTrackLengthMilliSeconds();
				Settings.play_current = 0;
				Settings.play_state = GetPlayingState();

				if ((meta_data != NULL) && (Settings.JLrecent || Settings.JLfrequent) &&
					(Settings.play_state == PLAYSTATE_PLAYING) && Settings.Add2RecentDocs)
				{
					// these are used to help minimise the impact of directly
					// querying the file for multiple pieces of metadata &/or
					// if there's an issue with the local library db handling
					INT_PTR db_error = FALSE;
					void *token = NULL;
					bool reentrant = false, already_tried = false;

					__try
					{
						const std::wstring title(meta_data->getMetadata(L"title", &token, &reentrant, &already_tried,
												 &db_error) + L" - " + meta_data->getMetadata(L"artist", &token,
												 &reentrant, &already_tried, &db_error) + ((Settings.play_total > 0) ?
												 L"  (" + tools::SecToTime(Settings.play_total / 1000) + L")" : L""));

						IShellLink *psl = NULL;
						if ((tools::CreateShellLink(filename.c_str(), title.c_str(), &psl) == S_OK) && psl)
						{
							const SHARDAPPIDINFOLINK applink = { psl, GetAppID() };
							wchar_t temp[32]/* = { 0 }*/;
							temp[0] = 0;
							psl->SetDescription(TimeNow2Str(temp, ARRAYSIZE(temp), TRUE));
							// based on testing, this & things in the
							// CreateShellLink() sometimes fails :'(
							// I can't find any reason for it. due to
							// that it is necessary to try & catch it
							// so we don't take down the entire thing
							SHAddToRecentDocs(SHARD_APPIDINFOLINK, &applink);
						}

						// try to ensure we clean-up everything even if
						// the CreateShellLink failed e.g. on SetPath()
						if (psl)
						{
							psl->Release();
						}
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
					}

					plugin.metadata->FreeExtendedFileInfoToken(&token);
				}

				DwmInvalidateIconicBitmaps(hWnd);
				ResetThumbnail();
				break;
			}
			/*case IPC_SETDIALOGBOXPARENT:
			case IPC_UPDATEDIALOGBOXPARENT:*/
			// instead of checking for the above
			// we'll use this since WACUP 1.6.4+
			// which consolidates & filters out
			// duplicate messages to reduce work
			case IPC_CB_ONDIALOGPARENTCHANGE:
			{
				// we cache this now as winamp will have
				// cached it too by now so we're in-sync
				dialogParent = (HWND)wParam;
				if (!IsWindow(dialogParent))
				{
					dialogParent = hWnd;
				}

				// look at things that could need us to
				// force a refresh of the iconic bitmap
				SetThumbnailTimer();
				break;
			}
			case IPC_CB_ONTOGGLEMANUALADVANCE:
			{
				pladv = (int)wParam;
				updateRepeatButton();
				break;
			}
			case IPC_CB_ONTOGGLEREPEAT:
			{
				repeat = (int)wParam;
				updateRepeatButton();
				break;
			}
			case IPC_CB_ONTOGGLESHUFFLE:
			{
				Settings.state_shuffle = (int)wParam;
				updateToolbar();
				break;
			}
			case IPC_PLAYLIST_MODIFIED:
			{
				Settings.play_playlistlen = (int)wParam;
				break;
			}
			case IPC_ADDBOOKMARK:
			case IPC_ADDBOOKMARKW:
			{
				if (wParam)
				{
					SetTimer(plugin.hwndParent, 6673, 1000, TimerProc);
				}
				break;
			}
			case IPC_WACUP_IS_CLOSING:
			{
				// give things a nudge
				closing = true;
				running = false;

				if (GetTaskbarMode() && Settings.Overlay &&
					(itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX)))
				{
					// make sure this is done otherwise it is
					// possible especially if not set to show
					// in the taskbar but has a pinned icon &
					// it could end up stuck on the last mode
					itaskbar->SetIconOverlay(NULL, L"");
				}
				break;
			}
			case IPC_IS_MINIMISED_OR_RESTORED:
			case IPC_SKIN_CHANGED_NEW:
			{
				if (plugin.messages)
				{
					const bool minimised = (lParam == IPC_IS_MINIMISED_OR_RESTORED);
					if (!minimised)
					{
						// this is needed when the vu mode is enabled to allow the
						// data to be obtained if the main wnidow mode is disabled
						static void (__cdecl *export_sa_setreq)(int) = (void (__cdecl *)(int))GetSADataFunc(1);
						if (export_sa_setreq)
						{
							export_sa_setreq(Settings.VuMeter);
						}

#ifndef _WIN64
						LPCWSTR skin_name = NULL;
						classicSkin = !IsModernSkinActive(&skin_name);

						modernSUI = false;
						modernFix = (skin_name && *skin_name && SameStrN(skin_name, L"Winamp Modern", 13));
						if (!classicSkin)
						{
							// see if it's likely to be a SUI or not as
							// we need it to help determine how we will
							// capture the main window for the preview 
							// as WM_PRINTCLIENT is slow for SUI skins
							// but is needed for others especially if
							// we're wanting to do support alpha better
							EnumChildWindows(dialogParent, checkSkinProc, 0);
						}
#endif
					}

					if (itaskbar > reinterpret_cast<iTaskBar *>(USHRT_MAX))
					{
						itaskbar->SetWindowAttr();

						UpdateOverlyStatus(true);
					}
				}
				// fall-through for the other handling needed
			}
			default:
			{
				// make sure if not playing but prev / next is done
				// that we update the thumbnail for the current one
				if ((lParam == IPC_FILE_TAG_MAY_HAVE_UPDATEDW) ||
#ifndef _WIN64
					(lParam == IPC_FILE_TAG_MAY_HAVE_UPDATED) ||
#endif
					(lParam == IPC_CB_MISC) &&
					((wParam == IPC_CB_MISC_TITLE) ||
					(wParam == IPC_CB_MISC_AA_OPT_CHANGED) ||
					(wParam == IPC_CB_MISC_TITLE_RATING) ||
					(wParam == IPC_CB_MISC_ON_STOP) ||
					(wParam == IPC_CB_MISC_ADVANCED_NEXT_ON_STOP)))
				{
					EnterCriticalSection(&metadata_cs);

					wchar_t buffer[FILENAME_SIZE]/* = { 0 }*/;
					LPCWSTR p = GetPlayingFilename(0, NULL, buffer, ARRAYSIZE(buffer), NULL);
					if (p != NULL)
					{
						reset_metadata(p);
					}

					LeaveCriticalSection(&metadata_cs);

					DwmInvalidateIconicBitmaps(hWnd);
					ResetThumbnail();
				}
				else if ((lParam == IPC_CB_MISC) && (wParam == IPC_CB_MISC_STATUS))
				{
					UpdateOverlyStatus(false);
				}
				else if ((lParam == IPC_CB_MISC) && (wParam == IPC_CB_MISC_VOLUME))
				{
					Settings.play_volume = (int)GetSetVolume((WPARAM)-666, FALSE);
				}
				else if (lParam == IPC_WACUP_HAS_LOADED)
				{
					SetTimer(plugin.hwndParent, 6672, 100, TimerProc);
				}
				else if (lParam == IPC_WACUP_IS_CLOSING)
				{
					// to help avoid anything else being
					// done when wacup is starting close
					// then we'll stop responding to any
					// messages to avoid some crashes...
					plugin.messages = NULL;
				}
				break;
			}
		}
	}
	else if ((uMsg == WM_SYSCOMMAND) && (wParam == SC_CLOSE))
	{
		PostMessage(plugin.hwndParent, WM_COMMAND, 40001, 0);
	}
	else if (uMsg == WM_COMMAND)
	{
		if (HIWORD(wParam) == THBN_CLICKED)
		{
			switch (LOWORD(wParam))
			{
				case TB_PREVIOUS:
				case TB_NEXT:
				{
					SendMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(((LOWORD(wParam) == TB_PREVIOUS) ? 40044 : 40048), 0), 0);
					Settings.play_playlistpos = GetPlaylistPosition();

					if (Settings.Thumbnailbackground == BG_ALBUMART)
					{
						ResetThumbnail();

						if (Settings.play_state != PLAYSTATE_PLAYING)
						{
							DwmInvalidateIconicBitmaps(plugin.hwndParent);
						}
					}

					EnterCriticalSection(&metadata_cs);

					wchar_t buffer[FILENAME_SIZE]/* = { 0 }*/;
					LPCWSTR p = GetPlayingFilename(0, NULL, buffer, ARRAYSIZE(buffer), NULL);
					if (p != NULL)
					{
						reset_metadata(p);
					}

					LeaveCriticalSection(&metadata_cs);

					if (CreateThumbnailDrawer())
					{
						thumbnaildrawer->ThumbnailPopup();
					}
					break;
				}
				case TB_PLAYPAUSE:
				{
					const int res = GetPlayingState();
					PostMessage(plugin.hwndParent, WM_COMMAND,
								MAKEWPARAM(((res == 1) ?
								40046 : 40045), 0), 0);
					Settings.play_state = res;
					break;
				}
				case TB_STOP:
				{
					PostMessage(plugin.hwndParent, WM_COMMAND,
								MAKEWPARAM(40047, 0), 0);
					Settings.play_state = PLAYSTATE_NOTPLAYING; 
					break;
				}
				case TB_RATE:
				{
					ratewnd = LangCreateDialog(IDD_RATEDLG, plugin.hwndParent, rateWndProc, 0);
					if (IsWindow(ratewnd))
					{
						RECT rc = { 0 };
						POINT point = { 0 };
						GetCursorPos(&point);
						GetWindowRect(ratewnd, &rc);
						MoveWindow(ratewnd, (point.x - 245), (point.y - 15), (rc.right - rc.left), (rc.bottom - rc.top), false);
						SetTimer(plugin.hwndParent, 6669, 8000, TimerProc);
						ShowWindow(ratewnd, SW_SHOWNA);
					}
					break;
				}
				case TB_VOLDOWN:
				{
					Settings.play_volume -= 25;
					if (Settings.play_volume < 0)
					{
						Settings.play_volume = 0;
					}
					PostMessage(plugin.hwndParent, WM_WA_IPC, Settings.play_volume, IPC_SETVOLUME);
					break;
				}
				case TB_VOLUP:
				{
					Settings.play_volume += 25;
					if (Settings.play_volume > 255)
					{
						Settings.play_volume = 255;
					}
					PostMessage(plugin.hwndParent, WM_WA_IPC, Settings.play_volume, IPC_SETVOLUME);
					break;
				}
				case TB_OPENFILE:
				{
					PostMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)(HWND)0, IPC_OPENFILEBOX);
					break;
				}
				case TB_MUTE:
				{
					PostMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_TOGGLE_MUTE);
					break;
				}
				case TB_STOPAFTER:
				{
					PostMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(40157, 0), 0);
					break;
				}
				case TB_REPEAT:
				{
					// get
					Settings.state_repeat = repeat;
					if (Settings.state_repeat == 1 && pladv == 1)
					{
						Settings.state_repeat = 2;
					}

					++Settings.state_repeat;

					if (Settings.state_repeat > 2)
					{
						Settings.state_repeat = 0;
					}

					SendMessage(plugin.hwndParent, WM_WA_IPC, Settings.state_repeat >= 1 ? 1 : 0, IPC_SET_REPEAT);
					SendMessage(plugin.hwndParent, WM_WA_IPC, Settings.state_repeat == 2 ? 1 : 0, IPC_SET_MANUALPLADVANCE);            
					updateToolbar();
					break;
				}
				case TB_SHUFFLE:
				{
					Settings.state_shuffle = !Settings.state_shuffle;
					SendMessage(plugin.hwndParent, WM_WA_IPC, Settings.state_shuffle, IPC_SET_SHUFFLE);
					updateToolbar();
					break;
				}
				case TB_JTFE:
				{
					ShowWindow(plugin.hwndParent, SW_SHOWNORMAL);
					SetForegroundWindow(plugin.hwndParent);
					PostMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(WINAMP_JUMPFILE, 0), 0);
					break;
				}
				case TB_OPENEXPLORER:
				{
					const MetaData* meta_data = get_metadata();
					if (meta_data != NULL)
					{
						LPCWSTR filename = meta_data->getFileName();
						if (filename && *filename && !IsPathURL(filename))
						{
							plugin.explorerfindfile->AddAndShowFile(filename);
						}
					}
					break;
				}
				case TB_DELETE:
				{
					const MetaData* meta_data = (GetPlaylistAllowHardDelete() ? get_metadata() : NULL);
					if (meta_data != NULL)
					{
						wchar_t path[MAX_PATH]/* = { 0 }*/;
						CopyCchStr(path, ARRAYSIZE(path), meta_data->getFileName());

						const int saved_play_state = Settings.play_state;
						SendMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(40047, 0), 0);
						Settings.play_state = PLAYSTATE_NOTPLAYING;

						plugin.metadata->ClearCacheByFileType(path);

						SHFILEOPSTRUCT fileop = { 0, FO_DELETE, path, 0, (FILEOP_FLAGS)
												  ((GetPlaylistRecycleMode() ? FOF_ALLOWUNDO :
																0) | FOF_FILESONLY), 0, 0, 0 };
						if (!FileAction(&fileop))
						{
							/*SendMessage(GetPlaylistWnd(), WM_WA_IPC, IPC_PE_DELETEINDEX, GetPlaylistPosition());/*/
							PlaylistRemoveItem(GetPlaylistPosition());/**/
						}

						if (saved_play_state == PLAYSTATE_PLAYING)
						{
							PostMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(40045, 0), 0);
						}
					}
					break;
				}
			}
		}
		else
		{
			switch (LOWORD(wParam))
			{
				case WINAMP_OPTIONS_WINDOWSHADE_GLOBAL:
				{
					if (hWnd != GetForegroundWindow())
					{
						break;
					}
					// fall through
				}
				case WINAMP_OPTIONS_WINDOWSHADE:
				{
					windowShade = !windowShade;
					break;
				}
			}
		}
	}
	else if (uMsg == WM_TASKBARBUTTONCREATED)
	{
		if ((itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX)) && itaskbar->Reset())
		{
			updateToolbar(GetThumbnailIcons(true));
		}

		StartSetupJumpList();
	}
}

void setup_settings(void)
{
	if (!SManager)
	{
		SManager = new SettingsManager();

		if (SManager != NULL)
		{
			// Read Settings into struct
			SManager->ReadSettings(Settings, TButtons);
		}
	}
}

const bool checkIfStillNeeded(void)
{
	bool still_needed = false;
	// this is far from ideal as there's nothing obvious that windows
	// provides to determine once we have done showing the preview so
	// we'll try & see if the preview taskbar window is visible & the
	// win+tab task view & the alt+tab overlay to avoid drawing mode.
	static const HWND previewlist = FindWindowEx(NULL, NULL, L"TaskListThumbnailWnd", L"");
	if (IsWindow(previewlist))
	{
		still_needed = (GetWindowLongPtr(previewlist, GWL_STYLE) & WS_VISIBLE);
	}

	if (!still_needed)
	{
		static const HWND win11taskview = FindWindowEx(NULL, NULL, L"XamlExplorerHostIslandWindow", L"");
		if (IsWindow(win11taskview))
		{
			still_needed = (GetWindowLongPtr(win11taskview, GWL_STYLE) & WS_VISIBLE);
		}
	}

	if (!still_needed)
	{
		const HWND taskview = FindWindowEx(NULL, NULL, L"Windows.UI.Core.CoreWindow", L"Task View");
		if (IsWindow(taskview) && (GetForegroundWindow() == taskview))
		{
			still_needed = true;
		}
	}

	if (!still_needed)
	{
		// is the Win+Tab dialog on at least Win10
		const HWND win7taskview = FindWindowEx(NULL, NULL, L"Flip3D", L"");
		if (IsWindow(win7taskview) && (GetForegroundWindow() == win7taskview))
		{
			still_needed = true;
		}
	}

	if (!still_needed)
	{
		// this is ok for Windows 10 afaict & then reverts to the older version
		HWND alttab = FindWindowEx(NULL, NULL, L"MultitaskingViewFrame", L"Task Switching");
		if (IsWindow(alttab) && (GetForegroundWindow() == alttab))
		{
			still_needed = true;
		}
		else
		{
			alttab = FindWindowEx(NULL, NULL, L"TaskSwitcherWnd", L"Task Switching");
			if (IsWindow(alttab) && (GetForegroundWindow() == alttab))
			{
				still_needed = true;
			}
		}
	}
	return still_needed;
}

VOID CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	switch (idEvent)
	{
		case 6667:	// main timer
		{
			// Create jumplist
			static bool setup;
			if (!setup)
			{
				setup = true;
				StartSetupJumpList();
			}

			if (setupthread != NULL)
			{
				CheckThreadHandleIsValid(&updatethread);
				break;
			}

			if (!(Settings.Progressbar || Settings.VuMeter) &&
				(itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX)))
			{
				itaskbar->SetProgressState(TBPF_NOPROGRESS);
			}

			if (Settings.play_state == PLAYSTATE_PLAYING || Settings.Thumbnailpb)
			{
				Settings.play_total = GetCurrentTrackLengthMilliSeconds();
			}

			if (Settings.play_state != PLAYSTATE_NOTPLAYING)
			{
				Settings.play_kbps = (int)GetInfoIPC(1);
				Settings.play_khz = (int)GetInfoIPC(0);
			}
			else
			{
				Settings.play_kbps = Settings.play_khz = 0;
			}

			const int cp = GetCurrentTrackPos();
			if (Settings.play_current != cp)
			{
				Settings.play_current = cp;

				// to ensure the overlay icons will show whilst
				// not hammering things when playback commences
				// we just do a one time update to get it right
				if (!finishedLoad)
				{
					finishedLoad = true;
					UpdateOverlyStatus(false);
				}

				switch (Settings.play_state)
				{
					case PLAYSTATE_PLAYING: 
					{
						if (Settings.play_current == -1 ||
							Settings.play_total <= 0)
						{ 
							static unsigned char count2 = 0;
							if (count2 == 8)
							{   
								count2 = 0;

								EnterCriticalSection(&metadata_cs);

								reset_metadata(L"", true);

								LeaveCriticalSection(&metadata_cs);
							}
							else
							{
								++count2;
							}
						}

						if (Settings.Progressbar && (itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX)))
						{
							if (Settings.play_current == -1 || Settings.play_total <= 0)
							{
								itaskbar->SetProgressState(Settings.Streamstatus ? TBPF_INDETERMINATE : TBPF_NOPROGRESS);
							}
							else
							{
								itaskbar->SetProgressState(TBPF_NORMAL);
								itaskbar->SetProgressValue(Settings.play_current, Settings.play_total);
							}
						}
						break;
					}
					case PLAYSTATE_PAUSED:
					{
						if (Settings.Progressbar && (itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX)))
						{
							itaskbar->SetProgressState(TBPF_PAUSED);
							itaskbar->SetProgressValue(Settings.play_current, Settings.play_total);

							if (Settings.play_total == -1)
							{
								itaskbar->SetProgressValue(100, 100);
							}
						}
						break;
					}
					default:
					{
						if (Settings.Progressbar && (itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX)))
						{
							if (Settings.Stoppedstatus)
							{
								itaskbar->SetProgressState(TBPF_ERROR);
								itaskbar->SetProgressValue(100, 100);
							}
							else
							{
								itaskbar->SetProgressState(TBPF_NOPROGRESS);
							}
						}
						break;
					}
				}
			}
			break;
		}
		case 6668:	//vumeter proc
		{
			if (Settings.VuMeter && (itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX)))
			{
				// we try to use the vumeter but revert
				// to the sa mode and convert as needed
				// this also forces these modes to work
				// when using a classic skin & sa/vu is
				// disabled like gen_ff has to work ok.
				static int (__cdecl *export_vu_get)(const int channel) =
					   (int (__cdecl *)(const int))GetVUDataFunc();
				int audiodata = (export_vu_get ? export_vu_get(-1) : -1);

				if (Settings.play_state != PLAYSTATE_PLAYING)
				{
					itaskbar->SetProgressState(TBPF_NOPROGRESS);
				}
				else
				{
					if (audiodata == -1)
					{
						static char * (__cdecl *export_sa_get)(char data[75*2+8]) =
							   (char * (__cdecl *)(char data[75*2+8]))GetSADataFunc(2);
						static char data[75*2+8]/* = {0}*/;
						const char *p = (char *)(export_sa_get ? export_sa_get(data) : 0);
						if (p)
						{
							int m = 0;
							for (int i = 75; i < 150; i++)
							{
								m = max(abs(m), p[i]);
							}
							audiodata = min(255, m * 16);
						}
					}

					if (audiodata <= 0)
					{
						itaskbar->SetProgressState(TBPF_NOPROGRESS);
					}
					else if (audiodata <= 150)
					{
						itaskbar->SetProgressState(TBPF_NORMAL);
					}
					else if (audiodata < 210)
					{
						itaskbar->SetProgressState(TBPF_PAUSED);
					}
					else
					{
						itaskbar->SetProgressState(TBPF_ERROR);
					}

					itaskbar->SetProgressValue(audiodata, 255);
				}
			}
			break;
		}
		case 6669:	//rate wnd
		{
			if (IsWindow(ratewnd))
			{
				DestroyWindow(ratewnd);
			}

			KillTimer(plugin.hwndParent, 6669);
			break;
		}
		case 6670:	//scroll redraw
		{
			const bool old_running = running;

			// if we've found something then use the
			// faster update timer as needed other &
			// if not then we can drop it down so we
			// minimise any re-checking that is done
			running = checkIfStillNeeded();
			if (running != old_running)
			{
				SetThumbnailTimer();
			}
			break;
		}
		case 6671:	// long running check mostly for Windows 11
		{
			// it's necessary to see if there's still a long
			// running instance running & manually kill it &
			// hope that it's not going to clash with others
			if (running && !checkIfStillNeeded())
			{
				running = false;
				SetThumbnailTimer();
			}
			break;
		}
		case 6672:
		{
			KillTimer(plugin.hwndParent, 6672);

			// we track this instead of re-querying all of the
			// time so as to minimise blocking of the main wnd
			// which helps to determine modern vs classic skin
			dialogParent = GetDialogBoxParent()/*/(HWND)SendMessage(hWnd, WM_WA_IPC, 0, IPC_GETDIALOGBOXPARENT)/*/;

			pladv = !!GetManualAdvance();

			windowShade = !!IsHWNDWndshade((WPARAM)-1)/*/SendMessage(hWnd, WM_WA_IPC, (WPARAM)-1, IPC_IS_WNDSHADE)/**/;

			// Accept messages even if Winamp was run as Administrator
			AddWindowMessageFilter(WM_COMMAND, NULL);
			AddWindowMessageFilter(WM_DWMSENDICONICTHUMBNAIL, NULL);
			AddWindowMessageFilter(WM_DWMSENDICONICLIVEPREVIEWBITMAP, NULL);

			// Register taskbarcreated message
			WM_TASKBARBUTTONCREATED = RegisterWindowMessage(L"TaskbarButtonCreated");

			setup_settings();

			// Timers, settings, icons
			Settings.play_playlistpos = GetPlaylistPosition();
			Settings.play_playlistlen = GetPlaylistLength();
			Settings.play_total = GetCurrentTrackLengthMilliSeconds();
			Settings.play_volume = (int)GetSetVolume((WPARAM)-666, FALSE);

			EnterCriticalSection(&metadata_cs);

			wchar_t buffer[FILENAME_SIZE]/* = { 0 }*/;
			LPCWSTR p = GetPlayingFilename(1, NULL, buffer, ARRAYSIZE(buffer), NULL);
			if (p != NULL)
			{
				reset_metadata(p);
			}

			LeaveCriticalSection(&metadata_cs);

			// update shuffle and repeat
			Settings.state_shuffle = GetShuffle();

			Settings.state_repeat = repeat = GetRepeat();
			if (Settings.state_repeat == 1 && pladv == 1)
			{
				Settings.state_repeat = 2;
			}

#ifdef USE_MOUSE
			// Set up hook for mouse scroll volume control
			if (Settings.VolumeControl)
			{
				hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, (HOOKPROC)KeyboardEvent, plugin.hDllInstance, NULL);
				if (hMouseHook == NULL)
				{
					MessageBox(plugin.hwndParent, LangString(IDS_ERROR_REGISTERING_MOUSE_HOOK),
												   (LPWSTR)plugin.description, MB_ICONWARNING);
				}
			}
#endif
			SetTimer(plugin.hwndParent, 6667, Settings.LowFrameRate ? 400 : 100, TimerProc);
			break;
		}
		case 6673:
		{
			StartSetupJumpList();
			break;
		}
	}
}

LRESULT CALLBACK rateWndProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		case WM_INITDIALOG:
		{
			DarkModeSetup(hwndDlg);

			// give an indication of the current rating for the item
			const int rating = (const int)GetSetMainRating(0, IPC_GETRATING);
			wchar_t rating_text[8]/* = { 0 }*/;
			PrintfCch(rating_text, ARRAYSIZE(rating_text), L"[%d]", rating);
			SetDlgItemText(hwndDlg, IDC_RATE1 + rating, rating_text);
			break;
		}
		case WM_COMMAND:
		{
			GetSetMainRating((LOWORD(wParam) - IDC_RATE1), IPC_SETRATING);
			DestroyWindow(hwndDlg);
			break;
		}
	}

	return 0;
}

LRESULT CALLBACK TabHandler_Taskbar(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
		case WM_INITDIALOG:
		{
			setup_settings();

			SettingsManager::WriteSettings_ToForm(hwnd, Settings);

			const BOOL enabled = GetTaskbarMode();
			CheckDlgButton(hwnd, IDC_SHOW_IN_TASKBAR, (enabled ? BST_CHECKED : BST_UNCHECKED));
			EnableControl(hwnd, 1267, enabled);
			EnableControl(hwnd, IDC_HIDE_ON_MINIMISE, enabled);

			CheckDlgButton(hwnd, IDC_HIDE_ON_MINIMISE, (GetTaskbarOnMinimiseMode() ? BST_CHECKED : BST_UNCHECKED));

			SetupTaskbarIcon(hwnd);
			break;
		}
		case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
				case IDC_SHOW_IN_TASKBAR:
				{
					const bool enabled = (IsDlgButtonChecked(hwnd, IDC_SHOW_IN_TASKBAR) == BST_CHECKED);
					EnableControl(hwnd, 1267, UpdateTaskbarMode(enabled));
					EnableControl(hwnd, IDC_HIDE_ON_MINIMISE, enabled);
					break;
				}
				case IDC_HIDE_ON_MINIMISE:
				{
					UpdateTaskbarOnMinimiseMode((IsDlgButtonChecked(hwnd, IDC_HIDE_ON_MINIMISE) == BST_CHECKED));
					break;
				}
				case IDC_CLEARALL:
				{
					if (ClearRecentFrequentEntries())
					{
						EnableControl(hwnd, IDC_CLEARALL, false);
					}
					break;
				}
				case IDC_CHECK_A2R:
				{
					Settings.Add2RecentDocs = !(Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK_A2R)) == BST_CHECKED);
					break;
				}
				case IDC_CHECK30:
				case IDC_CHECK31:
				case IDC_CHECK32:
				case IDC_CHECK33:
				case IDC_CHECK34:
				{
					Settings.JLpl = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK34)) == BST_CHECKED);
					Settings.JLbms = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK33)) == BST_CHECKED);
					Settings.JLtasks = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK32)) == BST_CHECKED);
					Settings.JLfrequent = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK31)) == BST_CHECKED);
					Settings.JLrecent = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK30)) == BST_CHECKED);

					if (!(Settings.JLbms || Settings.JLpl || Settings.JLtasks ||
						  Settings.JLfrequent || Settings.JLrecent))
					{
						Settings.JLrecent = true;
						Button_SetCheck(GetDlgItem(hwnd, IDC_CHECK30), BST_CHECKED);
					}
					StartSetupJumpList();
					break;
				}
				case IDC_CHECK3:
				{
					Settings.Overlay = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK3)) == BST_CHECKED);
					if (Settings.Overlay)
					{
						UpdateOverlyStatus(false);
					}
					else
					{
						if (itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX))
						{
							itaskbar->SetIconOverlay(NULL, L"");
						}
					}
					break;
				}
				case IDC_CHECK2:
				{
					Settings.Progressbar = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK2)) == BST_CHECKED);

					EnableControl(hwnd, IDC_CHECK4, Settings.Progressbar);
					EnableControl(hwnd, IDC_CHECK5, Settings.Progressbar);

					if (Settings.Progressbar)
					{
						SendDlgItemMessage(hwnd, IDC_CHECK26, (UINT) BM_SETCHECK, 0, 0);
						Settings.VuMeter = false;
					}
					break;
				}
				case IDC_CHECK4:
				{
					Settings.Streamstatus = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK4)) == BST_CHECKED);
					break;
				}
				case IDC_CHECK5:
				{
					Settings.Stoppedstatus = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK5)) == BST_CHECKED);
					break;
				}
				case IDC_CHECK26:
				{
					Settings.VuMeter = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK26)) == BST_CHECKED);

					if (Settings.VuMeter)
					{
						SendDlgItemMessage(hwnd, IDC_CHECK2, (UINT) BM_SETCHECK, 0, 0);
						EnableControl(hwnd, IDC_CHECK4, 0);
						EnableControl(hwnd, IDC_CHECK5, 0);
						Settings.Progressbar = false;
					}

					KillTimer(plugin.hwndParent, 6668);
					if (Settings.VuMeter)
					{
						SetTimer(plugin.hwndParent, 6668, 66, TimerProc);
					}
					break;
				}
				case IDC_ICON_COMBO:
				{
					if (HIWORD(wParam) == CBN_SELCHANGE)
					{
						UpdateTaskbarIcon(hwnd);
					}
					break;
				}
#ifdef USE_MOUSE
				case IDC_CHECK35:
				{
					Settings.VolumeControl = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK35)) == BST_CHECKED);

					if (Settings.VolumeControl)
					{
						if (!hMouseHook)
						{
							hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, (HOOKPROC) KeyboardEvent, plugin.hDllInstance, NULL);
							if (hMouseHook == NULL)
							{
								MessageBox(plugin.hwndParent, LangString(IDS_ERROR_REGISTERING_MOUSE_HOOK),
															   (LPWSTR)plugin.description, MB_ICONWARNING);
							}
						}
					}
					else
					{
						if (hMouseHook)
						{
							UnhookWindowsHookEx(hMouseHook);
							hMouseHook = NULL;
						}
					}
					break;
				}
#endif
			}
			break;
		}
		case WM_DESTROY:
		{
			// save the settings only when changed
			// instead of always applying on close
			SManager->WriteSettings(Settings);
			break;
		}
	}

	return 0;
}

static void UpdateContolButtons(HWND hwnd)
{
	const int ids[] = {IDC_STATIC29, IDC_PCB1, IDC_PCB2, IDC_PCB3,
					   IDC_PCB4, IDC_PCB5, IDC_PCB6, IDC_PCB7,
					   IDC_PCB8, IDC_PCB9, IDC_PCB10, IDC_PCB11,
					   IDC_PCB12, IDC_PCB13, IDC_PCB14, IDC_PCB15,
					   IDC_BUTTON_ORDER, IDC_LIST1, IDC_UPBUTT, IDC_DOWNBUTT};
	const bool allow_delete = GetPlaylistAllowHardDelete();
	for (int i = 0; i < ARRAYSIZE(ids); i++)
	{
		// if global support to delete items is
		// disabled but the action already set
		// then we need to offer a means to get
		// it removed from the active 'buttons'
		if ((i == 14) && !allow_delete)
		{
			if (std::find(TButtons.begin(), TButtons.end(), TB_DELETE) == TButtons.end())
			{
				EnableControl(hwnd, ids[i], FALSE);
				continue;
			}
		}

		EnableControl(hwnd, ids[i], Settings.Thumbnailbuttons);
	}
}

LRESULT CALLBACK TabHandler_ThumbnailImage(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
		case WM_INITDIALOG:
		{
			setup_settings();

			SettingsManager::WriteSettings_ToForm(hwnd, Settings);

			// Reset buttons
			for (int i = IDC_PCB1; i <= IDC_PCB15; i++)
			{
				SendDlgItemMessage(hwnd, i, BM_SETCHECK, BST_UNCHECKED, NULL);
			}

			// disable the 'text' section if needed
			if (Settings.Thumbnailbackground == BG_WINAMP)
			{
				const int ids[] = { IDC_TEXT_GROUP, IDC_EDIT3, IDC_BUTTON5,
								    IDC_BUTTON9, IDC_DEFAULT, IDC_BUTTON6,
								    IDC_CHECK8, IDC_CHECK1, IDC_CHECK29 };
				for (int i = 0; i < ARRAYSIZE(ids); i++)
				{
					EnableControl(hwnd, ids[i], FALSE);
				}

				DestroyControl(hwnd, IDC_BUTTON_HELP);
				SetDlgItemText(hwnd, IDC_TEXT_GROUP, LangString(IDS_TEXT_DISABLED));
			}

			const HWND list = GetDlgItem(hwnd, IDC_LIST1);
			wchar_t tooltip_buf[64]/* = { 0 }*/;
			for (size_t i = 0; i < TButtons.size(); ++i)
			{
				SendMessage(list, LB_SETITEMDATA, SendMessage(list, LB_ADDSTRING, NULL,
							(LPARAM)tools::getToolTip(TButtons[i], tooltip_buf,
									ARRAYSIZE(tooltip_buf), -1)), TButtons[i]);
				SendDlgItemMessage(hwnd, TButtons[i], BM_SETCHECK, BST_CHECKED, NULL);
			}
			
			// Set button icons
			// TODO would be nice for this to work
			//		better when dark mode is used!
			for (int i = 0; i < NR_BUTTONS; i++)
			{
				HICON icon = ImageListGetIcon(GetThumbnailIcons(false), tools::getBitmap((TB_PREVIOUS + i), !!(i == 10)), 0);
				SendDlgItemMessage(hwnd, IDC_PCB1 + i, BM_SETIMAGE, IMAGE_ICON, (LPARAM)icon);
				DestroyIcon(icon);
			}

			SetDlgItemText(hwnd, IDC_UPBUTT, L"\u25B2");
			SetDlgItemText(hwnd, IDC_DOWNBUTT, L"\u25BC");

			UpdateContolButtons(hwnd);
			break;
		}
		case WM_PAINT:
		{
			if (Settings.Thumbnailbackground != BG_WINAMP)
			{
				POINT points[5] = {0};
				RECT rect = {0};

				// button1
				GetDlgItemRect(hwnd, IDC_BUTTON9, &rect);

				// top right	// 0
				points[0].x = points[1].x = rect.right;
				points[0].y = rect.top;

				// bottom right	// 1
				points[1].y = rect.bottom;

				// button2
				GetDlgItemRect(hwnd, IDC_BUTTON6, &rect);

				// top right	// 2
				points[2].x = points[3].x = rect.right;
				points[2].y = rect.top;

				// bottom right	// 3
				points[3].y = rect.bottom;

				// editbox
				GetDlgItemRect(hwnd, IDC_EDIT3, &rect);

				// bottom right	// 4
				points[4].x = rect.right;
				points[4].y = rect.bottom;

				MapWindowPoints(NULL, hwnd, &points[0], ARRAYSIZE(points));

				PAINTSTRUCT paint = {0};
				HDC hdc = BeginPaint(hwnd, &paint);

				HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW)),
					 old_pen = (HPEN)SelectObject(hdc, pen);

				for (int i = 0, j = 0; i < 2; i++, j+=2)
				{
					HBRUSH brush = CreateSolidBrush((!i ? Settings.text_color : Settings.bgcolor)),
						   old_brush = (HBRUSH)SelectObject(hdc, brush);
					Rectangle(hdc, points[j].x + 7, points[j].y + 1, points[4].x, points[1 + j].y - 1);
					SelectObject(hdc, old_brush);
					DeleteObject(brush);
				}

				SelectObject(hdc, old_pen);
				DeleteObject(pen);
				EndPaint(hwnd, &paint);
			}
			break;
		}
		case WM_MOUSEMOVE:
		{
			if (Settings.Thumbnailbuttons)
			{
				SetDlgItemText(hwnd, IDC_STATIC29, LangString(IDS_MAX_BUTTONS));
			}
			break;
		}
		case WM_NOTIFY:
		{
			switch (wParam)
			{   
				case IDC_PCB1:
				case IDC_PCB2:
				case IDC_PCB3:
				case IDC_PCB4:
				case IDC_PCB5:
				case IDC_PCB6:
				case IDC_PCB7:
				case IDC_PCB8:
				case IDC_PCB9:
				case IDC_PCB10:
				case IDC_PCB11:
				case IDC_PCB12:
				case IDC_PCB13:
				case IDC_PCB14:
				case IDC_PCB15:
				{
					if ((((LPNMHDR)lParam)->code) == BCN_HOTITEMCHANGE)
					{
						if (Settings.Thumbnailbuttons)
						{
							wchar_t tooltip_buf[64]/* = { 0 }*/;
							SetDlgItemText(hwnd, IDC_STATIC29, tools::getToolTip(wParam,
											  tooltip_buf, ARRAYSIZE(tooltip_buf), -1));
						}

						UpdateContolButtons(hwnd);
					}
					break;
				}
			}
			break;
		}
		case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
				case IDC_BUTTON_RESTART:
				{
					PostMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)-1, IPC_RESTARTWINAMP);
					break;
				}
				case IDC_UPBUTT:
				{
					const HWND list = GetDlgItem(hwnd, IDC_LIST1);
					int index = (int)SendMessage(list, LB_GETCURSEL, NULL, NULL);
					if (index == 0 || index == -1)
					{
						break;
					}

					const int data = (const int)SendMessage(list, LB_GETITEMDATA, index, NULL);
					SendMessage(list, LB_DELETESTRING, index, NULL);

					wchar_t tooltip_buf[64]/* = { 0 }*/;
					index = (int)SendMessage(list, LB_INSERTSTRING, (WPARAM)index - 1,
											 (LPARAM)tools::getToolTip(data, tooltip_buf,
															ARRAYSIZE(tooltip_buf), -1));
					SendMessage(list, LB_SETITEMDATA, index, data);
					SendMessage(list, LB_SETCURSEL, index, NULL);

					// Populate buttons structure
					TButtons.clear();
					for (int i = 0; i != ListBox_GetCount(list); ++i)
					{
						TButtons.push_back((int)ListBox_GetItemData(list, i));
					}

					// Show note
					ShowControl(hwnd, IDC_BUTTON_RESTART, SW_SHOWNA);
					break;
				}
				case IDC_DOWNBUTT:
				{
					const HWND list = GetDlgItem(hwnd, IDC_LIST1);
					int index = (int)SendMessage(list, LB_GETCURSEL, NULL, NULL);
					if (index == ListBox_GetCount(list)-1 || index == -1)
					{
						return 0;
					}

					const int data = (const int)SendMessage(list, LB_GETITEMDATA, index, NULL);
					SendMessage(list, LB_DELETESTRING, index, NULL);

					wchar_t tooltip_buf[64]/* = { 0 }*/;
					index = (int)SendMessage(list, LB_INSERTSTRING, (WPARAM)index + 1,
										   (LPARAM)tools::getToolTip(data, tooltip_buf,
														  ARRAYSIZE(tooltip_buf), -1));
					SendMessage(list, LB_SETITEMDATA, index, data);
					SendMessage(list, LB_SETCURSEL, index, NULL);

					// Populate buttons structure
					TButtons.clear();
					for (int i = 0; i != ListBox_GetCount(list); ++i)
					{
						TButtons.push_back((int)ListBox_GetItemData(list, i));
					}

					// Show note
					ShowControl(hwnd, IDC_BUTTON_RESTART, SW_SHOWNA);
					break;
				}
				case IDC_CHECK6:
				{
					Settings.Thumbnailbuttons = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK6)) == BST_CHECKED);
					EnableControl(hwnd, IDC_CHECK27, (UINT)SendDlgItemMessage(hwnd, IDC_CHECK6, BM_GETCHECK, 0, 0));
					UpdateContolButtons(hwnd);

					if (Settings.Thumbnailbuttons)
					{
						if ((itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX)) && itaskbar->Reset())
						{
							updateToolbar(GetThumbnailIcons(false));
						}
					}
					else
					{
						// Show note
						ShowControl(hwnd, IDC_BUTTON_RESTART, SW_SHOWNA);
					}
					break;
				}
				case IDC_PCB1:
				case IDC_PCB2:
				case IDC_PCB3:
				case IDC_PCB4:
				case IDC_PCB5:
				case IDC_PCB6:
				case IDC_PCB7:
				case IDC_PCB8:
				case IDC_PCB9:
				case IDC_PCB10:
				case IDC_PCB11:
				case IDC_PCB12:
				case IDC_PCB13:
				case IDC_PCB14:
				case IDC_PCB15:
				{
					AddStringtoList(hwnd, LOWORD(wParam));
					break;
				}
				case IDC_EDIT3:
				{
					if (HIWORD(wParam) == EN_CHANGE)
					{
						GetWindowText(GetDlgItem(hwnd, IDC_EDIT3), Settings.Text, ARRAYSIZE(Settings.Text));

						std::wstring::size_type pos = std::wstring::npos;
						std::wstring text = Settings.Text;
						do 
						{
							pos = text.find(L"\r\n");
							if (pos != std::wstring::npos)
							{
								text.replace(pos, 2, L"\\r");
							}
						}
						while (pos != std::wstring::npos);
						CopyCchStr(Settings.Text, ARRAYSIZE(Settings.Text), text.c_str());

						ResetThumbnail();
					}
					break;
				}
				case IDC_BUTTON_HELP:
				{
					DecompressMessageBox(WASABI_API_LNG_HINST, WASABI_API_ORIG_HINST, IDR_HELP_GZ, GetPrefsHWND(),
														   LangString(IDS_INFORMATION), MB_ICONINFORMATION, true);
					break;
				}
				case IDC_DEFAULT:
				{
					if (MessageBox(hwnd, LangString(IDS_DEFAULT_TEXT),
								   (LPWSTR)plugin.description, MB_YESNO |
								   MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
					{
						SetDlgItemText(hwnd, IDC_EDIT3, L"%c%%s%%curpl% of "
									   L"%totalpl%.\r\n%c%%s%%title%\r\n%c"
									   L"%%s%%artist%\r\n\r\n%c%%s%%curtime%"
									   L"/%totaltime%\r\n%c%%s%Track #: "
									   L"%track%        Volume: %volume%%");
						PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_EDIT3, EN_CHANGE), NULL);
					}
					break;
				}
				case IDC_BUTTON5:
				{
					CHOOSEFONT cf = { 0 };
					cf.lStructSize = sizeof(cf);
					cf.hwndOwner = GetPrefsHWND();
					cf.rgbColors = Settings.text_color;
					cf.lpLogFont = &Settings.font;
					cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT |
							   // added these styles to prevent fonts being listed
							   // which appear to have no effect when used (will
							   // need to investigate further but this is a decent
							   // safety blanket on things for the time being - dro)
							   CF_SCALABLEONLY | CF_NOOEMFONTS | CF_TTONLY;

					if (PickFont(&cf))
					{
						Settings.font = *cf.lpLogFont;

						if (thumbnaildrawer != NULL)
						{
							thumbnaildrawer->ClearFonts();
						}
					}
					break;
				}
				case IDC_BUTTON6:
				case IDC_BUTTON9:
				{
					static COLORREF acrCustClr[16] = { 0 };	// array of custom colors
					const bool text = (LOWORD(wParam) == IDC_BUTTON9);
					CHOOSECOLOR cc = { 0 };			// common dialog box structure
					cc.lStructSize = sizeof(cc);
					cc.hwndOwner = GetPrefsHWND();
					cc.lpCustColors = acrCustClr;
					cc.rgbResult = (!text ? Settings.bgcolor : Settings.text_color);
					cc.Flags = CC_FULLOPEN | CC_RGBINIT;

					if (PickColour(&cc) == TRUE)
					{
						(!text ? Settings.bgcolor : Settings.text_color) = cc.rgbResult;
						InvalidateRect(hwnd, NULL, false);
					}
					break;
				}
				case IDC_CHECK8:
				{
					Settings.Antialias = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK8)) == BST_CHECKED);
					break;
				}
				case IDC_CHECK1:
				{
					Settings.Shrinkframe = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK1)) == BST_CHECKED);
					break;
				}
				case IDC_CHECK29:
				{
					Settings.Thumbnailpb = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK29)) == BST_CHECKED);
					break;
				}
			}
			break;
		}
		case WM_DESTROY:
		{
			// save the settings only when changed
			// instead of always applying on close
			SManager->WriteButtons(TButtons);
			SManager->WriteSettings(Settings);
			break;
		}
	}

	return 0;
}

BOOL CALLBACK EnumDialogControls(HWND hwnd, LPARAM lParam)
{
	DWORD id = GetDlgCtrlID(hwnd);
	if (id == IDC_THUMB_GROUP || id == IDC_RADIO9 ||
		id == IDC_THUMB_STATIC || id == IDC_RADIO10 ||
		id == IDC_CHECK36)
	{
		// skip over the header controls on the page
		return TRUE;
	}

	ShowWindow(hwnd, (lParam ? SW_SHOWNA : SW_HIDE));
	return TRUE;
}

void UpdateIconControls(HWND hwnd)
{
	EnableControl(hwnd, IDC_ICONSIZE, Settings.AsIcon);
	EnableControl(hwnd, IDC_SLIDER1, Settings.AsIcon);
	EnableControl(hwnd, IDC_RADIO4, Settings.AsIcon);
	EnableControl(hwnd, IDC_RADIO7, Settings.AsIcon);
	EnableControl(hwnd, IDC_ICONPOS, Settings.AsIcon);
	EnableControl(hwnd, IDC_RADIO6, Settings.AsIcon);
	EnableControl(hwnd, IDC_RADIO8, Settings.AsIcon);
}

void UpdateThumbnail(void)
{
	if (itaskbar > reinterpret_cast<iTaskBar*>(USHRT_MAX))
	{
		if (itaskbar->Reset())
		{
			updateToolbar(GetThumbnailIcons(false));
		}

		itaskbar->SetWindowAttr();
	}

	ResetThumbnail();
}

LRESULT CALLBACK TabHandler_Thumbnail(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
		case WM_INITDIALOG:
		{
			setup_settings();

			SettingsManager::WriteSettings_ToForm(hwnd, Settings);

			SendDlgItemMessage(hwnd, IDC_EDIT2, EM_SETREADONLY, TRUE, NULL);
			SendDlgItemMessage(hwnd, IDC_SLIDER1, TBM_SETRANGE, FALSE, MAKELPARAM(30, 100));
			SendDlgItemMessage(hwnd, IDC_SLIDER_TRANSPARENCY, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));

			switch (Settings.IconPosition)
			{
				case IP_UPPERLEFT:
				{
					SetDlgItemText(hwnd, IDC_ICONPOS, LangString(IDS_ICON_POSITION_TL));
					break;
				}
				case IP_UPPERRIGHT:
				{
					SetDlgItemText(hwnd, IDC_ICONPOS, LangString(IDS_ICON_POSITION_TR));
					break;
				}
				case IP_LOWERLEFT:
				{
					SetDlgItemText(hwnd, IDC_ICONPOS, LangString(IDS_ICON_POSITION_BL));
					break;
				}
				case IP_LOWERRIGHT:
				{
					SetDlgItemText(hwnd, IDC_ICONPOS, LangString(IDS_ICON_POSITION_BR));
					break;
				}
			}

			EnumChildWindows(hwnd, EnumDialogControls, (Settings.Thumbnailbackground != BG_WINAMP));

			UpdateIconControls(hwnd);
			break;
		}
		case WM_HSCROLL:
		{
			wchar_t text[64]/* = { 0 }*/;
			if (HWNDIsCtrl(lParam, hwnd, IDC_SLIDER1))
			{
				Settings.IconSize = (int)SendMessage((HWND)lParam, TBM_GETPOS, NULL, NULL);
				PrintfCch(text, ARRAYSIZE(text), LangString(IDS_ICON_SIZE), Settings.IconSize);
				SetDlgItemText(hwnd, IDC_ICONSIZE, text);
			}
			else if (HWNDIsCtrl(lParam, hwnd, IDC_SLIDER_TRANSPARENCY))
			{
				Settings.BG_Transparency = (int)SendMessage((HWND)lParam, TBM_GETPOS, NULL, NULL);
				PrintfCch(text, ARRAYSIZE(text), L"%d%%", Settings.BG_Transparency);
				SetDlgItemText(hwnd, IDC_TRANSPARENCY_PERCENT, text);
			}
			UpdateThumbnail();
			break;
		}
		case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
				case IDC_RADIO1:
				case IDC_RADIO2:
				case IDC_RADIO3:
				case IDC_RADIO9:
				case IDC_RADIO10:
				{
					if (SendDlgItemMessage(hwnd, IDC_RADIO9, (UINT) BM_GETCHECK, 0 , 0))
					{
						Settings.Thumbnailbackground = BG_WINAMP;
					}
					else if (SendDlgItemMessage(hwnd, IDC_RADIO1, (UINT) BM_GETCHECK, 0 , 0))
					{
						Settings.Thumbnailbackground = BG_TRANSPARENT;
					}
					else if (SendDlgItemMessage(hwnd, IDC_RADIO2, (UINT) BM_GETCHECK, 0 , 0))
					{
						Settings.Thumbnailbackground = BG_ALBUMART;
					}
					else if (SendDlgItemMessage(hwnd, IDC_RADIO3, (UINT) BM_GETCHECK, 0 , 0))
					{
						Settings.Thumbnailbackground = BG_CUSTOM;
						if (GetControlTextLength(hwnd, IDC_EDIT2) == 0)
						{
							PostDlgItemMessage(hwnd, IDC_BUTTON3, BM_CLICK, 0, 0);
						}
					}

					EnumChildWindows(hwnd, EnumDialogControls, (Settings.Thumbnailbackground != BG_WINAMP));

					UpdateThumbnail();

					SetThumbnailTimer();
					DwmInvalidateIconicBitmaps(plugin.hwndParent);
					break;
				}
				case IDC_EDIT2:
				{
					GetWindowText(GetDlgItem(hwnd, IDC_EDIT2), Settings.BGPath, MAX_PATH);
					break;
				}
				case IDC_BUTTON3:
				{
					wchar_t filename[MAX_PATH]/* = {0}*/;
					IFileDialog *pfd = NULL;
					filename[0] = 0;

					// CoCreate the dialog object.
					if (SUCCEEDED(CreateCOMInProc(CLSID_FileOpenDialog,
						__uuidof(IFileDialog), (LPVOID*)&pfd)) && pfd)
					{
						// Show the dialog
						wchar_t tmp[128]/* = { 0 }*/, tmp2[128]/* = { 0 }*/;
						size_t filter_position = 0;
						LPCWSTR filter_str = GetImageFilesFilter(&filter_position);
						const COMDLG_FILTERSPEC rgSpec[] =
						{
							{ filter_str,  (filter_str + filter_position) }
						};

						pfd->SetFileTypes(1, rgSpec);
						pfd->SetOkButtonLabel(LngStringCopy(IDS_USE_AS_THUMB_BKGND, tmp, ARRAYSIZE(tmp)));
						pfd->SetTitle(LngStringCopy(IDS_SELECT_IMAGE_FILE, tmp2, ARRAYSIZE(tmp2)));

						if (SUCCEEDED(pfd->Show(GetPrefsHWND())))
						{
							// Obtain the result of the user's interaction with the dialog.
							IShellItem *psiResult = NULL;
							if (SUCCEEDED(pfd->GetResult(&psiResult)))
							{
								wchar_t *w = NULL;
								psiResult->GetDisplayName(SIGDN_FILESYSPATH, &w);
								psiResult->Release();
								CopyCchStr(filename, ARRAYSIZE(filename), w);
								MemFreeCOM(w);
							}
						} 
						pfd->Release();
					}

					if (filename[0])
					{
						SetDlgItemText(hwnd, IDC_EDIT2, filename);
						ResetThumbnail();
					}
					else if (!Settings.BGPath[0])
					{
						SendDlgItemMessage(hwnd, IDC_RADIO2, (UINT)BM_SETCHECK, BST_CHECKED, 0);
						SendDlgItemMessage(hwnd, IDC_RADIO3, (UINT)BM_SETCHECK, BST_UNCHECKED, 0);
					}
					break;
				}
				case IDC_COMBO1:
				{
					Settings.Revertto = (int)SendDlgItemMessage(hwnd, IDC_COMBO1, CB_GETCURSEL, 0, 0);
					ResetThumbnail();
					break;
				}
				case IDC_CHECK25:
				{
					Settings.AsIcon = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK25)) == BST_CHECKED);
					UpdateIconControls(hwnd);
					UpdateThumbnail();
					break;
				}
				case IDC_RADIO4:
				case IDC_RADIO6:
				case IDC_RADIO7:
				case IDC_RADIO8:
				{
					if (SendDlgItemMessage(hwnd, IDC_RADIO4, (UINT)BM_GETCHECK, 0 , 0))
					{
						SetDlgItemText(hwnd, IDC_ICONPOS, LangString(IDS_ICON_POSITION_TL));
						Settings.IconPosition = IP_UPPERLEFT;
					}
					else if (SendDlgItemMessage(hwnd, IDC_RADIO7, (UINT)BM_GETCHECK, 0 , 0))
					{
						SetDlgItemText(hwnd, IDC_ICONPOS, LangString(IDS_ICON_POSITION_BL));
						Settings.IconPosition = IP_LOWERLEFT;
					}
					else if (SendDlgItemMessage(hwnd, IDC_RADIO6, (UINT)BM_GETCHECK, 0 , 0))
					{
						SetDlgItemText(hwnd, IDC_ICONPOS, LangString(IDS_ICON_POSITION_TR));
						Settings.IconPosition = IP_UPPERRIGHT;
					}
					else if (SendDlgItemMessage(hwnd, IDC_RADIO8, (UINT)BM_GETCHECK, 0 , 0))
					{
						SetDlgItemText(hwnd, IDC_ICONPOS, LangString(IDS_ICON_POSITION_BR));
						Settings.IconPosition = IP_LOWERRIGHT;
					}

					UpdateThumbnail();
					break;
				}
				case IDC_CHECK36:
				{
					Settings.LowFrameRate = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK36)) == BST_CHECKED);
					SetThumbnailTimer();
					break;
				}
			} // switch
			break;
		}
		case WM_DESTROY:
		{
			// save the settings only when changed
			// instead of always applying on close
			SManager->WriteSettings(Settings);
			break;
		}
	}

	return 0;
}

void AddStringtoList(HWND window, const int control_ID)
{
	HWND button = GetDlgItem(window, control_ID),
		 list = GetDlgItem(window, IDC_LIST1);
	const bool checked = !Button_GetCheck(button);

	// Populate list
	if (checked)
	{
		if (ListBox_GetCount(list) > 0)
		{
			wchar_t tooltip_buf[64]/* = { 0 }*/;
			const int index = (const int)SendMessage(list, LB_FINDSTRINGEXACT, (WPARAM)-1,
													 (LPARAM)tools::getToolTip(control_ID,
													 tooltip_buf, ARRAYSIZE(tooltip_buf), -1));
			if (index != LB_ERR)
			{
				ListBox_DeleteString(list, index);
			}
		}
	}
	else
	{
		if (ListBox_GetCount(list) == 0)
		{
			wchar_t tooltip_buf[64]/* = { 0 }*/;
			const int index = (const int)SendMessage(list, LB_ADDSTRING, NULL, (LPARAM)
											 tools::getToolTip(control_ID, tooltip_buf,
														  ARRAYSIZE(tooltip_buf), -1));
			SendMessage(list, LB_SETITEMDATA, index, control_ID);
		}
		else
		{
			if (ListBox_GetCount(list) >= 7)
			{
				Button_SetCheck(button, BST_UNCHECKED);
				SetDlgItemText(window, IDC_STATIC29, LangString(IDS_MAX_BUTTONS));
			}
			else
			{
				wchar_t tooltip_buf[64]/* = { 0 }*/;
				LPCWSTR tooltip = tools::getToolTip(control_ID, tooltip_buf,
												ARRAYSIZE(tooltip_buf), -1);
				if (SendMessage(list, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)tooltip) == LB_ERR) //no duplicate
				{
					ListBox_SetItemData(list, SendMessage(list, LB_ADDSTRING, NULL, (LPARAM)tooltip), control_ID);
				}
			}
		}
	}

	// Populate buttons structure
	TButtons.clear();
	for (int i = 0; i != ListBox_GetCount(list); ++i)
	{
		TButtons.push_back((int)ListBox_GetItemData(list, i));
	}

	// Show note
	ShowControl(window, IDC_BUTTON_RESTART, SW_SHOWNA);
}

#ifdef USE_MOUSE
LRESULT CALLBACK KeyboardEvent(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (!thumbshowing || wParam != WM_MOUSEWHEEL)
	{
		return CallNextHookEx(NULL, nCode, wParam, lParam);
	}

	if ((short)((HIWORD(((MSLLHOOKSTRUCT*)lParam)->mouseData))) > 0)
	{
		Settings.play_volume += 7;
		if (Settings.play_volume > 255)
		{
			Settings.play_volume = 255;
		}

		PostMessage(plugin.hwndParent, WM_WA_IPC, Settings.play_volume, IPC_SETVOLUME);
	}
	else
	{
		Settings.play_volume -= 7;
		if (Settings.play_volume < 0)
		{
			Settings.play_volume = 0;
		}

		PostMessage(plugin.hwndParent, WM_WA_IPC, Settings.play_volume, IPC_SETVOLUME);
	}

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}
#endif

// This is an export function called by winamp which returns this plugin info.
extern "C" __declspec(dllexport) winampGeneralPurposePlugin * winampGetGeneralPurposePlugin(void)
{
	return &plugin;
}

extern "C" __declspec(dllexport) int winampUninstallPlugin(HINSTANCE hDllInst, HWND hwndDlg, int param)
{
	if (UninstallSettingsPrompt(reinterpret_cast<const wchar_t *>(plugin.description), 0, NULL))
	{
		no_uninstall = false;

		const winamp_paths *paths = GetPaths();
		if (FileExists(paths->win7shell_ini_file))
		{
			RemoveFile(paths->win7shell_ini_file);
		}

		JumpList *jl = new JumpList(true);
		if (jl != NULL)
		{
			delete jl;
		}
	}

	return GEN_PLUGIN_UNINSTALL_REBOOT;
}

RUNTIME_HELPER_HANDLER

//////////////////////////////////////////////////////////////////////////////