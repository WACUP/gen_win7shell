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
		if (mfilename)
		{
			plugin.memmgr->sysFree(mfilename);
		}
		mfilename = plugin.memmgr->sysDupStr((wchar_t*)filename);
	}
}

std::wstring MetaData::getMetadata(const std::wstring &tag)
{
	if (!cache.empty() && cache.find(tag) != cache.end())
	{
		return cache.find(tag)->second;
	}

	if (mfilename && *mfilename)
	{
		wchar_t buffer[GETFILEINFO_TITLE_LENGTH] = { 0 };
		// cache the response as long as we got a valid result
		extendedFileInfoStructW efis = { mfilename, tag.c_str(), buffer, ARRAYSIZE(buffer) };
		if (GetFileInfoHookable((WPARAM)&efis, TRUE, NULL, NULL) && buffer[0])
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
	return L"";
}

LPCWSTR MetaData::getFileName(void) const
{
	return (mfilename && *mfilename ? mfilename : L"");
}