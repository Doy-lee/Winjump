#define UNICODE
#define _UNICODE

#ifndef VC_EXTRALEAN
  #define VC_EXTRALEAN
#endif

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#include "Common.h"

#include <Windows.h>
#include <Windowsx.h>
#include <commctrl.h>

// TODO(doyle): Organise all our globals siting around.
// TODO(doyle): Platform layer separation
// TODO(doyle): Store process EXE name so we can also search by that in the text
// filtering
// TODO(doyle): Search by index in list to help drilldown after the initial
// search
// TODO(doyle): Stop screen flickering by having better listbox updating
// mechanisms
// TODO(doyle): Clean up this cesspool.

GLOBAL_VAR bool globalRunning;

typedef struct Win32Program
{
	wchar_t title[256];
	i32     titleLen;

	HWND    handle;
	DWORD   pid;
} Win32Program;

enum WinjumpWindows
{
	winjumpwindows_main_client,
	winjumpwindows_list_window_entries,
	winjumpwindows_input_search_entries,
	winjumpwindows_count,
};

typedef struct Win32ProgramArray
{
	Win32Program item[128];
	i32          index;
} Win32ProgramArray;

typedef struct WinjumpState
{
	HFONT defaultFont;
	HWND  window[winjumpwindows_count];

	WNDPROC defaultWindowProc;
	WNDPROC defaultWindowProcEditBox;
	Win32ProgramArray programArray;

	// TODO(doyle): Use this flag to reduce the times the window reupdates
	bool currentlyFiltering;
} WinjumpState;

enum Win32Resources
{
	win32resource_edit_text_buffer,
	win32resource_list,
};

#define OFFSET_TO_STATE_PTR 0
#define MAX_WINDOW_TITLE_LEN ARRAY_COUNT(((Win32Program *)0)->title)

FILE_SCOPE void winjump_displayWindow(HWND windowHandle)
{

	// IsIconic == if window is minimised
	if (IsIconic(windowHandle))
	{
		ShowWindow(windowHandle, SW_RESTORE);
	}

	SetForegroundWindow(windowHandle);
}

BOOL CALLBACK EnumWindowsProcCallback(HWND windowHandle, LPARAM lParam)
{
	Win32ProgramArray *programArray = (Win32ProgramArray *)lParam;
	if ((programArray->index + 1) < ARRAY_COUNT(programArray->item))
	{
		Win32Program window = {};
		i32 titleLen =
		    GetWindowText(windowHandle, window.title, MAX_WINDOW_TITLE_LEN);
		window.titleLen = titleLen;

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

			HWND rootWindow = GetAncestor(windowHandle, GA_ROOTOWNER);
			HWND lastPopup;
			do
			{
				lastPopup = GetLastActivePopup(rootWindow);
				if (IsWindowVisible(lastPopup) && lastPopup == windowHandle)
				{
					GetWindowThreadProcessId(windowHandle, &window.pid);
					window.handle                              = windowHandle;
					programArray->item[programArray->index++] = window;
					break;
				}

				rootWindow = lastPopup;
				lastPopup  = GetLastActivePopup(rootWindow);
			} while (lastPopup != rootWindow);
		}

		return true;
	}
	else
	{
		// NOTE(doyle): Returning false will stop any new window enumeration
		// results
		return false;
	}
}

FILE_SCOPE LRESULT CALLBACK captureEnterWindowProcCallback(HWND editWindow,
                                                           UINT msg,
                                                           WPARAM wParam,
                                                           LPARAM lParam)
{
	WinjumpState *state =
	    (WinjumpState *)GetWindowLongPtr(editWindow, GWLP_USERDATA);

	LRESULT result = 0;
	switch (msg)
	{
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP:
		case WM_KEYDOWN:
		case WM_KEYUP:
		{
			u32 vkCode = wParam;
			// bool keyWasDown = ((lParam & (1 << 30)) != 0);
			// bool keyIsDown = ((lParam & (1 << 31)) == 0);

			switch (vkCode)
			{
				case VK_RETURN:
				{
					if (state->programArray.index > 0)
					{
						Win32Program programToShow =
						    state->programArray.item[0];
						winjump_displayWindow(programToShow.handle);
						SendMessage(editWindow, WM_SETTEXT, 0, (LPARAM)L"");
					}
				}
				break;
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
					    state->window[winjumpwindows_input_search_entries];
					SetWindowText(inputBox, L"");
				}
			}
		}

		default:
		{
			return CallWindowProc(state->defaultWindowProcEditBox, editWindow,
			                      msg, wParam, lParam);
		}
		break;
	}

	return result;
}

FILE_SCOPE LRESULT CALLBACK mainWindowProcCallback(HWND window, UINT msg,
                                                   WPARAM wParam, LPARAM lParam)
{
	LRESULT result = 0;

	WinjumpState *state = nullptr;
	if (msg == WM_CREATE)
	{
		CREATESTRUCT *win32DeriveStruct = (CREATESTRUCT *)lParam;
		state = (WinjumpState *)win32DeriveStruct->lpCreateParams;
		SetWindowLongPtr(window, OFFSET_TO_STATE_PTR, (LONG_PTR)state);
	}
	else
	{
		state = (WinjumpState *)GetWindowLongPtr(window, OFFSET_TO_STATE_PTR);
	}

	switch (msg)
	{
		case WM_CREATE:
		{
			RECT clientRect;
			GetClientRect(window, &clientRect);

			i32 margin = 5;
			LONG clientWidth  = clientRect.right;
			LONG clientHeight = clientRect.bottom;

			v2 editP       = V2i(margin, margin);
			i32 editWidth  = clientWidth - (2 * margin);
			i32 editHeight = 25;

			HWND editHandle = CreateWindowEx(
			  WS_EX_CLIENTEDGE,
			  L"EDIT",
			  NULL,
			  WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP,
			  (i32)editP.x,
			  (i32)editP.y,
			  editWidth,
			  editHeight,
			  window,
			  (HMENU)win32resource_edit_text_buffer,
			  NULL,
			  NULL
			);
			state->window[winjumpwindows_input_search_entries] = editHandle;
			SetFocus(editHandle);
			state->defaultWindowProcEditBox =
			    (WNDPROC)SetWindowLongPtr(editHandle, GWLP_WNDPROC,
			                     (LONG_PTR)captureEnterWindowProcCallback);
			SetWindowLongPtr(editHandle, GWLP_USERDATA, (LONG_PTR)state);

			v2 listP       = V2(editP.x, (editP.y + editHeight + margin));
			i32 listWidth  = clientWidth - (2 * margin);
			i32 listHeight = clientHeight - (i32)editP.y - editHeight - margin;

			HWND listHandle = CreateWindowEx(
			  WS_EX_CLIENTEDGE,
			  L"LISTBOX",
			  NULL,
			  WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |  WS_HSCROLL
			  | LBS_NOTIFY,
			  (i32)listP.x,
			  (i32)listP.y,
			  listWidth,
			  listHeight,
			  window,
			  (HMENU)win32resource_list,
			  NULL,
			  NULL
			);
			state->window[winjumpwindows_list_window_entries] = listHandle;

			{ // Set To Use Default Font
				NONCLIENTMETRICS metrics = {};
				metrics.cbSize           = sizeof(NONCLIENTMETRICS);

				i32 readFontResult =
				    SystemParametersInfo(SPI_GETNONCLIENTMETRICS,
				                         sizeof(NONCLIENTMETRICS), &metrics, 0);

				if (readFontResult)
				{
					state->defaultFont =
					    CreateFontIndirect(&metrics.lfMessageFont);
					bool redrawImmediately = true;
					for (i32 i = 0; i < winjumpwindows_count; i++)
					{
						HWND windowToSendMsg = state->window[i];
						SendMessage(windowToSendMsg, WM_SETFONT,
						            (WPARAM)state->defaultFont,
						            redrawImmediately);
					}
					// TODO(doyle): Clean up, DeleteObject(defaultFont);
				}
				else
				{
					// TODO(doyle): Logging, default font query failed
					ASSERT(INVALID_CODE_PATH);
				}
			}
		}
		break;

		case WM_COMMAND:
		{
			WORD notificationCode = HIWORD((DWORD)wParam);
			HWND handle           = (HWND)lParam;
			if (handle == state->window[win32resource_list])
			{
				if (notificationCode == LBN_SELCHANGE)
				{
					Win32ProgramArray programArray = state->programArray;
					LRESULT selectedIndex =
					    SendMessage(handle, LB_GETCURSEL, 0, 0);

					// NOTE: LB_ERR if list unselected
					if (selectedIndex != LB_ERR)
					{
						ASSERT(selectedIndex < ARRAY_COUNT(programArray.item));

						Win32Program programToShow =
						    programArray.item[selectedIndex];
						LRESULT itemPid = SendMessage(handle, LB_GETITEMDATA,
						                              selectedIndex, 0);
						ASSERT((u32)itemPid == programToShow.pid);

						SendMessage(handle, LB_SETCURSEL, (WPARAM)-1, 0);

						winjump_displayWindow(programToShow.handle);
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
			winjump_displayWindow(window);

			HWND editBox = state->window[winjumpwindows_input_search_entries];
			SetFocus(editBox);
		}
		break;

		case WM_SIZE:
		{
		}
		break;

		default:
		{
			result = DefWindowProc(window, msg, wParam, lParam);
		}
		break;

	}

	return result;
}

GLOBAL_VAR LARGE_INTEGER globalQueryPerformanceFrequency;
inline FILE_SCOPE f32 getTimeFromQueryPerfCounter(LARGE_INTEGER start,
                                                LARGE_INTEGER end)
{
	f32 result = (f32)(end.QuadPart - start.QuadPart) /
	             globalQueryPerformanceFrequency.QuadPart;
	return result;
}

inline LARGE_INTEGER getWallClock()
{
	LARGE_INTEGER result;
	QueryPerformanceCounter(&result);

	return result;
}

void debug_checkWin32ListContents(HWND listBox, wchar_t listEntries[128][256])
{
	LRESULT listSize = SendMessage(listBox, LB_GETCOUNT, 0, 0);
	for (i32 i = 0; i < listSize; i++)
	{
		SendMessage(listBox, LB_GETTEXT, i, (LPARAM)listEntries[i]);
	}
}

i32 debug_getListEntrySize(wchar_t listEntries[128][256])
{
	i32 result = 0;
	for (i32 i = 0; i < 128; i++)
	{
		if (listEntries[i][0])
		{
			result++;
		}
		else
		{
			break;
		}
	}

	return result;
}

#include <stdio.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nShowCmd)
{
	i32 bytesToReserveAfterWindowInst = sizeof(WinjumpState *);

	WNDCLASSEX wc =
	{
		sizeof(WNDCLASSEX),
		CS_HREDRAW | CS_VREDRAW,
		mainWindowProcCallback,
		0, // int cbClsExtra
		bytesToReserveAfterWindowInst, // int cbWndExtra
		hInstance,
		LoadIcon(NULL, IDI_APPLICATION),
		LoadCursor(NULL, IDC_ARROW),
		GetSysColorBrush(COLOR_3DFACE),
		L"", // LPCTSTR lpszMenuName
		L"WinjumpWindowClass",
		NULL, // HICON hIconSm
	};

	if (!RegisterClassEx(&wc)) {
		// TODO(doyle): Logging, couldn't register class
		ASSERT(INVALID_CODE_PATH);
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
	WinjumpState state      = {};
	state.defaultWindowProc = mainWindowProcCallback;

	HWND mainWindowHandle =
	    CreateWindowEx(0, wc.lpszClassName, L"Winjump", windowStyle,
	                   CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
	                   r.bottom - r.top, NULL, NULL, hInstance, &state);
	if (!mainWindowHandle)
	{
		// TODO(doyle): Logging, couldn't create root window
		ASSERT(INVALID_CODE_PATH);
		return -1;
	}
	state.window[winjumpwindows_main_client] = mainWindowHandle;

#define GUID_HOTKEY_ACTIVATE_APP 10983
	RegisterHotKey(mainWindowHandle, GUID_HOTKEY_ACTIVATE_APP, MOD_ALT, 'K');
	QueryPerformanceFrequency(&globalQueryPerformanceFrequency);

	LARGE_INTEGER startFrameTime;
	f32 targetSecondsPerFrame = 1 / 16.0f;
	f32 frameTimeInS = 0.0f;

	ASSERT(common_wcharAsciiToLowercase(L'A') == L'a');
	ASSERT(common_wcharAsciiToLowercase(L'a') == L'a');
	ASSERT(common_wcharAsciiToLowercase(L' ') == L' ');

	MSG msg;

	while (globalRunning)
	{
		startFrameTime = getWallClock();

		Win32ProgramArray *programArray = &state.programArray;
		HWND listBox = state.window[winjumpwindows_list_window_entries];

		// Insert new programs into the list box and remove dead ones
		{
			Win32ProgramArray emptyArray = {};
			*programArray                = emptyArray;
			ASSERT(programArray->index == 0);

			EnumWindows(EnumWindowsProcCallback, (LPARAM)programArray);

			const LRESULT listFirstVisibleIndex =
			    SendMessage(listBox, LB_GETTOPINDEX, 0, 0);
			const LRESULT listSize = SendMessage(listBox, LB_GETCOUNT, 0, 0);

			for (LRESULT itemIndex = 0;
			     (itemIndex < listSize) && (itemIndex < programArray->index);
			     itemIndex++)
			{
				Win32Program *currProgram = &programArray->item[itemIndex];

				// IMPORTANT: We set item data for the entry, so the entry
				// must exist before we check PID. We create this entry when
				// we check the string to see if it exists as the index.
				// Compare program title strings
				wchar_t entryString[ARRAY_COUNT(currProgram->title)] = {};
				LRESULT entryStringLen = SendMessage(
				    listBox, LB_GETTEXT, itemIndex, (LPARAM)entryString);

				if (common_wstrcmp(currProgram->title, entryString) != 0)
				{
					LRESULT insertIndex =
					    SendMessage(listBox, LB_INSERTSTRING, itemIndex,
					                (LPARAM)currProgram->title);

					LRESULT itemCount =
					    SendMessage(listBox, LB_DELETESTRING, itemIndex + 1, 0);
				}

				// Compare list entry item data, pid
				LRESULT listEntryPid =
				    SendMessage(listBox, LB_GETITEMDATA, itemIndex, 0);
				if (currProgram->pid != (DWORD)listEntryPid)
				{
					LRESULT result = SendMessage(listBox, LB_SETITEMDATA,
					                             itemIndex, currProgram->pid);
				}
			}

			if (listSize < programArray->index)
			{
				for (i32 i = listSize; i < programArray->index; i++)
				{
					Win32Program *program = &programArray->item[i];
					LRESULT insertIndex = SendMessage(listBox, LB_ADDSTRING, 0,
					                                  (LPARAM)program->title);
					LRESULT result = SendMessage(listBox, LB_SETITEMDATA,
					                             insertIndex, program->pid);
				}
			}
			else
			{
				wchar_t listEntries[128][256] = {};
				debug_checkWin32ListContents(listBox, listEntries);

				for (i32 i = programArray->index; i < listSize; i++)
				{
					LRESULT result =
					    SendMessage(listBox, LB_DELETESTRING, i, 0);

					wchar_t inLoopListEntries[128][256] = {};
					debug_checkWin32ListContents(listBox, inLoopListEntries);
				}
			}

			SendMessage(listBox, LB_SETTOPINDEX, listFirstVisibleIndex, 0);
		}

		// TODO(doyle): Currently filtering will refilter the list every
		// time, causing reflashing on the screen as the control updates
		// multiple times per second. We should have a "changed" flag or
		// something so we can just keep the old list if nothing has
		// changed or, only change the ones that have changed.

		// Get Line from Edit Control and filter list results
		{
			HWND editBox = state.window[winjumpwindows_input_search_entries];
			// NOTE: Set first char is size of buffer as required by win32
			wchar_t editBoxText[MAX_WINDOW_TITLE_LEN] = {};
			editBoxText[0]                            = MAX_WINDOW_TITLE_LEN;

			LRESULT numCharsCopied =
			    SendMessage(editBox, EM_GETLINE, 0, (LPARAM)editBoxText);
			if (numCharsCopied > 0)
			{
				state.currentlyFiltering = true;
				ASSERT(numCharsCopied < MAX_WINDOW_TITLE_LEN);

				for (i32 i = 0; i < programArray->index; i++)
				{
					Win32Program *program         = &programArray->item[i];
					wchar_t *titleWords[32]       = {};
					i32 titleWordsIndex           = 0;
					titleWords[titleWordsIndex++] = program->title;

					// TODO(doyle): Be smart, split by more than just
					// spaces, like file directories
					for (i32 j = 1; j + 1 < program->titleLen; j++)
					{
						wchar_t *checkSpace    = &program->title[j];
						wchar_t *oneAfterSpace = &program->title[j + 1];
						if (*checkSpace == L' ' && *oneAfterSpace != L' ')
						{
							// NOTE: Beginning of new word
							titleWords[titleWordsIndex++] = oneAfterSpace;
						}
					}

					bool textMatches = true;
					for (i32 j = 0; j < titleWordsIndex; j++)
					{
						textMatches        = true;
						wchar_t *editChar  = editBoxText;
						wchar_t *titleChar = titleWords[j];

						// TODO(doyle): Right now words are split by
						// spaces
						// and we semantically separate them by giving
						// out
						// ptrs to each word (from the same word
						// allocation), delimiting it by spaces
						for (; *editChar && *titleChar && *titleChar != ' ';
						     editChar++, titleChar++)
						{
							wchar_t editCharLowercase =
							    common_wcharAsciiToLowercase(*editChar);
							wchar_t titleCharLowercase =
							    common_wcharAsciiToLowercase(*titleChar);
							if (editCharLowercase != titleCharLowercase)
							{
								textMatches = false;
								break;
							}
						}

						if (textMatches) break;
					}

					if (!textMatches)
					{
						LRESULT result =
						    SendMessage(listBox, LB_DELETESTRING, i, 0);

						for (i32 j = i; j + 1 < programArray->index; j++)
						{
							programArray->item[j] = programArray->item[j + 1];
						}

						i--;
						programArray->index--;
						if (programArray->index >= 0)
						{
							Win32Program emptyProgram = {};
							programArray->item[programArray->index] =
							    emptyProgram;
						}
					}
				}
			}
		}

		while (PeekMessage(&msg, mainWindowHandle, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		LARGE_INTEGER endWorkTime = getWallClock();
		f32 workTimeInS =
		    getTimeFromQueryPerfCounter(startFrameTime, endWorkTime);

		if (workTimeInS < targetSecondsPerFrame)
		{
			DWORD remainingTimeInMs =
			    (DWORD)((targetSecondsPerFrame - workTimeInS) * 1000);
			Sleep(remainingTimeInMs);
		}

		LARGE_INTEGER endFrameTime = getWallClock();
		frameTimeInS =
		    getTimeFromQueryPerfCounter(startFrameTime, endFrameTime);
		f32 msPerFrame = 1000.0f * frameTimeInS;

		wchar_t windowTitleBuffer[128] = {};
		_snwprintf_s(windowTitleBuffer, ARRAY_COUNT(windowTitleBuffer),
		             ARRAY_COUNT(windowTitleBuffer), L"Winjump | %5.2f ms/f",
		             msPerFrame);
		SetWindowText(mainWindowHandle, windowTitleBuffer);
	}

	return 0;
}
