#ifndef metadata_h__
#define metadata_h__

#include <windows.h>
#include <string>
#include <map>

class MetaData
{
public:
	MetaData() : mfilename(L"")/*, m_play_count(0)*/ {}
	bool reset(const std::wstring &filename, const bool force = false);
	std::wstring getMetadata(const std::wstring &tag);
	std::wstring getFileName() const;
	//bool CheckPlayCount();

private:
	std::map<std::wstring, std::wstring> cache;
	std::wstring mfilename;
	//int m_play_count;
};

#endif // metadata_h__