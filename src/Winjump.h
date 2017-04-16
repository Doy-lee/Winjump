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

typedef struct WinjumpState
{
	HFONT   font;
	Win32Window window[winjumpwindow_count];

	DqnArray<Win32Program>           programArray;
	DqnArray<DqnArray<Win32Program>> programArraySnapshotStack;

	bool isFilteringResults;
	bool configIsStale;

	i32  searchStringLen;
} WinjumpState;

#endif /* WINJUMP_H */
