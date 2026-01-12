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
	explicit SettingsManager();

	void ReadSettings(sSettings &Destination_struct, std::vector<int> &tba);

	static void WriteButtons(std::vector<int> &tba);

	int GetInt(const bool section, const std::wstring &key, const int default_value) const;
	bool GetBool(const bool section, const std::wstring &key, const bool default_value) const;
	void GetString(const bool section, wchar_t* output, const size_t output_len,
				   const std::wstring &key, const std::wstring &default_value) const;

	// all of these compare against the default value to prevent
	// saving the value to the settings file if there's no need.
	void WriteInt(const bool section, const std::wstring &key, const int value, const int default_value) const;
	void WriteBool(const bool section, const std::wstring &key, const bool value, const bool default_value) const;
	void WriteString(const bool section, const std::wstring &key, const std::wstring &value, const std::wstring &default_value) const;
};

#endif // settings_h__