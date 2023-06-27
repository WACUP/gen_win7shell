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
	const int NR_THUMB_BUTTONS = 19,
			  NR_OVERLAY_ICONS = 3;

	// Functions declarations
	std::wstring getBookmarks(void);
	HRESULT CreateShellLink(LPCWSTR filename, LPCWSTR pszTitle, IShellLink **ppsl);
	HIMAGELIST prepareOverlayIcons(void);
	HIMAGELIST prepareIcons(void);
	std::wstring SecToTime(const int sec);
	LPCWSTR getToolTip(const WPARAM button, const int mode = -1);
	const int getBitmapCount(void);
	const int getBitmap(const int button, const int mode);
}

#endif // tools_h__