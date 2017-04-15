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
	winjumpwindow_list_program_entries,
	winjumpwindow_input_search_entries,
	winjumpwindow_status_bar,
	winjumpwindow_count,
};

typedef struct WinjumpState
{
	HFONT   font;
	HWND    window[winjumpwindow_count];
	WNDPROC defaultWindowProc;
	WNDPROC defaultWindowProcEditBox;
	WNDPROC defaultWindowProcListBox;

	DqnArray<Win32Program> programArray;
	bool                   currentlyFiltering;
	bool                   configIsStale;
} WinjumpState;

#endif /* WINJUMP_H */
