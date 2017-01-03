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

typedef struct Win32Window
{
	char  title[256];
	HWND  handle;
	DWORD pid;
} Win32Window;

enum WindowTypeIndex
{
	windowtype_main_client,
	windowtype_list_window_entries,
	windowtype_input_search_entries,
	windowtype_count,
};

typedef struct Win32WindowArray
{
	Win32Window window[128];
	i32         index;
} Win32WindowArray;

typedef struct WinjumpState
{
	bool  init;
	bool  keyboardActivityThisFrame;
	bool  hotkeyPulledFocus;
	v2    frameBufferSize;
	HFONT defaultFont;
	HWND  window[windowtype_count];

	f32   listUpdateTimer;
	f32   listUpdateRateInS;
	Win32WindowArray windowList;
} WinjumpState;

typedef struct Win32Input
{
	i32 prevScrollX;
	i32 prevScrollY;
} Win32Input;
GLOBAL_VAR Win32Input input = {};

enum Win32Resources
{
	win32resource_edit_text_buffer,
	win32resource_list,
};

#define OFFSET_TO_STATE_PTR 0
#define MAX_WINDOW_TITLE_LEN ARRAY_COUNT(((Win32Window *)0)->title)

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
	Win32WindowArray *windowArr = (Win32WindowArray *)lParam;
	if ((windowArr->index + 1) < ARRAY_COUNT(windowArr->window))
	{
		Win32Window window = {};
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
					window.handle                         = windowHandle;
					windowArr->window[windowArr->index++] = window;
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

LOCAL_PERSIST WNDPROC originalWindowProc;
INTERNAL LRESULT CALLBACK hotkeyWindowProcCallback(HWND window, UINT msg,
                                                   WPARAM wParam, LPARAM lParam)
{
	LRESULT result = 0;
	switch (msg)
	{
		case WM_HOTKEY:
		{
			WinjumpState *state =
			    (WinjumpState *)GetWindowLongPtr(window, GWLP_USERDATA);
			state->hotkeyPulledFocus = true;

			winjump_displayWindow(window);
		}
		break;

		default:
		{
			return CallWindowProc(originalWindowProc, window, msg, wParam,
			                      lParam);
		}
		break;
	}

	return result;
}

INTERNAL LRESULT CALLBACK
mainWindowProcCallback(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
{
	LRESULT result = 0;

	switch (msg)
	{
		case WM_CREATE:
		{
			CREATESTRUCT *win32DeriveStruct = (CREATESTRUCT *)lParam;
			WinjumpState *state =
			    (WinjumpState *)win32DeriveStruct->lpCreateParams;
			SetWindowLongPtr(window, OFFSET_TO_STATE_PTR, (LONG_PTR)state);

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
			  "EDIT",
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

			v2 listP       = V2(editP.x, (editP.y + editHeight + margin));
			i32 listWidth  = clientWidth - (2 * margin);
			i32 listHeight = clientHeight - (i32)editP.y - editHeight - margin;

			HWND listHandle = CreateWindowEx(
			  WS_EX_CLIENTEDGE,
			  "LISTBOX",
			  NULL,
			  WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL,
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

		case WM_DESTROY:
		case WM_CLOSE:
		{
			globalRunning = false;
		}
		break;

		case WM_SIZE:
		{
		}
		break;

		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP:
		case WM_KEYDOWN:
		case WM_KEYUP:
		{
			u32 vkCode = wParam;

			bool keyWasDown = ((lParam & (1 << 30)) != 0);
			bool keyIsDown  = ((lParam & (1 << 31)) == 0);

			if (keyIsDown)
			{
				switch (vkCode)
				{

					case VK_LBUTTON:
					case VK_RBUTTON:
					case VK_MBUTTON:
					{
						i32 index = 0;
						if (vkCode == VK_RBUTTON)
							index = 1;
						else if (vkCode == VK_MBUTTON)
							index = 2;
					}

					break;
				}
			}
		}
		break;

		case WM_MOUSEWHEEL:
		{
			i32 keyModifiers = GET_KEYSTATE_WPARAM(wParam);
			i16 wheelDelta   = GET_WHEEL_DELTA_WPARAM(wParam);

			i32 xPos = GET_X_LPARAM(lParam);
			i32 yPos = GET_Y_LPARAM(lParam);

			i32 deltaX = xPos - input.prevScrollX;
			i32 deltaY = yPos - input.prevScrollY;

			if (deltaY > 0)
			{
			}
			else if (deltaY < 0)
			{
			}
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
		"", // LPCTSTR lpszMenuName
		"WinjumpWindowClass",
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

	WinjumpState state      = {};
	state.init              = true;
	state.listUpdateRateInS = 1.0f;
	state.listUpdateTimer   = state.listUpdateRateInS;
	globalRunning           = true;

	HWND mainWindowHandle =
	    CreateWindowEx(0, wc.lpszClassName, "Winjump", windowStyle,
	                   CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
	                   r.bottom - r.top, NULL, NULL, hInstance, &state);
	if (!mainWindowHandle)
	{
		// TODO(doyle): Logging, couldn't create root window
		ASSERT(INVALID_CODE_PATH);
		return -1;
	}
	state.window[windowtype_main_client] = mainWindowHandle;

    QueryPerformanceFrequency(&globalQueryPerformanceFrequency);

	LARGE_INTEGER startFrameTime;
	f32 targetSecondsPerFrame = 1 / 16.0f;
	f32 frameTimeInS = 0.0f;

	MSG msg;
	while (globalRunning)
	{
		startFrameTime = getWallClock();

		while (PeekMessage(&msg, mainWindowHandle, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		state.listUpdateTimer += frameTimeInS;
		if (state.listUpdateTimer >= state.listUpdateRateInS)
		{
			Win32WindowArray *windowArray = &state.windowList;
			ASSERT(windowArray->index == 0);

			state.listUpdateTimer = 0.0f;
			EnumWindows(EnumWindowsProcCallback, (LPARAM)windowArray);

			bool arrayEntryValidated[ARRAY_COUNT(windowArray->window)] = {};

			HWND listBox     = state.window[windowtype_list_window_entries];
			LRESULT listSize = SendMessage(listBox, LB_GETCOUNT, 0, 0);

			for (LRESULT itemIndex = 0; itemIndex < listSize; itemIndex++)
			{
				bool programInListStillRunning = false;
				LRESULT listEntryPid =
				    SendMessage(listBox, LB_GETITEMDATA, itemIndex, 0);
				ASSERT(listEntryPid != LB_ERR);
				for (i32 windowIndex = 0; windowIndex < windowArray->index;
				     windowIndex++)
				{
					Win32Window *currProgram =
					    &windowArray->window[windowIndex];
					// TODO(doyle): Since our window enumeration method is not
					// reliable, sometimes we pick up two windows from the same
					// PID, so we also check to see if we've validated the entry
					// before yet.
					if (currProgram->pid == (DWORD)listEntryPid &&
					    !arrayEntryValidated[windowIndex])
					{
						char entryString[ARRAY_COUNT(currProgram->title)] = {};
						LRESULT entryStringLen =
						    SendMessage(listBox, LB_GETTEXT, itemIndex,
						                (LPARAM)entryString);
						ASSERT(entryStringLen != LB_ERR);

						if (common_strcmp(currProgram->title, entryString) != 0)
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

						arrayEntryValidated[windowIndex] = true;
						programInListStillRunning        = true;
						break;
					}
				}

				if (!programInListStillRunning)
				{
					LRESULT result =
					    SendMessage(listBox, LB_DELETESTRING, itemIndex, 0);
					ASSERT(result != LB_ERR);
				}
			}

			for (i32 i = 0; i < windowArray->index; i++)
			{
				if (!arrayEntryValidated[i])
				{
					Win32Window *window = &windowArray->window[i];
					LRESULT insertIndex = SendMessage(listBox, LB_ADDSTRING, 0,
					                                  (LPARAM)window->title);
					ASSERT(insertIndex != LB_ERR);

					LRESULT result = SendMessage(listBox, LB_SETITEMDATA,
					                             insertIndex, window->pid);
					ASSERT(result != LB_ERR);
				}
			}

			Win32WindowArray emptyArray = {};
			state.windowList            = emptyArray;
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

		char windowTitleBuffer[128] = {};
		_snprintf_s(windowTitleBuffer, ARRAY_COUNT(windowTitleBuffer),
		            ARRAY_COUNT(windowTitleBuffer), "Winjump | %5.2f ms/f",
		            msPerFrame);
		SetWindowText(mainWindowHandle, windowTitleBuffer);
	}

	return 0;
}
