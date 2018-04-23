#ifndef tools_h__
#define tools_h__

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <string>
#include <sstream>
#include <fstream>
#include <propvarutil.h>
#include <propkey.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "sdk/winamp/wa_ipc.h"

namespace tools
{
	// Namespace variables
	const int NR_THUMB_BUTTONS = 19;

	// Functions declarations
	std::wstring getBookmarks();
	HRESULT CreateShellLink(PCWSTR filename, PCWSTR pszTitle, IShellLink **ppsl);
	bool is_in_recent(std::wstring &filename);
	HIMAGELIST prepareIcons();
	std::wstring SecToTime(const int sec);
	LPCWSTR getToolTip(const int button, const int mode = -1);
	int getBitmap(const int button, const int mode);
}

#endif // tools_h__