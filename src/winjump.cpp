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
GLOBAL_VAR bool globalRunning;

typedef struct Win32Program
{
	wchar_t title[256];
	HWND    handle;
	DWORD   pid;
} Win32Program;

enum WindowTypeIndex
{
	windowtype_main_client,
	windowtype_list_window_entries,
	windowtype_input_search_entries,
	windowtype_count,
};

typedef struct Win32ProgramArray
{
	Win32Program item[128];
	i32          index;
} Win32ProgramArray;

typedef struct WinjumpState
{
	bool  init;
	bool  keyboardActivityThisFrame;
	bool  hotkeyPulledFocus;
	v2    frameBufferSize;
	HFONT defaultFont;
	HWND  window[windowtype_count];

	WNDPROC defaultWindowProc;
	WNDPROC defaultWindowProcEditBox;

	f32   listUpdateTimer;
	f32   listUpdateRateInS;
	Win32ProgramArray programArray;
} WinjumpState;

enum Win32Resources
{
	win32resource_edit_text_buffer,
	win32resource_list,
};

#define OFFSET_TO_STATE_PTR 0
#define MAX_WINDOW_TITLE_LEN ARRAY_COUNT(((Win32Program *)0)->title)

INTERNAL void winjump_displayWindow(HWND windowHandle)
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
		GetWindowText(windowHandle, window.title, MAX_WINDOW_TITLE_LEN);

		// If we receive an empty string as a window title, then we want to
		// ignore it. So if the string is defined, then we increment index
		if (window.title[0])
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

INTERNAL LRESULT CALLBACK captureEnterWindowProcCallback(HWND editWindow,
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
			bool keyIsDown  = ((lParam & (1 << 31)) == 0);
			if (keyIsDown)
			{
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
		}
		break;

		default:
		{
			return CallWindowProc(state->defaultWindowProcEditBox, editWindow,
			                      msg, wParam, lParam);
		}
		break;
	}

	return result;
}

INTERNAL LRESULT CALLBACK mainWindowProcCallback(HWND window, UINT msg,
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
			state->window[windowtype_input_search_entries] = editHandle;
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
			  WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
			  (i32)listP.x,
			  (i32)listP.y,
			  listWidth,
			  listHeight,
			  window,
			  (HMENU)win32resource_list,
			  NULL,
			  NULL
			);
			state->window[windowtype_list_window_entries] = listHandle;

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
					for (i32 i = 0; i < windowtype_count; i++)
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

			HWND editBox = state->window[windowtype_input_search_entries];
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
inline INTERNAL f32 getTimeFromQueryPerfCounter(LARGE_INTEGER start,
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
	state.init              = true;
	state.listUpdateRateInS = 0.5f;
	state.listUpdateTimer   = state.listUpdateRateInS;
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
	state.window[windowtype_main_client] = mainWindowHandle;

#define GUID_HOTKEY_ACTIVATE_APP 10983
	ASSERT(RegisterHotKey(mainWindowHandle, GUID_HOTKEY_ACTIVATE_APP, MOD_ALT,
	                      'K'));

	QueryPerformanceFrequency(&globalQueryPerformanceFrequency);

	LARGE_INTEGER startFrameTime;
	f32 targetSecondsPerFrame = 1 / 16.0f;
	f32 frameTimeInS = 0.0f;

	MSG msg;
	while (globalRunning)
	{
		startFrameTime = getWallClock();

		state.listUpdateTimer += frameTimeInS;
		if (state.listUpdateTimer >= state.listUpdateRateInS)
		{
			state.listUpdateTimer = 0.0f;

			Win32ProgramArray *programArray = &state.programArray;
			Win32ProgramArray emptyArray    = {};
			state.programArray              = emptyArray;
			ASSERT(programArray->index == 0);

			EnumWindows(EnumWindowsProcCallback, (LPARAM)programArray);

			bool arrayEntryValidated[ARRAY_COUNT(programArray->item)] = {};

			HWND listBox     = state.window[windowtype_list_window_entries];
			LRESULT listSize = SendMessage(listBox, LB_GETCOUNT, 0, 0);

			LRESULT listFirstVisibleIndex =
			    SendMessage(listBox, LB_GETTOPINDEX, 0, 0);

			for (LRESULT itemIndex = 0;
			     (itemIndex < listSize) && (itemIndex < programArray->index);
			     itemIndex++)
			{
				bool programInListStillRunning = false;
				LRESULT listEntryPid =
				    SendMessage(listBox, LB_GETITEMDATA, itemIndex, 0);
				ASSERT(listEntryPid != LB_ERR);

				Win32Program *currProgram = &programArray->item[itemIndex];
				if (currProgram->pid == (DWORD)listEntryPid &&
				    !arrayEntryValidated[itemIndex])
				{
					wchar_t entryString[ARRAY_COUNT(currProgram->title)] = {};
					LRESULT entryStringLen = SendMessage(
					    listBox, LB_GETTEXT, itemIndex, (LPARAM)entryString);
					ASSERT(entryStringLen != LB_ERR);

					if (common_wstrcmp(currProgram->title, entryString) != 0)
					{
						LRESULT insertIndex =
						    SendMessage(listBox, LB_INSERTSTRING, itemIndex,
						                (LPARAM)currProgram->title);
						ASSERT(insertIndex == itemIndex);

						LRESULT result =
						    SendMessage(listBox, LB_SETITEMDATA, itemIndex,
						                currProgram->pid);
						ASSERT(result != LB_ERR);

						LRESULT itemCount = SendMessage(
						    listBox, LB_DELETESTRING, itemIndex + 1, 0);
						ASSERT(itemCount == listSize);
					}

					arrayEntryValidated[itemIndex] = true;
					programInListStillRunning      = true;
				}
				else if (currProgram->pid != (DWORD)listEntryPid)
				{
					LRESULT insertIndex =
					    SendMessage(listBox, LB_INSERTSTRING, itemIndex,
					                (LPARAM)currProgram->title);
					ASSERT(insertIndex == itemIndex);

					LRESULT result = SendMessage(listBox, LB_SETITEMDATA,
					                             itemIndex, currProgram->pid);
					ASSERT(result != LB_ERR);

					LRESULT itemCount =
					    SendMessage(listBox, LB_DELETESTRING, itemIndex + 1, 0);
					ASSERT(itemCount == listSize);

					arrayEntryValidated[itemIndex] = true;
					programInListStillRunning        = true;
				}
			}

			if (listSize < programArray->index)
			{
				for (i32 i = listSize; i < programArray->index; i++)
				{
					Win32Program *program = &programArray->item[i];
					LRESULT insertIndex = SendMessage(listBox, LB_ADDSTRING, 0,
					                                  (LPARAM)program->title);
					ASSERT(insertIndex != LB_ERR);

					LRESULT result = SendMessage(listBox, LB_SETITEMDATA,
					                             insertIndex, program->pid);
					ASSERT(result != LB_ERR);
				}
			}
			else
			{
				for (i32 i = programArray->index; i < listSize; i++)
				{
					LRESULT result =
					    SendMessage(listBox, LB_DELETESTRING, i, 0);
					ASSERT(result != LB_ERR);
				}
			}

			{ // Get Line from Edit Control and Filter

				HWND editBox = state.window[windowtype_input_search_entries];
				// NOTE: Set first char as size of buffer as required by win32
				wchar_t editBoxText[MAX_WINDOW_TITLE_LEN] = {};
				editBoxText[0] = MAX_WINDOW_TITLE_LEN;

				LRESULT numCharsCopied =
				    SendMessage(editBox, EM_GETLINE, 0, (LPARAM)editBoxText);

				if (numCharsCopied > 0)
				{

					ASSERT(numCharsCopied < MAX_WINDOW_TITLE_LEN);
					for (i32 i = 0; i < programArray->index; i++)
					{
						Win32Program *program = &programArray->item[i];
						bool textMatches    = true;

						wchar_t *editChar   = editBoxText;
						wchar_t *titleChar = program->title;
						for (; *editChar && *titleChar;
						     editChar++, titleChar++)
						{
							if (*editChar != *titleChar)
							{
								textMatches = false;
								break;
							}
						}

						if (!textMatches)
						{
							LRESULT result = SendMessage(
							    listBox, LB_DELETESTRING, i, 0);
							ASSERT(result != LB_ERR);

							for (i32 j = i; j + 1 < programArray->index; j++)
							{
								programArray->item[j] =
								    programArray->item[j + 1];
							}

							i--;
							programArray->index--;
							if (programArray->index >= 0)
							{
								Win32Program emptyProgram          = {};
								programArray->item[programArray->index] =
								    emptyProgram;
							}
						}
					}
				}
			}

			SendMessage(listBox, LB_SETTOPINDEX, listFirstVisibleIndex, 0);
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
