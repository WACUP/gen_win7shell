#include <windows.h>
#include <windowsx.h>
#include <string>
#include <vector>
#include <sstream>

#include "gen_win7shell.h"
#include "resource.h"
#include "api.h"
#include "sdk/winamp/wa_ipc.h"
#include "lines.h"
#include "tools.h"
#include <loader/loader/utils.h>

lines::lines(sSettings &Settings, MetaData &Metadata) :
			 m_settings(Settings), m_metadata(Metadata) {}

std::wstring lines::MetaWord(const std::wstring &word, linesettings&current_line_settings,
							 void **token, bool* reentrant, bool* already_tried, INT_PTR *db_error)
{
	if (word == L"d")
	{
		current_line_settings.dontscroll = true;
		return L"";
	}
	else if (word == L"c")
	{
		current_line_settings.center = true;
		return L"";
	}
	else if (word == L"s")
	{
		current_line_settings.shadow = true;
		return L"";
	}
	else if (word == L"f")
	{
		current_line_settings.forceleft = true;
		return L"";
	}
	else if (word == L"l")
	{
		current_line_settings.largefont = true;
		return L"";
	}
	else if (word == L"b")
	{
		current_line_settings.darkbox = true;
		return L"";
	}
	else if (word == L"curtime")
	{
		const int res = m_settings.play_current;
		if (res == -1)
		{
			return L"~";
		}
		return tools::SecToTime(res / 1000);
	}
	else if (word == L"timeleft")
	{
		const int cur = m_settings.play_current, tot = m_settings.play_total;
		if (cur == -1 || tot <= 0)
		{
			return L"~";
		}
		return tools::SecToTime((tot - cur) / 1000);
	}
	else if (word == L"totaltime")
	{
		const int tot = m_settings.play_total;
		if (tot <= 0)
		{
			return L"~";
		}
		return tools::SecToTime(tot / 1000);
	}
	else if (word == L"kbps")
	{
		const int inf = m_settings.play_kbps;
		if (!inf)
		{
			return L"~";
		}

		wchar_t str[16]/* = { 0 }*/;
		I2WStr(inf, str, ARRAYSIZE(str));
		return str;
	}
	else if (word == L"khz")
	{
		const int inf = m_settings.play_khz;
		if (!inf)
		{
			return L"~";
		}

		wchar_t str[16]/* = { 0 }*/;
		I2WStr(inf, str, ARRAYSIZE(str));
		return str;
	}
	else if (word == L"volume")
	{
		wchar_t str[16]/* = { 0 }*/;
		I2WStr(((m_settings.play_volume * 100) / 255), str, ARRAYSIZE(str));
		return str;
	}
	else if (word == L"shuffle")
	{
		static wchar_t tmp[8];
		return LngStringCopy((m_settings.state_shuffle ? IDS_ON : IDS_OFF), tmp, 8);
	}
	else if (word == L"repeat")
	{
		static wchar_t tmp[8];
		return LngStringCopy((repeat ? IDS_ON : IDS_OFF), tmp, 8);
	}
	else if (word == L"curpl")
	{
		wchar_t str[16]/* = { 0 }*/;
		I2WStr((m_settings.play_playlistlen ? m_settings.play_playlistpos + 1 : 0), str, ARRAYSIZE(str));
		return str;
	}
	else if (word == L"totalpl")
	{
		wchar_t str[16]/* = { 0 }*/;
		I2WStr(m_settings.play_playlistlen, str, ARRAYSIZE(str));
		return str;
	}
	else
	{
		const bool rating1 = (word == L"rating1");
		if (rating1 || word == L"rating2")
		{
			const int rating = (int)GetSetMainRating(0, IPC_GETRATING);
			if (rating1)
			{
				wchar_t str[16]/* = { 0 }*/;
				I2WStr(rating, str, ARRAYSIZE(str));
				return str;
			}
			else
			{
				std::wstringstream w;
				for (int i = 0; i < rating; i++)
				{
					w << L"\u2605";
				}

				for (int i = 0; i < 5 - rating; i++)
				{
					w << L"\u2606";
				}
				return w.str();
			}
		}

		return m_metadata.getMetadata(word, token, reentrant, already_tried, db_error);
	}
}

void lines::Parse()
{
	// these are used to help minimise the impact of directly
	// querying the file for multiple pieces of metadata &/or
	// if there's an issue with the local library db handling
	INT_PTR db_error = FALSE;
	bool reentrant = false, already_tried = false;
	void *token = NULL;

	m_linesettings.clear();
	m_texts.clear();

	// parse all text
	std::wstring::size_type pos = 0;
	std::wstring text = m_settings.Text;
	do 
	{
		// cppcheck-suppress nonStandardCharLiteral
		#pragma warning(disable:4066)
		std::wstring::size_type pos_2 = text.find_first_of(L'\\r', pos);
		#pragma warning(default:4066)
		if (pos_2 == std::wstring::npos)
		{
			m_texts.push_back(text.substr(pos));
			break;
		}

		m_texts.push_back(text.substr(pos, pos_2 - pos));
		pos = pos_2 + 2;
	}
	while (pos != std::wstring::npos);

	// replace text formatting tags
	for (std::size_t index = 0; index != m_texts.size(); ++index)
	{
		linesettings current_line_settings = {0};

		std::wstring::size_type meta_pos = 0;
		do
		{
			meta_pos = m_texts[index].find_first_of(L'%', meta_pos);

			if (meta_pos != std::wstring::npos)
			{
				if (meta_pos != 0 && m_texts[index][meta_pos - 1] == L'\\')
				{
					m_texts[index].erase(meta_pos - 1, 1);
					continue;
				}

				std::wstring::size_type meta_pos2 = m_texts[index].find_first_of(L'%', meta_pos + 1);
				if (meta_pos2 != std::wstring::npos)
				{
					// determine the %xxx% field to be processed
					const std::wstring metaword = m_texts[index].substr(meta_pos + 1, (meta_pos2 - (meta_pos + 1))),
									   parsed = (!metaword.empty() ? MetaWord(metaword, current_line_settings,
														&token, &reentrant, &already_tried, &db_error) : L"");
					m_texts[index].replace(meta_pos, (metaword.size() + 2), parsed);

					// and then if it's a non-formatting field
					// we move the position on to allow for %
					// characters in the obtained text to not
					// be treated as a potential %xxx% field.
					if (!parsed.empty())
					{
						meta_pos += (parsed.size() + 1);
					}
				}
				else
				{
					++meta_pos;
				}
			}
		}
		while (meta_pos != std::wstring::npos);

		m_linesettings.push_back(current_line_settings);
	}

	plugin.metadata->FreeExtendedFileInfoToken(&token);
}