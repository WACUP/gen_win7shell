#include <Windows.h>
#include <ObjBase.h>
#include <dwmapi.h>
#include <fstream>
#include "gen_win7shell.h"
#include "taskbar.h"
#include <loader/loader/utils.h>

iTaskBar::iTaskBar(sSettings& settings) : pTBL(NULL), progressbarstate(TBPF_NOPROGRESS), m_settings(settings)
{
}

iTaskBar::~iTaskBar()
{
	if (pTBL != NULL)
	{
		pTBL->Release();
		pTBL = NULL;
	}
}

bool iTaskBar::Reset()
{
	if (pTBL != NULL)
	{
		pTBL->Release();
		pTBL = NULL;
	}

	HRESULT hr = CreateCOMInProc(CLSID_TaskbarList, IID_ITaskbarList,
									reinterpret_cast<void**>(&pTBL));
	if (!SUCCEEDED(hr) || !pTBL)
	{
		return false;
	}

	const bool ret = SUCCEEDED(pTBL->HrInit());
	SetWindowAttr();
	return ret;
}

void iTaskBar::ThumbBarUpdateButtons(std::vector<THUMBBUTTON>& buttons, HIMAGELIST ImageList)
{
	if (pTBL != NULL)
	{
		if (ImageList != NULL)
		{
			pTBL->ThumbBarSetImageList(plugin.hwndParent, ImageList);
			pTBL->ThumbBarAddButtons(plugin.hwndParent, (int)buttons.size(), (LPTHUMBBUTTON)&buttons[0]);
		}
		else
		{
			pTBL->ThumbBarUpdateButtons(plugin.hwndParent, (int)buttons.size(), (LPTHUMBBUTTON)&buttons[0]);
		}
	}
}

void iTaskBar::SetIconOverlay(HICON icon, LPCWSTR text)
{
	if (pTBL != NULL)
	{
		__try
		{
			pTBL->SetOverlayIcon(plugin.hwndParent, icon, text);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}
}

void iTaskBar::SetWindowAttr(void) const
{
	// if we're under a classic skin & to show the main
	// window then we can just let Windows handle it as
	// it will do a much better job with less resources
	BOOL enabled = (!IsIconic(plugin.hwndParent) ? !(classicSkin &&
				   (m_settings.Thumbnailbackground == BG_WINAMP)) : /*!classicSkin/*/TRUE/**/);
	DwmSetWindowAttribute(plugin.hwndParent, DWMWA_HAS_ICONIC_BITMAP, &enabled, sizeof(enabled));
	DwmSetWindowAttribute(plugin.hwndParent, DWMWA_FORCE_ICONIC_REPRESENTATION, &enabled, sizeof(enabled));

	DwmInvalidateIconicBitmaps(plugin.hwndParent);
}

void iTaskBar::SetProgressValue(ULONGLONG completed, ULONGLONG total)
{
	if (pTBL != NULL)
	{
		pTBL->SetProgressValue(plugin.hwndParent, completed, total);
	}
}

void iTaskBar::SetProgressState(TBPFLAG newstate)
{
	if (newstate != progressbarstate)
	{
		if (pTBL != NULL)
		{
			pTBL->SetProgressState(plugin.hwndParent, newstate);
		}
		progressbarstate = newstate;
	}
}