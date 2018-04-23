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
	iTaskBar();
	~iTaskBar();

	bool Reset();
	HRESULT ThumbBarUpdateButtons(std::vector<THUMBBUTTON>& buttons, HIMAGELIST ImageList);
	void SetProgressState(TBPFLAG newstate);
	void SetIconOverlay(HICON icon, const std::wstring &text);
	void SetProgressValue(ULONGLONG completed, ULONGLONG total);    

private:
	static void SetWindowAttr(/*const bool flip*/);

	ITaskbarList4* pTBL;
	TBPFLAG progressbarstate;
};

#endif // taskbar_h__