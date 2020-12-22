#include "gen_win7shell.h"
#include <map>
#include <ctype.h>
#include "metadata.h"
#include <sdk/winamp/wa_ipc.h>
#include <loader/loader/utils.h>

bool MetaData::reset(const std::wstring &filename, const bool force)
{
	if (force || (filename != mfilename))
	{
		cache.clear();
		mfilename = filename;
		return true;
	}

	return false;
}

std::wstring MetaData::getMetadata(const std::wstring &tag)
{
	if (!cache.empty() && cache.find(tag) != cache.end())
	{
		return cache.find(tag)->second;
	}

	wchar_t buffer[2048] = {0};
	extendedFileInfoStructW exFIS = {0};
	exFIS.filename = _wcsdup(mfilename.c_str());
	exFIS.metadata = _wcsdup(tag.c_str());
	exFIS.ret = buffer;
	exFIS.retlen = ARRAYSIZE(buffer);

	// cache the response as long as we got a valid result
	const int efiWret = !!SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)&exFIS,
									  IPC_GET_EXTENDED_FILE_INFOW_HOOKABLE);

	free((void *)exFIS.filename);
	free((void *)exFIS.metadata);

	if (efiWret && buffer[0])
	{
		cache[tag] = buffer;
		return buffer;
	}
	else
	{
		std::wstring ret;
		const bool artist = (tag == L"artist"), title = (tag == L"title");
		if (title || artist)
		{
			ret = GetPlayingTitle(0);
			if (ret.empty())
			{
				return L"";
			}

			size_t pos = ret.find_first_of('-');
			if (pos != std::wstring::npos && pos != 0)
			{
				if (title)
				{
					ret = std::wstring(ret, pos + 1, ret.length() - (pos - 2));
					if (ret[0] == L' ')
					{
						ret.erase(0, 1);
					}
				}
				else if (artist)
				{
					ret = std::wstring(ret, 0, pos);
					--pos;
					if (ret[pos] == L' ')
					{
						ret.erase(pos, 1);
					}
				}
			}
			else
			{
				if (artist)
				{
					return L"";
				}
			}
		}
		else
		{
			return L"";
		}

		cache[tag] = ret;
		return ret;
	}
}

std::wstring MetaData::getFileName() const
{
	return mfilename;
}

bool MetaData::CheckPlayCount()
{
	if (m_play_count > 50)
	{
		m_play_count = 0;
		return true;
	}

	++m_play_count;
	return false;
}