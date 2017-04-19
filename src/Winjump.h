#ifndef WINJUMP_H
#define WINJUMP_H

#ifndef VC_EXTRALEAN
	#define VC_EXTRALEAN
#endif

#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include "dqn.h"

typedef struct Win32Program
{
	wchar_t title[256];
	i32     titleLen;

	wchar_t exe[256];
	i32     exeLen;

	HWND    window;
	DWORD   pid;

	i32 lastStableIndex;
} Win32Program;

enum WinjumpWindows
{
	winjumpwindow_main_client,
	winjumpwindow_tab,
	winjumpwindow_btn_change_font,
	winjumpwindow_text_hotkey_is_valid,
	winjumpwindow_text_hotkey_winjump_activate,
	winjumpwindow_hotkey_winjump_activate,
	winjumpwindow_list_program_entries,
	winjumpwindow_input_search_entries,
	winjumpwindow_status_bar,
	winjumpwindow_count,
};

#define WIN32_NOT_PART_OF_TAB -1
typedef struct Win32Window
{
	HWND handle;
	i32  tabIndex = WIN32_NOT_PART_OF_TAB;

	// The default proc to fallback to when subclassing. Is 0 if not defined
	WNDPROC defaultProc;
} Win32Window;

enum AppHotkeyModifier
{
	apphotkeymodifier_alt,
	apphotkeymodifier_shift,
	apphotkeymodifier_ctrl,
	apphotkeymodifier_count,
};

typedef struct AppHotkey
{
	char virtualKey                 = 'K';
	enum AppHotkeyModifier modifier = apphotkeymodifier_alt;
} AppHotkey;

typedef struct WinjumpState
{
	HFONT   font;
	Win32Window window[winjumpwindow_count];

	DqnArray<Win32Program>           programArray;
	DqnArray<DqnArray<Win32Program>> programArraySnapshotStack;

	bool isFilteringResults;
	bool configIsStale;
	i32  searchStringLen;

	AppHotkey appHotkey;
} WinjumpState;

#define WIN32_GUID_HOTKEY_ACTIVATE_APP 10983

u32 winjump_hotkey_to_string(AppHotkey hotkey, char *const buf, u32 bufSize);
u32 winjump_apphotkey_to_win32_hkm_hotkey_modifier(enum AppHotkeyModifier modifier);
u32 winjump_apphotkey_to_win32_mod_hotkey_modifier(enum AppHotkeyModifier modifier);

#endif /* WINJUMP_H */
