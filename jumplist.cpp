#include <objectarray.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <string>
#include <sstream>
#include <map>
#include "jumplist.h"
#include "api.h"
#include "tools.h"
#include <loader/loader/paths.h>
#include <loader/loader/utils.h>

JumpList::JumpList(const bool delete_now) : pcdl(NULL)
{
	if (SUCCEEDED(CreateCOMInProc(CLSID_DestinationList,
		__uuidof(ICustomDestinationList), (LPVOID*)&pcdl)) && pcdl)
	{
		LPCWSTR AppID = GetAppID();

		pcdl->SetAppID(AppID);

		IApplicationDocumentLists *padl = NULL;
		if (SUCCEEDED(CreateCOMInProc(CLSID_ApplicationDocumentLists,
			__uuidof(IApplicationDocumentLists), (LPVOID*)&padl)) && padl)
		{
			CleanJL(AppID, padl, ADLT_RECENT);
			CleanJL(AppID, padl, ADLT_FREQUENT);
			padl->Release();
		}

		if (delete_now)
		{
			__try
			{
				pcdl->DeleteList(AppID);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
			}
		}
	}
}

JumpList::~JumpList()
{
	if (pcdl != NULL)
	{
		pcdl->Release();
	}
}

// Creates a CLSID_ShellLink to insert into the Tasks section of the Jump List. This type of
// Jump List item allows the specification of an explicit command line to execute the task.
HRESULT JumpList::_CreateShellLink(const std::wstring &path, PCWSTR pszArguments,
								   PCWSTR pszTitle, IShellLink **ppsl,
								   const int iconindex, const int mode)
{
	IShellLink *psl = NULL;
	HRESULT hr = CreateCOMInProc(CLSID_ShellLink,
				 __uuidof(IShellLink), (LPVOID*)&psl);
	if (SUCCEEDED(hr))
	{
		if (psl)
		{
			psl->SetIconLocation(path.c_str(), iconindex);
			if (mode)
			{
				__try
				{
					// due to how WACUP works, a wacup.exe or
					// a winamp.exe might be being used under
					// the x86 build so we need to obtain the
					// correct filepath for the loader in use
					LPCWSTR loader_path = GetPaths()->wacup_loader_exe;
					if (mode == 1)
					{
						wchar_t shortfname[MAX_PATH]/* = { 0 }*/;
						if (GetShortPathName(loader_path, shortfname, ARRAYSIZE(shortfname)))
						{
							hr = psl->SetPath(shortfname);
						}
						else
						{
							hr = S_FALSE;
						}
					}
					else
					{
						hr = psl->SetPath(loader_path);
					}
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					hr = S_FALSE;
				}
			}
			else
			{
				__try
				{
					hr = psl->SetPath(L"rundll32.exe");
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					hr = S_FALSE;
				}
			}

			if (SUCCEEDED(hr))
			{
				hr = psl->SetArguments(pszArguments);

				if (SUCCEEDED(hr))
				{
					// The title property is required on Jump List items provided as an IShellLink
					// instance.  This value is used as the display name in the Jump List.
					IPropertyStore *pps = NULL;
					hr = psl->QueryInterface(IID_PPV_ARGS(&pps));
					if (SUCCEEDED(hr))
					{
						PROPVARIANT propvar = { 0 };
						hr = PropVarFromStr(pszTitle, &propvar);
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
							ClearPropVariant(&propvar);
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
	}
	return hr;
}

void JumpList::CreateJumpList(const std::wstring &pluginpath, const std::wstring &pref,
							  const std::wstring &openfile, const std::wstring &bookmarks,
							  const std::wstring &pltext, const bool recent,
							  const bool frequent, const bool tasks, const bool addbm,
							  const bool playlist, const bool &closing)
{
	UINT cMinSlots = 0;
	IObjectArray *poaRemoved = NULL;
	IObjectCollection *poc = NULL;
	HRESULT hr = pcdl->BeginList(&cMinSlots, IID_PPV_ARGS(&poaRemoved));

	if (SUCCEEDED(CreateCOMInProc(CLSID_EnumerableObjectCollection,
				  __uuidof(IObjectCollection), (LPVOID*)&poc)) && poc)
	{
		bool has_bm = false;
		if (addbm && (hr == S_OK))
		{
			const std::wstring& bms = tools::getBookmarks();
			std::wstringstream ss(bms);
			std::wstring line1, line2;
			bool b = false;
			has_bm = !bms.empty();
			while (getline(ss, line1) && !closing)
			{
				if (b)
				{
					IShellLink *psl = NULL;
					hr = _CreateShellLink(pluginpath, line2.c_str(), line1.c_str(), &psl, 2, 1);

					if (!_IsItemInArray(line2, poaRemoved))
					{
						psl->SetDescription(line2.c_str());
						poc->AddObject(psl);
					}

					psl->Release();
					b = false;
				}
				else
				{
					line2.resize(MAX_PATH);
					if (GetShortPathName(line1.c_str(), &line2[0], MAX_PATH) == 0)
					{
						line2 = line1;
					}
					else
					{
						line2.shrink_to_fit();
					}

					b = true;
				}
			}
		}

		if (SUCCEEDED(hr) && !closing)
		{
			if (recent)
			{
				pcdl->AppendKnownCategory(KDC_RECENT);
			}

			if (frequent)
			{
				pcdl->AppendKnownCategory(KDC_FREQUENT);
			}
		
			if (addbm && has_bm)
			{
				_AddCategoryToList(poc, bookmarks);
			}

			if (playlist)
			{
				hr = _AddCategoryToList2(pluginpath, pltext);
			}

			if (tasks)
			{
				_AddTasksToList(pluginpath, pref, openfile);
			}

			if (SUCCEEDED(hr))
			{
				__try
				{
					// Commit the list-building transaction.
					pcdl->CommitList();
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
				}
			}
		}
		poaRemoved->Release();
		poc->Release();
	}
}

HRESULT JumpList::_AddTasksToList(const std::wstring &pluginpath, const std::wstring &pref, const std::wstring &openfile)
{
	IObjectCollection *poc = NULL;
	HRESULT hr = CreateCOMInProc(CLSID_EnumerableObjectCollection,
					  __uuidof(IObjectCollection), (LPVOID*)&poc);
	if (SUCCEEDED(hr))
	{
		if (poc)
		{
			IShellLink *psl = NULL;
			hr = _CreateShellLink(pluginpath, L"/COMMAND=40012", pref.c_str(), &psl, 0, 2);
			if (SUCCEEDED(hr))
			{
				poc->AddObject(psl);
				psl->Release();
			}

			hr = _CreateShellLink(pluginpath, L"/COMMAND=40029", openfile.c_str(), &psl, 1, 2);
			if (SUCCEEDED(hr))
			{
				poc->AddObject(psl);
				psl->Release();
			}

			IObjectArray *poa = NULL;
			hr = poc->QueryInterface(IID_PPV_ARGS(&poa));
			if (SUCCEEDED(hr))
			{
				if (poa)
				{
					// Add the tasks to the Jump List. Tasks
					// always appear in the canonical "Tasks"
					// category that is displayed at the bottom
					// of the Jump List, after other categories
					hr = pcdl->AddUserTasks(poa);
					poa->Release();
				}
			}
			poc->Release();
		}
	}
	return hr;
}

// Determines if the provided IShellItem is listed in the array of items that the user has removed
bool JumpList::_IsItemInArray(const std::wstring &path, IObjectArray *poaRemoved)
{
	bool fRet = false;
	UINT cItems = 0;
	if (SUCCEEDED(poaRemoved->GetCount(&cItems)))
	{
		IShellLink *psiCompare = NULL;
		for (UINT i = 0; !fRet && i < cItems; i++)
		{
			if (SUCCEEDED(poaRemoved->GetAt(i, IID_PPV_ARGS(&psiCompare))))
			{
				if (psiCompare)
				{
					wchar_t removedpath[MAX_PATH]/* = { 0 }*/;
					removedpath[0] = 0;
					psiCompare->GetArguments(removedpath, ARRAYSIZE(removedpath));
					fRet = !path.compare(removedpath);
					psiCompare->Release();
				}
			}
		}
	}
	return fRet;
}

// Adds a custom category to the Jump List.  Each item that should be in the category is added
// to an ordered collection, and then the category is appended to the Jump List as a whole.
HRESULT JumpList::_AddCategoryToList(IObjectCollection *poc, const std::wstring &bookmarks)
{
	IObjectArray *poa = NULL;
	HRESULT hr = poc->QueryInterface(IID_PPV_ARGS(&poa));
	if (SUCCEEDED(hr))
	{
		if (poa)
		{
			// Add the category to the Jump List.  If there were more categories,
			// they would appear from top to bottom in the order they were appended.
			hr = pcdl->AppendCategory(bookmarks.c_str(), poa);
			poa->Release();
		}
	}

	return hr;
}

// Adds a custom category to the Jump List.  Each item that should be in the category is added to
// an ordered collection, and then the category is appended to the Jump List as a whole.
HRESULT JumpList::_AddCategoryToList2(const std::wstring &pluginpath, const std::wstring &pltext)
{
	IObjectCollection *poc = NULL;
	HRESULT hr = CreateCOMInProc(CLSID_EnumerableObjectCollection,
					  __uuidof(IObjectCollection), (LPVOID*)&poc);
	if (SUCCEEDED(hr) && poc)
	{
		// enumerate through playlists (need to see if can use api_playlists.h via sdk)
		const size_t count = (WASABI_API_PLAYLISTS ? WASABI_API_PLAYLISTS->GetCount() : 0);
			for (size_t i = 0; i < count; i++)
			{
				size_t numItems = 0;
				IShellLink *psl = NULL;

				wchar_t tmp[MAX_PATH]/* = {0}*/;
				std::wstring title = WASABI_API_PLAYLISTS->GetName(i);

				WASABI_API_PLAYLISTS->GetInfo(i, api_playlists_itemCount, &numItems, sizeof(numItems));
				PrintfCch(tmp, ARRAYSIZE(tmp), L" [%d]", numItems);
				title += tmp;

				hr = _CreateShellLink(pluginpath, WASABI_API_PLAYLISTS->GetFilename(i), title.c_str(), &psl, 3, 1);
				if (SUCCEEDED(hr))
				{
					if (psl)
					{
						psl->SetDescription(WASABI_API_PLAYLISTS->GetFilename(i));
						poc->AddObject(psl);
						psl->Release();
				}
			}
		}

		IObjectArray *poa = NULL;
		hr = poc->QueryInterface(IID_PPV_ARGS(&poa));
		if (SUCCEEDED(hr))
		{
			if (poa)
			{
				// Add the category to the Jump List.  If there were more categories,
				// they would appear from top to bottom in the order they were appended.
				/*hr = */pcdl->AppendCategory(pltext.c_str(), poa);
				poa->Release();
			}
		}

		return S_OK;
	}

	return S_FALSE;
}

bool JumpList::CleanJL(LPCWSTR AppID, IApplicationDocumentLists *padl, APPDOCLISTTYPE type)
{
	IObjectArray *poa = NULL;
	padl->SetAppID(AppID);
	HRESULT hr;
	
	try
	{
		hr = padl->GetList(type, 0, IID_PPV_ARGS(&poa));
	}
	catch (...)
	{
		return false;
	}

	if (SUCCEEDED(hr))
	{
		UINT count = 0;
		hr = poa->GetCount(&count);
		if (SUCCEEDED(hr) && (count > 100))
		{
			IApplicationDestinations *pad = NULL;
			hr = CreateCOMInProc(CLSID_ApplicationDestinations,
								 __uuidof(IApplicationDestinations), (LPVOID*)&pad);
			if (SUCCEEDED(hr) && pad)
			{
				pad->SetAppID(AppID);

				if (SUCCEEDED(hr))
				{
					for (UINT i = (count - 1); i > 100; --i)
					{
						IShellLink* psi = NULL;
						hr = poa->GetAt(i, IID_PPV_ARGS(&psi));

						if (SUCCEEDED(hr))
						{
							try
							{
								pad->RemoveDestination(psi);
							}
							catch (...)
							{
								continue;
							}
						}
					}
				}

				pad->Release();
			}
		}

		poa->Release();
	}
	else
	{
		wchar_t path[MAX_PATH]/* = {0}*/;
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, path)))
		{
			std::wstring filepath(path);
			filepath += std::wstring(L"\\Microsoft\\Windows\\Recent\\AutomaticDestinations"
									 L"\\879d567ffa1f5b9f.automaticDestinations-ms", 89);
			if (RemoveFile(filepath.c_str()) == 0)
			{
				return false;
			}
		}
	}

	return true;
}