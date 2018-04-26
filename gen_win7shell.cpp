#define PLUGIN_VERSION L"3.0.2"
#define ICONSIZEPX 50
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
#include <loader/hook/get_api_service.h>
#include <loader/loader/utils.h>
#include <loader/loader/paths.h>
#include <loader/hook/squash.h>

// TODO add to lang.h
// Taskbar Integration plugin (gen_win7shell.dll)
// {0B1E9802-CA15-4939-8445-FD800E8BFF9A}
static const GUID GenWin7PlusShellLangGUID = 
{ 0xb1e9802, 0xca15, 0x4939, { 0x84, 0x45, 0xfd, 0x80, 0xe, 0x8b, 0xff, 0x9a } };

UINT WM_TASKBARBUTTONCREATED = (UINT)-1;
std::wstring AppID(L"Winamp"),	// this is updated on loading to what the
								// running WACUP install has generated as
								// it otherwise makes multiple instances
								// tricky to work with independently
			 SettingsFile;

bool thumbshowing = false, no_uninstall = true,
	 classicSkin = true, windowShade = false,
	 doubleSize = false, modernSUI = false,
	 running = false;
HWND ratewnd = 0, dialogParent = 0;
int pladv = 1, repeat = 0;
LPARAM delay_ipc = -1;
HIMAGELIST theicons = NULL;
#ifdef USE_MOUSE
HHOOK hMouseHook = NULL;
#endif
COLORREF acrCustClr[16] = {0};	// array of custom colors
sSettings Settings = {0};
std::vector<int> TButtons;
iTaskBar *itaskbar = NULL;
MetaData metadata;
renderer* thumbnaildrawer = NULL;

api_service *WASABI_API_SVC = 0;
api_memmgr *WASABI_API_MEMMGR = 0;
api_albumart *AGAVE_API_ALBUMART = 0;
api_playlists *AGAVE_API_PLAYLISTS = 0;
api_explorerfindfile *WASABI_API_EXPLORERFINDFILE = 0;
api_skin *WASABI_API_SKIN = 0;
api_language *WASABI_API_LNG = 0;
// these two must be declared as they're used by the language api's
// when the system is comparing/loading the different resources
HINSTANCE WASABI_API_LNG_HINST = 0, WASABI_API_ORIG_HINST = 0;

// CALLBACKS
VOID CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
						 UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
LRESULT CALLBACK rateWndProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);
#ifdef USE_MOUSE
LRESULT CALLBACK KeyboardEvent(int nCode, WPARAM wParam, LPARAM lParam);
#endif

extern "C" __declspec(dllexport) LRESULT CALLBACK TabHandler_Taskbar(HWND, UINT, WPARAM, LPARAM);
extern "C" __declspec(dllexport) LRESULT CALLBACK TabHandler_Thumbnail(HWND, UINT, WPARAM, LPARAM);
extern "C" __declspec(dllexport) LRESULT CALLBACK TabHandler_ThumbnailImage(HWND, UINT, WPARAM, LPARAM);

void updateToolbar(HIMAGELIST ImageList = NULL);
void SetupJumpList();
void AddStringtoList(HWND window, const int control_ID);

// Winamp EVENTS
int init(void);
void config(void);
void quit(void);

// this structure contains plugin information, version, name...
winampGeneralPurposePlugin plugin =
{
	GPPHDR_VER_U,
	(char*)L"Taskbar Integration",
	init,
	config,
	quit,
	0,
	0
};

bool CreateThumbnailDrawer()
{
	if (thumbnaildrawer == NULL)
	{
		// Create thumbnail renderer
		thumbnaildrawer = new renderer(Settings, metadata);
	}

	return (thumbnaildrawer != NULL);
}

static wchar_t pluginTitleW[256];
wchar_t* BuildPluginNameW(void)
{
	if (!pluginTitleW[0])
	{
		StringCchPrintf(pluginTitleW, ARRAYSIZE(pluginTitleW), WASABI_API_LNGSTRINGW(IDS_PLUGIN_NAME), PLUGIN_VERSION);
	}
	return pluginTitleW;
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
				const int len = lstrlen(path);
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

void SetupAppID()
{
	// we do this to make sure we can group things correctly
	// especially if used in a plug-in in Winamp so that the
	// taskbar handling will be correct vs existing pinnings
	// under wacup, we do a few things so winamp.original is
	// instead re-mapped to winamp.exe so the load is called
	LPWSTR id = NULL;
	GetCurrentProcessExplicitAppUserModelID(&id);
	if (!id)
	{
		wchar_t self_path[MAX_PATH] = {0};
		if (GetModuleFileName(NULL, self_path, MAX_PATH))
		{
			wchar_t app_id[MAX_PATH] = {0};
			if (!GenerateAppIDFromFolder(self_path, app_id))
			{
				wcsncpy(app_id, self_path, MAX_PATH);
			}

			PathRenameExtension(app_id, L".exe");

			// TODO: auto-pin icon (?)
			if (SetCurrentProcessExplicitAppUserModelID(app_id) != S_OK)
			{
				MessageBoxEx(plugin.hwndParent,
							 WASABI_API_LNGSTRINGW(IDS_ERROR_SETTING_APPID),
							 BuildPluginNameW(), MB_ICONWARNING | MB_OK, 0);
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
}

// event functions follow
int init() 
{
	WASABI_API_SVC = GetServiceAPIPtr();/*/
	// load all of the required wasabi services from the winamp client
	WASABI_API_SVC = reinterpret_cast<api_service*>(SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GET_API_SERVICE));
	if (WASABI_API_SVC == reinterpret_cast<api_service*>(1)) WASABI_API_SVC = NULL;/**/
	if (WASABI_API_SVC != NULL)
	{
		/************************************************************************/
		/* Winamp services                                                      */
		/************************************************************************/
		ServiceBuild(WASABI_API_SVC, WASABI_API_MEMMGR, memMgrApiServiceGuid);
		ServiceBuild(WASABI_API_SVC, AGAVE_API_ALBUMART, albumArtGUID);
		ServiceBuild(WASABI_API_SVC, AGAVE_API_PLAYLISTS, api_playlistsGUID);
		ServiceBuild(WASABI_API_SVC, WASABI_API_LNG, languageApiGUID);
		WASABI_API_START_LANG(plugin.hDllInstance, GenWin7PlusShellLangGUID);

		plugin.description = (char*)BuildPluginNameW();

		// Override window procedure
		SetWindowSubclass(plugin.hwndParent, WndProc, (UINT_PTR)WndProc, 0);

		// Delay loading mst parts until later on to improve the overall load time
		delay_ipc = SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)&"7+_ipc", IPC_REGISTER_WINAMP_IPCMESSAGE);
		PostMessage(plugin.hwndParent, WM_WA_IPC, 0, delay_ipc);

		return GEN_INIT_SUCCESS;
	}
	return GEN_INIT_FAILURE;
}

#define AddItemToMenu(hmenu, id, text) AddItemToMenu2(hmenu, id, text, (UINT)-1, 0);
void AddItemToMenu2(HMENU hmenu, const UINT id, LPWSTR text, const UINT pos, const int fByPosition)
{
	MENUITEMINFO mii = {sizeof(MENUITEMINFO), MIIM_ID | MIIM_TYPE | MIIM_DATA,
						(text ? MFT_STRING : MFT_SEPARATOR), 0, id, 0, 0, 0, 0,
						text, (text ? lstrlen(text) : 0)};
	if (id == -2)
	{
		mii.fMask |= MIIM_STATE;
		mii.fState = MFS_GRAYED;
	}
	else if (id == -3)
	{
		mii.fType |= MFT_MENUBARBREAK;
	}

	InsertMenuItem(hmenu, pos, fByPosition, &mii);
}

void config()
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
			StringCchPrintf(text, 512, WASABI_API_LNGSTRINGW(IDS_ABOUT_MESSAGE),
							L"Darren Owen aka DrO (2018)", TEXT(__DATE__));
			AboutMessageBox(list, text, (LPWSTR)plugin.description);
			break;
		}
		case 2:
		{
			// leave the WACUP core to handle opening to the 'taskbar' preferences node
			PostMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)-667, IPC_OPENPREFSTOPAGE);
			break;
		}
	}

	DestroyMenu(popup);
}

void quit() 
{
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

	ServiceRelease(WASABI_API_SVC, WASABI_API_MEMMGR, memMgrApiServiceGuid);
	ServiceRelease(WASABI_API_SVC, AGAVE_API_ALBUMART, albumArtGUID);
	ServiceRelease(WASABI_API_SVC, AGAVE_API_PLAYLISTS, api_playlistsGUID);
	ServiceRelease(WASABI_API_SVC, WASABI_API_LNG, languageApiGUID);
	ServiceRelease(WASABI_API_SVC, WASABI_API_EXPLORERFINDFILE, ExplorerFindFileApiGUID);
	ServiceRelease(WASABI_API_SVC, WASABI_API_SKIN, skinApiServiceGuid);
}

HWND WINAPI TASKBAR_CreateDialogParam(HINSTANCE original, LPCWSTR id, HWND parent, DLGPROC proc, LPARAM param)
{
	return WASABI_API_CREATEDIALOGPARAMW((int)id, parent, proc, param);
}

// callback so i can get some of the prefence dialog messages
void PrefDialogCallback(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, INT section_idx)
{
	if (msg == -1)
	{
		static int _wa_cur_page = -1;
		int wa_cur_page = 0;
		if (!wp)
		{
			LPARAM* cur_page = (LPARAM*)lp;
			*cur_page = wa_cur_page = Settings.LastTab;
			if (_wa_cur_page == -1)
			{
				_wa_cur_page = wa_cur_page;
			}
		}
		else if (wp == 1)
		{
			if (_wa_cur_page != -1)
			{
				wa_cur_page = lp;
				if ((wa_cur_page != _wa_cur_page) && no_uninstall)
				{
					Settings.LastTab = wa_cur_page;

					// save the settings only when changed
					SettingsManager SManager;
					SManager.WriteSettings(Settings);
				}
			}
		}
	}
}

void updateToolbar(HIMAGELIST ImageList)
{
	if ((itaskbar != NULL) && Settings.Thumbnailbuttons)
	{
		std::vector<THUMBBUTTON> thbButtons;
		for (size_t i = 0; i != TButtons.size(); ++i)
		{
			THUMBBUTTON button = {THB_BITMAP | THB_TOOLTIP, TButtons[i], 0, NULL, {0}, THBF_ENABLED};

			if (button.iId == TB_RATE || button.iId == TB_STOPAFTER ||
				button.iId == TB_DELETE || button.iId == TB_JTFE ||
				button.iId == TB_OPENEXPLORER)
			{
				button.dwMask = button.dwMask | THB_FLAGS;
				button.dwFlags = THBF_DISMISSONCLICK;
			}
			else if (button.iId == TB_PLAYPAUSE)
			{
				button.iBitmap = tools::getBitmap(button.iId, (Settings.play_state == PLAYSTATE_PLAYING ? 1 : 0));
				wcsncpy(button.szTip, tools::getToolTip(TB_PLAYPAUSE, Settings.play_state), ARRAYSIZE(button.szTip));
			} 
			else if (button.iId == TB_REPEAT)
			{
				button.iBitmap = tools::getBitmap(button.iId, Settings.state_repeat);
				wcsncpy(button.szTip, tools::getToolTip(TB_REPEAT, Settings.state_repeat), ARRAYSIZE(button.szTip));
			} 
			else if (button.iId == TB_SHUFFLE)
			{
				button.iBitmap = tools::getBitmap(button.iId, Settings.play_state == Settings.state_shuffle);
				wcsncpy(button.szTip, tools::getToolTip(TB_SHUFFLE, Settings.state_shuffle), ARRAYSIZE(button.szTip));
			}

			if (!button.iBitmap)
			{
				button.iBitmap = tools::getBitmap(button.iId, 0);
			}

			if (!button.szTip[0])
			{
				wcsncpy(button.szTip, tools::getToolTip(button.iId, 0), ARRAYSIZE(button.szTip));
			}

			thbButtons.push_back(button);
		}

		itaskbar->ThumbBarUpdateButtons(thbButtons, ImageList);
	}
}

BOOL CALLBACK checkSkinProc(HWND hwnd, LPARAM lParam)
{
	wchar_t cl[24] = {0};
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

void updateRepeatButton()
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

void SetThumbnailTimer()
{
	KillTimer(plugin.hwndParent, 6670);
	SetTimer(plugin.hwndParent, 6670, (Settings.Thumbnailbackground == BG_WINAMP) ?
			 (!Settings.LowFrameRate ? Settings.MFT : Settings.MST) :
			 (!Settings.LowFrameRate ? Settings.TFT : Settings.TST), TimerProc);
}

void ResetThumbnail()
{
	if (CreateThumbnailDrawer())
	{
		thumbnaildrawer->ClearBackground();
		thumbnaildrawer->ClearCustomBackground();
		thumbnaildrawer->ThumbnailPopup();
	}
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
						 UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	if ((message == WM_COMMAND || message == WM_SYSCOMMAND))
	{
		if (LOWORD(wParam) == WINAMP_OPTIONS_DSIZE)
		{
			doubleSize = !doubleSize;
		}
		else if (LOWORD(wParam) == WINAMP_FILE_SHUFFLE)
		{
			Settings.state_shuffle = !Settings.state_shuffle;
			updateToolbar();
		}
		else if (LOWORD(wParam) == ID_PE_MANUAL_ADVANCE)
		{
			pladv = !pladv;
			updateRepeatButton();
		}
		else if (LOWORD(wParam) == WINAMP_FILE_REPEAT)
		{
			repeat = !repeat;
			updateRepeatButton();
		}
	}

	switch (message)
	{
		case WM_DWMSENDICONICTHUMBNAIL:
		{
			if (CreateThumbnailDrawer())
			{
				// just update the dimensions and let the timer
				// process the rendering later on as is needed.
				thumbnaildrawer->SetDimensions(HIWORD(lParam), LOWORD(lParam));
				running = true;

				SetThumbnailTimer();
			}
			return 0;
		}
		case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
				case WINAMP_OPTIONS_WINDOWSHADE_GLOBAL:
				{
					if (hwnd != GetForegroundWindow())
					{
						break;
					}
				}
				case WINAMP_OPTIONS_WINDOWSHADE:
				{
					windowShade = !windowShade;
					break;
				}
			}

			if (HIWORD(wParam) == THBN_CLICKED)
			{
				switch (LOWORD(wParam))
				{
					case TB_PREVIOUS:
					case TB_NEXT:
					{
						SendMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(((LOWORD(wParam) == TB_PREVIOUS) ? 40044 : 40048), 0), 0);
						Settings.play_playlistpos = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETLISTPOS);

						if (Settings.Thumbnailbackground == BG_ALBUMART)
						{
							ResetThumbnail();

							if (Settings.play_state != PLAYSTATE_PLAYING)
							{
								DwmInvalidateIconicBitmaps(dialogParent);
							}
						}

						const int index = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETLISTPOS);
						LPCWSTR p = (LPCWSTR)SendMessage(plugin.hwndParent, WM_WA_IPC, index, IPC_GETPLAYLISTFILEW); 

						if (p != NULL)
						{
							metadata.reset(p);
						}

						if (CreateThumbnailDrawer())
						{
							thumbnaildrawer->ThumbnailPopup();
						}
						return 0;
					}
					case TB_PLAYPAUSE:
					{
						const int res = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_ISPLAYING);
						PostMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(((res == 1) ? 40046 : 40045), 0), 0);
						Settings.play_state = res;
						return 0;
					}
					case TB_STOP:
					{
						PostMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(40047, 0), 0);
						Settings.play_state = PLAYSTATE_NOTPLAYING; 
						return 0;
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
						ShowWindow(ratewnd, SW_SHOW);
						return 0;
					}
					case TB_VOLDOWN:
					{
						Settings.play_volume -= 25;
						if (Settings.play_volume < 0)
						{
							Settings.play_volume = 0;
						}
						PostMessage(plugin.hwndParent, WM_WA_IPC, Settings.play_volume, IPC_SETVOLUME);
						return 0;
					}
					case TB_VOLUP:
					{
						Settings.play_volume += 25;
						if (Settings.play_volume > 255)
						{
							Settings.play_volume = 255;
						}
						PostMessage(plugin.hwndParent, WM_WA_IPC, Settings.play_volume, IPC_SETVOLUME);
						return 0;
					}
					case TB_OPENFILE:
					{
						PostMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)(HWND)0, IPC_OPENFILEBOX);
						return 0;
					}
					case TB_MUTE:
					{
						static int lastvolume;
						if (Settings.play_volume == 0)
						{
							Settings.play_volume = lastvolume;
							PostMessage(plugin.hwndParent, WM_WA_IPC, Settings.play_volume, IPC_SETVOLUME);
						}
						else
						{
							lastvolume = Settings.play_volume;
							PostMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_SETVOLUME);
						}
						return 0;
					}
					case TB_STOPAFTER:
					{
						PostMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(40157, 0), 0);
						return 0;
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
						return 0;
					}
					case TB_SHUFFLE:
					{
						Settings.state_shuffle = !Settings.state_shuffle;
						SendMessage(plugin.hwndParent, WM_WA_IPC, Settings.state_shuffle, IPC_SET_SHUFFLE);
						updateToolbar();
						return 0;
					}
					case TB_JTFE:
					{
						ShowWindow(plugin.hwndParent, SW_SHOWNORMAL);
						SetForegroundWindow(plugin.hwndParent);
						PostMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(WINAMP_JUMPFILE, 0), 0);
						return 0;
					}
					case TB_OPENEXPLORER:
					{
						LPCWSTR filename = metadata.getFileName().c_str();
						if (PathFileExists(filename))
						{
							if (WASABI_API_EXPLORERFINDFILE == NULL)
							{
								ServiceBuild(WASABI_API_SVC, WASABI_API_EXPLORERFINDFILE, ExplorerFindFileApiGUID);
							}
							if (WASABI_API_EXPLORERFINDFILE != NULL)
							{
								WASABI_API_EXPLORERFINDFILE->AddFile(filename);
								WASABI_API_EXPLORERFINDFILE->ShowFiles();
							}
						}
						return 0;
					}
					case TB_DELETE:
					{
						SHFILEOPSTRUCTW fileop = {0};
						wchar_t path[MAX_PATH] = {0};
						lstrcpyn(path, metadata.getFileName().c_str(), ARRAYSIZE(path));

						fileop.wFunc = FO_DELETE;
						fileop.pFrom = path;
						fileop.pTo = L"";
						fileop.fFlags = FOF_ALLOWUNDO | FOF_FILESONLY;
						fileop.lpszProgressTitle = L"";

						const int saved_play_state = Settings.play_state;
						SendMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(40047, 0), 0);
						Settings.play_state = PLAYSTATE_NOTPLAYING; 

						if (SHFileOperation(&fileop) == 0)
						{
							SendMessage((HWND)SendMessage(plugin.hwndParent, WM_WA_IPC, IPC_GETWND_PE, IPC_GETWND), WM_WA_IPC,
											  IPC_PE_DELETEINDEX, SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETLISTPOS));
						}

						if (saved_play_state == PLAYSTATE_PLAYING)
						{
							PostMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(40045, 0), 0);
						}

						return 0;
					}
				}
			}
			break;
		}
		case WM_WA_IPC:
		{
			switch (lParam)
			{
				case IPC_PLAYING_FILEW:
				{
					if (wParam == 0)
					{
						return 0;
					}

					std::wstring filename(L"");
					try
					{
						filename = (wchar_t*)wParam;
					}
					catch (...)
					{
						return 0;
					}

					Settings.play_playlistpos = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETLISTPOS);

					if (filename.empty())
					{
						LPCWSTR p = (LPCWSTR)SendMessage(plugin.hwndParent, WM_WA_IPC, Settings.play_playlistpos, IPC_GETPLAYLISTFILEW); 
						if (p != NULL)
						{
							filename = p;
						}
					}

					metadata.reset(filename);
					if (metadata.CheckPlayCount())
					{
						JumpList *JL = new JumpList(AppID);
						if (JL != NULL)
						{
							delete JL;
						}
					}

					Settings.play_total = SendMessage(plugin.hwndParent, WM_WA_IPC, 2, IPC_GETOUTPUTTIME);
					Settings.play_current = 0;
					Settings.play_state = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_ISPLAYING);

					if ((Settings.JLrecent || Settings.JLfrequent) && !tools::is_in_recent(filename))
					{
						std::wstring title(metadata.getMetadata(L"title") + L" - " + metadata.getMetadata(L"artist"));

						if (Settings.play_total > 0)
						{
							title += L"  (" + tools::SecToTime(Settings.play_total / 1000) + L")";
						}

						IShellLink *psl = NULL;
						SHARDAPPIDINFOLINK applink = {0};
						if ((tools::CreateShellLink(filename.c_str(), title.c_str(), &psl) == S_OK) &&
							(Settings.play_state == PLAYSTATE_PLAYING) && Settings.Add2RecentDocs && psl)
						{
							time_t rawtime = NULL;
							time (&rawtime);
							psl->SetDescription(_wctime(&rawtime));
							applink.psl = psl;
							applink.pszAppID = AppID.c_str();
							SHAddToRecentDocs(SHARD_LINK, psl);
							psl->Release();
						}
					}

					DwmInvalidateIconicBitmaps(dialogParent);
					ResetThumbnail();
					break;
				}
				case IPC_CB_MISC:
				{
					switch (wParam)
					{
						case IPC_CB_MISC_STATUS:
						{
							Settings.play_state = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_ISPLAYING);

							updateToolbar();

							if (Settings.Overlay)
							{
								wchar_t tmp[64] = {0};
								HICON icon = NULL;
								switch (Settings.play_state)
								{
									case PLAYSTATE_PLAYING:
									{
										if (itaskbar != NULL)
										{
											icon = ImageList_GetIcon(theicons, tools::getBitmap(TB_PLAYPAUSE, 0), 0);
											itaskbar->SetIconOverlay(icon, WASABI_API_LNGSTRINGW_BUF(IDS_PLAYING, tmp, 64));
										}
										break;
									}
									case PLAYSTATE_PAUSED:
									{
										if (itaskbar != NULL)
										{
											icon = ImageList_GetIcon(theicons, tools::getBitmap(TB_PLAYPAUSE, 1), 0);
											itaskbar->SetIconOverlay(icon, WASABI_API_LNGSTRINGW_BUF(IDS_PAUSED, tmp, 64));
										}
										break;
									}
									default:
									{
										if (itaskbar != NULL)
										{
											icon = ImageList_GetIcon(theicons, tools::getBitmap(TB_STOP, 1), 0);
											itaskbar->SetIconOverlay(icon, WASABI_API_LNGSTRINGW_BUF(IDS_PAUSED, tmp, 64));
										}
										break;
									}
								}

								if (icon != NULL)
								{
									DestroyIcon(icon);
								}
							}
							break;
						}
						case IPC_CB_MISC_VOLUME:
						{
							Settings.play_volume = IPC_GETVOLUME(plugin.hwndParent);
							break;
						}
					}
					break;
				}
				case IPC_SETDIALOGBOXPARENT:
				case IPC_UPDATEDIALOGBOXPARENT:
				{
					DwmInvalidateIconicBitmaps(dialogParent);

					// we cache this now as winamp will have
					// cached it too by now so we're in-sync
					dialogParent = (HWND)wParam;
					if (!IsWindow(dialogParent))
					{
						dialogParent = hwnd;
					}
					break;
				}
				default:
				{
					if (lParam == delay_ipc)
					{
						// we track this instead of re-querying all of the
						// time so as to minimise blocking of the main wnd
						dialogParent = (HWND)SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)0, IPC_GETDIALOGBOXPARENT);
						if (!IsWindow(dialogParent))
						{
							dialogParent = hwnd;
						}

						pladv = !!SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GET_MANUALPLADVANCE);

						windowShade = !!SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)-1, IPC_IS_WNDSHADE);

						doubleSize = !!SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)0, IPC_ISDOUBLESIZE);

						// Accept messages even if Winamp was run as Administrator
						ChangeWindowMessageFilter(WM_COMMAND, 1);
						ChangeWindowMessageFilter(WM_DWMSENDICONICTHUMBNAIL, 1);
						ChangeWindowMessageFilter(WM_DWMSENDICONICLIVEPREVIEWBITMAP, 1);

						// Register taskbarcreated message
						WM_TASKBARBUTTONCREATED = RegisterWindowMessage(L"TaskbarButtonCreated");

						// we do this to make sure we can group things correctly
						// especially if used in a plug-in in Winamp so that the
						// taskbar handling will be correct vs existing pinnings
						SetupAppID();

						wchar_t ini_path[MAX_PATH] = {0};
						PathCombine(ini_path, get_paths()->settings_dir, L"Plugins\\win7shell.ini");
						SettingsFile = ini_path;

						// Read Settings into struct
						SettingsManager SManager;
						SManager.ReadSettings(Settings, TButtons);

						// Create jumplist
						SetupJumpList();

						// Timers, settings, icons
						Settings.play_playlistpos = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETLISTPOS);
						Settings.play_playlistlen = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETLISTLENGTH);
						Settings.play_total = SendMessage(plugin.hwndParent, WM_WA_IPC, 2, IPC_GETOUTPUTTIME);
						Settings.play_volume = IPC_GETVOLUME(plugin.hwndParent);

						theicons = tools::prepareIcons();

						LPCWSTR p = (LPCWSTR)SendMessage(plugin.hwndParent, WM_WA_IPC, Settings.play_playlistpos, IPC_GETPLAYLISTFILEW); 
						if (p != NULL)
						{
							metadata.reset(p);
						}

						// update shuffle and repeat
						Settings.state_shuffle = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GET_SHUFFLE);

						Settings.state_repeat = repeat = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GET_REPEAT);
						if (Settings.state_repeat == 1 && pladv == 1)
						{
							Settings.state_repeat = 2;
						}

						// Create the taskbar interface
						itaskbar = new iTaskBar();
						if ((itaskbar != NULL) && itaskbar->Reset())
						{
							updateToolbar(theicons);
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
											 BuildPluginNameW(), MB_ICONWARNING, 0);
							}
						}
#endif

						CreateThumbnailDrawer();

						if (Settings.VuMeter)
						{
							SetTimer(plugin.hwndParent, 6668, 66, TimerProc);
						}

						SetTimer(plugin.hwndParent, 6667, Settings.LowFrameRate ? 400 : 100, TimerProc);
					}
					break;
				}
			}
			break;
		}
		case WM_SYSCOMMAND:
		{
			if (wParam == SC_CLOSE)
			{
				PostMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(40001, 0), 0);
			}
			break;
		}
	}

	if (message == WM_TASKBARBUTTONCREATED)
	{
		if ((itaskbar != NULL) && itaskbar->Reset())
		{
			updateToolbar(theicons);
		}

		SetupJumpList();
	}

	LRESULT ret = DefSubclassProc(hwnd, message, wParam, lParam);	 

	if (message == WM_SIZE)
	{
		// look at things that could need us to
		// force a refresh of the iconic bitmap
		// this is mainly for classic skins...
		SetThumbnailTimer();
	}
	else if (message == WM_WA_IPC)
	{
		switch (lParam)
		{
			case IPC_PLAYLIST_MODIFIED:
			{
				Settings.play_playlistlen = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETLISTLENGTH);
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
				// delay doing this until needed as it then
				// copes with modern skins being later used
				if (!WASABI_API_SKIN)
				{
					ServiceBuild(WASABI_API_SVC, WASABI_API_SKIN, skinApiServiceGuid);
				}

				classicSkin = (!WASABI_API_SKIN || WASABI_API_SKIN &&
							   // TODO pull in the localised version from gen_ff
							   //		to ensure the checking will work correctly
							   !_wcsnicmp(WASABI_API_SKIN->getSkinName(), L"No skin loaded", 14));

				// this is needed when the vu mode is enabled to allow the
				// data to be obtained if the main wnidow mode is disabled
				static void (*export_sa_setreq)(int) =
					   (void (__cdecl *)(int))SendMessage(plugin.hwndParent, WM_WA_IPC, 1, IPC_GETSADATAFUNC);
				if (export_sa_setreq)
				{
					export_sa_setreq(Settings.VuMeter);
				}

				// fall-through for the other handling needed
			}
			case IPC_SETDIALOGBOXPARENT:
			case IPC_UPDATEDIALOGBOXPARENT:
			{
				// look at things that could need us to
				// force a refresh of the iconic bitmap
				SetThumbnailTimer();

				modernSUI = false;
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
				break;
			}
			default:
			{
				// make sure if not playing but prev / next is done
				// that we update the thumbnail for the current one
				if ((lParam == IPC_FILE_TAG_MAY_HAVE_UPDATED) || (lParam == IPC_FILE_TAG_MAY_HAVE_UPDATEDW) ||
					(lParam == IPC_CB_MISC) && ((wParam == IPC_CB_MISC_TITLE) ||
					(wParam == IPC_CB_MISC_AA_OPT_CHANGED) || (wParam == IPC_CB_MISC_TITLE_RATING)))
				{

					const int index = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETLISTPOS);
					LPCWSTR p = (LPCWSTR)SendMessage(plugin.hwndParent, WM_WA_IPC, index, IPC_GETPLAYLISTFILEW); 

					if (p != NULL)
					{
						metadata.reset(p);
					}

					DwmInvalidateIconicBitmaps(dialogParent);
					ResetThumbnail();
					break;
				}
			}
		}
	}

	return ret;
}

void CheckThumbShowing()
{
#if 0
	if (thumbshowing)
	{
		POINT pt = {0};
		GetCursorPos(&pt);
		wchar_t class_name[24] = {0};
		GetClassName(WindowFromPoint(pt), class_name, ARRAYSIZE(class_name));

		if (lstrcmpi(class_name, L"MultitaskingViewFrame") &&
			lstrcmpi(class_name, L"TaskListThumbnailWnd") &&
			lstrcmpi(class_name, L"MSTaskListWClass") &&
			lstrcmpi(class_name, L"ToolbarWindow32"))
		{
			thumbshowing = false;
			//KillTimer(plugin.hwndParent, 6670);
		}

		//DwmInvalidateIconicBitmaps(dialogParent);
	}
#endif
}

VOID CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	switch (idEvent)
	{
		case 6667:	// main timer
		{
			CheckThumbShowing();

			if (!(Settings.Progressbar || Settings.VuMeter) && (itaskbar != NULL))
			{
				itaskbar->SetProgressState(TBPF_NOPROGRESS);
			}

			if (Settings.play_state == PLAYSTATE_PLAYING || Settings.Thumbnailpb)
			{
				Settings.play_total = SendMessage(plugin.hwndParent, WM_WA_IPC, 2, IPC_GETOUTPUTTIME);
			}

			if (Settings.play_state != PLAYSTATE_NOTPLAYING)
			{
				Settings.play_kbps = SendMessage(plugin.hwndParent, WM_WA_IPC, 1, IPC_GETINFO);
				Settings.play_khz = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETINFO);
			}
			else
			{
				Settings.play_kbps = Settings.play_khz = 0;
			}

			const int cp = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETOUTPUTTIME);
			if (Settings.play_current == cp)
			{
				return;
			}

			Settings.play_current = cp;

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
					   (int(__cdecl *)(int))SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETVUDATAFUNC);
				static char * (*export_sa_get)(char data[75*2+8]) =
					   (char * (__cdecl *)(char data[75*2+8]))SendMessage(plugin.hwndParent, WM_WA_IPC, 2, IPC_GETSADATAFUNC);
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
			if (CreateThumbnailDrawer() && running)
			{
				thumbnaildrawer->ClearBackground();
				HBITMAP thumbnail = thumbnaildrawer->GetThumbnail();
				if (thumbnail != NULL)
				{
					HRESULT hr = DwmSetIconicThumbnail(plugin.hwndParent, thumbnail, 0);

					DeleteObject(thumbnail);
					thumbnail = NULL;

					if (FAILED(hr))
					{
						KillTimer(plugin.hwndParent, 6670);
						break;
					}

					if (classicSkin)
					{
						RECT r = {0};
						GetClientRect(plugin.hwndParent, &r);

						Gdiplus::Bitmap bmp((r.right - r.left), (r.bottom - r.top), PixelFormat32bppPARGB);
						Gdiplus::Graphics gfx(&bmp);

						HDC hdc = gfx.GetHDC();
						SendMessage(plugin.hwndParent, WM_PRINTCLIENT, (WPARAM)hdc,
									PRF_CHILDREN | PRF_CLIENT | PRF_NONCLIENT);
						gfx.ReleaseHDC(hdc);

						bmp.GetHBITMAP(NULL, &thumbnail);
						if (thumbnail)
						{
							DwmSetIconicLivePreviewBitmap(plugin.hwndParent, thumbnail, NULL, 0);
							DeleteObject(thumbnail);
						}
					}
				}
			}

			CheckThumbShowing();
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
			switch (LOWORD(wParam))
			{
				case IDC_RATE1:
				{
					PostMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_SETRATING);
					DestroyWindow(hwndDlg);
					break;
				}
				case IDC_RATE2:
				{
					PostMessage(plugin.hwndParent, WM_WA_IPC, 1, IPC_SETRATING);
					DestroyWindow(hwndDlg);
					break;
				}
				case IDC_RATE3:
				{
					PostMessage(plugin.hwndParent, WM_WA_IPC, 2, IPC_SETRATING);
					DestroyWindow(hwndDlg);
					break;
				}
				case IDC_RATE4:
				{
					PostMessage(plugin.hwndParent, WM_WA_IPC, 3, IPC_SETRATING);
					DestroyWindow(hwndDlg);
					break;
				}
				case IDC_RATE5:
				{
					PostMessage(plugin.hwndParent, WM_WA_IPC, 4, IPC_SETRATING);
					DestroyWindow(hwndDlg);
					break;
				}
				case IDC_RATE6:
				{
					PostMessage(plugin.hwndParent, WM_WA_IPC, 5, IPC_SETRATING);
					DestroyWindow(hwndDlg);
					break;
				}
			}
			break;
		}
	}

	return 0;
}

HWND PrefsHWND()
{
	return (HWND)SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETPREFSWND);
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
			EnableWindow(GetDlgItem(hwnd, 1267), enabled);

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
					EnableWindow(GetDlgItem(hwnd, 1267), UpdateTaskbarMode(enabled));
					EnableWindow(GetDlgItem(hwnd, IDC_ICON_COMBO), enabled);
					break;
				}
				case IDC_CLEARALL:
				{
					wchar_t filepath[MAX_PATH] = {0};
					SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, filepath);
					PathAppend(filepath, L"\\Microsoft\\Windows\\Recent\\AutomaticDestinations"
										 L"\\879d567ffa1f5b9f.automaticDestinations-ms");

					if (DeleteFile(filepath) != 0)
					{
						EnableWindow(GetDlgItem(hwnd, IDC_CLEARALL), false);
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

					if (Settings.JLbms || Settings.JLpl || Settings.JLtasks ||
						Settings.JLfrequent || Settings.JLrecent)
					{
						SetupJumpList();
					}
					else
					{
						Settings.JLrecent = true;
						Button_SetCheck(GetDlgItem(hwnd, IDC_CHECK30), BST_CHECKED);
						SetupJumpList();
					}
					break;
				}
				case IDC_CHECK3:
				{
					Settings.Overlay = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK3)) == BST_CHECKED);
					if (Settings.Overlay)
					{
						SendMessage(plugin.hwndParent, WM_WA_IPC, IPC_CB_MISC_STATUS, IPC_CB_MISC);
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

					EnableWindow(GetDlgItem(hwnd, IDC_CHECK4), Settings.Progressbar);
					EnableWindow(GetDlgItem(hwnd, IDC_CHECK5), Settings.Progressbar);

					if (Settings.Progressbar)
					{
						SendMessage(GetDlgItem(hwnd, IDC_CHECK26), (UINT) BM_SETCHECK, 0, 0);	
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
						SendMessage(GetDlgItem(hwnd, IDC_CHECK2), (UINT) BM_SETCHECK, 0, 0);	
						EnableWindow(GetDlgItem(hwnd, IDC_CHECK4), 0);
						EnableWindow(GetDlgItem(hwnd, IDC_CHECK5), 0);
						Settings.Progressbar = false;
					}

					KillTimer(plugin.hwndParent, 6668);
					if (Settings.VuMeter)
					{
						SetTimer(plugin.hwndParent, 6668, 66, TimerProc);
					}
					break;
				}
				case 1267:
				{
					// TODO localise this
					wchar_t temp[256] = {0};
					if (MessageBox(hwnd, WASABI_API_LNGSTRINGW_BUF(IDS_DISABLE_SUPPORT_TEXT, temp, ARRAYSIZE(temp)),
								   WASABI_API_LNGSTRINGW(IDS_DISABLE_SUPPORT), MB_YESNO | MB_ICONQUESTION) == IDYES)
					{
						WritePrivateProfileStringW(L"plugins", L"gen_win7shell.dll", L"1",
												   get_paths()->profile_ini_file);
						PostMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)-1, IPC_RESTARTWINAMP);
					}
					break;
				}
				case IDC_ICON_COMBO:
				{
					if (HIWORD(wParam) == CBN_SELCHANGE)
					{
						UpdateTaskberIcon(hwnd);
					}
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
											 BuildPluginNameW(), MB_ICONWARNING, 0);
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
		EnableWindow(GetDlgItem(hwnd, ids[i]), Settings.Thumbnailbuttons);
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
				SendMessage(GetDlgItem(hwnd, i), BM_SETCHECK, BST_UNCHECKED, NULL);
			}

			SettingsManager::WriteSettings_ToForm(hwnd, plugin.hwndParent, Settings);

			// disable the 'text' section if needed
			if (Settings.Thumbnailbackground == BG_WINAMP)
			{
				const int ids[] = {IDC_TEXT_GROUP, IDC_EDIT3, IDC_BUTTON5,
								   IDC_BUTTON9, IDC_DEFAULT, IDC_BUTTON6,
								   IDC_CHECK8, IDC_CHECK1, IDC_CHECK29};
				for (int i = 0; i < ARRAYSIZE(ids); i++)
				{
					EnableWindow(GetDlgItem(hwnd, ids[i]), FALSE);
				}

				DestroyWindow(GetDlgItem(hwnd, IDC_BUTTON_HELP));
				SetDlgItemText(hwnd, IDC_TEXT_GROUP, WASABI_API_LNGSTRINGW(IDS_TEXT_DISABLED));
			}

			const HWND list = GetDlgItem(hwnd, IDC_LIST1);
			for (size_t i = 0; i < TButtons.size(); ++i)
			{
				const int index = SendMessage(list, LB_ADDSTRING, NULL, (LPARAM)tools::getToolTip(TButtons[i], -1));
				SendMessage(list, LB_SETITEMDATA, index, TButtons[i]);
				SendMessage(GetDlgItem(hwnd, TButtons[i]), BM_SETCHECK, BST_CHECKED, NULL);
			}
			
			// Set button icons
			for (int i = 0; i < NR_BUTTONS; i++)
			{
				HICON icon = ImageList_GetIcon(theicons, tools::getBitmap(TB_PREVIOUS+i, i == 10 ? 1 : 0), 0);
				SendMessage(GetDlgItem(hwnd, IDC_PCB1+i), BM_SETIMAGE, IMAGE_ICON, (LPARAM)icon);
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
				SetWindowText(GetDlgItem(hwnd, IDC_STATIC29), WASABI_API_LNGSTRINGW(IDS_MAX_BUTTONS));
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
							SetWindowText(GetDlgItem(hwnd, IDC_STATIC29), tools::getToolTip(wParam));
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
					int index = SendMessage(list, LB_GETCURSEL, NULL, NULL);
					if (index == 0 || index == -1)
					{
						break;
					}

					const int data = SendMessage(list, LB_GETITEMDATA, index, NULL);
					SendMessage(list, LB_DELETESTRING, index, NULL);
					index = SendMessage(list, LB_INSERTSTRING, index - 1, (LPARAM)tools::getToolTip(data));
					SendMessage(list, LB_SETITEMDATA, index, data);
					SendMessage(list, LB_SETCURSEL, index, NULL);

					// Populate buttons structure
					TButtons.clear();
					for (int i = 0; i != ListBox_GetCount(list); ++i)
					{
						TButtons.push_back(ListBox_GetItemData(list, i));
					}

					// Show note
					ShowWindow(GetDlgItem(hwnd, IDC_BUTTON_RESTART), SW_SHOW);
					break;
				}
				case IDC_DOWNBUTT:
				{
					const HWND list = GetDlgItem(hwnd, IDC_LIST1);
					int index = SendMessage(list, LB_GETCURSEL, NULL, NULL);
					if (index == ListBox_GetCount(list)-1 || index == -1)
					{
						return 0;
					}

					const int data = SendMessage(list, LB_GETITEMDATA, index, NULL);
					SendMessage(list, LB_DELETESTRING, index, NULL);
					index = SendMessage(list, LB_INSERTSTRING, index+1, (LPARAM)tools::getToolTip(data));
					SendMessage(list, LB_SETITEMDATA, index, data);
					SendMessage(list, LB_SETCURSEL, index, NULL);

					// Populate buttons structure
					TButtons.clear();
					for (int i = 0; i != ListBox_GetCount(list); ++i)
					{
						TButtons.push_back(ListBox_GetItemData(list, i));
					}

					// Show note
					ShowWindow(GetDlgItem(hwnd, IDC_BUTTON_RESTART), SW_SHOW);
					break;
				}
				case IDC_CHECK6:
				{
					Settings.Thumbnailbuttons = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK6)) == BST_CHECKED);
					EnableWindow(GetDlgItem(hwnd, IDC_CHECK27), SendMessage(GetDlgItem(hwnd, IDC_CHECK6), (UINT) BM_GETCHECK, 0, 0));
					UpdateContolButtons(hwnd);

					if (Settings.Thumbnailbuttons)
					{
						if ((itaskbar != NULL) && itaskbar->Reset())
						{
							updateToolbar(theicons);
						}
					}
					else
					{
						// Show note
						ShowWindow(GetDlgItem(hwnd, IDC_BUTTON_RESTART), SW_SHOW);
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
						wcsncpy(Settings.Text, text.c_str(), ARRAYSIZE(Settings.Text));

						ResetThumbnail();
					}
					break;
				}
				case IDC_BUTTON_HELP:
				{
					DWORD data_size = 0;
					unsigned char *data = (unsigned char *)WASABI_API_LOADRESFROMFILEW(L"GZ", MAKEINTRESOURCEW(IDR_HELP_GZ), &data_size),
								  *output = NULL;

					decompress_resource(data, data_size, &output, 0);
					MessageBoxEx(PrefsHWND(), AutoWide((LPCSTR)output, CP_UTF8),
								 WASABI_API_LNGSTRINGW(IDS_INFORMATION), MB_ICONINFORMATION, 0);

					if (output)
					{
						free(output);
					}
					break;
				}
				case IDC_DEFAULT:
				{
					SetWindowText(GetDlgItem(hwnd, IDC_EDIT3), L"%c%%s%%curpl% of %totalpl%.\r\n"
								  L"%c%%s%%title%\r\n%c%%s%%artist%\r\n\r\n%c%%s%%curtime%/"
								  L"%totaltime%\r\n%c%%s%Track #: %track%        Volume: %volume%%");
					PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_EDIT3, EN_CHANGE), NULL);
					break;
				}
				case IDC_BUTTON5:
				{
					CHOOSEFONT cf = {0};
					cf.lStructSize = sizeof(cf);
					cf.hwndOwner = PrefsHWND();
					cf.rgbColors = Settings.text_color;
					cf.lpLogFont = &Settings.font;
					cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT |
							   // added these styles to prevent fonts being listed
							   // which appear to have no effect when used (will
							   // need to investigate further but this is a decent
							   // safety blanket on things for the time being - dro)
							   CF_SCALABLEONLY | CF_NOOEMFONTS | CF_TTONLY;

					if (ChooseFont(&cf))
					{
						Settings.font = *cf.lpLogFont;
					}
					break;
				}
				case IDC_BUTTON6:
				case IDC_BUTTON9:
				{
					const bool text = (LOWORD(wParam) == IDC_BUTTON9);
					CHOOSECOLOR cc = {0};			// common dialog box structure
					cc.lStructSize = sizeof(cc);
					cc.hwndOwner = PrefsHWND();
					cc.lpCustColors = (LPDWORD)acrCustClr;
					cc.rgbResult = (!text ? Settings.bgcolor : Settings.text_color);
					cc.Flags = CC_FULLOPEN | CC_RGBINIT;

					if (ChooseColor(&cc) == TRUE) 
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

	ShowWindow(hwnd, (lParam ? SW_SHOW : SW_HIDE));
	return TRUE;
}

LRESULT CALLBACK TabHandler_Thumbnail(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
		case WM_INITDIALOG:
		{
			SettingsManager::WriteSettings_ToForm(hwnd, plugin.hwndParent, Settings);
			SendMessage(GetDlgItem(hwnd, IDC_EDIT2), EM_SETREADONLY, TRUE, NULL);
			SendMessage(GetDlgItem(hwnd, IDC_SLIDER1), TBM_SETRANGE, FALSE, MAKELPARAM(30, 100));
			SendMessage(GetDlgItem(hwnd, IDC_SLIDER_TRANSPARENCY), TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));

			switch (Settings.IconPosition)
			{
				case IP_UPPERLEFT:
				{
					SetWindowText(GetDlgItem(hwnd, IDC_ICONPOS), WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_TL));
					break;
				}
				case IP_UPPERRIGHT:
				{
					SetWindowText(GetDlgItem(hwnd, IDC_ICONPOS), WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_TR));
					break;
				}
				case IP_LOWERLEFT:
				{
					SetWindowText(GetDlgItem(hwnd, IDC_ICONPOS), WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_BL));
					break;
				}
				case IP_LOWERRIGHT:
				{
					SetWindowText(GetDlgItem(hwnd, IDC_ICONPOS), WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_BR));
					break;
				}
			}

			EnumChildWindows(hwnd, EnumDialogControls, (Settings.Thumbnailbackground != BG_WINAMP));
			break;
		}
		case WM_HSCROLL:
		{
			DWORD slider = GetDlgCtrlID((HWND)lParam);
			if (slider == IDC_SLIDER1)
			{
				wchar_t text[64] = {0};
				Settings.IconSize = SendMessage((HWND)lParam, TBM_GETPOS, NULL, NULL);
				StringCchPrintf(text, ARRAYSIZE(text), WASABI_API_LNGSTRINGW(IDS_ICON_SIZE), Settings.IconSize);
				SetWindowTextW(GetDlgItem(hwnd, IDC_ICONSIZE), text);
				ResetThumbnail();
			}
			else if (slider == IDC_SLIDER_TRANSPARENCY)
			{
				wchar_t text[64] = {0};
				Settings.BG_Transparency = SendMessage((HWND)lParam, TBM_GETPOS, NULL, NULL);
				StringCchPrintf(text, ARRAYSIZE(text), L"%d%%", Settings.BG_Transparency);
				SetWindowTextW(GetDlgItem(hwnd, IDC_TRANSPARENCY_PERCENT), text);
				ResetThumbnail();
			}

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
					if (SendMessage(GetDlgItem(hwnd, IDC_RADIO9), (UINT) BM_GETCHECK, 0 , 0))
					{
						Settings.Thumbnailbackground = BG_WINAMP;
					}
					else if (SendMessage(GetDlgItem(hwnd, IDC_RADIO1), (UINT) BM_GETCHECK, 0 , 0))
					{
						Settings.Thumbnailbackground = BG_TRANSPARENT;
					}
					else if (SendMessage(GetDlgItem(hwnd, IDC_RADIO2), (UINT) BM_GETCHECK, 0 , 0))
					{
						Settings.Thumbnailbackground = BG_ALBUMART;
					}
					else if (SendMessage(GetDlgItem(hwnd, IDC_RADIO3), (UINT) BM_GETCHECK, 0 , 0))
					{
						Settings.Thumbnailbackground = BG_CUSTOM;
						if (GetWindowTextLength(GetDlgItem(hwnd, IDC_EDIT2)) == 0)
						{
							PostMessage(GetDlgItem(hwnd, IDC_BUTTON3), BM_CLICK, 0, 0);
						}
					}

					EnumChildWindows(hwnd, EnumDialogControls, (Settings.Thumbnailbackground != BG_WINAMP));

					if ((itaskbar != NULL) && itaskbar->Reset())
					{
						updateToolbar(theicons);
					}

					SetThumbnailTimer();
					DwmInvalidateIconicBitmaps(dialogParent);
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
						hr = pfd->Show(PrefsHWND());

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
								lstrcpyn(filename, w, ARRAYSIZE(filename));
								CoTaskMemFree(w);
							}
						} 
						pfd->Release();
					}

					if (filename[0])
					{
						SetWindowText(GetDlgItem(hwnd, IDC_EDIT2), filename);
						ResetThumbnail();
					}
					else if (!Settings.BGPath[0])
					{
						SendMessage(GetDlgItem(hwnd, IDC_RADIO2), (UINT)BM_SETCHECK, BST_CHECKED, 0);
						SendMessage(GetDlgItem(hwnd, IDC_RADIO3), (UINT)BM_SETCHECK, BST_UNCHECKED, 0);
					}
					break;
				}
				case IDC_COMBO1:
				{
					Settings.Revertto = SendMessage(GetDlgItem(hwnd, IDC_COMBO1), CB_GETCURSEL, 0, 0);
					ResetThumbnail();
					break;
				}
				case IDC_CHECK25:
				{
					Settings.AsIcon = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHECK25)) == BST_CHECKED);
					ResetThumbnail();
					break;
				}
				case IDC_RADIO4:
				case IDC_RADIO6:
				case IDC_RADIO7:
				case IDC_RADIO8:
				{
					if (SendMessage(GetDlgItem(hwnd, IDC_RADIO4), (UINT)BM_GETCHECK, 0 , 0))
					{
						SetWindowText(GetDlgItem(hwnd, IDC_ICONPOS), WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_TL));
						Settings.IconPosition = IP_UPPERLEFT;
					}
					else if (SendMessage(GetDlgItem(hwnd, IDC_RADIO7), (UINT)BM_GETCHECK, 0 , 0))
					{
						SetWindowText(GetDlgItem(hwnd, IDC_ICONPOS), WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_BL));
						Settings.IconPosition = IP_LOWERLEFT;
					}
					else if (SendMessage(GetDlgItem(hwnd, IDC_RADIO6), (UINT)BM_GETCHECK, 0 , 0))
					{
						SetWindowText(GetDlgItem(hwnd, IDC_ICONPOS), WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_TR));
						Settings.IconPosition = IP_UPPERRIGHT;
					}
					else if (SendMessage(GetDlgItem(hwnd, IDC_RADIO8), (UINT)BM_GETCHECK, 0 , 0))
					{
						SetWindowText(GetDlgItem(hwnd, IDC_ICONPOS), WASABI_API_LNGSTRINGW(IDS_ICON_POSITION_BR));
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
			const int index = SendMessage(list, LB_FINDSTRINGEXACT, (WPARAM)-1,
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
			const int index = SendMessage(list, LB_ADDSTRING, NULL, (LPARAM)tools::getToolTip(control_ID));
			SendMessage(list, LB_SETITEMDATA, index, control_ID);
		}
		else
		{
			if (ListBox_GetCount(list) >= 7)
			{
				Button_SetCheck(button, BST_UNCHECKED);
				SetWindowText(GetDlgItem(window, IDC_STATIC29), WASABI_API_LNGSTRINGW(IDS_MAX_BUTTONS));
			}
			else
			{
				LPCWSTR tooltip = tools::getToolTip(control_ID);
				if (SendMessage(list, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)tooltip) == LB_ERR) //no duplicate
				{
					const int index = SendMessage(list, LB_ADDSTRING, NULL, (LPARAM)tooltip);
					ListBox_SetItemData(list, index, control_ID);
				}
			}
		}
	}

	// Populate buttons structure
	TButtons.clear();
	for (int i = 0; i != ListBox_GetCount(list); ++i)
	{
		TButtons.push_back(ListBox_GetItemData(list, i));
	}

	// Show note
	ShowWindow(GetDlgItem(window, IDC_BUTTON_RESTART), SW_SHOW);
}

void SetupJumpList()
{
	JumpList *jl = new JumpList(AppID, true);
	if ((jl != NULL) && (Settings.JLbms || Settings.JLfrequent ||
		Settings.JLpl || Settings.JLrecent || Settings.JLtasks))
	{
		static wchar_t pluginPath[MAX_PATH] = {0}, tmp1[128],
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

		jl->CreateJumpList(pluginPath, tmp1, tmp2, tmp3, tmp4, Settings.JLrecent,
						   Settings.JLfrequent, Settings.JLtasks, Settings.JLbms,
						   Settings.JLpl, tools::getBookmarks());
		delete jl;
	}
}

#ifdef USE_MOUSE
LRESULT CALLBACK KeyboardEvent(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (!thumbshowing || wParam != WM_MOUSEWHEEL)
	{
		return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
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

	return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
}
#endif

// This is an export function called by winamp which returns this plugin info.
extern "C" __declspec(dllexport) winampGeneralPurposePlugin * winampGetGeneralPurposePlugin()
{
	return &plugin;
}

extern "C" __declspec(dllexport) int winampUninstallPlugin(HINSTANCE hDllInst, HWND hwndDlg, int param)
{
	if (MessageBoxEx(hwndDlg, WASABI_API_LNGSTRINGW(IDS_UNINSTALL_PROMPT),
					 BuildPluginNameW(), MB_YESNO, 0) == IDYES)
	{
		no_uninstall = false;

		wchar_t ini_path[MAX_PATH] = {0};
		PathCombine(ini_path, get_paths()->settings_dir, L"Plugins\\win7shell.ini");
		if (PathFileExists(ini_path))
		{
			DeleteFile(ini_path);
		}

		JumpList *jl = new JumpList(AppID, true);
		if (jl != NULL)
		{
			delete jl;
		}
	}

	return GEN_PLUGIN_UNINSTALL_REBOOT;
}