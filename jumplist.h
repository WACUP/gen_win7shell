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

	void CreateJumpList(const std::wstring &pluginpath, const std::wstring &pref,
						const std::wstring &openfile, const std::wstring &bookmarks,
						const std::wstring &pltext, const bool recent,
						const bool frequent, const bool tasks, const bool addbm,
						const bool playlist, const std::wstring &bms);

private:
	HRESULT _CreateShellLink(const std::wstring &path, PCWSTR pszArguments,
							 PCWSTR pszTitle, IShellLink **ppsl,
							 const int iconindex, const int mode = 0);
	bool _IsItemInArray(std::wstring path, IObjectArray *poaRemoved);
	HRESULT _AddTasksToList(const std::wstring &pluginpath, const std::wstring &pref,
							const std::wstring &openfile);
	HRESULT _AddCategoryToList(IObjectCollection *poc, const std::wstring &bookmarks);
	HRESULT _AddCategoryToList2(const std::wstring &pluginpath, const std::wstring &pltext);

	bool CleanJL(LPCWSTR AppID, IApplicationDocumentLists *padl, APPDOCLISTTYPE type);

	ICustomDestinationList *pcdl;
};

#endif // jumplist_h__