#ifndef lines_h__
#define lines_h__

#include <string>
#include <vector>
#include "gen_win7shell.h"
#include "metadata.h"

class lines
{
public:
	lines(sSettings &Settings, MetaData &Metadata);

	std::wstring::size_type GetNumberOfLines() const { return m_texts.size(); }
	std::wstring GetLineText(const size_t index) const { return m_texts[index]; }
	linesettings GetLineSettings(const size_t index) const { return m_linesettings[index]; }

	void Parse();

private:
	std::wstring MetaWord(const std::wstring &word, linesettings&current_line_settings,
						  void **token, bool* reentrant, bool* already_tried, INT_PTR *db_error);

	std::vector<std::wstring> m_texts;
	std::vector<linesettings> m_linesettings;
	sSettings &m_settings;
	MetaData &m_metadata;
};

#endif // lines_h__