#include <windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <windowsx.h>
#include <strsafe.h>
#include <commctrl.h>
#include "settings.h"
#include "gen_win7shell.h"
#include "api.h"
#include "resource.h"
#include <sdk/nu/autowide.h>
#include <sdk/nu/autochar.h>
#include <sdk/nu/autocharfn.h>

int SettingsManager::GetInt(const std::wstring &key, const int default_value) const
{
	return GetPrivateProfileIntW(currentSection.c_str(), key.c_str(), default_value, SettingsFile.c_str());
}

bool SettingsManager::GetBool(const std::wstring &key, const bool default_value) const
{
	return !!GetPrivateProfileIntW(currentSection.c_str(), key.c_str(), default_value, SettingsFile.c_str());
}

std::wstring SettingsManager::GetString(const std::wstring &key, const std::wstring &default_value,
										const size_t max_size) const
{
	LPWSTR _section = (LPWSTR)currentSection.c_str(),
		   _key = (LPWSTR)key.c_str(),
		   _default = (LPWSTR)default_value.c_str(),
		   _file = (LPWSTR)SettingsFile.c_str();

	std::string buffer;
	buffer.resize(max_size);
	const DWORD len = GetPrivateProfileStringA(AutoChar(_section, CP_UTF8), AutoChar(_key, CP_UTF8),
											   AutoChar(_default, CP_UTF8), &buffer[0],
											   max_size, AutoCharFn(_file));
	buffer.resize(len);

	if (!buffer.empty())
	{
		const std::wstring read = AutoWide(buffer.c_str(), CP_UTF8);
		return read;
	}
	return L"";
}

void SettingsManager::WriteInt(const std::wstring &key, const int value,
							   const int default_value) const
{
	if (value != default_value)
	{
		wchar_t str[16] = {0};
		StringCchPrintf(str, 16, L"%d", value);
		WritePrivateProfileStringW(currentSection.c_str(), key.c_str(),
								   str, SettingsFile.c_str());
	}
	else
	{
		WritePrivateProfileStringW(currentSection.c_str(), key.c_str(),
								   NULL, SettingsFile.c_str());
	}
}

void SettingsManager::WriteBool(const std::wstring &key, const bool value,
								const bool default_value) const
{
	WritePrivateProfileStringW(currentSection.c_str(), key.c_str(), ((value != default_value) ?
							   (value ? L"1" : L"0") : NULL), SettingsFile.c_str());
}

void SettingsManager::WriteString(const std::wstring &key, const std::wstring &value,
								  const std::wstring &default_value) const
{
	LPWSTR _section = (LPWSTR)currentSection.c_str(),
		   _key = (LPWSTR)key.c_str(),
		   _file = (LPWSTR)SettingsFile.c_str();
	if (value != default_value)
	{
		LPWSTR _value = (LPWSTR)value.c_str();
		WritePrivateProfileStringA(AutoChar(_section, CP_UTF8), AutoChar(_key, CP_UTF8),
								   AutoChar(_value, CP_UTF8), AutoCharFn(_file));
	}
	else
	{
		WritePrivateProfileStringA(AutoChar(_section, CP_UTF8), AutoChar(_key, CP_UTF8),
								   NULL, AutoCharFn(_file));
	}
}

void SettingsManager::ReadSettings(sSettings &Destination_struct, std::vector<int> &tba)
{
	currentSection = SECTION_NAME_GENERAL;

	// Read all values from .ini file to Destination_struct
	Destination_struct.Add2RecentDocs = GetBool(L"Add2RecentDocs", true);
	Destination_struct.Antialias = GetBool(L"AntiAlias", true);
	Destination_struct.AsIcon = GetBool(L"AsIcon", true);
	wcsncpy(Destination_struct.BGPath, GetString(L"BGPath", L"", MAX_PATH).c_str(),
												  ARRAYSIZE(Destination_struct.BGPath));
	Destination_struct.JLbms = GetBool(L"JLBookMarks", true);
	Destination_struct.JLfrequent = GetBool(L"JLFrequent", true);
	Destination_struct.JLpl = GetBool(L"JLPlayList", true);
	Destination_struct.JLrecent = GetBool(L"JLRecent", true);
	Destination_struct.JLtasks = GetBool(L"JLTasks", true);
	Destination_struct.Overlay = GetBool(L"IconOverlay", true);
	Destination_struct.Progressbar = GetBool(L"Progress", false);
	Destination_struct.Revertto = GetInt(L"Revert", BG_TRANSPARENT);
	Destination_struct.Shrinkframe = GetBool(L"ShrinkFrame", false);
	Destination_struct.Stoppedstatus = GetBool(L"StoppedStatusOn", true);
	Destination_struct.Streamstatus = GetBool(L"StreamStatusOn", true);
	wcsncpy(Destination_struct.Text, GetString(L"Text", L"‡").c_str(),
											   ARRAYSIZE(Destination_struct.Text));

	if (!lstrcmp(Destination_struct.Text, L"‡"))
	{
		wcsncpy(Destination_struct.Text,
				L"%c%%s%%curpl% of %totalpl%.\\r%c%%s%%title%"
				L"\\r%c%%s%%artist%\\r\\r%c%%s%%curtime%/%totaltime%"
				L"\\r%c%%s%Track #: %track%        Volume: %volume%%",
				ARRAYSIZE(Destination_struct.Text));
	}

	Destination_struct.Thumbnailbackground = GetInt(L"ThumbnailBG", BG_WINAMP);
	Destination_struct.Thumbnailbuttons = GetBool(L"ThumbnailButtons", true);
	Destination_struct.Thumbnailpb = GetBool(L"ThumbnailPB", false);
#ifdef USE_MOUSE
	Destination_struct.VolumeControl = GetBool(L"VolumeControl", false);
#endif
	Destination_struct.VuMeter = GetBool(L"VUMeter", false);   
	Destination_struct.LowFrameRate = GetBool(L"LowFrameRate", false);
	Destination_struct.LastTab = GetInt(L"LastTab", 0);
	Destination_struct.IconSize = GetInt(L"IconSize", 50);
	Destination_struct.IconPosition = GetInt(L"IconPosition", IP_UPPERLEFT);
	Destination_struct.BG_Transparency = GetInt(L"BG_Transparency", 80);

	// Decoding bool[16] Buttons
	const std::wstring Buttons = GetString(L"Buttons", L"1111100000000000");
	for (int i = 0; i != 16; ++i)
	{
		Destination_struct.Buttons[i] = (Buttons[i] == '1' ? true : false);
	}

	// Read font
	currentSection = SECTION_NAME_FONT;

	if (!GetPrivateProfileStructW(SECTION_NAME_FONT, L"font", &Destination_struct.font,
								  sizeof(Destination_struct.font), SettingsFile.c_str()))
	{
		wcsncpy_s(Destination_struct.font.lfFaceName, L"Segoe UI",
				  ARRAYSIZE(Destination_struct.font.lfFaceName));
		Destination_struct.font.lfHeight = -13;
		Destination_struct.font.lfWeight = FW_NORMAL;
	}

	Destination_struct.text_color = GetInt(L"color", RGB(255, 255, 255));
	Destination_struct.bgcolor = GetInt(L"bgcolor", RGB(0, 0, 0));

	Destination_struct.MFT = GetInt(L"MFT", 200);
	Destination_struct.MST = GetInt(L"MST", 500);
	Destination_struct.TFT = GetInt(L"TFT", 33);
	Destination_struct.TST = GetInt(L"TST", 66);

	// Read buttons
	std::wstring text;
	text.resize(100);
	const DWORD len = GetPrivateProfileString(SECTION_NAME_GENERAL, L"ThumbButtons",
											  L"1300,1301,1302,1303,1308,1314", &text[0],
											  99, SettingsFile.c_str());
	if (len > 0)
	{
		text.resize(len);
	}

	tba.clear();
	std::wstring::size_type pos = std::wstring::npos;
	do
	{
		pos = text.find_first_of(L',');
		std::wstringstream buffer;
		buffer << text.substr(0, pos);

		int code = 0;
		buffer >> code;
		if (code < 1300)
		{
			text.erase(0, pos + 1);
			continue;
		}

		tba.push_back(code);
		text.erase(0, pos+1);
	} 
	while (pos != std::wstring::npos);

	if (tba.empty())
	{
		tba.push_back(1300);
		tba.push_back(1301);
		tba.push_back(1302);
		tba.push_back(1303);
		tba.push_back(1308);
	}
}

void SettingsManager::WriteSettings(const sSettings &Source_struct)
{
	currentSection = SECTION_NAME_GENERAL;

	// Write values form Source_struct to .ini file
	WriteBool(L"Add2RecentDocs", Source_struct.Add2RecentDocs, true);
	WriteBool(L"AntiAlias", Source_struct.Antialias, true);
	WriteBool(L"AsIcon", Source_struct.AsIcon, true);
	WriteString(L"BGPath", Source_struct.BGPath, L"");
	WriteBool(L"JLBookMarks", Source_struct.JLbms, true);
	WriteBool(L"JLFrequent", Source_struct.JLfrequent, true);
	WriteBool(L"JLPlayList", Source_struct.JLpl, true);
	WriteBool(L"JLRecent", Source_struct.JLrecent, true);
	WriteBool(L"JLTasks", Source_struct.JLtasks, true);
	WriteBool(L"IconOverlay", Source_struct.Overlay, true);
	WriteBool(L"Progress", Source_struct.Progressbar, false);
	WriteInt(L"Revert", Source_struct.Revertto, BG_TRANSPARENT);
	WriteBool(L"ShrinkFrame", Source_struct.Shrinkframe, false);
	WriteBool(L"StoppedStatusOn", Source_struct.Stoppedstatus, true);
	WriteBool(L"StreamStatusOn", Source_struct.Streamstatus, true);
	WriteString(L"Text", Source_struct.Text, L"%c%%s%%curpl% of %totalpl%."
				L"\\r%c%%s%%title%\\r%c%%s%%artist%\\r\\r%c%%s"
				L"%%curtime%/%totaltime%\\r%c%%s%Track #: "
				L"%track%        Volume: %volume%%");
	WriteInt(L"ThumbnailBG", Source_struct.Thumbnailbackground, BG_WINAMP);
	WriteBool(L"ThumbnailButtons", Source_struct.Thumbnailbuttons, true);
	WriteBool(L"ThumbnailPB", Source_struct.Thumbnailpb, false);
#ifdef USE_MOUSE
	WriteBool(L"VolumeControl", Source_struct.VolumeControl, false);
#endif
	WriteBool(L"VUMeter", Source_struct.VuMeter, false);
	WriteBool(L"LowFrameRate", Source_struct.LowFrameRate, false);
	WriteInt(L"LastTab", Source_struct.LastTab, 0);
	WriteInt(L"IconSize", Source_struct.IconSize, 50);
	WriteInt(L"IconPosition", Source_struct.IconPosition, IP_UPPERLEFT);
	WriteInt(L"BG_Transparency", Source_struct.BG_Transparency, 80);

	// Encoding bool[16] Buttons
	std::wstringstream s;
	for (int i = 0; i != 16; ++i)
	{
		s << Source_struct.Buttons[i];
	}
	WriteString(L"Buttons", s.str(), L"1111100000000000");

	// Font
	currentSection = SECTION_NAME_FONT;

	const LOGFONT ft = {-13, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, 0, L"Segoe UI"};
	if (memcmp(&ft, &Source_struct.font, sizeof(LOGFONT)))
	{
		WritePrivateProfileStructW(currentSection.c_str(), L"font", (LPVOID)(&Source_struct.font),
								   sizeof(Source_struct.font), SettingsFile.c_str());
	}
	else
	{
		WritePrivateProfileStructW(currentSection.c_str(), L"font",
								   NULL, 0, SettingsFile.c_str());
	}

	WriteInt(L"color", Source_struct.text_color, RGB(255, 255, 255));
	WriteInt(L"bgcolor", Source_struct.bgcolor, RGB(0, 0, 0));

	// now we see if the file remaining is empty as
	// there's no point in keeping an empty file...
	if (PathFileExists(SettingsFile.c_str()))
	{
		HANDLE hFile = INVALID_HANDLE_VALUE;
		DWORD size = INVALID_FILE_SIZE;
		if ((hFile = CreateFile(SettingsFile.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING,
								FILE_ATTRIBUTE_NORMAL, NULL)) != INVALID_HANDLE_VALUE)
		{
			size = GetFileSize(hFile, NULL);
			CloseHandle(hFile);
		}

		if (!size)
		{
			DeleteFile(SettingsFile.c_str());
		}
	}
}

void SettingsManager::WriteSettings_ToForm(HWND hwnd, HWND WinampWnd, const sSettings &Settings)
{
	//Aero peek settings
	switch (Settings.Thumbnailbackground)
	{
		case BG_TRANSPARENT:
		{
			SendMessage(GetDlgItem(hwnd, IDC_RADIO1), (UINT)BM_SETCHECK, BST_CHECKED, 0);
			break;
		}
		case BG_ALBUMART:
		{
			SendMessage(GetDlgItem(hwnd, IDC_RADIO2), (UINT)BM_SETCHECK, BST_CHECKED, 0);
			break;
		}
		case BG_CUSTOM:
		{
			SendMessage(GetDlgItem(hwnd, IDC_RADIO3), (UINT)BM_SETCHECK, BST_CHECKED, 0);
			break;
		}
		case BG_WINAMP:
		{
			SendMessage(GetDlgItem(hwnd, IDC_RADIO9), (UINT)BM_SETCHECK, BST_CHECKED, 0);
			// this is so when the main mode is toggled that the image has something valid
			SendMessage(GetDlgItem(hwnd, IDC_RADIO2), (UINT)BM_SETCHECK, BST_CHECKED, 0);
			break;
		}
	}

	if (Settings.Thumbnailbackground != BG_WINAMP)
	{
		SendMessage(GetDlgItem(hwnd, IDC_RADIO10), (UINT)BM_SETCHECK, BST_CHECKED, 0);
	}

	switch (Settings.IconPosition)
	{
		case IP_UPPERLEFT:
		{
			SendMessage(GetDlgItem(hwnd, IDC_RADIO4), (UINT)BM_SETCHECK, BST_CHECKED, 0);
			break;
		}
		case IP_LOWERLEFT:
		{
			SendMessage(GetDlgItem(hwnd, IDC_RADIO7), (UINT)BM_SETCHECK, BST_CHECKED, 0);
			break;
		}
		case IP_UPPERRIGHT:
		{
			SendMessage(GetDlgItem(hwnd, IDC_RADIO6), (UINT)BM_SETCHECK, BST_CHECKED, 0);
			break;
		}
		case IP_LOWERRIGHT:
		{
			SendMessage(GetDlgItem(hwnd, IDC_RADIO8), (UINT)BM_SETCHECK, BST_CHECKED, 0);
			break;
		}
	}

	SendMessage(GetDlgItem(hwnd, IDC_COMBO1), CB_ADDSTRING, 0, (LPARAM)WASABI_API_LNGSTRINGW(IDS_TRANSPARENT));
	SendMessage(GetDlgItem(hwnd, IDC_COMBO1), CB_ADDSTRING, 0, (LPARAM)WASABI_API_LNGSTRINGW(IDS_ALBUM_ART));
	SendMessage(GetDlgItem(hwnd, IDC_COMBO1), CB_ADDSTRING, 0, (LPARAM)WASABI_API_LNGSTRINGW(IDS_CUSTOM_BACKGROUND));

	SendMessage(GetDlgItem(hwnd, IDC_COMBO1), CB_SETCURSEL, Settings.Revertto, 0);

	//Player control buttons
	SendMessage(GetDlgItem(hwnd, IDC_CHECK6), (UINT) BM_SETCHECK, Settings.Thumbnailbuttons, 0);

	//Progressbar
	SendMessage(GetDlgItem(hwnd, IDC_CHECK2), (UINT) BM_SETCHECK, Settings.Progressbar, 0);

	//Streamstatus
	SendMessage(GetDlgItem(hwnd, IDC_CHECK4), (UINT) BM_SETCHECK, Settings.Streamstatus, 0);

	//Stoppedstatus
	SendMessage(GetDlgItem(hwnd, IDC_CHECK5), (UINT) BM_SETCHECK, Settings.Stoppedstatus, 0);

	//Overlay
	SendMessage(GetDlgItem(hwnd, IDC_CHECK3), (UINT) BM_SETCHECK, Settings.Overlay, 0);

	//antialias
	SendMessage(GetDlgItem(hwnd, IDC_CHECK8), (UINT) BM_SETCHECK, Settings.Antialias, 0);

	//shrinkframe
	SendMessage(GetDlgItem(hwnd, IDC_CHECK1), (UINT) BM_SETCHECK, Settings.Shrinkframe, 0);

	//Show image as icon
	SendMessage(GetDlgItem(hwnd, IDC_CHECK25), (UINT) BM_SETCHECK, Settings.AsIcon, 0);

	//VU Meter
	SendMessage(GetDlgItem(hwnd, IDC_CHECK26), (UINT) BM_SETCHECK, Settings.VuMeter, 0);

	//Thumbnail pb
	SendMessage(GetDlgItem(hwnd, IDC_CHECK29), (UINT) BM_SETCHECK, Settings.Thumbnailpb, 0);

	//Add 2 recent
	SendMessage(GetDlgItem(hwnd, IDC_CHECK_A2R), (UINT) BM_SETCHECK, !Settings.Add2RecentDocs, 0);

	SendMessage(GetDlgItem(hwnd, IDC_CHECK30), (UINT) BM_SETCHECK, Settings.JLrecent, 0);
	SendMessage(GetDlgItem(hwnd, IDC_CHECK31), (UINT) BM_SETCHECK, Settings.JLfrequent, 0);
	SendMessage(GetDlgItem(hwnd, IDC_CHECK32), (UINT) BM_SETCHECK, Settings.JLtasks, 0);
	SendMessage(GetDlgItem(hwnd, IDC_CHECK33), (UINT) BM_SETCHECK, Settings.JLbms, 0);
	SendMessage(GetDlgItem(hwnd, IDC_CHECK34), (UINT) BM_SETCHECK, Settings.JLpl, 0);

#ifdef USE_MOUSE
	SendMessage(GetDlgItem(hwnd, IDC_CHECK35), (UINT) BM_SETCHECK, Settings.VolumeControl, 0);
#endif

	SendMessage(GetDlgItem(hwnd, IDC_CHECK36), (UINT) BM_SETCHECK, static_cast<WPARAM>(Settings.LowFrameRate), 0);

	//Trackbar
	SendMessage(GetDlgItem(hwnd, IDC_SLIDER1), TBM_SETPOS, TRUE, Settings.IconSize);
	SendMessage(GetDlgItem(hwnd, IDC_SLIDER_TRANSPARENCY), TBM_SETPOS, TRUE, Settings.BG_Transparency);

	std::wstringstream size;
	size << "Icon size (" << SendMessage(GetDlgItem(hwnd, IDC_SLIDER1), TBM_GETPOS, NULL, NULL) << "%)";
	SetWindowTextW(GetDlgItem(hwnd, IDC_ICONSIZE), size.str().c_str());

	size.str(L"");
	size << SendMessage(GetDlgItem(hwnd, IDC_SLIDER_TRANSPARENCY), TBM_GETPOS, NULL, NULL) << "%";
	SetWindowTextW(GetDlgItem(hwnd, IDC_TRANSPARENCY_PERCENT), size.str().c_str());

	SetWindowText(GetDlgItem(hwnd, IDC_EDIT2), Settings.BGPath);

	std::wstring tmpbuf = Settings.Text;
	std::wstring::size_type pos = std::wstring::npos;
	do 
	{
		pos = tmpbuf.find(L"\\r");
		if (pos != std::wstring::npos)
		{
			tmpbuf.replace(pos, 2, L"\r\n");
		}
	}
	while (pos != std::wstring::npos);

	SetWindowText(GetDlgItem(hwnd, IDC_EDIT3), tmpbuf.c_str());

	EnableWindow(GetDlgItem(hwnd, IDC_CHECK4), Settings.Progressbar);
	EnableWindow(GetDlgItem(hwnd, IDC_CHECK5), Settings.Progressbar);
	EnableWindow(GetDlgItem(hwnd, IDC_CHECK27), Settings.Thumbnailbuttons);
}

void SettingsManager::WriteButtons(std::vector<int> &tba)
{
	if (tba.size() > 7)
	{
		tba.resize(7);
	}

	std::wstringstream button_TextStream;

	for (size_t i = 0; i != tba.size(); ++i)
	{
		button_TextStream << tba[i] << ",";
	}

	std::wstring button_Text = button_TextStream.str();
	button_Text.erase(button_Text.length() - 1, 1);

	if (lstrcmp(button_Text.c_str(), L"1300,1301,1302,1303,1308,1314"))
	{
		WritePrivateProfileString(SECTION_NAME_GENERAL, L"ThumbButtons",
								  button_Text.c_str(), SettingsFile.c_str());
	}
	else
	{
		WritePrivateProfileStringW(SECTION_NAME_GENERAL, L"ThumbButtons",
								   NULL, SettingsFile.c_str());
	}
}