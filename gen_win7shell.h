// gen_win7shell.h
#ifndef gen_myplugin_h
//---------------------------------------------------------------------------
#define gen_myplugin_h

#include <windows.h>
#include <string>
#include <vector>

#include <sdk/winamp/gen.h>

struct sSettings
{
	// thumbnail
	bool Thumbnailenabled;
	int Thumbnailbackground;
	bool Thumbnailbuttons;
	bool Progressbar;
	bool Streamstatus;
	bool Stoppedstatus;
	bool Overlay;
	int Revertto;
	wchar_t BGPath[MAX_PATH];
	wchar_t Text[1024];

	bool Add2RecentDocs;
	bool Antialias;
	bool Shrinkframe;
	bool VuMeter;
	bool Buttons[16];
	bool Thumbnailpb;

	// icon settings
	bool AsIcon;
	int IconSize;
	int IconPosition;
	int BG_Transparency;

	// jumplist
	bool JLrecent;
	bool JLfrequent;
	bool JLtasks;
	bool JLbms;
	bool JLpl;

	// misc
#ifdef USE_MOUSE
	bool VolumeControl;
#endif
	bool LowFrameRate;
	int LastTab;

	// playback info
	int play_current;
	int play_total;
	int play_playlistpos;
	int play_playlistlen;
	int play_volume;
	int play_state;
	int play_kbps;
	int play_khz;

	int state_repeat;
	int state_shuffle;

	// font settings
	LOGFONT font;
	DWORD text_color;
	DWORD bgcolor;

	// read only timer tweaks
	int MFT;	// main window fast timer
	int MST;	// main window slow timer
	int TFT;	// text mode fast timer
	int TST;	// text mode slow timer
};

struct linesettings
{
	bool center;
	bool largefont;
	bool forceleft;
	bool shadow;
	bool darkbox;
	bool dontscroll;
};

enum BG_Mode
{ 
	BG_TRANSPARENT,
	BG_ALBUMART,
	BG_CUSTOM,
	BG_WINAMP
};

enum IconPosition
{
	IP_UPPERLEFT,
	IP_LOWERLEFT,
	IP_UPPERRIGHT,
	IP_LOWERRIGHT
};

enum ThumbButtonID
{
	TB_PREVIOUS = 1300,
	TB_PLAYPAUSE,
	TB_STOP,
	TB_NEXT,
	TB_RATE,
	TB_VOLDOWN,
	TB_VOLUP,
	TB_OPENFILE,
	TB_MUTE,
	TB_STOPAFTER,
	TB_REPEAT,
	TB_SHUFFLE,
	TB_JTFE,
	TB_DELETE,
	TB_OPENEXPLORER
};

#define SECTION_NAME_GENERAL L"general"
#define SECTION_NAME_FONT L"font"

#define PLAYSTATE_PLAYING 1
#define PLAYSTATE_PAUSED 3
#define PLAYSTATE_NOTPLAYING 0

extern winampGeneralPurposePlugin plugin;
extern std::wstring SettingsFile;
extern bool windowShade, classicSkin, doubleSize, modernSUI, modernFix;
extern HWND dialogParent;
extern int repeat;

#endif