#define PLUGIN_VERSION L"4.0.4"

#define NR_BUTTONS 15

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <strsafe.h>
#include <shobjidl.h>
#include <gdiplus.h>
#include <process.h>

#include "gen_win7shell.h"
#include <sdk/winamp/wa_ipc.h>
#include <sdk/winamp/wa_msgids.h>
#include <sdk/winamp/wa_cup.h>
#include <sdk/Agave/Language/lang.h>
#include <sdk/winamp/ipc_pe.h>
#include <sdk/nu/autowide.h>
#include <common/wa_prefs.h>
#include "resource.h"
#include "api.h"
#include "tools.h"
#include "metadata.h"
#include "jumplist.h"
#include "settings.h"
#include "taskbar.h"
#include "renderer.h"
#include <loader/loader/utils.h>
#include <loader/loader/paths.h>
#include <loader/hook/squash.h>
#include <loader/hook/plugins.h>

// TODO add to lang.h
// Taskbar Integration plugin (gen_win7shell.dll)
// {0B1E9802-CA15-4939-8445-FD800E8BFF9A}
static const GUID GenWin7PlusShellLangGUID = 
{ 0xb1e9802, 0xca15, 0x4939, { 0x84, 0x45, 0xfd, 0x80, 0xe, 0x8b, 0xff, 0x9a } };

UINT WM_TASKBARBUTTONCREATED = (UINT)-1;
std::wstring AppID,	// this is updated on loading to what the
					// running WACUP install has generated as
					// it otherwise makes multiple instances
					// tricky to work with independently
			 SettingsFile;

bool thumbshowing = false, no_uninstall = true,
	 classicSkin = true, windowShade = false,
	 modernSUI = false, modernFix = false,
	 finishedLoad = false, running = false;
HWND ratewnd = 0, dialogParent = 0;
int pladv = 1, repeat = 0;
LPARAM delay_ipc = -1;
#ifdef USE_MOUSE
HHOOK hMouseHook = NULL;
#endif
COLORREF acrCustClr[16] = { 0 };	// array of custom colors
sSettings Settings = { 0 };
std::vector<int> TButtons;
iTaskBar *itaskbar = NULL;
MetaData metadata;
renderer *thumbnaildrawer = NULL;
HANDLE updatethread = NULL;

api_albumart *AGAVE_API_ALBUMART = 0;
api_playlists *AGAVE_API_PLAYLISTS = 0;
//api_explorerfindfile *WASABI_API_EXPLORERFINDFILE = 0;
api_skin *WASABI_API_SKIN = 0;
api_language *WASABI_API_LNG = 0;
// these two must be declared as they're used by the language api's
// when the system is comparing/loading the different resources
HINSTANCE WASABI_API_LNG_HINST = 0, WASABI_API_ORIG_HINST = 0;

// CALLBACKS
VOID CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
LRESULT CALLBACK rateWndProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);
#ifdef USE_MOUSE
LRESULT CALLBACK KeyboardEvent(int nCode, WPARAM wParam, LPARAM lParam);
#endif

extern "C" __declspec(dllexport) LRESULT CALLBACK TabHandler_Taskbar(HWND, UINT, WPARAM, LPARAM);
extern "C" __declspec(dllexport) LRESULT CALLBACK TabHandler_Thumbnail(HWND, UINT, WPARAM, LPARAM);
extern "C" __declspec(dllexport) LRESULT CALLBACK TabHandler_ThumbnailImage(HWND, UINT, WPARAM, LPARAM);

void updateToolbar(HIMAGELIST ImageList = NULL);
void SetupJumpList(void);
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

bool CreateThumbnailDrawer(const bool always_create = true)
{
	if ((thumbnaildrawer == NULL) && always_create)
	{
		// Create thumbnail renderer
		thumbnaildrawer = new renderer(Settings, metadata);
	}

	return (thumbnaildrawer != NULL);
}

const bool GenerateAppIDFromFolder(const wchar_t *search_path, wchar_t *app_id)
{
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

	IKnownFolderManager* pkfm = NULL;
	HRESULT hr = CoCreateInstance(CLSID_KnownFolderManager, NULL,
								  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pkfm));

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
				if (!_wcsnicmp(search_path, path, len))
				{
					// and if they are then we'll merge
					// the two together to get the final
					// version of the string for an appid
					KNOWNFOLDERID pkfid = {0};
					if (SUCCEEDED(pFolder->GetId(&pkfid)))
					{
						wchar_t szGuid[40] = {0};
						StringFromGUID2(pkfid, szGuid, 40);
						StringCchPrintf(app_id, MAX_PATH, L"%s%s", szGuid, &search_path[len]);
					}
				}
			}
			CoTaskMemFree(path);
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
			wchar_t self_path[MAX_PATH] = { 0 };
			if (GetModuleFileName(NULL, self_path, ARRAYSIZE(self_path)))
			{
				wchar_t app_id[MAX_PATH] = { 0 };
				if (!GenerateAppIDFromFolder(self_path, app_id))
				{
					(void)StringCchCopy(app_id, ARRAYSIZE(app_id), self_path);
				}

				RenameExtension(app_id, L".exe");

				// TODO: auto-pin icon (?)
				if (SetCurrentProcessExplicitAppUserModelID(app_id) != S_OK)
				{
					MessageBoxEx(plugin.hwndParent,
								 WASABI_API_LNGSTRINGW(IDS_ERROR_SETTING_APPID),
								 (LPWSTR)plugin.description, MB_ICONWARNING | MB_OK, 0);
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
		}

		if (AppID.empty())
		{
			AppID = L"WACUP";
		}
	}
	return AppID.c_str();
}

// event functions follow
int init(void)
{
	/************************************************************************/
	/* Winamp services                                                      */
	/************************************************************************/
	//ServiceBuild(plugin.service, WASABI_API_MEMMGR, memMgrApiServiceGuid);
	//ServiceBuild(plugin.service, AGAVE_API_ALBUMART, albumArtGUID);
	AGAVE_API_ALBUMART = plugin.albumart;
	//ServiceBuild(plugin.service, AGAVE_API_PLAYLISTS, api_playlistsGUID);
	AGAVE_API_PLAYLISTS = plugin.playlists;
	//ServiceBuild(plugin.service, WASABI_API_LNG, languageApiGUID);
	WASABI_API_LNG = plugin.language;
	WASABI_API_START_LANG(plugin.hDllInstance, GenWin7PlusShellLangGUID);

	wchar_t pluginTitleW[256] = { 0 };
	StringCchPrintf(pluginTitleW, ARRAYSIZE(pluginTitleW), WASABI_API_LNGSTRINGW(IDS_PLUGIN_NAME), PLUGIN_VERSION);
	plugin.description = (char*)_wcsdup(pluginTitleW);

	// Delay loading mst parts until later on to improve the overall load time
	delay_ipc = RegisterIPC((WPARAM)&"7+_ipc");
	PostMessage(plugin.hwndParent, WM_WA_IPC, 0, delay_ipc);

	return GEN_INIT_SUCCESS;
}

void config(void)
{
	HMENU popup = CreatePopupMenu();
	RECT r = {0};

	AddItemToMenu(popup, 128, (LPWSTR)plugin.description);
	EnableMenuItem(popup, 128, MF_BYCOMMAND | MF_GRAYED | MF_DISABLED);
	AddItemToMenu(popup, (UINT)-1, 0);
	AddItemToMenu(popup, 2, WASABI_API_LNGSTRINGW(IDS_OPEN_TASKBAR_PREFS));
	AddItemToMenu(popup, (UINT)-1, 0);
	AddItemToMenu(popup, 1, WASABI_API_LNGSTRINGW(IDS_ABOUT));

	HWND list =	FindWindowEx(GetParent(GetFocus()), 0, L"SysListView32",0);
	ListView_GetItemRect(list, ListView_GetSelectionMark(list), &r, LVIR_BOUNDS);
	ClientToScreen(list, (LPPOINT)&r);

	switch (TrackPopupMenu(popup, TPM_RETURNCMD | TPM_LEFTBUTTON, r.left, r.top, 0, list, NULL))
	{
		case 1:
		{
			wchar_t text[512] = {0};
			StringCchPrintf(text, ARRAYSIZE(text), WASABI_API_LNGSTRINGW(IDS_ABOUT_MESSAGE),
							L"Darren Owen aka DrO (2018-2022)", TEXT(__DATE__));
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
	KillTimer(plugin.hwndParent, 6667);
	KillTimer(plugin.hwndParent, 6668);
	KillTimer(plugin.hwndParent, 6670);
	KillTimer(plugin.hwndParent, 6671);

	if (updatethread != NULL)
	{
		running = false;

		WaitForSingleObject(updatethread, INFINITE);
		CloseHandle(updatethread);
		updatethread = NULL;
	}

#ifdef USE_MOUSE
	if (hMouseHook != NULL)
	{
		UnhookWindowsHookEx(hMouseHook);
	}
#endif

	if (itaskbar != NULL)
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
	//ServiceRelease(plugin.service, AGAVE_API_ALBUMART, albumArtGUID);
	//ServiceRelease(plugin.service, AGAVE_API_PLAYLISTS, api_playlistsGUID);
	//ServiceRelease(plugin.service, WASABI_API_LNG, languageApiGUID);
	//ServiceRelease(plugin.service, WASABI_API_EXPLORERFINDFILE, ExplorerFindFileApiGUID);
	ServiceRelease(plugin.service, WASABI_API_SKIN, skinApiServiceGuid);
}

void updateToolbar(HIMAGELIST ImageList)
{
	if ((itaskbar != NULL) && Settings.Thumbnailbuttons)
	{
		std::vector<THUMBBUTTON> thbButtons;
		const size_t count = TButtons.size();
		for (size_t i = 0; i != count; ++i)
		{
			THUMBBUTTON button = { THB_BITMAP | THB_TOOLTIP, (UINT)TButtons[i], 0, NULL, {0}, THBF_ENABLED };

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
				(void)StringCchCopy(button.szTip, ARRAYSIZE(button.szTip), tools::getToolTip(TB_PLAYPAUSE, Settings.play_state));
			}
			else if (button.iId == TB_REPEAT)
			{
				button.iBitmap = tools::getBitmap(button.iId, Settings.state_repeat);
				(void)StringCchCopy(button.szTip, ARRAYSIZE(button.szTip), tools::getToolTip(TB_REPEAT, Settings.state_repeat));
			}
			else if (button.iId == TB_SHUFFLE)
			{
				button.iBitmap = tools::getBitmap(button.iId, Settings.play_state == Settings.state_shuffle);
				(void)StringCchCopy(button.szTip, ARRAYSIZE(button.szTip), tools::getToolTip(TB_SHUFFLE, Settings.state_shuffle));
			}

			if (!button.iBitmap)
			{
				button.iBitmap = tools::getBitmap(button.iId, 0);
			}

			if (!button.szTip[0])
			{
				(void)StringCchCopy(button.szTip, ARRAYSIZE(button.szTip), tools::getToolTip(button.iId, 0));
			}

			thbButtons.push_back(button);
		}

		if (itaskbar != NULL)
		{
			itaskbar->ThumbBarUpdateButtons(thbButtons, ImageList);
		}
	}
}

BOOL CALLBACK checkSkinProc(HWND hwnd, LPARAM lParam)
{
	wchar_t cl[24] = { 0 };
	GetClassName(hwnd, cl, ARRAYSIZE(cl));
	if (!_wcsnicmp(cl, L"BaseWindow_RootWnd", 18))
	{
		// if any of these are a child window of
		// the current skin being used then it's
		// very likely it's a SUI modern skin...
		// BaseWindow_RootWnd -> BaseWindow_RootWnd -> Winamp *
		HWND child = GetWindow(hwnd, GW_CHILD);
		if (IsWindow(child))
		{
			cl[0] = 0;
			GetClassName(child, cl, ARRAYSIZE(cl));
			if (!_wcsnicmp(cl, L"Winamp EQ", 9) ||
				!_wcsnicmp(cl, L"Winamp PE", 9) ||
				!_wcsnicmp(cl, L"Winamp Gen", 10) ||
				!_wcsnicmp(cl, L"Winamp Video", 12))
			{
				modernSUI = true;
				return FALSE;
			}
		}
	}
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

DWORD WINAPI UpdateThread(LPVOID lp)
{
	while (CreateThumbnailDrawer() && running)
	{
		HBITMAP thumbnail = thumbnaildrawer->GetThumbnail();
		if (thumbnail != NULL)
		{
			const HRESULT hr = DwmSetIconicThumbnail(plugin.hwndParent, thumbnail, 0);

			DeleteObject(thumbnail);
			thumbnail = NULL;

			if (FAILED(hr))
			{
				KillTimer(plugin.hwndParent, 6670);
				KillTimer(plugin.hwndParent, 6671);
				running = false;
				break;
			}
		}

		Sleep((Settings.Thumbnailbackground == BG_WINAMP) ?
			  (!Settings.LowFrameRate ? Settings.MFT : Settings.MST) :
			  (!Settings.LowFrameRate ? Settings.TFT : Settings.TST));
	}

	if (updatethread != NULL)
	{
		CloseHandle(updatethread);
		updatethread = NULL;
	}
	return 0;
}

void SetThumbnailTimer(void)
{
	KillTimer(plugin.hwndParent, 6670);
	SetTimer(plugin.hwndParent, 6670, (Settings.Thumbnailbackground == BG_WINAMP) ?
			 (!Settings.LowFrameRate ? Settings.MFT : Settings.MST) :
			 (!Settings.LowFrameRate ? Settings.TFT : Settings.TST), TimerProc);

	KillTimer(plugin.hwndParent, 6671);

	if (updatethread == NULL)
	{
		updatethread = CreateThread(0, 0, UpdateThread, 0, 0, NULL);
	}

	SetTimer(plugin.hwndParent, 6671, 30000, TimerProc);
}

void ResetThumbnail(void)
{
	if (CreateThumbnailDrawer(false))
	{
		thumbnaildrawer->ClearAlbumart();
		thumbnaildrawer->ClearBackground();
		thumbnaildrawer->ClearCustomBackground();
		thumbnaildrawer->ClearFonts();
		thumbnaildrawer->ThumbnailPopup();
	}
}

HIMAGELIST GetThumbnailIcons(const bool force_refresh)
{
	static HIMAGELIST theicons;
	if (!theicons || force_refresh)
	{
		if (theicons)
		{
			ImageList_Destroy(theicons);
			theicons = NULL;
		}

		theicons = tools::prepareIcons();
	}
	return theicons;
}

HIMAGELIST GetOverlayIcons(const bool force_refresh)
{
	static HIMAGELIST overlayicons;
	if (!overlayicons || force_refresh)
	{
		if (overlayicons)
		{
			ImageList_Destroy(overlayicons);
			overlayicons = NULL;
		}

		overlayicons = tools::prepareOverlayIcons();
	}
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
		static wchar_t *playing_str = WASABI_API_LNGSTRINGW_DUP(IDS_PLAYING),
					   *paused_str = WASABI_API_LNGSTRINGW_DUP(IDS_PAUSED);
		HICON icon = NULL;
		switch (Settings.play_state)
		{
			case PLAYSTATE_PLAYING:
			{
				if (itaskbar != NULL)
				{
					const int index = tools::getBitmap(TB_PLAYPAUSE, 0);
					icon = ImageList_GetIcon(GetOverlayIcons(force_refresh), (index - 1), 0);
					if (icon == NULL)
					{
						icon = ImageList_GetIcon(GetThumbnailIcons(false/*force_refresh*/), index, 0);
					}
					itaskbar->SetIconOverlay(icon, playing_str);
				}
				break;
			}
			case PLAYSTATE_PAUSED:
			{
				if (itaskbar != NULL)
				{
					const int index = tools::getBitmap(TB_PLAYPAUSE, 1);
					icon = ImageList_GetIcon(GetOverlayIcons(force_refresh), (index - 1), 0);
					if (icon == NULL)
					{
						icon = ImageList_GetIcon(GetThumbnailIcons(false/*force_refresh*/), index, 0);
					}
					itaskbar->SetIconOverlay(icon, paused_str);
				}
				break;
			}
			default:
			{
				if (itaskbar != NULL)
				{
					const int index = tools::getBitmap(TB_STOP, 1);
					icon = ImageList_GetIcon(GetOverlayIcons(force_refresh), index, 0);
					if (icon == NULL)
					{
						icon = ImageList_GetIcon(GetThumbnailIcons(false/*force_refresh*/), index, 0);
					}

					if (itaskbar != NULL)
					{
						itaskbar->SetIconOverlay(icon, paused_str);
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

void __cdecl MessageProc(HWND hWnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
{
	if ((uMsg == WM_DWMSENDICONICTHUMBNAIL) ||
		(uMsg == WM_DWMSENDICONICLIVEPREVIEWBITMAP))
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
	else if (uMsg == WM_WA_IPC)
	{
		switch (lParam)
		{
			case IPC_PLAYING_FILEW:
			{
				Settings.play_playlistpos = GetPlaylistPosition();

				std::wstring filename((wchar_t*)wParam);
				if (filename.empty())
				{
					LPCWSTR p = GetPlayingFilename(1);
					if (p != NULL)
					{
						filename = p;
					}
				}

				metadata.reset(filename);
				if (metadata.CheckPlayCount())
				{
					JumpList *JL = new JumpList(GetAppID());
					if (JL != NULL)
					{
						delete JL;
					}
				}

				Settings.play_total = GetCurrentTrackLengthMilliSeconds();
				Settings.play_current = 0;
				Settings.play_state = GetPlayingState();

				if ((Settings.JLrecent || Settings.JLfrequent)/* && !tools::is_in_recent(filename)*/ &&
					(Settings.play_state == PLAYSTATE_PLAYING) && Settings.Add2RecentDocs)
				{
					__try
					{
						const std::wstring title(metadata.getMetadata(L"title") +
												 L" - " + metadata.getMetadata(L"artist") +
												 ((Settings.play_total > 0) ? L"  (" +
												 tools::SecToTime(Settings.play_total / 1000) + L")" : L""));

						IShellLink *psl = NULL;
						if ((tools::CreateShellLink(filename.c_str(), title.c_str(), &psl) == S_OK) && psl)
						{
							const SHARDAPPIDINFOLINK applink = { psl, GetAppID() };
							time_t rawtime = NULL;
							time(&rawtime);
							psl->SetDescription(_wctime(&rawtime));
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
					SetupJumpList();
				}
				break;
			}
			case IPC_SKIN_CHANGED_NEW:
			{
				// this is needed when the vu mode is enabled to allow the
				// data to be obtained if the main wnidow mode is disabled
				static void(*export_sa_setreq)(int) = (void(__cdecl *)(int))GetSADataFunc(1);
				if (export_sa_setreq)
				{
					export_sa_setreq(Settings.VuMeter);
				}

				// delay doing this until needed as it then
				// copes with modern skins being later used
				if (WASABI_API_SKIN == NULL)
				{
					ServiceBuild(plugin.service, WASABI_API_SKIN, skinApiServiceGuid);
				}

				// TODO pull in the localised version from gen_ff
				//		to ensure the checking will work correctly
				LPCWSTR skin_name = (WASABI_API_SKIN != NULL ? WASABI_API_SKIN->getSkinName() : NULL);
				classicSkin = (!WASABI_API_SKIN || WASABI_API_SKIN &&
							  // TODO pull in the localised version from gen_ff
							  //		to ensure the checking will work correctly
							  (skin_name && *skin_name && !_wcsicmp(skin_name, L"No skin loaded")));

				modernSUI = false;
				modernFix = (skin_name && *skin_name && !_wcsnicmp(skin_name, L"Winamp Modern", 13));
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

				if (itaskbar != NULL)
				{
					itaskbar->SetWindowAttr();
				}

				UpdateOverlyStatus(true);

				// fall-through for the other handling needed
			}
			default:
			{
				// make sure if not playing but prev / next is done
				// that we update the thumbnail for the current one
				if ((lParam == IPC_FILE_TAG_MAY_HAVE_UPDATEDW) ||
					(lParam == IPC_FILE_TAG_MAY_HAVE_UPDATED) ||
					(lParam == IPC_CB_MISC) &&
					((wParam == IPC_CB_MISC_TITLE) ||
					(wParam == IPC_CB_MISC_AA_OPT_CHANGED) ||
					(wParam == IPC_CB_MISC_TITLE_RATING) ||
					(wParam == IPC_CB_MISC_ON_STOP) ||
					(wParam == IPC_CB_MISC_ADVANCED_ON_STOP)))
				{
					LPCWSTR p = GetPlayingFilename(0);
					if (p != NULL)
					{
						metadata.reset(p);
					}

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
				else if (lParam == delay_ipc)
				{
					// we track this instead of re-querying all of the
					// time so as to minimise blocking of the main wnd
					// which helps to determine modern vs classic skin
					dialogParent = GetDialogBoxParent()/*/(HWND)SendMessage(hWnd, WM_WA_IPC, 0, IPC_GETDIALOGBOXPARENT)/*/;

					pladv = !!GetManualAdvance();

					windowShade = !!IsHWNDWndshade((WPARAM)-1)/*/SendMessage(hWnd, WM_WA_IPC, (WPARAM)-1, IPC_IS_WNDSHADE)/**/;

					if (!OnWINE())
					{
						// Accept messages even if Winamp was run as Administrator
						ChangeWindowMessageFilter(WM_COMMAND, 1);
						ChangeWindowMessageFilter(WM_DWMSENDICONICTHUMBNAIL, 1);
						ChangeWindowMessageFilter(WM_DWMSENDICONICLIVEPREVIEWBITMAP, 1);
					}

					// Register taskbarcreated message
					WM_TASKBARBUTTONCREATED = RegisterWindowMessage(L"TaskbarButtonCreated");

					wchar_t ini_path[MAX_PATH] = { 0 };
					CombinePath(ini_path, GetPaths()->settings_sub_dir, L"win7shell.ini");
					SettingsFile = ini_path;

					// Read Settings into struct
					SettingsManager SManager;
					SManager.ReadSettings(Settings, TButtons);

					// Timers, settings, icons
					Settings.play_playlistpos = GetPlaylistPosition();
					Settings.play_playlistlen = GetPlaylistLength();
					Settings.play_total = GetCurrentTrackLengthMilliSeconds();
					Settings.play_volume = (int)GetSetVolume((WPARAM)-666, FALSE);

					LPCWSTR p = GetPlayingFilename(1);
					if (p != NULL)
					{
						metadata.reset(p);
					}

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
						hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, (HOOKPROC) KeyboardEvent, plugin.hDllInstance, NULL);
						if (hMouseHook == NULL)
						{
							MessageBoxEx(plugin.hwndParent,
											WASABI_API_LNGSTRINGW(IDS_ERROR_REGISTERING_MOUSE_HOOK),
											(LPWSTR)plugin.description, MB_ICONWARNING, 0);
						}
					}
#endif
					SetTimer(plugin.hwndParent, 6667, Settings.LowFrameRate ? 400 : 100, TimerProc);
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

					LPCWSTR p = GetPlayingFilename(0);
					if (p != NULL)
					{
						metadata.reset(p);
					}

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
					ratewnd = WASABI_API_CREATEDIALOGW(IDD_RATEDLG, plugin.hwndParent, rateWndProc);

					RECT rc = {0};
					POINT point = {0};
					GetCursorPos(&point);
					GetWindowRect(ratewnd, &rc);
					MoveWindow(ratewnd, point.x - 155, point.y - 15, rc.right - rc.left, rc.bottom - rc.top, false);
					KillTimer(plugin.hwndParent, 6669);
					SetTimer(plugin.hwndParent, 6669, 5000, TimerProc);
					ShowWindow(ratewnd, SW_SHOWNA);
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
					LPCWSTR filename = metadata.getFileName().c_str();
					if (filename && *filename)
					{
						if (FileExists(filename))
						{
							/*if (WASABI_API_EXPLORERFINDFILE == NULL)
							{
								ServiceBuild(plugin.service, WASABI_API_EXPLORERFINDFILE, ExplorerFindFileApiGUID);
							}
							if (WASABI_API_EXPLORERFINDFILE != NULL)
							{
								WASABI_API_EXPLORERFINDFILE->AddFile(filename);
								WASABI_API_EXPLORERFINDFILE->ShowFiles();
							}/*/
							plugin.explorerfindfile->AddAndShowFile(filename);
						}
						else
						{
							// TODO it would be useful if there was a common interface
							//		that allows for determining the real filename when
							//		there's extras after the extension or custom urls
							//		so it can just be used where needed without plug-in
							//		workarounds to fix issues (explorerfindfile is meant
							//		cope with zip:// but for some reason it fails now &
							//		it just won't cope with cda:// style entries either)
							if (!_wcsnicmp(filename, L"zip://", 6))
							{
								filename += 6;
							}

							// if there's an extension then we'll give it another go
							// as typically no extension means that it won't work...
							if (FindPathExtension(filename))
							{
								plugin.explorerfindfile->AddAndShowFile(filename);
							}
						}
					}
					break;
				}
				case TB_DELETE:
				{
					SHFILEOPSTRUCT fileop = { 0 };
					wchar_t path[MAX_PATH] = { 0 };
					(void)StringCchCopy(path, ARRAYSIZE(path), metadata.getFileName().c_str());

					fileop.wFunc = FO_DELETE;
					fileop.pFrom = path;
					fileop.pTo = L"";
					fileop.fFlags = FOF_ALLOWUNDO | FOF_FILESONLY;
					fileop.lpszProgressTitle = L"";

					const int saved_play_state = Settings.play_state;
					SendMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(40047, 0), 0);
					Settings.play_state = PLAYSTATE_NOTPLAYING; 

					if (FileAction(&fileop) == 0)
					{
						SendMessage(GetPlaylistWnd(), WM_WA_IPC, IPC_PE_DELETEINDEX, GetPlaylistPosition());
					}

					if (saved_play_state == PLAYSTATE_PLAYING)
					{
						PostMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(40045, 0), 0);
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
		if ((itaskbar != NULL) && itaskbar->Reset())
		{
			updateToolbar(GetThumbnailIcons(true));
		}

		// TODO can this be done in a background
		//		thread so it won't block as some
		//		installs seem to be slow to run
		SetupJumpList();
	}
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
				SetupJumpList();

				// Create the taskbar interface
				itaskbar = new iTaskBar(Settings);
				if ((itaskbar != NULL) && itaskbar->Reset())
				{
					updateToolbar(GetThumbnailIcons(false));
				}

				if (Settings.VuMeter)
				{
					SetTimer(hwnd, 6668, 66, TimerProc);
				}
			}

			if (!(Settings.Progressbar || Settings.VuMeter) && (itaskbar != NULL))
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
								metadata.reset(L"", true);
							}
							else
							{
								++count2;
							}
						}

						if (Settings.Progressbar && (itaskbar != NULL))
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
						if (Settings.Progressbar && (itaskbar != NULL))
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
						if (Settings.Progressbar && (itaskbar != NULL)) 
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
			if (Settings.VuMeter && (itaskbar != NULL))
			{
				// we try to use the vumeter but revert
				// to the sa mode and convert as needed
				// this also forces these modes to work
				// when using a classic skin & sa/vu is
				// disabled like gen_ff has to work ok.
				static int (*export_vu_get)(int channel) =
					   (int(__cdecl *)(int))GetVUDataFunc();
				static char * (*export_sa_get)(char data[75*2+8]) =
					   (char * (__cdecl *)(char data[75*2+8]))GetSADataFunc(2);
				int audiodata = (export_vu_get ? export_vu_get(0) : -1);

				if (Settings.play_state != PLAYSTATE_PLAYING)
				{
					itaskbar->SetProgressState(TBPF_NOPROGRESS);
				}
				else
				{
					if (audiodata == -1)
					{
						char data[75*2+8] = {0};
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
					else if (audiodata > 150 && audiodata < 210)
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
			// this is far from ideal as there's nothing obvious that windows
			// provides to determine once we have done showing the preview so
			// we'll try & see if the preview taskbar window is visible & the
			// win+tab task view & the alt+tab overlay to avoid drawing mode.
			static HWND previewlist = FindWindow(L"TaskListThumbnailWnd", L"");
			if (IsWindow(previewlist))
			{
				running = (GetWindowLongPtr(previewlist, GWL_STYLE) & WS_VISIBLE);
			}

			if (!running)
			{
				const HWND taskview = FindWindow(L"Windows.UI.Core.CoreWindow", L"Task View");
				if (IsWindow(taskview) && (GetForegroundWindow() == taskview))
				{
					running = true;
				}
			}

			/*if (!running)
			{
				const HWND win7taskview = FindWindow(L"Flip3D", L"");
				if (IsWindow(win7taskview) && (GetForegroundWindow() == win7taskview))
				{
					running = true;
				}
			}*/

			if (!running)
			{
				// this is ok for Windows 10 afaict & then reverts to the older version
				HWND alttab = FindWindow(L"MultitaskingViewFrame", L"Task Switching");
				if (IsWindow(alttab) && (GetForegroundWindow() == alttab))
				{
					running = true;
				}
				else
				{
					alttab = FindWindow(L"TaskSwitcherWnd", L"Task Switching");
					if (IsWindow(alttab) && (GetForegroundWindow() == alttab))
					{
						running = true;
					}
				}
			}
			break;
		}
		case 6671:	// long running check mostly for Windows 11
		{
			KillTimer(plugin.hwndParent, 6671);

			// TODO until above is updated to correctly deal with
			//		Windows 11 (not touching the beta preview for
			//		now) it's necessary to see if there's a long
			//		running instance running & manually kill it &
			//		hope that it's not going to clash with others
			const bool was_running = running;
			if (running)
			{
				running = false;
				KillTimer(plugin.hwndParent, 6670);
			}

			if (was_running)
			{
				SetTimer(plugin.hwndParent, 6671, 30000, TimerProc);
			}
			break;
		}
	}
}

LRESULT CALLBACK rateWndProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		case WM_COMMAND:
		{
			WPARAM value = (WPARAM)-1;
			switch (LOWORD(wParam))
			{
				case IDC_RATE1:
				{
					value = 0;
					break;
				}
				case IDC_RATE2:
				{
					value = 1;
					break;
				}
				case IDC_RATE3:
				{
					value = 2;
					break;
				}
				case IDC_RATE4:
				{
					value = 3;
					break;
				}
				case IDC_RATE5:
				{
					value = 4;
					break;
				}
				case IDC_RATE6:
				{
					value = 5;
					break;
				}
			}

			if (value != (WPARAM)-1)
			{
				GetSetMainRating(value, IPC_SETRATING);
			}

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
			SettingsManager::WriteSettings_ToForm(hwnd, plugin.hwndParent, Settings);

			const BOOL enabled = GetTaskbarMode();
			CheckDlgButton(hwnd, IDC_SHOW_IN_TASKBAR, (enabled ? BST_CHECKED : BST_UNCHECKED));
			EnableControl(hwnd, 1267, enabled);
			EnableControl(hwnd, IDC_HIDE_ON_MINIMISE, enabled);

			CheckDlgButton(hwnd, IDC_HIDE_ON_MINIMISE, (GetTaskbarOnMinimiseMode() ? BST_CHECKED : BST_UNCHECKED));

			SetupTaskberIcon(hwnd);
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
					wchar_t filepath[MAX_PATH] = {0};
					SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, filepath);
					AppendOnPath(filepath, L"\\Microsoft\\Windows\\Recent\\AutomaticDestinations"
										   L"\\879d567ffa1f5b9f.automaticDestinations-ms");

					if (DeleteFile(filepath) != 0)
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
					SetupJumpList();
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
						if ((itaskbar != NULL))
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
						UpdateTaskberIcon(hwnd);
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
								MessageBoxEx(plugin.hwndParent,
											 WASABI_API_LNGSTRINGW(IDS_ERROR_REGISTERING_MOUSE_HOOK),
											 (LPWSTR)plugin.description, MB_ICONWARNING, 0);
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
			// save the settings only when changed instead of always on closing
			SettingsManager SManager;
			SManager.WriteSettings(Settings);
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
	for (int i = 0; i < ARRAYSIZE(ids); i++)
	{
		EnableControl(hwnd, ids[i], Settings.Thumbnailbuttons);
	}
}

LRESULT CALLBACK TabHandler_ThumbnailImage(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
		case WM_INITDIALOG:
		{
			// Reset buttons
			for (int i = IDC_PCB1; i <= IDC_PCB15; i++)
			{
				SendDlgItemMessage(hwnd, i, BM_SETCHECK, BST_UNCHECKED, NULL);
			}

			SettingsManager::WriteSettings_ToForm(hwnd, plugin.hwndParent, Settings);

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

				DestroyWindow(GetDlgItem(hwnd, IDC_BUTTON_HELP));
				SetDlgItemText(hwnd, IDC_TEXT_GROUP, WASABI_API_LNGSTRINGW(IDS_TEXT_DISABLED));
			}

			const HWND list = GetDlgItem(hwnd, IDC_LIST1);
			for (size_t i = 0; i < TButtons.size(); ++i)
			{
				SendMessage(list, LB_SETITEMDATA, SendMessage(list, LB_ADDSTRING, NULL,
							(LPARAM)tools::getToolTip(TButtons[i], -1)), TButtons[i]);
				SendDlgItemMessage(hwnd, TButtons[i], BM_SETCHECK, BST_CHECKED, NULL);
			}
			
			// Set button icons
			for (int i = 0; i < NR_BUTTONS; i++)
			{
				HICON icon = ImageList_GetIcon(GetThumbnailIcons(false), tools::getBitmap(TB_PREVIOUS + i, i == 10 ? 1 : 0), 0);
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
				GetWindowRect(GetDlgItem(hwnd, IDC_BUTTON9), &rect);

				// top right	// 0
				points[0].x = points[1].x = rect.right;
				points[0].y = rect.top;

				// bottom right	// 1
				points[1].y = rect.bottom;

				// button2
				GetWindowRect(GetDlgItem(hwnd, IDC_BUTTON6), &rect);

				// top right	// 2
				points[2].x = points[3].x = rect.right;
				points[2].y = rect.top;

				// bottom right	// 3
				points[3].y = rect.bottom;

				// editbox
				GetWindowRect(GetDlgItem(hwnd, IDC_EDIT3), &rect);

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
				SetDlgItemText(hwnd, IDC_STATIC29, WASABI_API_LNGSTRINGW(IDS_MAX_BUTTONS));
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
							SetDlgItemText(hwnd, IDC_STATIC29, tools::getToolTip(wParam));
						}
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
					index = (int)SendMessage(list, LB_INSERTSTRING, index - 1,
											 (LPARAM)tools::getToolTip(data));
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
					index = (int)SendMessage(list, LB_INSERTSTRING, index + 1,
											 (LPARAM)tools::getToolTip(data));
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
						if ((itaskbar != NULL) && itaskbar->Reset())
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
						(void)StringCchCopy(Settings.Text, ARRAYSIZE(Settings.Text), text.c_str());

						ResetThumbnail();
					}
					break;
				}
				case IDC_BUTTON_HELP:
				{
					DWORD data_size = 0;
					unsigned char *data = (unsigned char *)WASABI_API_LOADRESFROMFILEW(L"GZ", MAKEINTRESOURCEW(IDR_HELP_GZ), &data_size),
								  *output = NULL;

					DecompressResource(data, data_size, &output, 0);
					MessageBoxEx(GetPrefsHWND(), AutoWide((LPCSTR)output, CP_UTF8),
								 WASABI_API_LNGSTRINGW(IDS_INFORMATION), MB_ICONINFORMATION, 0);
					DecompressResourceFree(output);
					break;
				}
				case IDC_DEFAULT:
				{
					SetDlgItemText(hwnd, IDC_EDIT3, L"%c%%s%%curpl% of %totalpl%.\r\n"
								  L"%c%%s%%title%\r\n%c%%s%%artist%\r\n\r\n%c%%s%%curtime%/"
								  L"%totaltime%\r\n%c%%s%Track #: %track%        Volume: %volume%%");
					PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_EDIT3, EN_CHANGE), NULL);
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
						thumbnaildrawer->ClearFonts();
					}
					break;
				}
				case IDC_BUTTON6:
				case IDC_BUTTON9:
				{
					const bool text = (LOWORD(wParam) == IDC_BUTTON9);
					CHOOSECOLOR cc = { 0 };			// common dialog box structure
					cc.lStructSize = sizeof(cc);
					cc.hwndOwner = GetPrefsHWND();
					cc.lpCustColors = (LPDWORD)acrCustClr;
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
			// save the settings only when changed instead of always on closing
			SettingsManager SManager;
			SManager.WriteButtons(TButtons);
			SManager.WriteSettings(Settings);
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

LRESULT CALLBACK TabHandler_Thumbnail(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
		case WM_INITDIALOG:
		{
			SettingsManager::WriteSettings_ToForm(hwnd, plugin.hwndParent, Settings);
			SendDlgItemMessage(hwnd, IDC_EDIT2, EM_SETREADONLY, TRUE, NULL);
			SendDlgItemMessage(hwnd, IDC_SLIDER1, TBM_SETRANGE, FALSE, MAKELPARAM(30, 100));
			SendDlgItemMessage(hwnd, IDC_SLIDER_TRANSPARENCY, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));

			switch (Settings.IconPosition)
			{
				case IP_UPPERLEFT:
				{
					SetDlgItemText(hwnd, IDC_ICONPOS, WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_TL));
					break;
				}
				case IP_UPPERRIGHT:
				{
					SetDlgItemText(hwnd, IDC_ICONPOS, WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_TR));
					break;
				}
				case IP_LOWERLEFT:
				{
					SetDlgItemText(hwnd, IDC_ICONPOS, WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_BL));
					break;
				}
				case IP_LOWERRIGHT:
				{
					SetDlgItemText(hwnd, IDC_ICONPOS, WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_BR));
					break;
				}
			}

			EnumChildWindows(hwnd, EnumDialogControls, (Settings.Thumbnailbackground != BG_WINAMP));

			UpdateIconControls(hwnd);
			break;
		}
		case WM_HSCROLL:
		{
			wchar_t text[64] = { 0 };
			DWORD slider = GetDlgCtrlID((HWND)lParam);
			if (slider == IDC_SLIDER1)
			{
				Settings.IconSize = (int)SendMessage((HWND)lParam, TBM_GETPOS, NULL, NULL);
				StringCchPrintf(text, ARRAYSIZE(text), WASABI_API_LNGSTRINGW(IDS_ICON_SIZE), Settings.IconSize);
				SetDlgItemText(hwnd, IDC_ICONSIZE, text);
			}
			else if (slider == IDC_SLIDER_TRANSPARENCY)
			{
				Settings.BG_Transparency = (int)SendMessage((HWND)lParam, TBM_GETPOS, NULL, NULL);
				StringCchPrintf(text, ARRAYSIZE(text), L"%d%%", Settings.BG_Transparency);
				SetDlgItemText(hwnd, IDC_TRANSPARENCY_PERCENT, text);
			}
			ResetThumbnail();
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
						if (GetWindowTextLength(GetDlgItem(hwnd, IDC_EDIT2)) == 0)
						{
							PostDlgItemMessage(hwnd, IDC_BUTTON3, BM_CLICK, 0, 0);
						}
					}

					EnumChildWindows(hwnd, EnumDialogControls, (Settings.Thumbnailbackground != BG_WINAMP));

					if (itaskbar != NULL)
					{
						if (itaskbar->Reset())
						{
							updateToolbar(GetThumbnailIcons(false));
						}

						itaskbar->SetWindowAttr();
					}

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
					wchar_t filename[MAX_PATH] = {0};
					IFileDialog *pfd = NULL;

					// CoCreate the dialog object.
					HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
					if (SUCCEEDED(hr) && pfd)
					{
						// Show the dialog
						wchar_t tmp[128] = {0}, tmp2[128] = {0}, tmp3[128] = {0};
						COMDLG_FILTERSPEC rgSpec[] =
						{ 
							{ WASABI_API_LNGSTRINGW_BUF(IDS_ALL_IMAGE_FORMATS, tmp, 128),
							L"*.bmp;*.gif;*.jpg;*.jpeg;*.png"
							}
						};

						pfd->SetFileTypes(1, rgSpec);
						pfd->SetOkButtonLabel(WASABI_API_LNGSTRINGW_BUF(IDS_USE_AS_THUMB_BKGND, tmp2, 128));
						pfd->SetTitle(WASABI_API_LNGSTRINGW_BUF(IDS_SELECT_IMAGE_FILE, tmp3, 128));
						hr = pfd->Show(GetPrefsHWND());

						if (SUCCEEDED(hr))
						{
							// Obtain the result of the user's interaction with the dialog.
							IShellItem *psiResult = NULL;
							hr = pfd->GetResult(&psiResult);

							if (SUCCEEDED(hr))
							{
								wchar_t *w = NULL;
								psiResult->GetDisplayName(SIGDN_FILESYSPATH, &w);
								psiResult->Release();
								(void)StringCchCopy(filename, ARRAYSIZE(filename), w);
								CoTaskMemFree(w);
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
					ResetThumbnail();
					break;
				}
				case IDC_RADIO4:
				case IDC_RADIO6:
				case IDC_RADIO7:
				case IDC_RADIO8:
				{
					if (SendDlgItemMessage(hwnd, IDC_RADIO4, (UINT)BM_GETCHECK, 0 , 0))
					{
						SetDlgItemText(hwnd, IDC_ICONPOS, WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_TL));
						Settings.IconPosition = IP_UPPERLEFT;
					}
					else if (SendDlgItemMessage(hwnd, IDC_RADIO7, (UINT)BM_GETCHECK, 0 , 0))
					{
						SetDlgItemText(hwnd, IDC_ICONPOS, WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_BL));
						Settings.IconPosition = IP_LOWERLEFT;
					}
					else if (SendDlgItemMessage(hwnd, IDC_RADIO6, (UINT)BM_GETCHECK, 0 , 0))
					{
						SetDlgItemText(hwnd, IDC_ICONPOS, WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_TR));
						Settings.IconPosition = IP_UPPERRIGHT;
					}
					else if (SendDlgItemMessage(hwnd, IDC_RADIO8, (UINT)BM_GETCHECK, 0 , 0))
					{
						SetDlgItemText(hwnd, IDC_ICONPOS, WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_BR));
						Settings.IconPosition = IP_LOWERRIGHT;
					}

					ResetThumbnail();
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
			// save the settings only when changed instead of always on closing
			SettingsManager SManager;
			SManager.WriteSettings(Settings);
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
			const int index = (const int)SendMessage(list, LB_FINDSTRINGEXACT, (WPARAM)-1,
													 (LPARAM)tools::getToolTip(control_ID));
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
			const int index = (const int)SendMessage(list, LB_ADDSTRING, NULL, (LPARAM)
													 tools::getToolTip(control_ID));
			SendMessage(list, LB_SETITEMDATA, index, control_ID);
		}
		else
		{
			if (ListBox_GetCount(list) >= 7)
			{
				Button_SetCheck(button, BST_UNCHECKED);
				SetDlgItemText(window, IDC_STATIC29, WASABI_API_LNGSTRINGW(IDS_MAX_BUTTONS));
			}
			else
			{
				LPCWSTR tooltip = tools::getToolTip(control_ID);
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

void SetupJumpList(void)
{
	JumpList *jl = new JumpList(GetAppID(), true);
	if (jl != NULL)
	{
		if (Settings.JLbms || Settings.JLfrequent ||
			Settings.JLpl || Settings.JLrecent || Settings.JLtasks)
		{
			static wchar_t pluginPath[MAX_PATH], tmp1[128],
						   tmp2[128], tmp3[128], tmp4[128];

			// to ensure things work reliably we
			// need an 8.3 style filepath for us
			if (!pluginPath[0])
			{
				GetModuleFileName(plugin.hDllInstance, pluginPath, MAX_PATH);
				GetShortPathName(pluginPath, pluginPath, MAX_PATH);
			}

			if (!tmp1[0])
			{
				WASABI_API_LNGSTRINGW_BUF(IDS_WINAMP_PREFERENCES, tmp1, 128);
			}

			if (!tmp2[0])
			{
				WASABI_API_LNGSTRINGW_BUF(IDS_OPEN_FILE, tmp2, 128);
			}

			if (!tmp3[0])
			{
				WASABI_API_LNGSTRINGW_BUF(IDS_BOOKMARKS, tmp3, 128);
			}

			if (!tmp4[0])
			{
				WASABI_API_LNGSTRINGW_BUF(IDS_PLAYLISTS, tmp4, 128);
			}

			__try
			{
				// based on testing, this & things in the
				// CreateShellLink() sometimes fails :'(
				// I can't find any reason for it. due to
				// that it is necessary to try & catch it
				// so we don't take down the entire thing
				jl->CreateJumpList(pluginPath, tmp1, tmp2, tmp3, tmp4, Settings.JLrecent,
								   Settings.JLfrequent, Settings.JLtasks, Settings.JLbms,
								   Settings.JLpl, tools::getBookmarks());
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
			}
		}
		delete jl;
	}
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
	if (MessageBoxEx(hwndDlg, WASABI_API_LNGSTRINGW(IDS_UNINSTALL_PROMPT),
					 (LPWSTR)plugin.description, MB_YESNO, 0) == IDYES)
	{
		no_uninstall = false;

		wchar_t ini_path[MAX_PATH] = { 0 };
		CombinePath(ini_path, GetPaths()->settings_sub_dir, L"win7shell.ini");
		if (FileExists(ini_path))
		{
			DeleteFile(ini_path);
		}

		JumpList *jl = new JumpList(GetAppID(), true);
		if (jl != NULL)
		{
			delete jl;
		}
	}

	return GEN_PLUGIN_UNINSTALL_REBOOT;
}