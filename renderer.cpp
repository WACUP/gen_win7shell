#include <Windows.h>
#include <string>
#include <strsafe.h>
#include <vector>
#include <gdiplus.h>
#include "renderer.h"
#include "gen_win7shell.h"
#include "api.h"
#include "lines.h"
#include <dwmapi.h>
#include <loader/loader/paths.h>
#include <loader/loader/ini.h>
#include <loader/loader/utils.h>

WA_UTILS_API HBITMAP GetMainWindowBmp(void);

extern CRITICAL_SECTION background_cs;

Gdiplus::Bitmap* ResizeAndCloneBitmap(Gdiplus::Bitmap *bmp, const float width, const float height)
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

void renderer::createArtwork(const int cur_w, const int cur_h, ARGB32 *cur_image)
{
	BITMAPINFO bmi = { 0 };
	InitBitmapForARGB32(&bmi, cur_w, cur_h);

	Gdiplus::Bitmap tmpbmp(&bmi, cur_image);

	float new_height = 0.f, new_width = 0.f, anchor = (m_height * 1.f);

	if (!m_settings.AsIcon)
	{
		if (m_width < m_height)
		{
			anchor = (m_width * 1.f);
		}
	}
	else
	{
		anchor = (_iconheight * 1.f);
	}

	if (cur_w > cur_h)
	{
		new_height = (float)cur_h / (float)cur_w * (float)anchor;
		new_height -= 2;
		new_width = anchor;

		if (new_height > m_height)
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

		if (new_width > m_width)
		{
			new_height = (float)cur_h / (float)cur_w * (float)anchor;
			new_height -= 2;
			new_width = anchor;
		}
	}

	m_iconheight = _iconheight = (int)new_height;
	m_iconwidth = _iconwidth = (int)new_width;

	// we don't want to keep doing this as it is going
	// to be very slow especially with >600x600 images
	// so we cache a re-sized image and then draw that
	albumart = ResizeAndCloneBitmap(&tmpbmp, new_width, new_height);

	if (cur_image)
	{
		plugin.memmgr->sysFree(cur_image);
	}
}

int __cdecl preview_sync_callback(const wchar_t *filename, const int w, const int h,
								  ARGB32 *callback_bits, void *user_data)
{
	// due to how this might be triggered (e.g. closing action) it's necessary to have it avoid
	// trying to do anything here so we don't then crash due to accessing a non-existant object
	renderer* this_renderer = reinterpret_cast<renderer*>(user_data);
	LPCWSTR fn = (running && this_renderer ? this_renderer->GetMetadata().getFileName() : NULL);
	if (SameStr(filename, fn))
	{
		this_renderer->createArtwork(w, h, callback_bits);
		return TRUE;
	}
	return FALSE;
}

bool renderer::getAlbumArt(const std::wstring &fname)
{
	if (!albumart && (WASABI_API_ALBUMART != NULL))
	{
		ARGB32 *cur_image = 0;
		int cur_w = 0, cur_h = 0;
		// when running under WACUP this request is cached for us
		// so we don't have to worry too much about it being slow
		int ret = WASABI_API_ALBUMART->GetAlbumArtAsyncResize(fname.c_str(), L"cover", this, 600,
															  600, FALSE, preview_sync_callback);
		if ((ret != ALBUMART_SUCCESS) && (ret != ALBUMART_GOTCACHE))
		{
			ret = WASABI_API_ALBUMART->GetAlbumArtResize(fname.c_str(), L"cover", &cur_w,
														 &cur_h, &cur_image, 600, 600, 0);
			if ((ret == ALBUMART_SUCCESS) || (ret == ALBUMART_GOTCACHE))
			{
				createArtwork(cur_w, cur_h, cur_image);
			}
			else
			{
				return false;
			}
		}
		else
		{
			// let the async callback do the render
			// call instead of returning a failure.
			return true;
		}
	}
	return render();
}

bool renderer::render(void)
{
	if (albumart)
	{
		Gdiplus::Graphics gfx(background);

		gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQuality);
		gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
		gfx.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);

		int iconleft = 0, icontop = 0;

		switch (m_settings.IconPosition)
		{
			case IP_LOWERLEFT:
			{
				icontop = m_height - m_iconheight - 2;
				break;
			}
			case IP_UPPERRIGHT:
			{
				iconleft = m_width - m_iconwidth - 2;
				break;
			}
			case IP_LOWERRIGHT:
			{
				iconleft = m_width - m_iconwidth - 2;
				icontop = m_height - m_iconheight - 2;
				break;
			}
		}

		if (!m_settings.AsIcon)
		{
			// TODO need to get this accounting for irregular
			//		images so they're centered in the window!
			iconleft = (m_width / 2) - (m_iconwidth / 2);
		}        
		/*else
		{
			// Draw icon shadow
			// note: have dropped this with the irregular image
			//		 handling as it was introducing a slight
			//		 performance penalty on older / slower h/w
			//		 and it's almost impossible to see its there
			gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			gfx.FillRectangle(&Gdiplus::SolidBrush(Gdiplus::Color::MakeARGB((BYTE)(110 - m_settings.BG_Transparency), 0, 0, 0)),
							  static_cast<Gdiplus::REAL>(iconleft + 1), static_cast<Gdiplus::REAL>(icontop + 1),
							  static_cast<Gdiplus::REAL>(m_iconwidth + 1), static_cast<Gdiplus::REAL>(m_iconheight + 1));
		}*/

		gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);

		//Calculate Alpha blend based on Transparency
		const float fBlend = (100.0f - m_settings.BG_Transparency) / 100.0f;

		// this will draw the image whether it needs
		// transparency or not as it will reliably
		// maintain the aspect ratio of the artwork
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
		return (gfx.DrawImage(albumart, Gdiplus::RectF(static_cast<Gdiplus::REAL>(iconleft),
							  static_cast<Gdiplus::REAL>(icontop), static_cast<Gdiplus::REAL>(m_iconwidth),
							  static_cast<Gdiplus::REAL>(m_iconheight)), 0, 0, static_cast<Gdiplus::REAL>(m_iconwidth),
							  static_cast<Gdiplus::REAL>(m_iconheight), Gdiplus::UnitPixel, &ImgAttr) == Gdiplus::Ok);
	}
	return false;
}

HBITMAP renderer::GetThumbnail(void)
{
	// not everyone is going to even cause the
	// preview to be generated so we will wait
	// until its needed to load gdiplus as its
	// a relatively slow to close down on exit
	/*if (!gdiplusToken)
	{
		gdiplusStartupInput.SuppressBackgroundThread = TRUE;
		Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput,
								&gdiplusStartupOutput);
		gdiplusStartupOutput.NotificationHook(&gdiplusBgThreadToken);
	}/*/
	if (!running || !SetupGDIplus())
	{
		return NULL;
	}/**/

	ClearBackground();

	//Calculate icon size
	_iconwidth = m_iconwidth;

	if (m_settings.AsIcon)
	{
		_iconheight = (m_settings.IconSize * m_height) / 100;
		_iconheight -= 2;
	}
	else
	{
		_iconheight = m_iconheight;
	}

	//Calculate Alpha blend based on Transparency
	const float fBlend = (100.f - m_settings.BG_Transparency) / 100.0f;

	const Gdiplus::ColorMatrix BitmapMatrix =
	{
		1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, fBlend, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 1.0f
	};

	bool tempfail = fail;
	int iconposition = m_settings.IconPosition;

	EnterCriticalSection(&background_cs);

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

		__try
		{
			background = new Gdiplus::Bitmap(m_width, m_height, PixelFormat32bppPARGB);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			background = NULL;
		}

		LeaveCriticalSection(&background_cs);

		if (!background)
		{
			return NULL;
		}

		Gdiplus::Graphics graphics(background);

		// so we've got something irrespective of the mode being used
		// it's simpler to ensure that the background has been filled
		// in with something that'll act like it's transparent to not
		// have it showing as a black rectangle if something fails...
		graphics.FillRectangle(&Gdiplus::SolidBrush(Gdiplus::Color::MakeARGB(2, 0, 0, 0)),
								static_cast<Gdiplus::REAL>(0), static_cast<Gdiplus::REAL>(0), 
								static_cast<Gdiplus::REAL>(m_width), static_cast<Gdiplus::REAL>(m_height));

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

				RECT r = { 0 };
				GetClientRect(dialogParent, &r);

				// because winamp modern is just weird due to it's drawers
				// it's simpler to force a half-height to make it look ok
				const INT height = (!modernFix ? (r.bottom - r.top) :
								   (INT)((r.bottom - r.top) / 1.74f)),
						  width = (r.right - r.left);
				Gdiplus::Bitmap bmp(width, height, PixelFormat32bppPARGB);
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
				// note: winamp modern is treated as a special case
				//		 due to the weirdness due to it's drawer :(
				if (modernSUI || modernFix)
				{
					const HDC hdcWindow = GetDCEx(dialogParent, NULL, DCX_CACHE | DCX_WINDOW);
					BitBlt(hdc, 0, 0, width, height, hdcWindow, 0, 0, SRCCOPY);
					ReleaseDC(dialogParent, hdcWindow);
				}
				else
				{
					DWORD_PTR ret = 0;
					SendMessageTimeout(dialogParent, WM_PRINTCLIENT, (WPARAM)hdc,
									   PRF_CHILDREN | PRF_NONCLIENT | PRF_CLIENT,
									   SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 1000, &ret);
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
				const bool doReplace = (modernFix || (/*!classicSkin &&*/ !modernSUI));
				Gdiplus::ImageAttributes ImgAtt;
				Gdiplus::Color queried(255, 0, 0, 0), replace(255, 0, 0, 0);
				if (doReplace)
				{
					bmp.GetPixel((width - 1), (height - 1), &queried);
					ImgAtt.SetColorKey(queried, queried, Gdiplus::ColorAdjustTypeBitmap);
				}

				// we draw a re-sized version of the captured window
				// whilst resetting certain pixels to have alpha so
				// that for some modern skins the preview is better
				// than it would otherwise be (due to gen_ff loosing
				// that information when it's put into the hdc *ugg*)
				if (!IsIconic(plugin.hwndParent))
				{
					RECT rt = { 0 };
					ScaleArtworkToArea(&rt, m_width, m_height, width, height);
					Gdiplus::Rect dest(rt.left, rt.top, (rt.right - rt.left), (rt.bottom - rt.top));
					graphics.DrawImage(&bmp, dest, 0, 0, width, height, Gdiplus::UnitPixel, (doReplace ?
									   (replace.ToCOLORREF() == queried.ToCOLORREF() ? &ImgAtt : 0) : 0));
				}
				else
				{
					bool use_icon = true;
					if (classicSkin)
					{
						const HBITMAP main_window_bmp = GetMainWindowBmp();
						if (main_window_bmp != NULL)
						{
							Gdiplus::Bitmap image(main_window_bmp, NULL);
							DeleteObject(main_window_bmp);

							const int image_width = image.GetWidth(), image_height = image.GetHeight();
							RECT adjust = { 0 };
							ScaleArtworkToArea(&adjust, m_width, m_height, image_width, image_height);
							Gdiplus::Rect dest(adjust.left, adjust.top,
											   adjust.right - adjust.left,
											   adjust.bottom - adjust.top);
							graphics.DrawImage(&image, dest, 0, 0, image_width,
											   image_height, Gdiplus::UnitPixel);
							use_icon = false;
						}
					}
					
					if (use_icon)
					{
						// this will either use the custom icon if specified
						// or the icon related to the loader currently used.
						// there is a disconnect if using wacup.exe loader &
						// having set the process icon to be the winamp.exe
						// but for what's only used if minimised I just cba!
						HICON win32_icon = NULL;
						if (GetWinampIniInt(L"tb_icon", 1) == 2)
						{
							wchar_t taskbar_tmp[MAX_PATH] = { 0 };

							win32_icon = (HICON)LoadImage(NULL, CombinePath(taskbar_tmp,
										 GetPaths()->settings_dir, L"taskbar.ico"), IMAGE_ICON,
										 256, 256, LR_LOADTRANSPARENT | LR_LOADFROMFILE);
						}

						bool from_core = false;
						if (win32_icon == NULL)
						{
							// due to how WACUP works a wacup.exe or
							// winamp.exe or might have been used as
							// the loader along with running as the
							// non-legacy or legacy modes so we will
							// let the core manage working out a big
							// icon for us to use based on the loader
							win32_icon = GetLoaderIcon(FALSE);
							from_core = true;
						}

						if (win32_icon != NULL)
						{
							Gdiplus::Bitmap icon(win32_icon);
							const int icon_width = icon.GetWidth(), icon_height = icon.GetHeight();
							Gdiplus::Rect dest((m_width / 2) - (icon_width / 2), (m_height / 2) -
											   (icon_height / 2), icon_width, icon_height);
							dest.Inflate(-(icon_width / 4), -(icon_height / 4));
							graphics.DrawImage(&icon, dest, 0, 0, icon_width,
											   icon_height, Gdiplus::UnitPixel,
											   (doReplace ? (replace.ToCOLORREF() ==
												queried.ToCOLORREF() ? &ImgAtt : 0) : 0));

							if (!from_core)
							{
								DestroyIcon(win32_icon);
							}
						}
					}
				}
				break;
			}
			case BG_ALBUMART:
			{
				// get album art
				if (!getAlbumArt(m_metadata.getFileName()) &&
					(m_settings.Revertto != BG_ALBUMART))
				{
					// fallback
					fail = true;
					GetThumbnail();
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
									new_height = img_height / img_width * (float)_iconheight;
									new_height -= 2.f;
									new_width = _iconwidth * 1.f;
								}
								else
								{
									new_width = img_width / img_height * (float)_iconwidth;
									new_width -= 2.f;
									new_height = _iconheight * 1.f;
								}

								int iconleft = 0, icontop = 0;

								switch (iconposition)
								{
									case IP_LOWERLEFT:
									{
										icontop = m_height - _iconheight - 2;
										break;
									}
									case IP_UPPERRIGHT:
									{
										iconleft = m_width - _iconheight - 2;
										break;
									}
									case IP_LOWERRIGHT:
									{
										iconleft = m_width - _iconheight - 2;
										icontop = m_height - _iconheight - 2;
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
									ImgAttr.SetColorMatrix(&BitmapMatrix, Gdiplus::ColorMatrixFlagsDefault,
																		  Gdiplus::ColorAdjustTypeBitmap);

									// Draw icon shadow
									gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
									gfx.FillRectangle(&Gdiplus::SolidBrush(Gdiplus::Color::MakeARGB((BYTE)(110 -
																		   m_settings.BG_Transparency), 0, 0, 0)),
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
									ImgAttr.SetColorMatrix(&BitmapMatrix, Gdiplus::ColorMatrixFlagsDefault,
																		  Gdiplus::ColorAdjustTypeBitmap);

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
							GetThumbnail();
						}
					}

					EnterCriticalSection(&background_cs);

					delete background;

					background = (Gdiplus::Bitmap*)custom_img->Clone();

					LeaveCriticalSection(&background_cs);
				}
				else if (m_settings.Revertto != BG_CUSTOM)
				{
					fail = true;
					GetThumbnail();
				}
				break;
			}
			default:
			{
				fail = true;
				GetThumbnail();
				break;
			}
		}
	}
	else
	{
		LeaveCriticalSection(&background_cs);
	}

	HBITMAP retbmp = NULL;
	if (!tempfail)
	{
		Gdiplus::REAL textheight = 0;
		Gdiplus::Bitmap *canvas = (background ? background->Clone(0, 0, background->GetWidth(),
									   background->GetHeight(), PixelFormat32bppPARGB) : NULL);
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
								m_textpositions[i] -= (m_settings.LowFrameRate ? 3 : 2);
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
									m_textpause = (m_settings.LowFrameRate ? 4 : 1);
								}

								if (!scroll_block && m_textpause == 0)
								{
									m_textpositions[i] -= (m_settings.LowFrameRate ? 3 : 2);
								}
							}
						}
					}

					// Setup fonts
					if (!normal_font || !large_font)
					{
						HDC h_gfx = gfx.GetHDC();
						if (h_gfx)
						{
							if (!normal_font)
							{
								normal_font = new Gdiplus::Font(h_gfx, &m_settings.font);
							}

							if (!large_font)
							{
								LOGFONT _large_font = m_settings.font;
								LONG large_size = -((m_settings.font.lfHeight * 72) /
													GetDeviceCaps(h_gfx, LOGPIXELSY));
								large_size += 4;
								_large_font.lfHeight = -MulDiv(large_size, GetDeviceCaps(h_gfx, LOGPIXELSY), 72);
								large_font = new Gdiplus::Font(h_gfx, &_large_font);
							}

							gfx.ReleaseHDC(h_gfx);
						}
					}

					// since its possible for the fonts on the machine
					// to not be valid then it's better to bail asap &
					// reduce the hit - gdi+ only likes true type font
					if ((normal_font && normal_font->IsAvailable()) &&
						(large_font && large_font->IsAvailable()))
					{
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

							const Gdiplus::PointF origin(0, 0);
							// this used to apply a parent_rect set to the size of the area
							// to be drawn into by the OS but that then messed up the check
							// for how long the string actually is which is needed when the
							// line is going to be scrolled to prevent being clipped short!
							gfx.MeasureString(current_text.c_str(), (INT)current_text.size(),
											  (current_settings.largefont ? large_font :
											  normal_font), origin, &sf, &ret_rect);
					
							if (ret_rect.GetBottom() == 0)
							{
								gfx.MeasureString(L"QWEXCyjM", -1, normal_font, origin, &sf, &ret_rect);
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
								text_gfx.DrawString(current_text.c_str(), (INT)current_text.size(),
													current_settings.largefont ? large_font :
													normal_font, Gdiplus::PointF(1, -1), &sf, &bgcolor);
							}

							//text
							text_gfx.DrawString(current_text.c_str(), (INT)current_text.size(),
												current_settings.largefont ? large_font :
												normal_font, Gdiplus::PointF(0, -2), &sf, &fgcolor);

							// Calculate text position
							int X = 0, CX = m_width;

							if (m_iconwidth == 0)
							{
								m_iconwidth = _iconwidth;
							}

							if (m_iconheight == 0)
							{
								m_iconheight = _iconheight;
							}

							if (m_settings.AsIcon && !no_icon && !current_settings.forceleft)
							{
								if ((iconposition == IP_UPPERLEFT) || 
									(iconposition == IP_LOWERLEFT))
								{
									X += m_iconwidth + 5;
									CX = m_width - X;
								}
								else if ((iconposition == IP_UPPERRIGHT) || 
										 (iconposition == IP_LOWERRIGHT))
								{
									CX = m_width - m_iconwidth - 5;
								}
							}
							else
							{
								CX *= 0.96f;
							}

							gfx.SetClip(Gdiplus::RectF(static_cast<Gdiplus::REAL>(X),
													   static_cast<Gdiplus::REAL>(0),
													   static_cast<Gdiplus::REAL>(CX),
													   static_cast<Gdiplus::REAL>(m_width)),
													   Gdiplus::CombineModeReplace);

							// Draw text bitmap to final bitmap with scrolling as needed
							// based on the icon vs background image mode where the area
							// can vary depending on the size of things vs overall image
							if ((text_bitmap.GetWidth() > (UINT)(CX + 2)) && !current_settings.dontscroll)
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
									m_textpause = (m_settings.LowFrameRate ? 4 : 1); // delay; in steps
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

								// Nr. pixels text jumps on each step when scrolling
								m_textpositions[text_index] = (m_settings.LowFrameRate ? 3 : 2);
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
				}

				// Draw progressbar (only if there's a need to do so)
				if (m_settings.Thumbnailpb &&
					(m_settings.play_total > 0) &&
					(m_settings.play_current > 0))
				{
					gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

					int Y = canvas->GetHeight() - 10;
					if (m_settings.Shrinkframe)
					{
						Y = (int)(textheight - 10);
					}

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
			if (no_text || !m_settings.Shrinkframe)
			{
				canvas->GetHBITMAP(NULL, &retbmp);
			}
			else
			{
				Gdiplus::Bitmap *shrink = canvas->Clone(0, 0, m_width, textheight > m_iconheight ?
														(int)textheight : (int)m_iconheight,
														PixelFormat32bppPARGB);
				if (shrink)
				{
					shrink->GetHBITMAP(NULL, &retbmp);
					delete shrink;
				}
			}

			delete canvas;
		}
	}
	return retbmp;
}

renderer::renderer(sSettings& settings, MetaData &metadata) : 
	/*gdiplusToken(0), gdiplusBgThreadToken(0),*/ custom_img(NULL),
	background(NULL), albumart(NULL), normal_font(NULL),
	large_font(NULL), m_settings(settings), m_metadata(metadata),
	m_width(-1), m_height(-1), m_iconwidth(0), m_iconheight(0),
	m_textpause((m_settings.LowFrameRate ? 4 : 1)), _iconwidth(0),
	_iconheight(0), no_icon(false), fail(false), scroll_block(false), no_text(false) { }

renderer::~renderer()
{
	ClearAlbumart();
	ClearBackground();
	ClearCustomBackground();
	ClearFonts();

	/*if (gdiplusToken != 0)
	{
		gdiplusStartupOutput.NotificationUnhook(gdiplusToken);
		Gdiplus::GdiplusShutdown(gdiplusToken);
		gdiplusToken = gdiplusBgThreadToken = 0;
	}*/
}

void renderer::SetDimensions(const int new_w, const int new_h) 
{
	m_width = new_w;
	m_height = new_h;
}

void renderer::ClearAlbumart(void)
{
	if (albumart)
	{
		delete albumart;
		albumart = NULL;
	}
}

void renderer::ClearBackground(void)
{
	EnterCriticalSection(&background_cs);

	if (background)
	{
		__try
		{
			delete background;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
		background = NULL;
	}

	LeaveCriticalSection(&background_cs);
}

void renderer::ClearFonts(void)
{
	if (normal_font)
	{
		delete normal_font;
		normal_font = NULL;
	}

	if (large_font)
	{
		delete large_font;
		large_font = NULL;
	}
}

void renderer::ThumbnailPopup(void)
{ 
	m_textpositions.clear();
	m_textpause = (m_settings.LowFrameRate ? 4 : 1);
}

void renderer::ClearCustomBackground(void)
{
	if (custom_img)
	{
		delete custom_img;
		custom_img = NULL;
	}
}