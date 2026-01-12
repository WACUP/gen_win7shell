#include <windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <windowsx.h>
#include <shlwapi.h>
#include <commctrl.h>
#include "settings.h"
#include "gen_win7shell.h"
#include "api.h"
#include "resource.h"
#include <loader/loader/utils.h>
#include <loader/loader/ini.h>

SettingsManager::SettingsManager()
{
	// now we see if the file remaining is empty as
	// there's no point in keeping an empty file...
	LPCWSTR settings_file = GetPaths()->win7shell_ini_file;
	if (FileExists(settings_file) && !GetFileSizeByPath(settings_file))
	{
		RemoveFile(settings_file);
	}
}

int SettingsManager::GetInt(const bool section, const std::wstring &key, const int default_value) const
{
	return GetNativeIniInt(WIN7SHELL_INI, (!section ? SECTION_NAME_GENERAL :
							SECTION_NAME_FONT), key.c_str(), default_value);
}

bool SettingsManager::GetBool(const bool section, const std::wstring &key, const bool default_value) const
{
	return GetNativeIniBool(WIN7SHELL_INI, (!section ? SECTION_NAME_GENERAL :
							 SECTION_NAME_FONT), key.c_str(), default_value);
}

void SettingsManager::GetString(const bool section, wchar_t *output, const size_t output_len, const
								std::wstring &key, const std::wstring &default_value) const
{
	GetNativeIniString(WIN7SHELL_INI, (!section ? SECTION_NAME_GENERAL : SECTION_NAME_FONT),
							 key.c_str(), default_value.c_str(), output, (DWORD)output_len);
}

void SettingsManager::WriteInt(const bool section, const std::wstring &key,
							   const int value, const int default_value) const
{
	if (value != default_value)
	{
		SaveNativeIniInt(WIN7SHELL_INI, (!section ? SECTION_NAME_GENERAL :
								  SECTION_NAME_FONT), key.c_str(), value);
	}
	else
	{
		SaveNativeIniString(WIN7SHELL_INI, (!section ? SECTION_NAME_GENERAL :
									  SECTION_NAME_FONT), key.c_str(), NULL);
	}
}

void SettingsManager::WriteBool(const bool section, const std::wstring &key,
								const bool value, const bool default_value) const
{
	SaveNativeIniString(WIN7SHELL_INI, (!section ? SECTION_NAME_GENERAL : SECTION_NAME_FONT),
					 key.c_str(), ((value != default_value) ? (value ? L"1" : L"0") : NULL));
}

void SettingsManager::WriteString(const bool section, const std::wstring &key, const std::wstring &value,
								  const std::wstring &default_value) const
{	
	SaveNativeIniStringUTF8(WIN7SHELL_INI, (!section ? SECTION_NAME_GENERAL : SECTION_NAME_FONT),
								 key.c_str(), ((value != default_value) ? value.c_str() : NULL));
}

void SettingsManager::ReadSettings(sSettings &Destination_struct, std::vector<int> &tba)
{
	// Read all values from .ini file to Destination_struct
	Destination_struct.Add2RecentDocs = GetBool(false, L"Add2RecentDocs", true);
	Destination_struct.Antialias = GetBool(false, L"AntiAlias", true);
	Destination_struct.AsIcon = GetBool(false, L"AsIcon", true);
	GetString(false, Destination_struct.BGPath, ARRAYSIZE(Destination_struct.BGPath), L"BGPath", L"");
	Destination_struct.JLbms = GetBool(false, L"JLBookMarks", true);
	Destination_struct.JLfrequent = GetBool(false, L"Frequent", false);
	Destination_struct.JLpl = GetBool(false, L"JLPlayList", true);
	Destination_struct.JLrecent = GetBool(false, L"Recent", false);
	Destination_struct.JLtasks = GetBool(false, L"JLTasks", true);
	Destination_struct.Overlay = GetBool(false, L"IconOverlay", true);
	Destination_struct.Progressbar = GetBool(false, L"Progress", false);
	Destination_struct.Revertto = GetInt(false, L"Revert", BG_TRANSPARENT);
	Destination_struct.Shrinkframe = GetBool(false, L"ShrinkFrame", false);
	Destination_struct.Stoppedstatus = GetBool(false, L"StoppedStatusOn", true);
	Destination_struct.Streamstatus = GetBool(false, L"StreamStatusOn", true);
	GetString(false, Destination_struct.Text, ARRAYSIZE(Destination_struct.Text), L"Text", L"‡");

	if (SameStr(Destination_struct.Text, L"‡"))
	{
		CopyCchStr(Destination_struct.Text, ARRAYSIZE(Destination_struct.Text),
				   L"%c%%s%%curpl% of %totalpl%.\\r%c%%s%%title%"
				   L"\\r%c%%s%%artist%\\r\\r%c%%s%%curtime%/%totaltime%"
				   L"\\r%c%%s%Track #: %track%        Volume: %volume%%");
	}

	Destination_struct.Thumbnailbackground = (GetInt(false, L"ThumbnailBG", BG_WINAMP) & 0xF);
	Destination_struct.Thumbnailbuttons = GetBool(false, L"ThumbnailButtons", true);
	Destination_struct.Thumbnailpb = GetBool(false, L"ThumbnailPB", false);
#ifdef USE_MOUSE
	Destination_struct.VolumeControl = GetBool(false, L"VolumeControl", false);
#endif
	Destination_struct.VuMeter = GetBool(false, L"VUMeter", false);
	Destination_struct.LowFrameRate = GetBool(false, L"LowFrameRate", false);
	Destination_struct.IconSize = GetInt(false, L"IconSize", 50);
	Destination_struct.IconPosition = GetInt(false, L"IconPosition", IP_UPPERLEFT);
	Destination_struct.BG_Transparency = GetInt(false, L"BG_Transparency", 80);

	Destination_struct.MFT = GetInt(false, L"MFT", 200);
	Destination_struct.MST = GetInt(false, L"MST", 500);
	Destination_struct.TFT = GetInt(false, L"TFT", 33);
	Destination_struct.TST = GetInt(false, L"TST", 66);

	// Read font
	if (!GetNativeIniStruct(WIN7SHELL_INI, SECTION_NAME_FONT, L"font",
		   &Destination_struct.font, sizeof(Destination_struct.font)))
	{
		CopyCchStr(Destination_struct.font.lfFaceName, ARRAYSIZE(
				   Destination_struct.font.lfFaceName), L"Segoe UI");
		Destination_struct.font.lfHeight = -23;
		Destination_struct.font.lfWeight = FW_NORMAL;
	}

	Destination_struct.text_color = GetInt(true, L"color", RGB(255, 255, 255));
	Destination_struct.bgcolor = GetInt(true, L"bgcolor", RGB(0, 0, 0));

	// if something went wrong with the config then we can try to correct it
	if ((Destination_struct.text_color == 1) && !Destination_struct.bgcolor)
	{
		Destination_struct.text_color = RGB(255, 255, 255);
	}

	// Read buttons
	std::wstring text;
	text.resize(100);
	const DWORD len = GetNativeIniString(WIN7SHELL_INI, SECTION_NAME_GENERAL,
										 L"ThumbButtons", L"~", &text[0], 99);
	if (len > 0)
	{
		text.resize(len);
	}

	tba.clear();

	if (!text.empty() && !SameStr(text.c_str(), L"~"))
	{
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
			text.erase(0, pos + 1);
		} while (pos != std::wstring::npos);
	}

	// deal with no prior config so the default can
	// be shown instead of it showing nothing as it
	// can do if the user unchecked all the buttons
	if (SameStr(text.c_str(), L"~") && tba.empty())
	{
		tba.push_back(1300);
		tba.push_back(1301);
		tba.push_back(1302);
		tba.push_back(1303);
		tba.push_back(1308);
		tba.push_back(1314);
	}
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
	if (!button_Text.empty())
	{
		button_Text.erase(button_Text.length() - 1, 1);
	}

	SaveNativeIniString(WIN7SHELL_INI, SECTION_NAME_GENERAL, L"ThumbButtons", (!SameStr(button_Text.c_str(),
										   L"1300,1301,1302,1303,1308,1314") ? button_Text.c_str() : NULL));
}