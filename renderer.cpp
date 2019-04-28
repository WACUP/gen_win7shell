#include <Windows.h>
#include <string>
#include <vector>
#include <gdiplus.h>
#include "renderer.h"
#include "gen_win7shell.h"
#include "api.h"
#include "lines.h"
#include <dwmapi.h>

Gdiplus::Bitmap* ResizeAncCloneBitmap(Gdiplus::Bitmap *bmp, const float width, const float height)
{
	const UINT o_height = bmp->GetHeight(),
			   o_width = bmp->GetWidth();
	const float ratio = ((float)o_width) / ((float)o_height);
	float n_width = width, n_height = height;

	if (o_width > o_height)
	{
		n_height = (n_width / ratio);
	}
	else
	{
		n_width = (n_height * ratio);
	}

	Gdiplus::Bitmap* newBitmap = new Gdiplus::Bitmap(static_cast<int>(n_width),
													 static_cast<int>(n_height),
													 bmp->GetPixelFormat());
	if (newBitmap)
	{
		Gdiplus::Graphics graphics(newBitmap);
		graphics.DrawImage(bmp, static_cast<Gdiplus::REAL>(0),
						   static_cast<Gdiplus::REAL>(0),
						   static_cast<Gdiplus::REAL>(n_width),
						   static_cast<Gdiplus::REAL>(n_height));
	}
	return newBitmap;
}

bool renderer::getAlbumArt(const std::wstring &fname, const int width,
						   const int height, int& iconsize)
{
	if (!albumart)
	{
		if (AGAVE_API_ALBUMART)
		{
			ARGB32 *cur_image = 0;
			int cur_w = 0, cur_h = 0;
			// when running under WACUP this request is cached for us
			// so we don't have to worry too much about it being slow
			if (AGAVE_API_ALBUMART->GetAlbumArt(fname.c_str(), L"cover", &cur_w,
												&cur_h, &cur_image) == ALBUMART_SUCCESS)
			{
				BITMAPINFO bmi = {0};
				bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bmi.bmiHeader.biWidth = cur_w;
				bmi.bmiHeader.biHeight = -cur_h;
				bmi.bmiHeader.biPlanes = 1;
				bmi.bmiHeader.biBitCount = 32;
				bmi.bmiHeader.biCompression = BI_RGB;

				Gdiplus::Bitmap tmpbmp(&bmi, cur_image);

				int iconleft = 0, icontop = 0;

				switch (m_settings.IconPosition)
				{
					case IP_LOWERLEFT:
					{
						icontop = height - iconsize - 2;
						break;
					}
					case IP_UPPERRIGHT:
					{
						iconleft = width - iconsize - 2;
						break;
					}
					case IP_LOWERRIGHT:
					{
						iconleft = width - iconsize - 2;
						icontop = height - iconsize - 2;
						break;
					}
				}

				float new_height = 0.f, new_width = 0.f, anchor = (height * 1.f);

				if (!m_settings.AsIcon)
				{
					if (width < height)
					{
						anchor = (width * 1.f);
					}
				}
				else
				{
					anchor = (iconsize * 1.f);
				}

				if (cur_w > cur_h)
				{
					new_height = (float)cur_h / (float)cur_w * (float)anchor;
					new_height -= 2;
					new_width = anchor;

					if (new_height > height)
					{
						new_width = (float)cur_w / (float)cur_h * (float)anchor;
						new_width -= 2;
						new_height = anchor;
					}
				}
				else
				{
					new_width = (float)cur_w / (float)cur_h * (float)anchor;
					new_width -= 2;
					new_height = anchor;

					if (new_width > width)
					{
						new_height = (float)cur_h / (float)cur_w * (float)anchor;
						new_height -= 2;
						new_width = anchor;
					}
				}

				iconsize = (int)new_width;

				// we don't want to keep doing this as it is going
				// to be very slow especially with >600x600 images
				// so we cache a re-sized image and then draw that
				albumart = ResizeAncCloneBitmap(&tmpbmp, new_width, new_height);

				if (cur_image)
				{
					WASABI_API_MEMMGR->sysFree(cur_image);
				}

				return render(width, height, iconsize);
			}
		}
		return false;
	}
	return render(width, height, iconsize);
}

bool renderer::render(const int width, const int height, const int iconsize)
{
	if (albumart)
	{
		Gdiplus::Graphics gfx(background);

		//Calculate Alpha blend based on Transparency
		float fBlend = (100.0f - m_settings.BG_Transparency) / 100.0f;

		gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);
		gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQuality);
		gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
		gfx.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);

		int iconleft = 0, icontop = 0;

		switch (m_settings.IconPosition)
		{
			case IP_LOWERLEFT:
			{
				icontop = height - iconsize - 2;
				break;
			}
			case IP_UPPERRIGHT:
			{
				iconleft = width - iconsize - 2;
				break;
			}
			case IP_LOWERRIGHT:
			{
				iconleft = width - iconsize - 2;
				icontop = height - iconsize - 2;
				break;
			}
		}

		// Draw icon shadow
		if (!m_settings.AsIcon)
		{
			iconleft = (int)((width / 2) - (m_iconwidth/*new_width*/ / 2));
		}        
		else
		{
			gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			gfx.FillRectangle(&Gdiplus::SolidBrush(Gdiplus::Color::MakeARGB((BYTE)(110 - m_settings.BG_Transparency), 0, 0, 0)),
							  static_cast<Gdiplus::REAL>(iconleft + 1), static_cast<Gdiplus::REAL>(icontop + 1),
							  static_cast<Gdiplus::REAL>(m_iconwidth + 1), static_cast<Gdiplus::REAL>(m_iconheight + 1));
		}

		gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);

		if (m_settings.BG_Transparency == 0)
		{
			gfx.DrawImage(albumart, Gdiplus::RectF(static_cast<Gdiplus::REAL>(iconleft),
						  static_cast<Gdiplus::REAL>(icontop),
						  static_cast<Gdiplus::REAL>(m_iconwidth),
						  static_cast<Gdiplus::REAL>(m_iconheight)));
		}
		else
		{
			const Gdiplus::ColorMatrix BitmapMatrix =
			{
				1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 0.0f, fBlend, 0.0f,
				0.0f, 0.0f, 0.0f, 0.0f, 1.0f
			};
			Gdiplus::ImageAttributes ImgAttr;
			ImgAttr.SetColorMatrix(&BitmapMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);

			gfx.DrawImage(albumart, Gdiplus::RectF(static_cast<Gdiplus::REAL>(iconleft),
						  static_cast<Gdiplus::REAL>(icontop),
						  static_cast<Gdiplus::REAL>(m_iconwidth),
						  static_cast<Gdiplus::REAL>(m_iconheight)), 0, 0,
						  static_cast<Gdiplus::REAL>(iconsize),
						  static_cast<Gdiplus::REAL>(iconsize),
						  Gdiplus::UnitPixel, &ImgAttr);
		}
		return true;
	}
	return false;
}

RECT ScaleArtworkToArea(int w, int h, int cur_w, int cur_h)
{
	// maintain 'square' stretching, fill in dst
	double aspX = (double)(w) / (double)cur_w,
		   aspY = (double)(h) / (double)cur_h,
		   asp = min(aspX, aspY);
	const int newW = (int)(cur_w * asp),
			  newH = (int)(cur_h * asp);

	RECT dst;
	dst.left = (w - newW) / 2;
	dst.top = (h - newH) / 2;
	dst.right = dst.left + newW;
	dst.bottom = dst.top + newH;
	return dst;
}

HBITMAP renderer::GetThumbnail(const bool get_bmp)
{
	//Calculate icon size
	int iconsize = m_height;

	if (m_settings.AsIcon)
	{
		if (m_width < iconsize)
		{
			iconsize = m_width;        
		}

		iconsize = (m_settings.IconSize * iconsize) / 100;
		iconsize -= 2;
	}

	//Calculate Alpha blend based on Transparency
	const float fBlend = (100.f - m_settings.BG_Transparency) / 100.0f;

	Gdiplus::ColorMatrix BitmapMatrix =
	{
		1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, fBlend, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 1.0f
	};

	bool tempfail = fail;
	int iconposition = m_settings.IconPosition;

	// Draw background if not valid
	if (!background)
	{
		if (m_settings.Shrinkframe)
		{
			if (iconposition == IP_LOWERLEFT)
			{
				iconposition = IP_UPPERLEFT;
			}
			else if (m_settings.IconPosition == IP_LOWERRIGHT)
			{
				iconposition = IP_UPPERRIGHT;
			}
		}

		no_icon = no_text = fail = false;
		background = new Gdiplus::Bitmap(m_width, m_height, PixelFormat32bppPARGB);
		Gdiplus::Graphics graphics(background);

		switch (tempfail ? m_settings.Revertto : m_settings.Thumbnailbackground)
		{
			case BG_TRANSPARENT:
			{
				no_icon = true;
				break;
			}
			case BG_WINAMP:
			{
				no_text = true;

				RECT r = {0};
				GetClientRect(dialogParent, &r);

				// see if it's a classic skin and it's in windowshade mode
				// as depending upon the OS, etc this may not be just 14px
				// (or 28px for double-size) so is better to double-check!
				if (classicSkin && windowShade)
				{
					r.bottom = r.top + (doubleSize ? 28 : 14);
				}

				Gdiplus::Bitmap bmp((r.right - r.left), (r.bottom - r.top), PixelFormat32bppPARGB);
				Gdiplus::Graphics gfx(&bmp);

				HDC hdc = gfx.GetHDC();

				gfx.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
				gfx.SetCompositingQuality(Gdiplus::CompositingQualityGammaCorrected);
				gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

				// for most cases just grabbing the window directly
				// is all that's needed and uses less resources but
				// for some (e.g. winamp modern) then we've got to
				// use WM_PRINTCLIENT to better support alpha levels
				// the main downside of WM_PRINTCLIENT is it's slow
				// when used as part of a SUI based modern skin :(
				if (modernSUI)
				{
					HDC hdcWindow = GetWindowDC(dialogParent);
					BitBlt(hdc, 0, 0, bmp.GetWidth(), bmp.GetHeight(), hdcWindow, 0, 0, SRCCOPY);
					ReleaseDC(dialogParent, hdcWindow);
				}
				else
				{
					SendMessage(dialogParent, WM_PRINTCLIENT, (WPARAM)hdc,
								PRF_CHILDREN | PRF_CLIENT | PRF_NONCLIENT);
				}

				gfx.ReleaseHDC(hdc);

				graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
				graphics.SetCompositingQuality(Gdiplus::CompositingQualityGammaCorrected);
				graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

				// find the colour of the almost bottom-right pixel
				// and see if it matches out expected colour from
				// gen_ff for what should be a non-visible section
				// of the skin (but it gets reset / not drawn onto
				// the hdc nicely to make use of the existing alpha)
				const bool doReplace = (!classicSkin && !modernSUI);
				Gdiplus::ImageAttributes ImgAtt;
				Gdiplus::Color queried(255, 0, 0, 0), replace(255, 0, 0, 0);
				if (doReplace)
				{
					bmp.GetPixel(bmp.GetWidth() - 1, bmp.GetHeight() - 1, &queried);
					ImgAtt.SetColorKey(queried, queried, Gdiplus::ColorAdjustTypeBitmap);
				}

				// we draw a re-sized version of the captured window
				// whilst resetting certain pixels to have alpha so
				// that for some modern skins the preview is better
				// than it would otherwise be (due to gen_ff loosing
				// that information when it's put into the hdc *ugg*)
				RECT rt = ScaleArtworkToArea(m_width, m_height, bmp.GetWidth(), bmp.GetHeight());
				Gdiplus::Rect dest(rt.left, rt.top, (rt.right - rt.left), (rt.bottom - rt.top));
				graphics.DrawImage(&bmp, dest, 0, 0, bmp.GetWidth(), bmp.GetHeight(), Gdiplus::UnitPixel,
								   (doReplace ? (replace.ToCOLORREF() == queried.ToCOLORREF() ? &ImgAtt : 0) : 0));
				break;
			}
			case BG_ALBUMART:
			{
				// get album art
				if (!getAlbumArt(m_metadata.getFileName(), m_width, m_height,
								 iconsize) && m_settings.Revertto != BG_ALBUMART)
				{
					// fallback
					fail = true;
					ClearBackground();
					GetThumbnail(true);
				}
				else
				{
					m_iconwidth = iconsize;
					m_iconheight = iconsize;
				}
				break;
			}
			case BG_CUSTOM:
			{
				if (m_settings.BGPath[0])
				{
					if (custom_img == NULL)
					{
						custom_img = new Gdiplus::Bitmap(m_width, m_height, PixelFormat32bppPARGB);

						Gdiplus::Image img(m_settings.BGPath, true);

						if (img.GetType() != 0)
						{
							Gdiplus::Graphics gfx(custom_img);
							gfx.SetInterpolationMode(Gdiplus::InterpolationModeBicubic);
							const float img_width = img.GetWidth() * 1.f,
										img_height = img.GetHeight() * 1.f;

							if (m_settings.AsIcon)
							{
								float new_height = 0.f, new_width = 0.f;

								if (img_width > img_height)
								{
									new_height = img_height / img_width * (float)iconsize;
									new_height -= 2.f;
									new_width = iconsize * 1.f;
								}
								else
								{
									new_width = img_width / img_height * (float)iconsize;
									new_width -= 2.f;
									new_height = iconsize * 1.f;
								}

								int iconleft = 0, icontop = 0;

								switch (iconposition)
								{
									case IP_LOWERLEFT:
									{
										icontop = m_height - iconsize - 2;
										break;
									}
									case IP_UPPERRIGHT:
									{
										iconleft = m_width - iconsize - 2;
										break;
									}
									case IP_LOWERRIGHT:
									{
										iconleft = m_width - iconsize - 2;
										icontop = m_height - iconsize - 2;
										break;
									}
								}

								if (m_settings.BG_Transparency == 0)
								{
									// Draw icon shadow
									gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
									gfx.FillRectangle(&Gdiplus::SolidBrush(Gdiplus::Color::MakeARGB(110, 0, 0, 0)),
													  static_cast<Gdiplus::REAL>(iconleft + 1),
													  static_cast<Gdiplus::REAL>(icontop + 1), 
													  static_cast<Gdiplus::REAL>(new_width + 1),
													  static_cast<Gdiplus::REAL>(new_height + 1));

									// Draw icon
									gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);
									gfx.DrawImage(&img, Gdiplus::RectF(static_cast<Gdiplus::REAL>(iconleft),
												  static_cast<Gdiplus::REAL>(icontop),
												  static_cast<Gdiplus::REAL>(new_width),
												  static_cast<Gdiplus::REAL>(new_height)));
								}
								else
								{
									Gdiplus::ImageAttributes ImgAttr;
									ImgAttr.SetColorMatrix(&BitmapMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);

									// Draw icon shadow
									gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
									gfx.FillRectangle(&Gdiplus::SolidBrush(Gdiplus::Color::MakeARGB((BYTE)(110 - m_settings.BG_Transparency), 0, 0, 0)),
													  static_cast<Gdiplus::REAL>(iconleft + 1),
													  static_cast<Gdiplus::REAL>(icontop + 1), 
													  static_cast<Gdiplus::REAL>(new_width + 1),
													  static_cast<Gdiplus::REAL>(new_height + 1));

									// Draw icon
									gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);
									gfx.DrawImage(&img, Gdiplus::RectF(static_cast<Gdiplus::REAL>(iconleft),
												  static_cast<Gdiplus::REAL>(icontop), 
												  static_cast<Gdiplus::REAL>(new_width),
												  static_cast<Gdiplus::REAL>(new_height)),
												  0, 0, img_width, img_height, Gdiplus::UnitPixel, &ImgAttr);
								}

								m_iconwidth = (int)new_width;
								m_iconheight = (int)new_height;
							}
							else
							{
								float new_height = 0.f, new_width = 0.f;

								if (m_width > m_height)
								{
									new_height = img_height / img_width * (float)m_width;
									new_width = m_width * 1.f;

									if (new_height > m_height)
									{
										new_width = img_width / img_height * (float)m_height;
										new_height = m_height * 1.f;
									}

								}
								else
								{
									new_width = img_width / img_height * (float)m_height;
									new_height = m_height * 1.f;

									if (new_width > m_width)
									{
										new_height = img_height / img_width * (float)m_width;
										new_width = m_width * 1.f;
									}
								}

								gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);

								if (m_settings.BG_Transparency == 0)
								{
									gfx.DrawImage(&img, Gdiplus::RectF((m_width / 2) - (new_width / 2), 0, 
												  static_cast<Gdiplus::REAL>(new_width),
												  static_cast<Gdiplus::REAL>(new_height)));
								}
								else
								{
									Gdiplus::ImageAttributes ImgAttr;
									ImgAttr.SetColorMatrix(&BitmapMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);

									gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);
									gfx.DrawImage(&img, Gdiplus::RectF((m_width / 2) - (new_width / 2), 0,
												  static_cast<Gdiplus::REAL>(new_width),
												  static_cast<Gdiplus::REAL>(new_height)),
												  0, 0, img_width, img_height,
												  Gdiplus::UnitPixel, &ImgAttr);
								}
							}
						}
						else if (m_settings.Revertto != BG_CUSTOM)
						{
							fail = true;
							ClearBackground();
							GetThumbnail(true);
						}
					}

					delete background;
					background = (Gdiplus::Bitmap*)custom_img->Clone();
				}
				else if (m_settings.Revertto != BG_CUSTOM)
				{
					fail = true;
					ClearBackground();
					GetThumbnail(true);
				}
				break;
			}
			default:
			{
				fail = true;
				ClearBackground();
				GetThumbnail(true);
				break;
			}
		}
	}

	if (tempfail)
	{
		return NULL;
	}

	HBITMAP retbmp = NULL;
	Gdiplus::REAL textheight = 0;
	Gdiplus::Bitmap *canvas = (background ? background->Clone(0, 0, background->GetWidth(),
															  background->GetHeight(),
															  PixelFormat32bppPARGB) : NULL);
	if (canvas)
	{
		Gdiplus::Graphics gfx(canvas);

		if (!no_text)
		{
			if (m_settings.Text[0])
			{
				// Draw text
				static lines text_parser(m_settings, m_metadata);
				text_parser.Parse();

				if (m_textpositions.empty())
				{
					m_textpositions.resize(text_parser.GetNumberOfLines(), 0);
				}
				else
				{
					for (std::vector<int>::size_type i = 0; i != m_textpositions.size(); ++i)
					{
						if (m_textpositions[i] != 0)
						{
							m_textpositions[i] -= 2;
						}
						else
						{
							if (scroll_block)
							{
								bool unblock = true;

								for (std::vector<int>::size_type j = 0; j != m_textpositions.size(); ++j)
								{
									if (m_textpositions[j] < 0)
									{
										unblock = false;
										break;
									}
								}

								scroll_block = !unblock;
								m_textpause = 60;
							}

							if (!scroll_block && m_textpause == 0)
							{
								m_textpositions[i] -= 2;
							}
						}
					}
				}

				// Setup fonts
				HDC h_gfx = gfx.GetHDC();
				Gdiplus::Font font(h_gfx, &m_settings.font);

				LOGFONT large_font = m_settings.font;
				LONG large_size = -((m_settings.font.lfHeight * 72) / GetDeviceCaps(h_gfx, LOGPIXELSY));
				large_size += 4;
				large_font.lfHeight = -MulDiv(large_size, GetDeviceCaps(h_gfx, LOGPIXELSY), 72);
				Gdiplus::Font largefont(h_gfx, &large_font);

				gfx.ReleaseHDC(h_gfx);

				Gdiplus::SolidBrush bgcolor(Gdiplus::Color(GetRValue(m_settings.bgcolor),
														   GetGValue(m_settings.bgcolor),
														   GetBValue(m_settings.bgcolor))),
									fgcolor(Gdiplus::Color(GetRValue(m_settings.text_color),
														   GetGValue(m_settings.text_color),
														   GetBValue(m_settings.text_color)));

				Gdiplus::StringFormat sf(Gdiplus::StringFormatFlagsNoWrap);
				const int text_space = 28;

				for (std::size_t text_index = 0; text_index != text_parser.GetNumberOfLines(); ++text_index)
				{
					Gdiplus::RectF ret_rect;
					std::wstring current_text = text_parser.GetLineText(text_index);
					linesettings current_settings = text_parser.GetLineSettings(text_index);

					// Measure size
					gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
					if (current_settings.largefont)
					{
						gfx.MeasureString(current_text.c_str(), -1, &largefont, Gdiplus::RectF(0, 0, 2000, 1000), &sf, &ret_rect);
					}
					else
					{
						gfx.MeasureString(current_text.c_str(), -1, &font, Gdiplus::RectF(0, 0, 2000, 1000), &sf, &ret_rect);
					}

					if (ret_rect.GetBottom() == 0)
					{
						if (current_text.empty())
						{
							gfx.MeasureString(L"QWEXCyjM", -1, &font, Gdiplus::RectF(0, 0,
											  static_cast<Gdiplus::REAL>(m_width),
											  static_cast<Gdiplus::REAL>(m_height)), &sf, &ret_rect);
						}
						else
						{
							gfx.MeasureString(L"QWEXCyjM", -1, &Gdiplus::Font(L"Segoe UI", 14),
											  Gdiplus::RectF(0, 0, static_cast<Gdiplus::REAL>(m_width),
											  static_cast<Gdiplus::REAL>(m_height)), &sf, &ret_rect);
						}
					}

					Gdiplus::Bitmap text_bitmap(static_cast<INT>(ret_rect.GetRight()),
												static_cast<INT>(ret_rect.GetBottom() - 1),
												PixelFormat32bppPARGB);
					Gdiplus::Graphics text_gfx(&text_bitmap);

					// Graphics setup
					text_gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);
					text_gfx.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
					text_gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
					text_gfx.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);

					(m_settings.Antialias) ? text_gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias) : 
											 text_gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintSingleBitPerPixelGridFit);

					// Draw box if needed
					if (current_settings.darkbox && !current_text.empty())
					{
						Gdiplus::SolidBrush boxbrush(Gdiplus::Color::MakeARGB(120, GetRValue(m_settings.bgcolor),
													 GetGValue(m_settings.bgcolor), GetBValue(m_settings.bgcolor)));

						ret_rect.Height += 2;
						text_gfx.FillRectangle(&boxbrush, ret_rect);
					}

					text_gfx.SetTextContrast(120);

					// Draw text to offscreen surface

					//shadow
					if (current_settings.shadow)
					{
						text_gfx.DrawString(current_text.c_str(), -1, 
											current_settings.largefont ? &largefont : &font, 
											Gdiplus::PointF(1, -1), &bgcolor);
					}

					//text
					text_gfx.DrawString(current_text.c_str(), -1, 
										current_settings.largefont ? &largefont : &font, 
										Gdiplus::PointF(0, -2), &fgcolor);

					// Calculate text position
					int X = 0, CX = m_width;

					if (m_iconwidth == 0)
					{
						m_iconwidth = iconsize;
					}

					if (m_iconheight == 0)
					{
						m_iconheight = iconsize;
					}

					if (m_settings.AsIcon && !no_icon && !current_settings.forceleft)
					{
						if ((iconposition == IP_UPPERLEFT || 
							iconposition == IP_LOWERLEFT) &&
							!current_settings.forceleft)
						{
							X += m_iconwidth + 5;
							CX = m_width - X;
						}
						else if (iconposition == IP_UPPERRIGHT || 
								 iconposition == IP_LOWERRIGHT)
						{
							CX = m_width - m_iconwidth - 5;
						}
					}

					gfx.SetClip(Gdiplus::RectF(static_cast<Gdiplus::REAL>(X),
											   static_cast<Gdiplus::REAL>(0),
											   static_cast<Gdiplus::REAL>(CX),
											   static_cast<Gdiplus::REAL>(m_width)),
											   Gdiplus::CombineModeReplace);

					// Draw text bitmap to final bitmap
					if (text_bitmap.GetWidth() > (UINT)(CX + 2) &&
						!current_settings.dontscroll)// && m_textpause[text_index] == 0)
					{
						// Draw scrolling text
						int left = m_textpositions[text_index];
						const int bmp_width = (int)text_bitmap.GetWidth(),
								  bmp_height = (int)text_bitmap.GetHeight();

						if (left + bmp_width < 0)
						{
							// reset text
							m_textpositions[text_index] = text_space;
							left = text_space;
							scroll_block = true;
						}

						if (left == 0 && m_textpause == 0)
						{
							m_textpause = 60; // delay; in steps
						}

						if (left + bmp_width >= CX)
						{
							gfx.DrawImage(&text_bitmap, X, (int)textheight, -left,
										  0, CX, bmp_height, Gdiplus::UnitPixel);
						}
						else
						{
							gfx.DrawImage(&text_bitmap, X, (int)textheight, -left, 0,
										  bmp_width + left, bmp_height, Gdiplus::UnitPixel);

							gfx.DrawImage(&text_bitmap, X + text_space + 2 + bmp_width + left,
										  (int)textheight, 0, 0, -left, bmp_height, Gdiplus::UnitPixel);
						}
					}
					else
					{
						// Draw non-scrolling text
						if (current_settings.center)
						{
							// Center text
							const int newleft = X + ((CX / 2) - (text_bitmap.GetWidth() / 2));
							gfx.DrawImage(&text_bitmap, newleft, (int)textheight, 0, 0,
										  text_bitmap.GetWidth(), text_bitmap.GetHeight(),
										  Gdiplus::UnitPixel);          
						}
						else
						{
							gfx.DrawImage(&text_bitmap, X, (int)textheight, 0, 0,
										  text_bitmap.GetWidth(), text_bitmap.GetHeight(),
										  Gdiplus::UnitPixel);          
						}

						m_textpositions[text_index] = 2; // Nr. pixels text jumps on each step when scrolling
					}

					gfx.ResetClip();

					textheight += text_bitmap.GetHeight();
				}

				if (m_textpause > 0)
				{
					--m_textpause;
				}

				if (!m_settings.Shrinkframe)
				{
					textheight = (Gdiplus::REAL)(m_height - 2);
				}

				if (m_settings.Thumbnailpb)
				{
					textheight += 25;
				}

				if (textheight > m_height - 2)
				{
					textheight = (Gdiplus::REAL)(m_height - 2);
				}
			}

			// Draw progressbar (only if there's a need to do so)
			if (m_settings.Thumbnailpb && (m_settings.play_total > 0) &&
				(m_settings.play_current > 0))
			{
				gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
				int Y = canvas->GetHeight() - 10;
				if (m_settings.Shrinkframe)
				{
					Y = (int)(textheight - 10);
				}

				//Pen p1(Color::White, 1);
				Gdiplus::Pen p1(Gdiplus::Color::MakeARGB(0xFF, 0xFF, 0xFF, 0xFF), 1);
				Gdiplus::Pen p2(Gdiplus::Color::MakeARGB(80, 0, 0, 0), 1);

				Gdiplus::SolidBrush b1(Gdiplus::Color::MakeARGB(50, 0, 0, 0));
				Gdiplus::SolidBrush b2(Gdiplus::Color::MakeARGB(220, 255, 255, 255));

				Gdiplus::RectF R1(44, (Gdiplus::REAL)Y, 10, 9);
				Gdiplus::RectF R2((Gdiplus::REAL)m_width - 56, (Gdiplus::REAL)Y, 10, 9);

				gfx.FillRectangle(&b1, 46, Y, m_width - 94, 9);
				gfx.DrawLine(&p2, 46, Y + 2, 46, Y + 6);
				gfx.DrawLine(&p2, m_width - 48, Y + 2, m_width - 48, Y + 6);

				gfx.DrawArc(&p1, R1, -90.0f, -180.0f);
				gfx.DrawArc(&p1, R2, 90.0f, -180.0f);

				gfx.DrawLine(&p1, 48, Y, m_width - 49, Y);
				gfx.DrawLine(&p1, 48, Y + 9, m_width - 49, Y + 9);

				gfx.SetSmoothingMode(Gdiplus::SmoothingModeDefault);
				gfx.FillRectangle(&b2, 48, Y + 3, (m_settings.play_current *
								  (m_width - 96)) / m_settings.play_total, 4);
			}
		}

		// finalize / garbage
		if (get_bmp)
		{
			if (no_text || !m_settings.Shrinkframe)
			{
				canvas->GetHBITMAP(NULL, &retbmp);        
			}
			else
			{
				Gdiplus::Bitmap *shrink = canvas->Clone(0, 0, m_width, textheight > m_iconheight ?
														(int)textheight : (int)m_iconheight,
														PixelFormat32bppPARGB);
				shrink->GetHBITMAP(NULL, &retbmp);
				delete shrink;
			}
		}

		delete canvas;
	}

	return retbmp;
}

renderer::renderer(sSettings& settings, MetaData &metadata) : 
	gdiplusToken(0),
	custom_img(NULL),
	background(NULL),
	albumart(NULL),
	m_settings(settings),
	m_metadata(metadata),
	m_width(-1), //thumbnail width
	m_height(-1), //thumbnail height
	m_iconwidth(0),
	m_iconheight(0),
	m_textpause(30), //number of frames for minimum text pause
	no_icon(false),
	fail(false),
	scroll_block(false),
	no_text(false)
{
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;   
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
}

renderer::~renderer()
{
	ClearAlbumart();
	ClearBackground();
	ClearCustomBackground();
	Gdiplus::GdiplusShutdown(gdiplusToken);
}

void renderer::SetDimensions(const int new_w, const int new_h) 
{
	m_width = new_w;
	m_height = new_h;
}

void renderer::ClearAlbumart()
{
	if (albumart)
	{
		delete albumart;
		albumart = NULL;
	}
}

void renderer::ClearBackground()
{
	if (background)
	{
		delete background;
		background = NULL;
	}
}

void renderer::ThumbnailPopup()
{ 
	m_textpositions.clear();
	m_textpause = 60;
}

void renderer::ClearCustomBackground()
{
	if (custom_img)
	{
		delete custom_img;
		custom_img = NULL;
	}
}