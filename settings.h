#ifndef settings_h__
#define settings_h__

#include <Windows.h>
#include <string>
#include <sstream>
#include <vector>
#include "gen_win7shell.h"
#include <loader/loader/paths.h>

class SettingsManager
{
public:
	explicit SettingsManager() : currentSection(SECTION_NAME_GENERAL) {}

	void ReadSettings(sSettings &Destination_struct, std::vector<int> &tba);
	void WriteSettings(const sSettings &Source_struct);

	static void WriteSettings_ToForm(HWND hwnd, const sSettings &Settings);
	static void WriteButtons(std::vector<int> &tba);

	int GetInt(const std::wstring &key, const int default_value) const;
	bool GetBool(const std::wstring &key, const bool default_value) const;
	void GetString(wchar_t* output, const size_t output_len, const std::wstring &key,
											const std::wstring &default_value) const;

	// all of these compare against the default value to prevent
	// saving the value to the settings file if there's no need.
	void WriteInt(const std::wstring &key, const int value, const int default_value) const;
	void WriteBool(const std::wstring &key, const bool value, const bool default_value) const;
	void WriteString(const std::wstring &key, const std::wstring &value, const std::wstring &default_value) const;

private:
	std::wstring currentSection;
};

#endif // settings_h__