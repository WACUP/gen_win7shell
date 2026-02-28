#ifndef jumplist_h__
#define jumplist_h__

#include <objectarray.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <string>
#include <map>

class JumpList
{
public:
	explicit JumpList(const bool delete_now = false);
	~JumpList();

	void CreateJumpList(LPCWSTR pluginpath, LPCWSTR loaderpath, LPCWSTR preferences,
						LPCWSTR openfile, LPCWSTR bookmarks, LPCWSTR playlisttext,
						const bool recent, const bool frequent, const bool tasks,
						const bool addbm, const bool playlist, const bool& closing);

private:
	HRESULT _CreateShellLink(LPCWSTR path, LPCWSTR loaderpath, LPCWSTR pszArguments,
							 LPCWSTR pszTitle, IShellLink **ppsl, const int iconindex, const int mode = 0);
	static bool _IsItemInArray(const std::wstring &path, IObjectArray *poaRemoved);
	HRESULT _AddTasksToList(IObjectCollection* poc, LPCWSTR pluginpath, LPCWSTR loaderpath,
							LPCWSTR preferences, LPCWSTR openfile);
	HRESULT _AddCategoryToList(IObjectCollection *poc, LPCWSTR bookmarks);
	HRESULT _AddCategoryToList2(IObjectCollection* poc, LPCWSTR pluginpath,
								LPCWSTR loaderpath, LPCWSTR playlisttext);

	static bool CleanJL(LPCWSTR AppID, IApplicationDocumentLists *padl, APPDOCLISTTYPE type);

	ICustomDestinationList *pcdl;
};

const bool ClearRecentFrequentEntries(void);

#endif // jumplist_h__