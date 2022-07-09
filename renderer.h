#ifndef renderer_h__
#define renderer_h__

#include <windows.h>
#include <GdiPlus.h>
#include <vector>
#include <string>
#include "gen_win7shell.h"
#include "metadata.h"
#include "lines.h"

class renderer
{
public:
	renderer(sSettings& settings, MetaData &metadata);
	~renderer();

	bool getAlbumArt(const std::wstring &fname/*, int& iconheight, int& iconwidth*/);
	typedef unsigned long ARGB32;
	void createArtwork(const int cur_w, const int cur_h, ARGB32 *cur_image);

	HBITMAP GetThumbnail();
	void ClearAlbumart();
	void ClearBackground();
	void ClearCustomBackground();
	void ClearFonts();
	void ThumbnailPopup();
	void SetDimensions(const int new_w, const int new_h);

	MetaData GetMetadata(void) { return m_metadata; }

private:
	ULONG_PTR gdiplusToken, gdiplusBgThreadToken;

	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartupOutput gdiplusStartupOutput;

	Gdiplus::Image *custom_img;
	Gdiplus::Bitmap *background;
	Gdiplus::Bitmap *albumart;

	Gdiplus::Font *normal_font;
	Gdiplus::Font *large_font;

	sSettings &m_settings;
	MetaData &m_metadata;

	int m_width, m_height, m_iconwidth, m_iconheight,
		m_textpause, _iconwidth, _iconheight;
	std::vector<int> m_textpositions;
	bool no_icon, fail, scroll_block, no_text;

	bool render(void);
};

#endif // renderer_h__