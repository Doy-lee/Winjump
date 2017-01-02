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

typedef struct Win32Window
{
	char title[256];
	HWND handle;
} Win32Window;

enum WindowTypeIndex
{
	windowtype_main_client,
	windowtype_list_window_entries,
	windowtype_input_search_entries,
	windowtype_count,
};

typedef struct WinjumpState
{
	bool init;
	bool keyboardActivityThisFrame;
	bool hotkeyPulledFocus;
	v2 frameBufferSize;
	HFONT defaultFont;

	HWND window[3];
} WinjumpState;

typedef struct Win32Input
{
	i32 prevScrollX;
	i32 prevScrollY;
} Win32Input;

enum Win32Resources
{
	win32resource_edit_text_buffer,
	win32resource_list,
};

#define OFFSET_TO_STATE_PTR 0
#define MAX_WINDOW_TITLE_LEN ARRAY_COUNT(((Win32Window *)0)->title)

GLOBAL_VAR Win32Input input        = {};
GLOBAL_VAR bool globalRunning      = true;
GLOBAL_VAR i32 windowListIndex     = 0;
GLOBAL_VAR Win32Window windowList[128];

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
	if ((windowListIndex + 1) < ARRAY_COUNT(windowList))
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
					window.handle                 = windowHandle;
					windowList[windowListIndex++] = window;
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

INTERNAL BOOL CALLBACK enumChildProcCallback(HWND window, LPARAM lParam)
{
	if (lParam == WM_SETFONT)
	{
		WinjumpState *state    = (WinjumpState *)GetWindowLongPtr(window, 0);
		bool redrawImmediately = true;
		SendMessage(window, WM_SETFONT, (WPARAM)state->defaultFont,
		            redrawImmediately);
	}
	else
	{
		ASSERT(INVALID_CODE_PATH);
	}

	return true;
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
			PostQuitMessage(0);
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

	WinjumpState state = {};
	state.init         = true;

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

    LARGE_INTEGER queryPerformanceFrequency;
    LARGE_INTEGER startFrameTime, endWorkTime, endFrameTime;
    QueryPerformanceFrequency(&queryPerformanceFrequency);
	f32 targetSecondsPerFrame = 1 / 15.0f;

	MSG msg;
	bool running = true;
	while (running)
	{
		QueryPerformanceCounter(&startFrameTime);
		SendMessage(state.window[windowtype_list_window_entries],
		            LB_RESETCONTENT, 0, 0);

		EnumWindows(EnumWindowsProcCallback, NULL);
		for (i32 i = 0; i < ARRAY_COUNT(windowList); i++)
		{
			if (windowList[i].title[0])
			{
				SendMessage(state.window[windowtype_list_window_entries],
				            LB_ADDSTRING, 0, (LPARAM)windowList[i].title);
			}
			else
			{
				break;
			}
		}

		while (PeekMessage(&msg, mainWindowHandle, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		for (i32 i = 0; i < windowListIndex; i++)
		{
			windowList[i] = {};
		}
		windowListIndex = 0;


		QueryPerformanceCounter(&endWorkTime);

		f32 workTimeInS = (f32)(startFrameTime.QuadPart - endWorkTime.QuadPart) /
		                  queryPerformanceFrequency.QuadPart;

		if (workTimeInS < targetSecondsPerFrame)
		{
			DWORD remainingTimeInMs =
			    (DWORD)((targetSecondsPerFrame - workTimeInS) * 1000);
			Sleep(remainingTimeInMs);
		}

		QueryPerformanceCounter(&endFrameTime);
		f32 frameTimeInS =
		    (f32)(startFrameTime.QuadPart - endFrameTime.QuadPart) /
		    queryPerformanceFrequency.QuadPart;
		f32 msPerFrame = 1000.0f * frameTimeInS;

		char windowTitleBuffer[128] = {};
		_snprintf_s(windowTitleBuffer, ARRAY_COUNT(windowTitleBuffer),
		            ARRAY_COUNT(windowTitleBuffer), "Winjump | %5.2f ms/f",
		            msPerFrame);
		SetWindowText(mainWindowHandle, windowTitleBuffer);
	}

	return 0;
}
