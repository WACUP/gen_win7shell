#include "gen_win7shell.h"
#include <strsafe.h>
#include "resource.h"
#include "tools.h"
#include "api.h"
#include <loader/loader/paths.h>
#include <loader/loader/utils.h>

namespace tools
{
	LPCWSTR getToolTip(const int button, const int mode)
	{
		int strID = -1;

		switch (button)
		{
			case TB_PREVIOUS:
			{
				strID = IDS_PREVIOUS;
				break;
			}
			case TB_PLAYPAUSE:
			{
				// mode 1 = playing, mode 0,3 = paused, -1 = button name
				if (mode == -1)
				{
					strID = IDS_PLAYPAUSE;
				}
				else if (mode == 1)
				{
					strID = IDS_PAUSE;
				}
				else
				{
					strID = IDS_PLAY;
				}
				break;
			}
			case TB_STOP:
			{
				strID = IDS_STOP;
				break;
			}
			case TB_NEXT:
			{
				strID = IDS_NEXT;
				break;
			}
			case TB_RATE:
			{
				strID = IDS_RATE;
				break;
			}
			case TB_VOLDOWN:
			{
				strID = IDS_VOLUME_DOWN;
				break;
			}
			case TB_VOLUP:
			{
				strID = IDS_VOLUME_UP;
				break;
			}
			case TB_OPENFILE:
			{
				strID = IDS_OPEN_FILE;
				break;
			}
			case TB_MUTE:
			{
				strID = IDS_MUTE;
				break;
			}
			case TB_STOPAFTER:
			{
				strID = IDS_STOP_AFTER_CURRENT;
				break;
			}
			case TB_REPEAT:
			{
				if (mode == -1) // button name
				{
					strID = IDS_REPEAT;
				}
				else if (mode == 0) // repeat off, toggle repeat all on
				{
					strID = IDS_REPEAT_ON_ALL;
				}
				else if (mode == 1) // repeat all on, toggle repeat current
				{
					strID = IDS_REPEAT_ON_CURRENT;
				}
				else // repeat current on, toggle repeat off
				{
					strID = IDS_REPEAT_OFF;
				}
				break;
			}
			case TB_SHUFFLE:
			{
				// mode 0 = shuffle is off, mode 1 = shuffle is on
				if (mode == -1) // button name
				{
					strID = IDS_SHUFFLE;
				}
				else if (mode == 0) // shuffle is off, toggle on
				{
					strID = IDS_SHUFFLE_ON;
				}
				else // shuffle is on, toggle off
				{
					strID = IDS_SHUFFLE_OFF;
				}
				break;
			}
			case TB_JTFE:
			{
				strID = IDS_JTFE;
				break;
			}
			case TB_DELETE:
			{
				strID = IDS_DELETE_PHYSICALLY;
				break;
			}
			case TB_OPENEXPLORER:
			{
				strID = IDS_OPEN_EXPLORER;
				break;
			}
		}

		if (strID != -1)
		{
			return WASABI_API_LNGSTRINGW(strID);
		}
		return L"";
	}

	HRESULT CreateShellLink(LPCWSTR filename, LPCWSTR pszTitle, IShellLink **ppsl)
	{
		if (ppsl && filename && *filename)
		{
			IShellLink *psl = NULL;
			HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl));
			if (SUCCEEDED(hr) && psl)
			{
				// due to how WACUP works, a wacup.exe or
				// a winamp.exe might be being used (this
				// is ignoring the winamp.original aspect
				// that this code was dealing with). this
				// call will get the appropriate filepath
				// for the instance of the loader in use!
				wchar_t fname[MAX_PATH] = { 0 };
				RealWACUPPath(fname, ARRAYSIZE(fname));

				LPSHELLFOLDER pDesktopFolder = 0;
				if (SUCCEEDED(SHGetDesktopFolder(&pDesktopFolder)) && pDesktopFolder)
				{
					BOOL failed = FALSE;
					LPITEMIDLIST filepidl = 0;
					// like below, this has also been seen to fail
					// so we will try & catch it & fail gracefully
					__try
					{
						hr = pDesktopFolder->ParseDisplayName(NULL, 0, fname, 0, &filepidl, 0);
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
						hr = S_FALSE;
						failed = TRUE;
					}

					if (SUCCEEDED(hr) && (filepidl != NULL))
					{
						// based on testing, both this & also the
						// psl->SetPath() are sometimes failing &
						// I can't find any reason for it. due to
						// that it is necessary to try & catch it
						// so we don't take down the entire thing
						__try
						{
							hr = psl->SetIDList(filepidl);
						}
						__except (EXCEPTION_EXECUTE_HANDLER)
						{
							hr = S_FALSE;
							failed = TRUE;
						}
					}
					else
					{
						hr = HRESULT_FROM_WIN32(GetLastError());
						failed = TRUE;
					}

					CoTaskMemFree(filepidl);
					pDesktopFolder->Release();

					if (failed)
					{
						psl->Release();
						return hr;
					}
				}

				wchar_t shortfname[MAX_PATH] = {0};
				GetShortPathName(fname, shortfname, ARRAYSIZE(shortfname));

				fname[0] = 0;
				if (GetShortPathName(filename, fname, ARRAYSIZE(fname)) == 0)
				{
					(void)StringCchCopy(fname, ARRAYSIZE(fname), filename);
				}
				psl->SetIconLocation(shortfname, 0);

				// for some reason this randomly crashes for
				// some setups & from the call stack its due
				// to trying to figure out the pidl from the
				// passed in path so we'll instead try to do
				// it ourselves & just pass in a pidl above
				//hr = psl->SetPath(shortfname);

				if (SUCCEEDED(hr))
				{
					hr = psl->SetArguments(fname);
					if (SUCCEEDED(hr))
					{
						// The title property is required on Jump List items provided as an IShellLink
						// instance.  This value is used as the display name in the Jump List.
						IPropertyStore *pps = NULL;
						hr = psl->QueryInterface(IID_PPV_ARGS(&pps));
						if (SUCCEEDED(hr))
						{
							PROPVARIANT propvar = {0};
							hr = InitPropVariantFromString(pszTitle, &propvar);
							if (SUCCEEDED(hr))
							{
								hr = pps->SetValue(PKEY_Title, propvar);
								if (SUCCEEDED(hr))
								{
									hr = pps->Commit();
									if (SUCCEEDED(hr))
									{
										hr = psl->QueryInterface(IID_PPV_ARGS(ppsl));
									}
								}
								PropVariantClear(&propvar);
							}
							pps->Release();
						}
					}
				}
				else
				{
					hr = HRESULT_FROM_WIN32(GetLastError());
				}
				psl->Release();
			}
			return hr;
		}
		return S_FALSE;
	}

	/*bool is_in_recent(std::wstring &filename)
	{
		wchar_t path[MAX_PATH] = {0};
		ExpandEnvironmentStrings(L"%appdata%\\Microsoft\\Windows\\Recent", path, ARRAYSIZE(path));
		PathAppend(path, filename.c_str());
		PathAddExtension(path, L".lnk");
		return !!PathFileExists(path);
	}*/

	HIMAGELIST prepareIcons(void)
	{
		HIMAGELIST himlIcons = ImageList_Create(GetSystemMetrics(SM_CXSMICON),
												GetSystemMetrics(SM_CYSMICON),
												ILC_COLOR32, NR_THUMB_BUTTONS, 0);

		for (int i = 0; i < NR_THUMB_BUTTONS; ++i)
		{
#if 1
			int icon = -1;
			switch (IDI_TBICON0 + i)
			{
				case IDI_TBICON0:	// stop
				{
					icon = 3;
					break;
				}
				case IDI_TBICON1:	// prev
				{
					icon = 0;
					break;
				}
				case IDI_TBICON2:	// pause
				{
					icon = 2;
					break;
				}
				case IDI_TBICON3:	// play
				{
					icon = 1;
					break;
				}
				case IDI_TBICON4:	// next
				{
					icon = 4;
					break;
				}
				case IDI_TBICON6:	// voldown
				{
					icon = 6;
					break;
				}
				case IDI_TBICON7:	// volup
				{
					icon = 7;
					break;
				}
				case IDI_TBICON8:	// open
				{
					icon = 5;
					break;
				}
			}

			if (icon != -1)
			{
				const int icons[] = {203/*prev*/, 204/*play*/, 208/*pause*/, 205/*stop*/,
									 206/*next*/, 207/*open*/, 209/*voldown*/, 210/*volup*/};
				HICON hicon = LoadIcon(GetModuleHandle(GetPaths()->wacup_core_dll), MAKEINTRESOURCE(icons[icon]));
				if (hicon == NULL)
				{
					return NULL;
				}
				else
				{
					ImageList_AddIcon(himlIcons, hicon);
				}
			}
			else
#endif
			{
				HICON hicon = LoadIcon(plugin.hDllInstance, MAKEINTRESOURCE(IDI_TBICON0 + i)); 
				if (hicon == NULL)
				{
					return NULL;
				}
				else
				{
					ImageList_AddIcon(himlIcons, hicon);
				}
			}
		}

		return himlIcons;
	}

	std::wstring SecToTime(const int sec)
	{
		const int ZH = sec / 3600,
				  ZM = sec / 60 - ZH * 60,
				  ZS = sec - (ZH * 3600 + ZM * 60);

		std::wstringstream ss;
		if (ZH != 0)
		{
			if (ZH < 10)
			{
				ss << L"0" << ZH << L":";
			}
			else
			{
				ss << ZH << L":";
			}
		}

		if (ZM < 10)
		{
			ss << L"0" << ZM << L":";
		}
		else
		{
			ss << ZM << L":";
		}

		if (ZS < 10)
		{
			ss << L"0" << ZS;
		}
		else
		{
			ss << ZS;
		}

		return ss.str();
	}

	std::wstring getBookmarks(void)
	{
		std::wifstream is(GetPaths()->winamp_bm_path/*(wchar_t*)SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_ADDBOOKMARKW)/**/);
		if (is.fail())
		{
			return L"";
		}

		std::wstring data;
		std::getline(is, data, L'\0');
		return data;
	}

	int getBitmap(const int button, const int mode)
	{
		switch (button)
		{
			case TB_PREVIOUS:
			{
				return 1;
			}
			case TB_PLAYPAUSE:
			{
				return (mode == 1) ? 2 : 3;
			}
			case TB_STOP:
			{
				return 0;
			}
			case TB_NEXT:
			{
				return 4;
			}
			case TB_RATE:
			{
				return 5;
			}
			case TB_VOLDOWN:
			{
				return 6;
			}
			case TB_VOLUP:
			{
				return 7;
			}
			case TB_OPENFILE:
			{
				return 8;
			}
			case TB_MUTE:
			{
				return 9;
			}
			case TB_STOPAFTER:
			{
				return 10;
			}
			case TB_REPEAT:
			{
				if (mode == 0)
				{
					return 11;
				}
				else if (mode == 1)
				{
					return 12;
				}
				else
				{
					return 13;
				}
			}
			case TB_SHUFFLE:
			{
				return (mode == 1) ? 14 : 15;
			}
			case TB_JTFE:
			{
				return 16;
			}
			case TB_DELETE:
			{
				return 17;
			}
			case TB_OPENEXPLORER:
			{
				return 18;
			}
		}

		return -1;
	}
}