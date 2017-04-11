#define DQN_IMPLEMENTATION // Enable the implementation
#define DQN_MAKE_STATIC    // Make all functions be static
#include "dqn.h"

#ifndef VC_EXTRALEAN
  #define VC_EXTRALEAN
#endif

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#include <Windowsx.h>
#include <Shlwapi.h>
#include <commctrl.h>

// For win32 GetProcessMemoryInfo()
#include <Psapi.h>

#include <stdio.h>

// TODO(doyle): Organise all our globals siting around.
// TODO(doyle): Search by index in list to help drilldown after the initial
// search
// TODO(doyle): Stop screen flickering by having better listbox updating
// mechanisms
// TODO(doyle): Clean up this cesspool.

// Returns length without null terminator, returns 0 if NULL
FILE_SCOPE i32 wstrlen(const wchar_t *a)
{
	i32 result = 0;
	while (a && a[result]) result++;
	return result;
}

FILE_SCOPE i32 wstrlen_delimit_with(const wchar_t *a, const wchar_t delimiter)
{
	i32 result = 0;
	while (a && a[result] && a[result] != delimiter) result++;
	return result;
}


FILE_SCOPE inline i32 wstrcmp(const wchar_t *a, const wchar_t *b)
{
	if (!a && !b) return -1;
	if (!a) return -1;
	if (!b) return -1;

	while ((*a) == (*b))
	{
		if (!(*a)) return 0;
		a++;
		b++;
	}

	return (((*a) < (*b)) ? -1 : 1);
}

FILE_SCOPE inline wchar_t wchar_to_lower(const wchar_t a)
{
	if (a >= L'A' && a <= L'Z')
	{
		i32 shiftOffset = L'a' - L'A';
		return (a + (wchar_t)shiftOffset);
	}

	return a;
}

FILE_SCOPE inline bool wchar_has_substring(wchar_t *a, i32 lenA,
                                           wchar_t *b, i32 lenB)
{
	if (!a || !b) return false;
	if (lenA == 0 || lenB == 0) return false;

	wchar_t *longStr, *shortStr;
	i32 longLen, shortLen;
	if (lenA > lenB)
	{
		longStr  = a;
		longLen  = lenA;

		shortStr = b;
		shortLen = lenB;
	}
	else
	{
		longStr  = b;
		longLen  = lenB;

		shortStr = a;
		shortLen = lenA;
	}

	bool matchedSubstr = false;
	for (i32 indexIntoLong = 0; indexIntoLong < longLen && !matchedSubstr;
	     indexIntoLong++)
	{
		// NOTE: As we scan through, if the longer string we index into becomes
		// shorter than the substring we're checking then the substring is not
		// contained in the long string.
		i32 remainingLenInLongStr = longLen - indexIntoLong;
		if (remainingLenInLongStr < shortLen) break;

		wchar_t *longSubstr = &longStr[indexIntoLong];
		i32 index = 0;
		for (;;)
		{
			if (wchar_to_lower(longSubstr[index]) ==
			    wchar_to_lower(shortStr[index]))
			{
				index++;
				if (index >= shortLen || !shortStr[index])
				{
					matchedSubstr = true;
					break;
				}
			}
			else
			{
				break;
			}
		}
	}

	return matchedSubstr;
}

FILE_SCOPE inline void wchar_str_to_lower(wchar_t *a, i32 len)
{
	for (i32 i = 0; i < len; i++)
		a[i]   = wchar_to_lower(a[i]);
}

typedef struct Win32Program
{
	wchar_t title[256];
	i32     titleLen;

	wchar_t exe[256];
	i32     exeLen;

	HWND    window;
	DWORD   pid;
} Win32Program;

enum WinjumpWindows
{
	winjumpwindow_main_client,
	winjumpwindow_list_window_entries,
	winjumpwindow_input_search_entries,
	winjumpwindow_status_bar,
	winjumpwindow_count,
};

enum Win32Resources
{
	win32resource_edit_text_buffer,
	win32resource_list,
	win32resource_status_bar,
};

typedef struct WinjumpState
{
	HFONT   defaultFont;
	HWND    window[winjumpwindow_count];
	WNDPROC defaultWindowProc;
	WNDPROC defaultWindowProcEditBox;

	DqnArray<Win32Program> programArray;
	bool                   currentlyFiltering;
} WinjumpState;

#define WIN32_MAX_PROGRAM_TITLE DQN_ARRAY_COUNT(((Win32Program *)0)->title)
FILE_SCOPE WinjumpState globalState;
FILE_SCOPE bool         globalRunning;

FILE_SCOPE void win32_display_window(HWND window)
{
	// IsIconic == if window is minimised
	if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
	SetForegroundWindow(window);
}

// Create the friendly name for representation in the list box
// - out: The output buffer
// - outLen: Length of the output buffer

// Returns the number of characters stored into the buffer
FILE_SCOPE i32 winjump_get_program_friendly_name(const Win32Program *program,
                                              wchar_t *out, i32 outLen)
{
	// +1 for null terminator
	const char additionalCharsToAdd[] = {' ', '-', ' '};
	i32 friendlyNameLen = program->titleLen + program->exeLen +
	                      DQN_ARRAY_COUNT(additionalCharsToAdd) + 1;

	DQN_ASSERT(outLen >= friendlyNameLen);

	i32 numStored = _snwprintf_s(out, outLen, outLen, L"%s - %s",
	                             program->title, program->exe);

	return numStored;
}

BOOL CALLBACK win32_enum_procs_callback(HWND window, LPARAM lParam)
{
	DqnArray<Win32Program> *programArray = &globalState.programArray;
	Win32Program program = {};
	i32 titleLen =
	    GetWindowTextW(window, program.title, WIN32_MAX_PROGRAM_TITLE);
	program.titleLen = titleLen;

	// If we receive an empty string as a window title, then we want to
	// ignore it. So if the string is defined, then we increment index
	if (titleLen > 0)
	{
		/*
		   SIMULATING ALT-TAB WINDOW RESULTS by Raymond Chen
		   https://blogs.msdn.microsoft.com/oldnewthing/20071008-00/?p=24863/
		   For each visible window, walk up its owner chain until you find
		   the root owner.  Then walk back down the visible last active
		   popup chain until you find a visible window. If you're back to
		   where you're started, then put the window in the Alt+Tab list.
		 */
		HWND rootWindow = GetAncestor(window, GA_ROOTOWNER);
		HWND lastPopup;
		do
		{
			lastPopup = GetLastActivePopup(rootWindow);
			if (IsWindowVisible(lastPopup) && lastPopup == window)
			{
				GetWindowThreadProcessId(window, &program.pid);

				program.window = window;
				HANDLE handle  = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
				                            FALSE, program.pid);
				if (handle != nullptr)
				{
					DWORD len   = DQN_ARRAY_COUNT(program.exe);
					BOOL result = QueryFullProcessImageNameW(handle, 0,
					                                         program.exe, &len);
					DQN_ASSERT(result != 0);

					// Len is input as the initial size of array, it then
					// gets modified and returns the number of characters in
					// the result. If len is then the len of the array,
					// there's potential that the path name got clipped.
					DQN_ASSERT(len != DQN_ARRAY_COUNT(program.exe));

					PathStripPathW(program.exe);
					program.exeLen = wstrlen(program.exe);
					CloseHandle(handle);
				}

				dqn_darray_push(programArray, program);
				break;
			}

			rootWindow = lastPopup;
			lastPopup  = GetLastActivePopup(rootWindow);
		} while (lastPopup != rootWindow);
	}


	return true;
}

FILE_SCOPE LRESULT CALLBACK win32_capture_enter_callback(HWND editWindow,
                                                         UINT msg,
                                                         WPARAM wParam,
                                                         LPARAM lParam)
{
	LRESULT result = 0;
	switch (msg)
	{
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP:
		case WM_KEYDOWN:
		case WM_KEYUP:
		{
			u32 vkCode = wParam;
			switch (vkCode)
			{
				case VK_RETURN:
				{
					DqnArray<Win32Program> *programArray =
					    &globalState.programArray;
					if (programArray->count > 0)
					{
						Win32Program programToShow = programArray->data[0];

						win32_display_window(programToShow.window);
						SetWindowText(editWindow, "");
						ShowWindow(
						    globalState.window[winjumpwindow_main_client],
						    SW_MINIMIZE);
					}
				}
				break;

				default:
				{
					return CallWindowProcW(globalState.defaultWindowProcEditBox,
					                       editWindow, msg, wParam, lParam);
				}
			}
		}
		break;

		// NOTE: Stop Window Bell on pressing on Enter
		case WM_CHAR:
		{
			switch (wParam)
			{
				case VK_RETURN:
				{
					return 0;
				}

				case VK_ESCAPE:
				{
					// If escape is pressed, empty the text
					HWND inputBox =
					    globalState.window[winjumpwindow_input_search_entries];
					SetWindowTextW(inputBox, L"");
					return 0;
				}

				default:
				{
					return CallWindowProcW(globalState.defaultWindowProcEditBox,
					                      editWindow, msg, wParam, lParam);
				}
			}
		}

		default:
		{
			return CallWindowProcW(globalState.defaultWindowProcEditBox, editWindow,
			                       msg, wParam, lParam);
		}
	}

	return result;
}

FILE_SCOPE LRESULT CALLBACK win32_main_callback(HWND window, UINT msg,
                                                WPARAM wParam, LPARAM lParam)
{
	LRESULT result = 0;
	switch (msg)
	{
		case WM_CREATE:
		{
			// NOTE(doyle): Don't set position here, since creation sends
			// a WM_SIZE, we just put all the size and position logic in there.

			////////////////////////////////////////////////////////////////////
			// Create Edit Window
			////////////////////////////////////////////////////////////////////
			HWND editWindow = CreateWindowExW(
			  WS_EX_CLIENTEDGE,
			  L"EDIT",
			  NULL,
			  WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP,
			  0,
			  0,
			  0,
			  0,
			  window,
			  (HMENU)win32resource_edit_text_buffer,
			  NULL,
			  NULL
			);
			globalState.window[winjumpwindow_input_search_entries] = editWindow;
			SetFocus(editWindow);
			globalState.defaultWindowProcEditBox = (WNDPROC)SetWindowLongPtrW(
			    editWindow, GWLP_WNDPROC,
			    (LONG_PTR)win32_capture_enter_callback);

			////////////////////////////////////////////////////////////////////
			// Create List Window
			////////////////////////////////////////////////////////////////////
			HWND listWindow = CreateWindowExW(
			  WS_EX_CLIENTEDGE | WS_EX_COMPOSITED,
			  L"LISTBOX",
			  NULL,
			  WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |  WS_HSCROLL
			  | LBS_NOTIFY,
			  0, // x
			  0, // y
			  0, // width
			  0, // height
			  window,
			  (HMENU)win32resource_list,
			  NULL,
			  NULL
			);
			globalState.window[winjumpwindow_list_window_entries] = listWindow;

			////////////////////////////////////////////////////////////////////
			// Create Status Bar
			////////////////////////////////////////////////////////////////////
			InitCommonControls();
			HWND statusWindow = CreateWindowExW(
			    0,                         // no extended styles
			    STATUSCLASSNAMEW,          // name of status bar class
			    NULL,                      // no text when first created
			    SBARS_SIZEGRIP |           // includes a sizing grip
			        WS_CHILD | WS_VISIBLE, // creates a visible child window
			    0,
			    0, 0, 0,                         // ignores size and position
			    window,                          // parent
			    (HMENU)win32resource_status_bar, // child window identifier
			    NULL, // handle to application instance
			    NULL);
			globalState.window[winjumpwindow_status_bar] = statusWindow;

			////////////////////////////////////////////////////////////////////
			// Use Default Font
			////////////////////////////////////////////////////////////////////
			{
				NONCLIENTMETRICS metrics = {};
				metrics.cbSize           = sizeof(NONCLIENTMETRICS);

				i32 readFontResult = SystemParametersInfo(
				    SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &metrics,
				    0);

				if (readFontResult)
				{
					globalState.defaultFont =
					    CreateFontIndirect(&metrics.lfMessageFont);
					bool redrawImmediately = true;
					for (i32 i = 0; i < winjumpwindow_count; i++)
					{
						HWND windowToSendMsg = globalState.window[i];
						SendMessageW(windowToSendMsg, WM_SETFONT,
						            (WPARAM)globalState.defaultFont,
						            redrawImmediately);
					}
					// TODO(doyle): Clean up, DeleteObject(defaultFont);
				}
				else
				{
					// TODO(doyle): Logging, default font query failed
					DQN_ASSERT(DQN_INVALID_CODE_PATH);
				}
			}
		}
		break;

		case WM_COMMAND:
		{
			WORD notificationCode = HIWORD((DWORD)wParam);
			HWND handle           = (HWND)lParam;
			if (handle == globalState.window[win32resource_list])
			{
				if (notificationCode == LBN_SELCHANGE)
				{
					LRESULT selectedIndex =
					    SendMessageW(handle, LB_GETCURSEL, 0, 0);

					// NOTE: LB_ERR if list unselected
					if (selectedIndex != LB_ERR)
					{
						DqnArray<Win32Program> *programArray =
						    &globalState.programArray;
						DQN_ASSERT(selectedIndex < programArray->count);

						Win32Program showProgram =
						    programArray->data[selectedIndex];
						LRESULT itemPid = SendMessageW(handle, LB_GETITEMDATA,
						                               selectedIndex, 0);
						DQN_ASSERT((u32)itemPid == showProgram.pid);
						SendMessageW(handle, LB_SETCURSEL, (WPARAM)-1, 0);
						win32_display_window(showProgram.window);
					}
				}
			}
		}
		break;

		case WM_CLOSE:
		case WM_DESTROY:
		{
			globalRunning = false;
		}
		break;

		case WM_HOTKEY:
		{
			win32_display_window(window);
			HWND editBox = globalState.window[winjumpwindow_input_search_entries];
			SetFocus(editBox);
		}
		break;

		case WM_SIZE:
		{
			RECT clientRect;
			GetClientRect(window, &clientRect);
			LONG clientWidth  = clientRect.right;
			LONG clientHeight = clientRect.bottom;
			const i32 margin  = 5;

			RECT statusBarRect;
			GetClientRect(globalState.window[winjumpwindow_status_bar],
			              &statusBarRect);
			LONG statusBarHeight = statusBarRect.bottom;
			LONG statusBarWidth  = statusBarRect.right;
			////////////////////////////////////////////////////////////////////
			// Position Edit Box and List Box
			////////////////////////////////////////////////////////////////////
			{
				// Resize the edit box that is used for filtering
				HWND editWindow =
				    globalState.window[winjumpwindow_input_search_entries];

				DqnV2 editP    = dqn_v2i(margin, margin);
				i32 editWidth  = clientWidth - (2 * margin);
				i32 editHeight = 25;

				MoveWindow(editWindow, (i32)editP.x, (i32)editP.y, editWidth,
				           editHeight, TRUE);

				// Resize the list window
				HWND listWindow =
				    globalState.window[winjumpwindow_list_window_entries];
				DqnV2 listP = dqn_v2(editP.x, (editP.y + editHeight + margin));
				i32 listWidth  = editWidth;
				i32 listHeight = clientHeight - (i32)listP.y - statusBarHeight;

				MoveWindow(listWindow, (i32)listP.x, (i32)listP.y, listWidth,
				           listHeight, TRUE);
			}

			////////////////////////////////////////////////////////////////////
			// Re-configure Status Bar on Resize
			////////////////////////////////////////////////////////////////////
			{
				HWND status = globalState.window[winjumpwindow_status_bar];
				// Pass through message so windows can handle anchoring the bar
				SendMessage(status, WM_SIZE, wParam, lParam);

				// Setup the parts of the status bar
				const WPARAM numParts  = 3;
				i32 partsPos[numParts] = {};

				i32 partsInterval = statusBarWidth / numParts;
				for (i32 i = 0; i < numParts; i++)
					partsPos[i] = partsInterval * (i + 1);
				SendMessageW(status, SB_SETPARTS, numParts, (LPARAM)partsPos);
			}
			result = DefWindowProcW(window, msg, wParam, lParam);
		}
		break;

		default:
		{
			result = DefWindowProcW(window, msg, wParam, lParam);
		}
		break;

	}

	return result;
}

void winjump_update()
{
	DqnArray<Win32Program> *programArray = &globalState.programArray;
	HWND listBox = globalState.window[winjumpwindow_list_window_entries];

	////////////////////////////////////////////////////////////////////////////
	// Insert new programs into the list box and remove dead ones
	////////////////////////////////////////////////////////////////////////////
	{
		DQN_ASSERT(dqn_darray_clear(programArray));
		EnumWindows(win32_enum_procs_callback, (LPARAM)programArray);

		const LRESULT listFirstVisibleIndex =
		    SendMessageW(listBox, LB_GETTOPINDEX, 0, 0);

		// Check displayed list entries against our new enumerated programs list
		i32 programArraySize   = (i32)programArray->count;
		const LRESULT listSize = SendMessageW(listBox, LB_GETCOUNT, 0, 0);
		for (LRESULT index = 0;
		     (index < listSize) && (index < programArraySize); index++)
		{
			Win32Program *currProgram = &programArray->data[index];

			// TODO(doyle): Tighten memory alloc using len vars in program
			// TODO(doyle): snprintf?

			// IMPORTANT: We set item data for the entry, so the entry
			// must exist before we check PID. We create this entry when
			// we check the string to see if it exists as the index.
			// NOTE: +4 for the " - " and the null terminator
			const i32 len = DQN_ARRAY_COUNT(currProgram->title) +
			                DQN_ARRAY_COUNT(currProgram->exe) + 4;
			wchar_t friendlyName[len] = {};
			winjump_get_program_friendly_name(currProgram, friendlyName, len);

			wchar_t entry[len] = {};
			LRESULT entryLen =
			    SendMessageW(listBox, LB_GETTEXT, index, (LPARAM)entry);
			if (wstrcmp(friendlyName, entry) != 0)
			{
				LRESULT insertIndex = SendMessageW(listBox, LB_INSERTSTRING,
				                                   index, (LPARAM)friendlyName);

				LRESULT itemCount =
				    SendMessageW(listBox, LB_DELETESTRING, index + 1, 0);
			}

			// Compare list entry item data, pid
			LRESULT entryPid =
			    SendMessageW(listBox, LB_GETITEMDATA, index, 0);
			if (currProgram->pid != (DWORD)entryPid)
			{
				LRESULT result = SendMessageW(listBox, LB_SETITEMDATA, index,
				                              currProgram->pid);
			}
		}

		// Fill the remainder of the list
		if (listSize < programArraySize)
		{
			for (i32 i = listSize; i < programArraySize; i++)
			{
				Win32Program *program = &programArray->data[i];
				const i32 len         = DQN_ARRAY_COUNT(program->title) +
				                DQN_ARRAY_COUNT(program->exe) + 4;
				wchar_t friendlyName[len] = {};
				winjump_get_program_friendly_name(program, friendlyName, len);

				LRESULT insertIndex = SendMessageW(listBox, LB_ADDSTRING, 0,
				                                   (LPARAM)friendlyName);
				LRESULT result = SendMessageW(listBox, LB_SETITEMDATA,
				                              insertIndex, program->pid);
			}
		}
		else
		{
			for (i32 i = programArraySize; i < listSize; i++)
			{
				LRESULT result = SendMessageW(listBox, LB_DELETESTRING, i, 0);
			}
		}

		SendMessageW(listBox, LB_SETTOPINDEX, listFirstVisibleIndex, 0);
	}

	// TODO(doyle): Currently filtering will refilter the list every
	// time, causing reflashing on the screen as the control updates
	// multiple times per second. We should have a "changed" flag or
	// something so we can just keep the old list if nothing has
	// changed or, only change the ones that have changed.

	////////////////////////////////////////////////////////////////////////////
	// Update List if there's any search filtering
	////////////////////////////////////////////////////////////////////////////
	{
		HWND editBox = globalState.window[winjumpwindow_input_search_entries];

		// NOTE: Set first char is size of buffer as required by win32
		wchar_t searchString[WIN32_MAX_PROGRAM_TITLE] = {};
		searchString[0] = DQN_ARRAY_COUNT(searchString);

		LRESULT searchStringLen =
		    SendMessageW(editBox, EM_GETLINE, 0, (LPARAM)searchString);
		wchar_str_to_lower(searchString, searchStringLen);
		if (searchStringLen > 0)
		{
			globalState.currentlyFiltering = true;
			DQN_ASSERT(searchStringLen < WIN32_MAX_PROGRAM_TITLE);

			u64 programArraySize = programArray->count;
			for (i32 i = 0; i < programArraySize; i++)
			{
				Win32Program *program = &programArray->data[i];
				const i32 friendlyNameLen = DQN_ARRAY_COUNT(program->title) +
				                            DQN_ARRAY_COUNT(program->exe) + 4;
				wchar_t friendlyName[friendlyNameLen] = {};
				winjump_get_program_friendly_name(program, friendlyName,
				                                  friendlyNameLen);

				wchar_t *searchPtr = searchString;
				if (!wchar_has_substring(searchString, searchStringLen,
				                         friendlyName, friendlyNameLen))
				{
					// If search string doesn't match, delete it from display
					LRESULT result =
					    SendMessageW(listBox, LB_DELETESTRING, i, 0);
					DQN_ASSERT(dqn_darray_remove_stable(programArray, i--));

					// Update index so we continue iterating over the correct
					// elements after removing it from the list since the for
					// loop is post increment and we're removing elements from
					// the list
					programArraySize--;
				}
			}
		}
	}

}

FILE_SCOPE void winjump_unit_test_local_functions()
{
	DQN_ASSERT(wchar_to_lower(L'A') == L'a');
	DQN_ASSERT(wchar_to_lower(L'a') == L'a');
	DQN_ASSERT(wchar_to_lower(L' ') == L' ');

	{
		wchar_t *a = L"Microsoft";
		wchar_t *b = L"icro";
		i32 lenA   = wstrlen(a);
		i32 lenB   = wstrlen(b);
		DQN_ASSERT(wchar_has_substring(a, lenA, b, lenB) == true);
		DQN_ASSERT(wchar_has_substring(a, lenA, L"iro", wstrlen(L"iro")) ==
		           false);
		DQN_ASSERT(wchar_has_substring(b, lenB, a, lenA) == true);
		DQN_ASSERT(wchar_has_substring(L"iro", wstrlen(L"iro"), a, lenA) ==
		           false);
		DQN_ASSERT(wchar_has_substring(L"", 0, L"iro", 4) == false);
		DQN_ASSERT(wchar_has_substring(L"", 0, L"", 0) == false);
		DQN_ASSERT(wchar_has_substring(NULL, 0, NULL, 0) == false);
	}

	{
		wchar_t *a = L"Micro";
		wchar_t *b = L"irob";
		i32 lenA   = wstrlen(a);
		i32 lenB   = wstrlen(b);
		DQN_ASSERT(wchar_has_substring(a, lenA, b, lenB) == false);
		DQN_ASSERT(wchar_has_substring(b, lenB, a, lenA) == false);
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nShowCmd)
{
	winjump_unit_test_local_functions();

	WNDCLASSEXW wc = {
	    sizeof(WNDCLASSEX),
	    CS_HREDRAW | CS_VREDRAW,
	    win32_main_callback,
	    0, // int cbClsExtra
	    0, // int cbWndExtra
	    hInstance,
	    LoadIcon(NULL, IDI_APPLICATION),
	    LoadCursor(NULL, IDC_ARROW),
	    GetSysColorBrush(COLOR_3DFACE),
	    L"", // LPCTSTR lpszMenuName
	    L"WinjumpWindowClass",
	    NULL, // HICON hIconSm
	};

	if (!RegisterClassExW(&wc))
	{
		DQN_WIN32_ERROR_BOX("RegisterClassExW() failed.", NULL);
		return -1;
	}

	// NOTE: Regarding Window Sizes
	// If you specify a window size, e.g. 800x600, Windows regards the 800x600
	// region to be inclusive of the toolbars and side borders. So in actuality,
	// when you blit to the screen blackness, the area that is being blitted to
	// is slightly smaller than 800x600. Windows provides a function to help
	// calculate the size you'd need by accounting for the window style.
	RECT r   = {};
	r.right  = 450;
	r.bottom = 200;

	DWORD windowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	AdjustWindowRect(&r, windowStyle, FALSE);

	globalRunning           = true;
	HWND mainWindow         = CreateWindowExW(
	    WS_EX_COMPOSITED, wc.lpszClassName,
	    L"Winjump | Press Alt-K to activate Winjump", windowStyle,
	    CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL,
	    NULL, hInstance, NULL);

	if (!mainWindow)
	{
		DQN_WIN32_ERROR_BOX("CreateWindowExW() failed.", NULL);
		return -1;
	}

	if (!dqn_darray_init(&globalState.programArray, 4))
	{
		DQN_WIN32_ERROR_BOX("dqn_darray_init() failed: Not enough memory.",
		                    NULL);
		return -1;
	}

	globalState.defaultWindowProc = win32_main_callback;
	globalState.window[winjumpwindow_main_client] = mainWindow;

#define GUID_HOTKEY_ACTIVATE_APP 10983
	RegisterHotKey(mainWindow, GUID_HOTKEY_ACTIVATE_APP, MOD_ALT, 'K');

	const f32 targetFramesPerSecond = 8.0f;
	f32 targetSecondsPerFrame       = 1 / targetFramesPerSecond;
	f32 targetMsPerFrame            = targetSecondsPerFrame * 1000.0f;
	f32 frameTimeInS                = 0.0f;

	while (globalRunning)
	{
		f64 startFrameTime = dqn_time_now_in_ms();

		MSG msg;
		while (PeekMessageW(&msg, mainWindow, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		winjump_update();
		////////////////////////////////////////////////////////////////////////
		// Update Status Bar
		////////////////////////////////////////////////////////////////////////
		HWND status = globalState.window[winjumpwindow_status_bar];
		{
			// Active Windows text in Status Bar
			{
				WPARAM partToDisplayAt = 2;
				char text[32]          = {};
				stbsp_sprintf(text, "Active Windows: %d",
				              globalState.programArray.count);
				SendMessage(status, SB_SETTEXT, partToDisplayAt, (LPARAM)text);
			}

			// Mem usage text in Status Bar
			{
				PROCESS_MEMORY_COUNTERS memCounter = {};
				if (GetProcessMemoryInfo(GetCurrentProcess(), &memCounter,
				                         sizeof(memCounter)))
				{
					WPARAM partToDisplayAt = 1;
					char text[32]          = {};
					stbsp_sprintf(text, "Memory: %'dkb",
					              (u32)(memCounter.WorkingSetSize / 1024.0f));
					SendMessage(status, SB_SETTEXT, partToDisplayAt,
					            (LPARAM)text);
				}
			}
		}

		////////////////////////////////////////////////////////////////////////
		// Frame Limiting
		////////////////////////////////////////////////////////////////////////
		f64 endWorkTime  = dqn_time_now_in_ms();
		f64 workTimeInMs = endWorkTime - startFrameTime;

		if (workTimeInMs < targetMsPerFrame)
		{
			DWORD remainingTimeInMs = (DWORD)(targetMsPerFrame - workTimeInMs);
			Sleep(remainingTimeInMs);
		}

		f64 endFrameTime  = dqn_time_now_in_ms();
		f64 frameTimeInMs = endFrameTime - startFrameTime;

		// Ms Per Frame text in Status Bar
		{
			WPARAM partToDisplayAt = 0;
			char text[32]          = {};
			stbsp_sprintf(text, "MsPerFrame: %.2f", (f32)frameTimeInMs);
			SendMessage(status, SB_SETTEXT, partToDisplayAt, (LPARAM)text);
		}
	}

	return 0;
}
