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

#include <stdio.h>

// TODO(doyle): Organise all our globals siting around.
// TODO(doyle): Platform layer separation
// TODO(doyle): Store process EXE name so we can also search by that in the text
// filtering
// TODO(doyle): Search by index in list to help drilldown after the initial
// search
// TODO(doyle): Stop screen flickering by having better listbox updating
// mechanisms
// TODO(doyle): Clean up this cesspool.

FILE_SCOPE i32 wstrlen(const wchar_t *a)
{
	i32 result = 0;
	while (a && a[result]) result++;
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


FILE_SCOPE bool globalRunning;

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
#define MAX_PROGRAM_TITLE_LEN DQN_ARRAY_COUNT(((Win32Program *)0)->title)

FILE_SCOPE void winjump_display_window(HWND window)
{

	// IsIconic == if window is minimised
	if (IsIconic(window))
	{
		ShowWindow(window, SW_RESTORE);
	}

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
	Win32ProgramArray *programArray = (Win32ProgramArray *)lParam;
	if ((programArray->index + 1) < DQN_ARRAY_COUNT(programArray->item))
	{
		Win32Program program = {};
		i32 titleLen =
		    GetWindowTextW(window, program.title, MAX_PROGRAM_TITLE_LEN);
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
					HANDLE handle  = OpenProcess(
					    PROCESS_QUERY_LIMITED_INFORMATION, FALSE, program.pid);
					if (handle != nullptr)
					{
						DWORD len   = DQN_ARRAY_COUNT(program.exe);
						BOOL result = QueryFullProcessImageNameW(
						    handle, 0, program.exe, &len);
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

					programArray->item[programArray->index++] = program;
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

FILE_SCOPE LRESULT CALLBACK win32_capture_enter_callback(HWND editWindow,
                                                         UINT msg,
                                                         WPARAM wParam,
                                                         LPARAM lParam)
{
	WinjumpState *state =
	    (WinjumpState *)GetWindowLongPtrW(editWindow, GWLP_USERDATA);

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
						winjump_display_window(programToShow.window);
						SendMessageW(editWindow, WM_SETTEXT, 0, (LPARAM)L"");
					}
				}
				break;

				default:
				{
					return CallWindowProcW(state->defaultWindowProcEditBox,
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
					    state->window[winjumpwindows_input_search_entries];
					SetWindowTextW(inputBox, L"");
					return 0;
				}

				default:
				{
					return CallWindowProcW(state->defaultWindowProcEditBox,
					                      editWindow, msg, wParam, lParam);
				}
			}
		}

		default:
		{
			return CallWindowProcW(state->defaultWindowProcEditBox, editWindow,
			                       msg, wParam, lParam);
		}
	}

	return result;
}

FILE_SCOPE LRESULT CALLBACK win32_main_callback(HWND window, UINT msg,
                                                WPARAM wParam, LPARAM lParam)
{
	LRESULT result = 0;

	WinjumpState *state = nullptr;
	if (msg == WM_CREATE)
	{
		CREATESTRUCT *win32DeriveStruct = (CREATESTRUCT *)lParam;
		state = (WinjumpState *)win32DeriveStruct->lpCreateParams;
		SetWindowLongPtrW(window, OFFSET_TO_STATE_PTR, (LONG_PTR)state);
	}
	else
	{
		state = (WinjumpState *)GetWindowLongPtrW(window, OFFSET_TO_STATE_PTR);
	}

	switch (msg)
	{
		case WM_CREATE:
		{
			RECT clientRect;
			GetClientRect(window, &clientRect);

			// NOTE(doyle): Don't set position here, since creation sends
			// a WM_SIZE, we just put all the size and position logic in there.
			HWND editHandle = CreateWindowExW(
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
			state->window[winjumpwindows_input_search_entries] = editHandle;
			SetFocus(editHandle);
			state->defaultWindowProcEditBox = (WNDPROC)SetWindowLongPtrW(
			    editHandle, GWLP_WNDPROC,
			    (LONG_PTR)win32_capture_enter_callback);
			SetWindowLongPtrW(editHandle, GWLP_USERDATA, (LONG_PTR)state);

			HWND listHandle = CreateWindowExW(
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
			state->window[winjumpwindows_list_window_entries] = listHandle;

			{ // Set To Use Default Font
				NONCLIENTMETRICS metrics = {};
				metrics.cbSize           = sizeof(NONCLIENTMETRICS);

				i32 readFontResult = SystemParametersInfo(
				    SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &metrics,
				    0);

				if (readFontResult)
				{
					state->defaultFont =
					    CreateFontIndirect(&metrics.lfMessageFont);
					bool redrawImmediately = true;
					for (i32 i = 0; i < winjumpwindows_count; i++)
					{
						HWND windowToSendMsg = state->window[i];
						SendMessageW(windowToSendMsg, WM_SETFONT,
						            (WPARAM)state->defaultFont,
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
			if (handle == state->window[win32resource_list])
			{
				if (notificationCode == LBN_SELCHANGE)
				{
					Win32ProgramArray programArray = state->programArray;
					LRESULT selectedIndex =
					    SendMessageW(handle, LB_GETCURSEL, 0, 0);

					// NOTE: LB_ERR if list unselected
					if (selectedIndex != LB_ERR)
					{
						DQN_ASSERT(selectedIndex < DQN_ARRAY_COUNT(programArray.item));

						Win32Program programToShow =
						    programArray.item[selectedIndex];
						LRESULT itemPid = SendMessageW(handle, LB_GETITEMDATA,
						                              selectedIndex, 0);
						DQN_ASSERT((u32)itemPid == programToShow.pid);

						SendMessageW(handle, LB_SETCURSEL, (WPARAM)-1, 0);

						winjump_display_window(programToShow.window);
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
			winjump_display_window(window);

			HWND editBox = state->window[winjumpwindows_input_search_entries];
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

			{
				// Resize the edit box that is used for filtering
				HWND editWindow =
				    state->window[winjumpwindows_input_search_entries];

				DqnV2 editP    = dqn_v2i(margin, margin);
				i32 editWidth  = clientWidth - (2 * margin);
				i32 editHeight = 25;

				MoveWindow(editWindow, (i32)editP.x, (i32)editP.y, editWidth,
				           editHeight, TRUE);

				// Resize the edit box that is used for filtering
				HWND listWindow =
				    state->window[winjumpwindows_list_window_entries];

				DqnV2 listP = dqn_v2(editP.x, (editP.y + editHeight + margin));
				i32 listWidth = clientWidth - (2 * margin);
				i32 listHeight =
				    clientHeight - (i32)editP.y - editHeight - margin;

				MoveWindow(listWindow, (i32)listP.x, (i32)listP.y, listWidth,
				           listHeight, TRUE);
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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nShowCmd)
{
	i32 bytesToReserveAfterWindowInst = sizeof(WinjumpState *);

	WNDCLASSEXW wc =
	{
		sizeof(WNDCLASSEX),
		CS_HREDRAW | CS_VREDRAW,
		win32_main_callback,
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

	if (!RegisterClassExW(&wc)) {
		// TODO(doyle): Logging, couldn't register class
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
	WinjumpState state      = {};
	state.defaultWindowProc = win32_main_callback;

	HWND mainWindow = CreateWindowExW(
	    WS_EX_COMPOSITED, wc.lpszClassName, L"Winjump", windowStyle,
	    CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL,
	    NULL, hInstance, &state);

	if (!mainWindow)
	{
		DQN_WIN32_ERROR_BOX("CreateWindowExW() failed.", NULL);
		return -1;
	}
	state.window[winjumpwindows_main_client] = mainWindow;

#define GUID_HOTKEY_ACTIVATE_APP 10983
	RegisterHotKey(mainWindow, GUID_HOTKEY_ACTIVATE_APP, MOD_ALT, 'K');

	const f32 targetFramesPerSecond = 16.0f;
	f32 targetSecondsPerFrame       = 1 / targetFramesPerSecond;
	f32 targetMsPerFrame            = targetSecondsPerFrame * 1000.0f;
	f32 frameTimeInS                = 0.0f;

	DQN_ASSERT(wchar_to_lower(L'A') == L'a');
	DQN_ASSERT(wchar_to_lower(L'a') == L'a');
	DQN_ASSERT(wchar_to_lower(L' ') == L' ');

	MSG msg;

	while (globalRunning)
	{
		f64 startFrameTime = dqn_time_now_in_ms();

		Win32ProgramArray *programArray = &state.programArray;
		HWND listBox = state.window[winjumpwindows_list_window_entries];

		// Insert new programs into the list box and remove dead ones
		{
			// TODO(doyle): Separate ui update from internal state update
			Win32ProgramArray emptyArray = {};
			*programArray                = emptyArray;
			DQN_ASSERT(programArray->index == 0);
			EnumWindows(win32_enum_procs_callback, (LPARAM)programArray);

			const LRESULT listFirstVisibleIndex =
			    SendMessageW(listBox, LB_GETTOPINDEX, 0, 0);

			const LRESULT listSize = SendMessageW(listBox, LB_GETCOUNT, 0, 0);

			for (LRESULT itemIndex = 0;
			     (itemIndex < listSize) && (itemIndex < programArray->index);
			     itemIndex++)
			{
				Win32Program *currProgram = &programArray->item[itemIndex];

				// IMPORTANT: We set item data for the entry, so the entry
				// must exist before we check PID. We create this entry when
				// we check the string to see if it exists as the index.
				// Compare program title strings

				// TODO(doyle): Tighten memory alloc using len vars in program
				// TODO(doyle): snprintf?

				// NOTE: +4 for the " - " and the null terminator
				const i32 len = DQN_ARRAY_COUNT(currProgram->title) +
				                DQN_ARRAY_COUNT(currProgram->exe) + 4;
				wchar_t friendlyName[len] = {};
				winjump_get_program_friendly_name(currProgram, friendlyName,
				                                  len);

				wchar_t entryString[len] = {};
				LRESULT entryStringLen   = SendMessageW(
				    listBox, LB_GETTEXT, itemIndex, (LPARAM)entryString);
				if (wstrcmp(friendlyName, entryString) != 0)
				{
					LRESULT insertIndex =
					    SendMessageW(listBox, LB_INSERTSTRING, itemIndex,
					                (LPARAM)friendlyName);

					LRESULT itemCount =
					    SendMessageW(listBox, LB_DELETESTRING, itemIndex + 1, 0);
				}

				// Compare list entry item data, pid
				LRESULT listEntryPid =
				    SendMessageW(listBox, LB_GETITEMDATA, itemIndex, 0);
				if (currProgram->pid != (DWORD)listEntryPid)
				{
					LRESULT result = SendMessageW(listBox, LB_SETITEMDATA,
					                             itemIndex, currProgram->pid);
				}
			}

			if (listSize < programArray->index)
			{
				for (i32 i = listSize; i < programArray->index; i++)
				{
					Win32Program *program = &programArray->item[i];
					const i32 len = DQN_ARRAY_COUNT(program->title) +
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
				for (i32 i = programArray->index; i < listSize; i++)
				{
					LRESULT result =
					    SendMessageW(listBox, LB_DELETESTRING, i, 0);
				}
			}

			SendMessageW(listBox, LB_SETTOPINDEX, listFirstVisibleIndex, 0);
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
			wchar_t editBoxText[MAX_PROGRAM_TITLE_LEN] = {};
			editBoxText[0]                            = MAX_PROGRAM_TITLE_LEN;

			LRESULT numCharsCopied =
			    SendMessageW(editBox, EM_GETLINE, 0, (LPARAM)editBoxText);
			if (numCharsCopied > 0)
			{
				state.currentlyFiltering = true;
				DQN_ASSERT(numCharsCopied < MAX_PROGRAM_TITLE_LEN);

				for (i32 i = 0; i < programArray->index; i++)
				{
					Win32Program *program = &programArray->item[i];
					const i32 len         = DQN_ARRAY_COUNT(program->title) +
					                        DQN_ARRAY_COUNT(program->exe) + 4;
					wchar_t friendlyName[len] = {};
					winjump_get_program_friendly_name(program, friendlyName,
					                                  len);

					// TODO(doyle): Hardcoded arbitrary limit
					wchar_t *titleWords[32]       = {};
					i32 titleWordsIndex           = 0;
					titleWords[titleWordsIndex++] = program->title;

					// TODO(doyle): Be smart, split by more than just
					// spaces, like file directories
					for (i32 j = 1; j + 1 < len; j++)
					{
						wchar_t *checkSpace    = &friendlyName[j];
						wchar_t *oneAfterSpace = &friendlyName[j + 1];
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
							    wchar_to_lower(*editChar);
							wchar_t titleCharLowercase =
							    wchar_to_lower(*titleChar);
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
						    SendMessageW(listBox, LB_DELETESTRING, i, 0);

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

		while (PeekMessageW(&msg, mainWindow, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		f64 endWorkTime  = dqn_time_now_in_ms();
		f64 workTimeInMs = endWorkTime - startFrameTime;

		if (workTimeInMs < targetMsPerFrame)
		{
			DWORD remainingTimeInMs = (DWORD)(targetMsPerFrame - workTimeInMs);
			Sleep(remainingTimeInMs);
		}

		f64 endFrameTime  = dqn_time_now_in_ms();
		f64 frameTimeInMs = endFrameTime - startFrameTime;

		wchar_t windowTitleBuffer[128] = {};
		_snwprintf_s(windowTitleBuffer, DQN_ARRAY_COUNT(windowTitleBuffer),
		             DQN_ARRAY_COUNT(windowTitleBuffer), L"Winjump | %5.2f ms/f",
		             frameTimeInMs);
		SetWindowTextW(mainWindow, windowTitleBuffer);
	}

	return 0;
}
