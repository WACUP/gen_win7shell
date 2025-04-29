#include "gen_win7shell.h"
#include <map>
#include <ctype.h>
#include "metadata.h"
#include <sdk/winamp/wa_ipc.h>
#include <sdk/winamp/gen.h>
#include <loader/loader/utils.h>
#include "api.h"

extern winampGeneralPurposePlugin plugin;

void MetaData::reset(LPCWSTR filename, const bool force)
{
	if (force || (filename != mfilename))
	{
		cache.clear();
		mfilename = SafeWideDupFreeOld(filename, mfilename);
	}
}

std::wstring MetaData::getMetadata(const std::wstring &tag, void **token,
								   bool* reentrant, bool* already_tried,
								   INT_PTR *db_error)
{
	if (!cache.empty() && cache.find(tag) != cache.end())
	{
		return cache.find(tag)->second;
	}

	if (mfilename && *mfilename)
	{
		wchar_t buffer[GETFILEINFO_TITLE_LENGTH]/* = { 0 }*/;
		buffer[0] = 0;

		// cache the response as long as we got a valid result
		GetFileMetaData(mfilename, tag.c_str(), buffer, ARRAYSIZE(buffer),
							   token, reentrant, already_tried, db_error);
		if (buffer[0])
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
				LPCWSTR playing_title = GetPlayingTitle(0);
				if (!playing_title || !*playing_title)
				{
					return L"";
				}
				ret = playing_title;

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
	return L"";
}

LPCWSTR MetaData::getFileName(void) const
{
	return (mfilename && *mfilename ? mfilename : L"");
}