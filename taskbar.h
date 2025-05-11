#ifndef taskbar_h__
#define taskbar_h__

#include <Windows.h>
#include <Shobjidl.h>
#include <string>
#include <vector>
#include "gen_win7shell.h"

class iTaskBar
{
public:
	explicit iTaskBar(sSettings& settings);
	~iTaskBar();

	bool Reset();
	void ThumbBarUpdateButtons(const std::vector<THUMBBUTTON>& buttons, HIMAGELIST ImageList);
	void SetProgressState(TBPFLAG newstate);
	void SetIconOverlay(HICON icon, LPCWSTR text);
	void SetProgressValue(ULONGLONG completed, ULONGLONG total);    

	void SetWindowAttr(void) const;

private:
	ITaskbarList4* pTBL;
	TBPFLAG progressbarstate;
	sSettings &m_settings;
};

#endif // taskbar_h__