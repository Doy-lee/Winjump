#define DQN_IMPLEMENTATION // Enable the implementation
#define DQN_MAKE_STATIC    // Make all functions be static
#include "dqn.h"

#ifndef VC_EXTRALEAN
  #define VC_EXTRALEAN
#endif

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <Windowsx.h>
#include <Shlwapi.h>
#include <commctrl.h>

// For win32 GetProcessMemoryInfo()
#include <Psapi.h>

#include <stdio.h>

// TODO(doyle): Safer subclassing?
// https://blogs.msdn.microsoft.com/oldnewthing/20031111-00/?p=41883/

// TODO(doyle): Organise all our globals siting around.
// TODO(doyle): Search by index in list to help drilldown after the initial
// search
// TODO(doyle): Stop screen flickering by having better listbox updating
// mechanisms
// TODO(doyle): Clean up this cesspool.

// Returns length without null terminator, returns 0 if NULL
FILE_SCOPE i32 wstrlen(const wchar_t *const a)
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

FILE_SCOPE bool wchar_is_digit(const wchar_t c)
{
	if (c >= L'0' && c <= L'9') return true;
	return false;
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

FILE_SCOPE inline bool wchar_has_substring(const wchar_t *const a, const i32 lenA,
                                           const wchar_t *const b, const i32 lenB)
{
	if (!a || !b) return false;
	if (lenA == 0 || lenB == 0) return false;

	const wchar_t *longStr, *shortStr;
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

		const wchar_t *longSubstr = &longStr[indexIntoLong];
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

FILE_SCOPE inline void wchar_str_to_lower(wchar_t *const a, const i32 len)
{
	for (i32 i = 0; i < len; i++)
		a[i]   = wchar_to_lower(a[i]);
}

FILE_SCOPE i32 wchar_str_to_i32(const wchar_t *const buf, const i32 bufSize)
{
	if (!buf || bufSize == 0) return 0;

	i32 index       = 0;
	bool isNegative = false;
	if (buf[index] == L'-' || buf[index] == L'+')
	{
		if (buf[index] == L'-') isNegative = true;
		index++;
	}
	else if (!wchar_is_digit(buf[index]))
	{
		return 0;
	}

	i32 result = 0;
	for (i32 i = index; i < bufSize; i++)
	{
		if (wchar_is_digit(buf[i]))
		{
			result *= 10;
			result += (buf[i] - L'0');
		}
		else
		{
			break;
		}
	}

	if (isNegative) result *= -1;

	return result;
}


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
#define FRIENDLY_NAME_LEN 250
FILE_SCOPE i32 winjump_get_program_friendly_name(const Win32Program *program,
                                                 i32 programIndex, wchar_t *out,
                                                 i32 outLen)
{

	// Friendly Name Format
	// <Index>: <Program Title> - <Program Exe>

	// For example
	// 1: Google Search - firefox.exe
	// 2: Winjump.cpp + (C:\winjump.cpp) - GVIM64 - firefox.exe
	i32 numStored =
	    _snwprintf_s(out, outLen, outLen, L"%2d: %s - %s", programIndex + 1,
	                 program->title, program->exe);
	DQN_ASSERT(numStored < FRIENDLY_NAME_LEN);

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

FILE_SCOPE LRESULT CALLBACK win32_list_box_callback(HWND window, UINT msg,
                                                    WPARAM wParam,
                                                    LPARAM lParam)
{
	LRESULT result = 0;
	switch (msg)
	{
		case WM_ERASEBKGND:
		{
			
			// NOTE: Returning true overrides background erasing which can
			// reduce flicker. But if we do so, slowly dragging the list box
			// will show the black erased background because, I think,
			// WM_PAINT's aren't being called but something else, so our
			// FillRect is not being called.
#if 0
			return true;
#else
			return CallWindowProcW(globalState.defaultWindowProcListBox, window,
			                       msg, wParam, lParam);
#endif
		}
		break;

		case WM_PAINT:
		{
			RECT clientRect;
			GetClientRect(window, &clientRect);

			PAINTSTRUCT paint = {};
			HDC deviceContext = BeginPaint(window, &paint);
			FillRect(deviceContext, &clientRect,
			         GetSysColorBrush(COLOR_WINDOW));
			CallWindowProcW(globalState.defaultWindowProcListBox, window,
			                WM_PAINT, (WPARAM)deviceContext, (LPARAM)0);
			BitBlt(deviceContext, 0, 0, clientRect.right, clientRect.bottom,
			       deviceContext, 0, 0, SRCCOPY);
			EndPaint(window, &paint);
		}
		break;

		default:
		{
			return CallWindowProcW(globalState.defaultWindowProcListBox, window,
			                       msg, wParam, lParam);
		}
		break;
	}

	return result;
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

enum Win32Menu
{
	win32menu_file_exit,
	win32menu_options_font,
	win32menu_count,
};

FILE_SCOPE void win32_menu_create(HWND window)
{
	HMENU menuBar  = CreateMenu();
	{ // File Menu
		HMENU menu = CreatePopupMenu();
		AppendMenuW(menuBar, MF_STRING | MF_POPUP, (UINT)menu, L"File");
		AppendMenuW(menu, MF_STRING, win32menu_file_exit, L"Exit");
	}

	{ // Options Menu
		HMENU menu = CreatePopupMenu();
		AppendMenuW(menuBar, MF_STRING | MF_POPUP, (UINT)menu, L"Options");
		AppendMenuW(menu, MF_STRING, win32menu_options_font, L"Font");
	}

	SetMenu(window, menuBar);
}

FILE_SCOPE void winjump_font_change(WinjumpState *const state, const HFONT font)
{
	if (font)
	{
		DeleteObject(state->font);
		state->font = font;

		bool redrawImmediately = false;
		for (i32 i = 0; i < winjumpwindow_count; i++)
		{
			HWND targetWindow = globalState.window[i];
			SendMessage(targetWindow, WM_SETFONT, (WPARAM)font,
			            redrawImmediately);
		}
	}
	else
	{
		DQN_WIN32_ERROR_BOX("winjump_font_change() failed: Font was NULL.",
		                    NULL);
	}
}

// NOTE: Resizing the search box will readjust elements dependent on its size
FILE_SCOPE void winjump_resize_search_box(WinjumpState *const state,
                                          i32 newWidth,
                                          i32 newHeight,
                                          const bool ignoreWidth,
                                          const bool ignoreHeight)
{
	const i32 MARGIN = 5;
	HWND editWindow  = globalState.window[winjumpwindow_input_search_entries];

	LONG origWidth, origHeight;
	dqn_win32_get_client_dim(editWindow, &origWidth, &origHeight);
	if (ignoreWidth)  newWidth  = origWidth;
	if (ignoreHeight) newHeight = origHeight;

	// Resize the edit box that is used for filtering
	DqnV2 editP = dqn_v2i(MARGIN, MARGIN);
	MoveWindow(editWindow, (i32)editP.x, (i32)editP.y, newWidth, newHeight,
	           TRUE);

	// Resize the list window
	{
		LONG statusBarHeight;
		dqn_win32_get_client_dim(globalState.window[winjumpwindow_status_bar],
		                         NULL, &statusBarHeight);

		LONG clientWidth, clientHeight;
		dqn_win32_get_client_dim(globalState.window[winjumpwindow_main_client],
		                         &clientWidth, &clientHeight);

		HWND listWindow = state->window[winjumpwindow_list_program_entries];
		DqnV2 listP     = dqn_v2(editP.x, (editP.y + newHeight + MARGIN));
		i32 listWidth   = newWidth;
		i32 listHeight  = clientHeight - (i32)listP.y - statusBarHeight;

		MoveWindow(listWindow, (i32)listP.x, (i32)listP.y, listWidth,
		           listHeight, TRUE);
	}
}

#define WINJUMP_STRING_TO_CALC_HEIGHT "H"
FILE_SCOPE void win32_font_calculate_dim(HDC deviceContext, HFONT font,
                                         char *string, LONG *width,
                                         LONG *height)
{
	if (!string) return;

	// NOTE: For some reason, even after sending WM_SETFONT to all Winjump
	// windows, using the DC to draw a text doesn't reflect the new font. We
	// track the current font, so just use that to calculate the search-size
	SelectObject(deviceContext, font);
	RECT rect = {};

	// Draw text non-visibly, output dimensions to rect
	DrawText(deviceContext, string, -1, &rect, DT_CALCRECT);

	if (width)  *width  = rect.right  - rect.left;
	if (height) *height = rect.bottom - rect.top;
}

FILE_SCOPE LRESULT CALLBACK win32_main_callback(HWND window, UINT msg,
                                                WPARAM wParam, LPARAM lParam)
{
	LRESULT result = 0;
	switch (msg)
	{
		case WM_CREATE:
		{
			win32_menu_create(window);
			globalState.defaultWindowProc                 = win32_main_callback;
			globalState.window[winjumpwindow_main_client] = window;

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
			  NULL,
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
			  NULL,
			  NULL,
			  NULL
			);
			globalState.defaultWindowProcListBox = (WNDPROC)SetWindowLongPtrW(
			    listWindow, GWLP_WNDPROC,
			    (LONG_PTR)win32_list_box_callback);
			globalState.window[winjumpwindow_list_program_entries] = listWindow;

			////////////////////////////////////////////////////////////////////
			// Create Status Bar
			////////////////////////////////////////////////////////////////////
			InitCommonControls();
			HWND statusWindow = CreateWindowExW(
			    WS_EX_COMPOSITED,
			    STATUSCLASSNAMEW,          // name of status bar class
			    NULL,                      // no text when first created
			    SBARS_SIZEGRIP |           // includes a sizing grip
			        WS_CHILD | WS_VISIBLE, // creates a visible child window
			    0,
			    0, 0, 0, // ignores size and position
			    window,  // parent
			    NULL,    // child window identifier
			    NULL,    // handle to application instance
			    NULL);
			globalState.window[winjumpwindow_status_bar] = statusWindow;

			////////////////////////////////////////////////////////////////////
			// Use Default Font
			////////////////////////////////////////////////////////////////////
			HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
			winjump_font_change(&globalState, font);
		}
		break;

		case WM_COMMAND:
		{

			// If command from control then,
			// HIWORD(wParam) = ControlNotification
			// LOWORD(wParam) = ControlID
			// lParam         = Control Handle
			HWND handle = (HWND)lParam;
			if (handle ==
			    globalState.window[winjumpwindow_list_program_entries])
			{
				WORD notificationCode = HIWORD((DWORD)wParam);
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
				break;
			}

			// If command from a menu item,
			// HIWORD(wParam) = 0
			// LOWORD(wParam) = MenuID
			// lParam         = 0
			if ((HIWORD(wParam) == 0 && lParam == 0) &&
			    (LOWORD(wParam) >= 0 && LOWORD(wParam) < win32menu_count))
			{
				switch (LOWORD(wParam))
				{
					case win32menu_file_exit:
					{
						globalRunning = false;
					}
					break;

					case win32menu_options_font:
					{
						LOGFONTW chosenFont = {};
						GetObjectW(globalState.font, sizeof(chosenFont),
						          &chosenFont);

						CHOOSEFONTW chooseFont = {};
						chooseFont.lStructSize = sizeof(chooseFont);
						chooseFont.hwndOwner   = window;
						chooseFont.lpLogFont   = &chosenFont;
						chooseFont.Flags =
						    CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;

						if (ChooseFontW(&chooseFont))
						{
							HFONT font = CreateFontIndirectW(&chosenFont);
							if (font)
							{
								winjump_font_change(&globalState, font);
								HWND editWindow =
								    globalState.window
								        [winjumpwindow_input_search_entries];
								HDC deviceContext = GetDC(editWindow);

								LONG newHeight;
								win32_font_calculate_dim(
								    deviceContext, globalState.font,
								    WINJUMP_STRING_TO_CALC_HEIGHT, NULL,
								    &newHeight);
								ReleaseDC(editWindow, deviceContext);

								newHeight = (LONG)(newHeight * 1.5f);
								winjump_resize_search_box(
								    &globalState, 0, newHeight, true, false);
							}
							else
							{
								DQN_WIN32_ERROR_BOX(
								    "CreateFontIndirectW() failed", NULL);
							}
						}
					}
					break;

					default:
					{
						// Do nothing
					}
					break;
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
			LONG clientWidth, clientHeight;
			dqn_win32_get_client_dim(window, &clientWidth, &clientHeight);

			////////////////////////////////////////////////////////////////////
			// Re-configure Status Bar on Resize
			////////////////////////////////////////////////////////////////////
			{
				HWND status = globalState.window[winjumpwindow_status_bar];

				// Setup the parts of the status bar
				const WPARAM numParts  = 3;
				i32 partsPos[numParts] = {};

				i32 partsInterval = clientWidth / numParts;
				for (i32 i = 0; i < numParts; i++)
					partsPos[i] = partsInterval * (i + 1);
				SendMessageW(status, SB_SETPARTS, numParts, (LPARAM)partsPos);

				// Pass through message so windows can handle anchoring the bar
				SendMessage(status, WM_SIZE, wParam, lParam);
			}

			////////////////////////////////////////////////////////////////////
			// Adjust the search box and entry list
			////////////////////////////////////////////////////////////////////
			{
				HWND editWindow =
				    globalState.window[winjumpwindow_input_search_entries];

				const i32 MARGIN = 5;
				LONG searchHeight, searchWidth = clientWidth - (2 * MARGIN);

				HDC deviceContext = GetDC(editWindow);
				win32_font_calculate_dim(deviceContext, globalState.font,
				                         WINJUMP_STRING_TO_CALC_HEIGHT, NULL,
				                         &searchHeight);
				searchHeight = (LONG)(searchHeight * 1.85f);

				ReleaseDC(editWindow, deviceContext);

				// NOTE: This also re-positions the list because it depends on
				// the search box position
				winjump_resize_search_box(&globalState, searchWidth,
				                          searchHeight, false, false);
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
	HWND listBox = globalState.window[winjumpwindow_list_program_entries];
	i32 firstVisibleIndex = SendMessageW(listBox, LB_GETTOPINDEX, 0, 0);

	DQN_ASSERT(dqn_darray_clear(programArray));
	EnumWindows(win32_enum_procs_callback, (LPARAM)programArray);

	////////////////////////////////////////////////////////////////////////////
	// Filter program array if user is actively searching
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

			i32 userSpecifiedIndex = -1;
			for (i32 j = 0; j < searchStringLen && searchString[j]; j++)
			{
				if (wchar_is_digit(searchString[j]))
				{
					userSpecifiedIndex =
					    wchar_str_to_i32(&searchString[j], searchStringLen - j);
					break;
				}
			}

			u64 programArraySize = programArray->count;
			for (i32 index = 0; index < programArraySize; index++)
			{
				Win32Program *program = &programArray->data[index];
				wchar_t friendlyName[FRIENDLY_NAME_LEN] = {};
				winjump_get_program_friendly_name(
				    program, index, friendlyName,
				    DQN_ARRAY_COUNT(friendlyName));

				wchar_t *searchPtr = searchString;
				if (!wchar_has_substring(searchString, searchStringLen,
				                         friendlyName, FRIENDLY_NAME_LEN))
				{
					// If search string doesn't match, delete it from display
					DQN_ASSERT(dqn_darray_remove_stable(programArray, index--));
					// Update index so we continue iterating over the correct
					// elements after removing it from the list since the for
					// loop is post increment and we're removing elements from
					// the list
					programArraySize--;
				}
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// Compare internal list with list box and remove dead ones
	////////////////////////////////////////////////////////////////////////////
	{
		// Check displayed list entries against our new enumerated programs list
		i32 programArraySize   = (i32)programArray->count;
		const LRESULT listSize = SendMessageW(listBox, LB_GETCOUNT, 0, 0);
		for (LRESULT index = 0;
		     (index < listSize) && (index < programArraySize); index++)
		{
			Win32Program *program = &programArray->data[index];

			// TODO(doyle): Tighten memory alloc using len vars in program
			wchar_t friendlyName[FRIENDLY_NAME_LEN] = {};
			winjump_get_program_friendly_name(program, index, friendlyName,
			                                  DQN_ARRAY_COUNT(friendlyName));

			wchar_t entry[FRIENDLY_NAME_LEN] = {};
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
			if (program->pid != (DWORD)entryPid)
			{
				LRESULT result = SendMessageW(listBox, LB_SETITEMDATA, index,
				                              program->pid);
			}
		}

		// Fill the remainder of the list
		if (listSize < programArraySize)
		{
			for (i32 i = listSize; i < programArraySize; i++)
			{
				Win32Program *program = &programArray->data[i];
				wchar_t friendlyName[FRIENDLY_NAME_LEN] = {};
				winjump_get_program_friendly_name(
				    program, i, friendlyName, DQN_ARRAY_COUNT(friendlyName));

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

		SendMessageW(listBox, LB_SETTOPINDEX, firstVisibleIndex, 0);
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
	AdjustWindowRect(&r, windowStyle, true);

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

#define GUID_HOTKEY_ACTIVATE_APP 10983
	RegisterHotKey(mainWindow, GUID_HOTKEY_ACTIVATE_APP, MOD_ALT, 'K');

	const f32 targetFramesPerSecond = 24.0f;
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
					              (u32)(memCounter.PagefileUsage / 1024.0f));
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
