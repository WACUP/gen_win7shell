#include <Windows.h>
#include <ObjBase.h>
#include <dwmapi.h>
#include <fstream>

#include "taskbar.h"

iTaskBar::iTaskBar() : pTBL(NULL), progressbarstate(TBPF_NOPROGRESS)
{    
	CoInitialize(0);
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
		return pTBL->ThumbBarAddButtons(plugin.hwndParent, buttons.size(), &buttons[0]);
	}

	return pTBL->ThumbBarUpdateButtons(plugin.hwndParent, buttons.size(), &buttons[0]);
}

void iTaskBar::SetIconOverlay(HICON icon, const std::wstring &text)
{
	if (pTBL)
	{
		pTBL->SetOverlayIcon(plugin.hwndParent, icon, text.c_str());
	}
}

void iTaskBar::SetWindowAttr(/*const bool flip*/)
{
	bool enabled = true;
	DwmInvalidateIconicBitmaps(dialogParent);
	DwmSetWindowAttribute(plugin.hwndParent, DWMWA_HAS_ICONIC_BITMAP, &enabled, sizeof(int));
	DwmSetWindowAttribute(plugin.hwndParent, DWMWA_FORCE_ICONIC_REPRESENTATION, &enabled, sizeof(int));

	/*if (flip)
	{
		DWMFLIP3DWINDOWPOLICY flip_policy = (flip ? DWMFLIP3D_EXCLUDEBELOW : DWMFLIP3D_DEFAULT);
		DwmSetWindowAttribute(plugin.hwndParent, DWMWA_FLIP3D_POLICY, &flip_policy, sizeof(int));
	}*/
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