#ifndef WINJUMP_H
#define WINJUMP_H

#define VC_EXTRALEAN 1
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#include "dqn.h"

struct Win32Program
{
	wchar_t title[256];
	i32     titleLen;

	wchar_t exe[256];
	i32     exeLen;

	HWND    window;
	DWORD   pid;

	i32 lastStableIndex;
};

enum WinjumpWindows
{
	WinjumpWindow_MainClient,
	WinjumpWindow_Tab,
	WinjumpWindow_BtnChangeFont,
	WinjumpWindow_TextHotkeyIsValid,
	WinjumpWindow_TextHotkeyWinjumpActivate,
	WinjumpWindow_HotkeyWinjumpActivate,
	WinjumpWindow_ListProgramEntries,
	WinjumpWindow_InputSearchEntries,
	WinjumpWindow_StatusBar,
	WinjumpWindow_Count,
};

#define WIN32_NOT_PART_OF_TAB -1
struct Win32Window
{
	HWND handle;
	i32  tabIndex = WIN32_NOT_PART_OF_TAB;

	// The default proc to fallback to when subclassing. Is 0 if not defined
	WNDPROC defaultProc;
};

struct AppHotkey
{
	char win32VirtualKey  = 'K';     // The character key
	i32  win32ModifierKey = MOD_ALT; // Alt/Shift/Ctrl key
};

struct WinjumpState
{
	HFONT   font;
	Win32Window window[WinjumpWindow_Count];

	DqnArray<Win32Program>           programArray;
	DqnArray<DqnArray<Win32Program>> programArraySnapshotStack;

	bool isFilteringResults;
	bool configIsStale;
	i32  searchStringLen;

	AppHotkey appHotkey = {};
};

#define WIN32_GUID_HOTKEY_ACTIVATE_APP 10983

u32 Winjump_HotkeyToString(AppHotkey hotkey, char *const buf, u32 bufSize);
bool Winjump_HotkeyIsValid(AppHotkey hotkey);
#endif /* WINJUMP_H */
