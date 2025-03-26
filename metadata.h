#ifndef metadata_h__
#define metadata_h__

#include <windows.h>
#include <string>
#include <map>

class MetaData
{
public:
	MetaData() : mfilename(NULL) {}
	void reset(LPCWSTR filename, const bool force = false);
	std::wstring getMetadata(const std::wstring &tag, void **token,
							 bool* reentrant, bool* already_tried,
							 INT_PTR *db_error);
	LPCWSTR getFileName(void) const;

private:
	std::map<std::wstring, std::wstring> cache;
	wchar_t *mfilename;
};

#endif // metadata_h__