#include <Windows.h>
#include <ObjBase.h>
#include <dwmapi.h>
#include <fstream>
#include "gen_win7shell.h"
#include "taskbar.h"

iTaskBar::iTaskBar(sSettings& settings) : pTBL(NULL), progressbarstate(TBPF_NOPROGRESS), m_settings(settings)
{
	CoInitializeEx(0, COINIT_APARTMENTTHREADED);
	SetWindowAttr();
}

iTaskBar::~iTaskBar()
{
	if (pTBL != NULL)
	{
		pTBL->Release();
		pTBL = NULL;
	}

	CoUninitialize();
}

bool iTaskBar::Reset()
{
	if (pTBL != NULL)
	{
		pTBL->Release();
		pTBL = NULL;
	}

	HRESULT hr = ::CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER,
									IID_ITaskbarList, reinterpret_cast<void**>(&pTBL));
	if (!SUCCEEDED(hr) || !pTBL)
	{
		return false;
	}

	const bool ret = SUCCEEDED(pTBL->HrInit());
	SetWindowAttr();
	return ret;
}

HRESULT iTaskBar::ThumbBarUpdateButtons(std::vector<THUMBBUTTON>& buttons, HIMAGELIST ImageList)
{
	if (ImageList != NULL)
	{
		pTBL->ThumbBarSetImageList(plugin.hwndParent, ImageList);
		return pTBL->ThumbBarAddButtons(plugin.hwndParent, (int)buttons.size(), &buttons[0]);
	}

	return pTBL->ThumbBarUpdateButtons(plugin.hwndParent, (int)buttons.size(), &buttons[0]);
}

void iTaskBar::SetIconOverlay(HICON icon, const std::wstring &text)
{
	if (pTBL)
	{
		pTBL->SetOverlayIcon(plugin.hwndParent, icon, text.c_str());
	}
}

void iTaskBar::SetWindowAttr(void)
{
	// if we're under a classic skin & to show the main
	// window then we can just let Windows handle it as
	// it will do a much better job with less resources
	BOOL enabled = !(classicSkin && (m_settings.Thumbnailbackground == BG_WINAMP));
	DwmSetWindowAttribute(plugin.hwndParent, DWMWA_HAS_ICONIC_BITMAP, &enabled, sizeof(enabled));
	DwmSetWindowAttribute(plugin.hwndParent, DWMWA_FORCE_ICONIC_REPRESENTATION, &enabled, sizeof(enabled));
	DwmInvalidateIconicBitmaps(plugin.hwndParent);
}

void iTaskBar::SetProgressValue(ULONGLONG completed, ULONGLONG total)
{
	if (pTBL)
	{
		pTBL->SetProgressValue(plugin.hwndParent, completed, total);
	}
}

void iTaskBar::SetProgressState(TBPFLAG newstate)
{
	if (newstate != progressbarstate)
	{
		if (pTBL)
		{
			pTBL->SetProgressState(plugin.hwndParent, newstate);
		}
		progressbarstate = newstate;
	}
}