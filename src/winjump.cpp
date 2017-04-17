#include "Winjump.h"

#include <Commdlg.h>
#include <Psapi.h>    // For win32 GetProcessMemoryInfo()
#include <Shlwapi.h>  // For MSVC snwsprintf
#include <Windowsx.h>
#include <commctrl.h> // For win32 choose font dialog
#include <stdio.h>

#include "Config.h"
#include "Wchar.h"

#ifndef WINJUMP_UNITY_BUILD
	#define DQN_IMPLEMENTATION
	#include "dqn.h"
#endif

#define WINJUMP_DEBUG_MODE

// TODO(doyle): Safer subclassing?
// https://blogs.msdn.microsoft.com/oldnewthing/20031111-00/?p=41883/

#define WIN32_GUID_HOTKEY_ACTIVATE_APP 10983
#define WIN32_UI_MARGIN 5
#define WIN32_MAX_PROGRAM_TITLE DQN_ARRAY_COUNT(((Win32Program *)0)->title)
FILE_SCOPE WinjumpState globalState;
FILE_SCOPE bool         globalRunning;

// Returns length without null terminator, returns 0 if NULL

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
                                                 wchar_t *out, i32 outLen)
{

	// Friendly Name Format
	// <Index>: <Program Title> - <Program Exe>

	// For example
	// 1: Google Search - firefox.exe
	// 2: Winjump.cpp + (C:\winjump.cpp) - GVIM64 - firefox.exe
	i32 numStored = _snwprintf_s(out, outLen, outLen, L"%2d: %s - %s",
	                             program->lastStableIndex + 1, program->title,
	                             program->exe);
	DQN_ASSERT(numStored < FRIENDLY_NAME_LEN);

	return numStored;
}

BOOL CALLBACK win32_enum_windows_callback(HWND window, LPARAM lParam)
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
					program.exeLen = wchar_strlen(program.exe);
					CloseHandle(handle);
				}

				Win32Program *result = dqn_array_push(programArray, program);
				if (result) result->lastStableIndex = (i32)programArray->count - 1;
				break;
			}

			rootWindow = lastPopup;
			lastPopup  = GetLastActivePopup(rootWindow);
		} while (lastPopup != rootWindow);
	}


	return true;
}

FILE_SCOPE Win32Window *winjump_get_win32window_from_hwnd(WinjumpState *state,
                                                          HWND hwnd)
{
	Win32Window *result = NULL;
	for (i32 i = 0; i < DQN_ARRAY_COUNT(state->window); i++)
	{
		if (state->window[i].handle == hwnd)
		{
			result = &state->window[i];
			return result;
		}
	}

	return result;
}

FILE_SCOPE LRESULT CALLBACK win32_list_box_callback(HWND window, UINT msg,
                                                    WPARAM wParam,
                                                    LPARAM lParam)
{
	Win32Window *win32Window =
	    winjump_get_win32window_from_hwnd(&globalState, window);
	DQN_ASSERT(win32Window);

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
#if 1
			return true;
#else
			return CallWindowProcW(win32Window->defaultProc, window, msg,
			                       wParam, lParam);
#endif
		}
		break;

		case WM_PAINT:
		{
#if 1
			RECT clientRect;
			GetClientRect(window, &clientRect);

			PAINTSTRUCT paint = {};
			HDC deviceContext = BeginPaint(window, &paint);
			{
				LONG clientWidth, clientHeight;
				dqn_win32_get_rect_dim(clientRect, &clientWidth, &clientHeight);

				// TODO(doyle): It's possible to cache these so we don't
				// recreate every frame.
				HDC drawDC         = CreateCompatibleDC(deviceContext);
				HBITMAP drawBitmap =
				    CreateCompatibleBitmap(drawDC, clientWidth, clientHeight);
				SelectObject(drawDC, drawBitmap);

				// Fill bitmap with sys color, then wndproc will paint the data
				// into the drawDc and then blit it out at once
				FillRect(drawDC, &clientRect, GetSysColorBrush(COLOR_WINDOW));
				CallWindowProcW(win32Window->defaultProc, window, WM_PAINT,
				                (WPARAM)drawDC, (LPARAM)0);

				BitBlt(deviceContext, 0, 0, clientWidth, clientHeight, drawDC,
				       0, 0, SRCCOPY);

				DeleteObject(drawDC);
				DeleteObject(drawBitmap);
			}
			EndPaint(window, &paint);
#else
			return CallWindowProcW(win32Window->defaultProc, window, msg,
			                       wParam, lParam);
#endif
		}
		break;

		default:
		{
			return CallWindowProcW(win32Window->defaultProc, window, msg, wParam,
			                       lParam);
		}
		break;
	}

	return result;
}

FILE_SCOPE LRESULT CALLBACK win32_edit_box_callback(HWND window,
                                                         UINT msg,
                                                         WPARAM wParam,
                                                         LPARAM lParam)
{
	Win32Window *win32Window =
	    winjump_get_win32window_from_hwnd(&globalState, window);
	DQN_ASSERT(win32Window);

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
						SetWindowText(window, "");
						ShowWindow(globalState.window[winjumpwindow_main_client]
						               .handle,
						           SW_MINIMIZE);
					}
				}
				break;

				default:
				{
					return CallWindowProcW(win32Window->defaultProc, window,
					                       msg, wParam, lParam);
				}
			}
		}
		break;

		case WM_CHAR:
		{
			switch (wParam)
			{
				case VK_RETURN:
				{
					// NOTE: Stop Window Bell on pressing on Enter
					return 0;
				}

				case VK_ESCAPE:
				{
					// If escape is pressed, empty the text
					HWND inputBox =
					    globalState.window[winjumpwindow_input_search_entries]
					        .handle;
					SetWindowTextW(inputBox, L"");
					return 0;
				}

				default:
				{
					return CallWindowProcW(win32Window->defaultProc, window,
					                       msg, wParam, lParam);
				}
			}
		}

		default:
		{
			return CallWindowProcW(win32Window->defaultProc, window, msg,
			                       wParam, lParam);
		}
	}

	return result;
}

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

FILE_SCOPE void win32_tab_get_offset_to_content(HWND window, LONG *offsetX,
                                                LONG *offsetY)
{
	if (window)
	{
		RECT rect;
		TabCtrl_GetItemRect(window, 0, &rect);
		if (offsetX) *offsetX = rect.left;
		if (offsetY) *offsetY = rect.bottom;
	}
}

// NOTE: Resizing the search box will readjust elements dependent on its size
FILE_SCOPE void winjump_resize_search_box(WinjumpState *const state,
                                          i32 newWidth,
                                          i32 newHeight,
                                          const bool ignoreWidth,
                                          const bool ignoreHeight)
{
	HWND tab = globalState.window[winjumpwindow_tab].handle;
	LONG tabOffsetY;
	win32_tab_get_offset_to_content(tab, NULL, &tabOffsetY);

	HWND editWindow =
	    globalState.window[winjumpwindow_input_search_entries].handle;
	LONG origWidth, origHeight;
	dqn_win32_get_client_dim(editWindow, &origWidth, &origHeight);

	if (ignoreWidth)  newWidth  = origWidth;
	if (ignoreHeight) newHeight = origHeight;

	// Resize the edit box that is used for filtering
	DqnV2 editP = dqn_v2i(WIN32_UI_MARGIN, tabOffsetY + WIN32_UI_MARGIN);
	MoveWindow(editWindow, (i32)editP.x, (i32)editP.y, newWidth, newHeight,
	           TRUE);

	// Resize the list window
	{
		LONG clientHeight;
		dqn_win32_get_client_dim(tab, NULL, &clientHeight);

		HWND listWindow =
		    state->window[winjumpwindow_list_program_entries].handle;
		DqnV2 listP = dqn_v2(editP.x, (editP.y + newHeight + WIN32_UI_MARGIN));
		i32 listWidth  = newWidth;
		i32 listHeight = clientHeight - (i32)listP.y;

		MoveWindow(listWindow, (i32)listP.x, (i32)listP.y, listWidth,
		           listHeight, TRUE);
	}
}

#define WINJUMP_STRING_TO_CALC_HEIGHT "H"
FILE_SCOPE void winjump_font_change(WinjumpState *const state, const HFONT font)
{
	if (font)
	{
		DeleteObject(state->font);
		state->font = font;

		bool redrawImmediately = false;
		for (i32 i = 0; i < winjumpwindow_count; i++)
		{
			HWND targetWindow = globalState.window[i].handle;
			SendMessage(targetWindow, WM_SETFONT, (WPARAM)font,
			            redrawImmediately);
		}

		HWND editWindow =
		    globalState.window[winjumpwindow_input_search_entries].handle;
		HDC deviceContext = GetDC(editWindow);

		LONG newHeight;
		win32_font_calculate_dim(deviceContext, globalState.font,
		                         WINJUMP_STRING_TO_CALC_HEIGHT, NULL,
		                         &newHeight);
		ReleaseDC(editWindow, deviceContext);

		// TODO(doyle): Repated in WM_SIZE
		const i32 MIN_SEARCH_HEIGHT = 18;
		newHeight = DQN_MAX(MIN_SEARCH_HEIGHT, (LONG)(newHeight * 1.85f));
		winjump_resize_search_box(&globalState, 0, newHeight, true, false);
	}
	else
	{
		DQN_WIN32_ERROR_BOX("winjump_font_change() failed: Font was NULL.",
		                    NULL);
	}
}

FILE_SCOPE inline i32
winjump_apphotkey_to_win32_hkm_hotkey_modifier(enum AppHotkeyModifier modifier)
{
	u32 result = 0;

	if (modifier == apphotkeymodifier_alt)
	{
		result = HOTKEYF_ALT;
	}
	else if (modifier == apphotkeymodifier_shift)
	{
		result = HOTKEYF_SHIFT;
	}
	else
	{
		DQN_ASSERT(modifier == apphotkeymodifier_ctrl);
		result = HOTKEYF_CONTROL;
	}

	return result;
}

FILE_SCOPE inline enum AppHotkeyModifier
winjump_win32_hkm_hotkey_modifier_to_apphotkey(i32 win32HkmHotkeyModifier)
{
	enum AppHotkeyModifier result;

	if (win32HkmHotkeyModifier == HOTKEYF_ALT)
	{
		result = apphotkeymodifier_alt;
	}
	else if (win32HkmHotkeyModifier == HOTKEYF_SHIFT)
	{
		result = apphotkeymodifier_shift;
	}
	else
	{
		DQN_ASSERT(win32HkmHotkeyModifier == HOTKEYF_CONTROL);
		result = apphotkeymodifier_ctrl;
	}

	return result;
}


FILE_SCOPE LRESULT CALLBACK win32_hotkey_winjump_activate_callback(
    HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
{
	Win32Window *win32Window =
	    winjump_get_win32window_from_hwnd(&globalState, window);
	DQN_ASSERT(win32Window);

	LRESULT result = 0;
	switch (msg)
	{
		case WM_PAINT:
		{
			AppHotkey *currHotkey = &globalState.appHotkey;
			// NOTE: This is a bug in Windows. Hotkeys are incompatible with
			// windows that have WS_EX_COMPOSITED causing a constant stream of
			// messages and subsequently flickering.

			// So we must override WM_PAINT and return 0.
			// Draw the default border and control
			DefWindowProcW(window, msg, wParam, lParam);

			////////////////////////////////////////////////////////////////////
			// Parse the hot-key
			////////////////////////////////////////////////////////////////////
			LRESULT newHotkey  = SendMessageW(window, HKM_GETHOTKEY, 0, 0);
			char newVirtualKey = LOBYTE(LOWORD(newHotkey));
			u8 newKeyModifier  = HIBYTE(LOWORD(newHotkey));

			HWND client = globalState.window[winjumpwindow_main_client].handle;
			char hotkeyString[32] = {};

			bool newHotkeyRegisteredSuccessfully = false;
			bool keyCombinationWasComplete       = false;

			// This is a new candidate hotkey, let's use it
			if (newVirtualKey >= 'A' && newVirtualKey <= 'Z' &&
			    (newKeyModifier == HOTKEYF_ALT ||
			     newKeyModifier == HOTKEYF_CONTROL ||
			     newKeyModifier == HOTKEYF_SHIFT))
			{
				keyCombinationWasComplete = true;

				currHotkey->virtualKey = newVirtualKey;
				currHotkey->modifier =
				    winjump_win32_hkm_hotkey_modifier_to_apphotkey(
				        newKeyModifier);

				// Convert to global hotkey defines
				u32 rhkModifier = 0;
				if (newKeyModifier == HOTKEYF_ALT)
					rhkModifier = MOD_ALT;
				else if (newKeyModifier == HOTKEYF_CONTROL)
					rhkModifier = MOD_CONTROL;
				else if (newKeyModifier == HOTKEYF_SHIFT)
					rhkModifier = MOD_SHIFT;
				DQN_ASSERT(rhkModifier != 0);

				// Apply the new hotkey
				UnregisterHotKey(client, WIN32_GUID_HOTKEY_ACTIVATE_APP);
				if (RegisterHotKey(client, WIN32_GUID_HOTKEY_ACTIVATE_APP,
				                   rhkModifier, currHotkey->virtualKey))
				{
					newHotkeyRegisteredSuccessfully = true;
				}
			}
			else
			{
				// Otherwise, entered hotkey is not a complete combination yet,
				// i.e. only modifier pressed, or only key pressed
			}

			///////////////////////////////////////////////////////////////////
			// Draw Hotkey + Caret
			///////////////////////////////////////////////////////////////////
			char *stringPtr             = hotkeyString;
			i32 currHotkeyWin32Modifier = winjump_apphotkey_to_win32_hkm_hotkey_modifier(currHotkey->modifier);

			if (currHotkeyWin32Modifier & HOTKEYF_ALT)
				stringPtr += dqn_sprintf(stringPtr, "%s", "Alt-");

			if (currHotkeyWin32Modifier & HOTKEYF_CONTROL)
				stringPtr += dqn_sprintf(hotkeyString, "%s", "Ctrl-");

			if (currHotkeyWin32Modifier & HOTKEYF_SHIFT)
				stringPtr += dqn_sprintf(hotkeyString, "%s", "Shift-");

			dqn_sprintf(stringPtr, "%c", currHotkey->virtualKey);

			RECT rect;
			GetClientRect(window, &rect);
			rect.left += 2;

			HDC deviceContext = GetDC(window);

			LONG stringWidth, stringHeight;
			win32_font_calculate_dim(deviceContext, globalState.font,
			                         hotkeyString, &stringWidth, &stringHeight);
			rect.right = rect.left + stringWidth;

			SelectObject(deviceContext, globalState.font);
			DrawText(deviceContext, hotkeyString, -1, &rect, DT_VCENTER | DT_SINGLELINE);

			const i32 NUDGE_X = 1;
			SetCaretPos(rect.right + NUDGE_X, rect.bottom - stringHeight - (i32)(0.25f * stringHeight));
			ReleaseDC(window, deviceContext);

			///////////////////////////////////////////////////////////////////
			// Update ui prompts and window title
			///////////////////////////////////////////////////////////////////
			if (keyCombinationWasComplete)
			{
				HWND textValidHotkey =
				    globalState.window[winjumpwindow_text_hotkey_is_valid]
				        .handle;
				if (newHotkeyRegisteredSuccessfully)
				{
					char newWindowTitle[256] = {};
					dqn_sprintf(newWindowTitle,
					            "Winjump | Press %s to activate Winjump",
					            hotkeyString);
					SetWindowText(client, newWindowTitle);
					SetWindowText(textValidHotkey,
					              "Hotkey is vacant and valid");
				}
				else
				{
					SetWindowText(client, "Winjump | No valid hotkey set");
					SetWindowText(textValidHotkey,
					              "Hotkey is in used and not valid");
				}
			}

			return 0;
		}
		break;

		default:
		{
			return CallWindowProcW(win32Window->defaultProc, window, msg,
			                       wParam, lParam);
		}
		break;
	}
}

FILE_SCOPE LRESULT CALLBACK win32_tab_ctrl_callback(HWND window, UINT msg,
                                                    WPARAM wParam,
                                                    LPARAM lParam)
{
	// TODO: Since edit box, list, btns etc, most main UI is living as childs in
	// the tab, all the command calls are being sent to tab ctrl callback

	// Not sure why they're not propagating to the main callback since
	// mainwindow owns tabs which owns the lists/btns etc
	Win32Window *win32Window =
	    winjump_get_win32window_from_hwnd(&globalState, window);
	DQN_ASSERT(win32Window);

	LRESULT result = 0;
	switch (msg)
	{
		case WM_COMMAND:
		{

			// If command from control then,
			// HIWORD(wParam) = ControlNotification
			// LOWORD(wParam) = ControlID
			// lParam         = Control Handle
			HWND handle = (HWND)lParam;
			if (handle ==
			    globalState.window[winjumpwindow_list_program_entries].handle)
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
				else
				{
					result = DefWindowProcW(window, msg, wParam, lParam);
				}
				break;
			}

			if (handle ==
			    globalState.window[winjumpwindow_btn_change_font].handle)
			{
				WORD notificationCode = HIWORD((DWORD)wParam);
				if (notificationCode == BN_CLICKED)
				{
					LOGFONTW chosenFont = {};
					GetObjectW(globalState.font, sizeof(chosenFont),
					           &chosenFont);

					CHOOSEFONTW chooseFont = {};
					chooseFont.lStructSize = sizeof(chooseFont);
					chooseFont.hwndOwner   = window;
					chooseFont.lpLogFont   = &chosenFont;
					chooseFont.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;

					if (ChooseFontW(&chooseFont))
					{
						HFONT font = CreateFontIndirectW(&chosenFont);
						if (font)
						{
							winjump_font_change(&globalState, font);
							globalState.configIsStale = true;
						}
						else
						{
							DQN_WIN32_ERROR_BOX("CreateFontIndirectW() failed",
							                    NULL);
						}
					}
				}
				else
				{
					result = DefWindowProcW(window, msg, wParam, lParam);
				}
				break;
			}

			// If command from a menu item,
			// HIWORD(wParam) = 0
			// LOWORD(wParam) = MenuID
			// lParam         = 0
			return CallWindowProcW(win32Window->defaultProc, window, msg,
			                       wParam, lParam);
		}
		break;

		default:
		{
			return CallWindowProcW(win32Window->defaultProc, window, msg,
			                       wParam, lParam);
		}
	}

	return result;
}

FILE_SCOPE void win32_tab_options_resize_windows(WinjumpState *state)
{
	HWND tab            = state->window[winjumpwindow_tab].handle;
	LONG contentOffsetY = 0;
	win32_tab_get_offset_to_content(tab, NULL, &contentOffsetY);

	LONG tabWidth;
	dqn_win32_get_client_dim(tab, &tabWidth, NULL);
	DqnV2 btnDim = dqn_v2(100, 30);

	// Position the set hotkey label
	LONG fontHeight   = 0;
	HDC deviceContext = GetDC(tab);
	win32_font_calculate_dim(deviceContext, globalState.font,
	                         WINJUMP_STRING_TO_CALC_HEIGHT, NULL, &fontHeight);
	ReleaseDC(tab, deviceContext);

	HWND hotkeyActivateLabel = state->window[winjumpwindow_text_hotkey_winjump_activate].handle;
	DqnV2 textHotkeyP        = dqn_v2i(WIN32_UI_MARGIN, WIN32_UI_MARGIN + contentOffsetY);
	DqnV2 textHotkeyDim      = dqn_v2i(tabWidth - WIN32_UI_MARGIN, fontHeight + WIN32_UI_MARGIN);
	MoveWindow(hotkeyActivateLabel, (i32)textHotkeyP.x, (i32)textHotkeyP.y,
	          (i32)textHotkeyDim.w, (i32)textHotkeyDim.h, true);

	// Position the hotkey control
	HWND hotkeyActivate = state->window[winjumpwindow_hotkey_winjump_activate].handle;
	DqnV2 hotkeyP       = dqn_v2(textHotkeyP.x, textHotkeyP.y + textHotkeyDim.h + WIN32_UI_MARGIN);
	MoveWindow(hotkeyActivate, (i32)hotkeyP.x, (i32)hotkeyP.y, (i32)btnDim.w, (i32)btnDim.h, true);

	// Position the hotkey is valid text next to the hotkey control
	{
		HWND text = state->window[winjumpwindow_text_hotkey_is_valid].handle;
		DqnV2 dim = dqn_v2(textHotkeyDim.w, btnDim.h);
		DqnV2 p   = dqn_v2(hotkeyP.x + btnDim.x + WIN32_UI_MARGIN, hotkeyP.y + (dim.h * 0.25f));
		MoveWindow(text, (i32)p.x, (i32)p.y, (i32)dim.w, (i32)dim.h, true);
	}

	// Position the change font button
	HWND btnChangeFont = state->window[winjumpwindow_btn_change_font].handle;
	DqnV2 btnP = dqn_v2(hotkeyP.x, hotkeyP.y + btnDim.h + WIN32_UI_MARGIN);
	MoveWindow(btnChangeFont, (i32)btnP.x, (i32)btnP.y, (i32)btnDim.w, (i32)btnDim.h, TRUE);

}

FILE_SCOPE LRESULT CALLBACK win32_main_callback(HWND window, UINT msg,
                                                WPARAM wParam, LPARAM lParam)
{
	LRESULT result = 0;
	switch (msg)
	{
		case WM_CREATE:
		{
			InitCommonControls();
			globalState.window[winjumpwindow_main_client].handle = window;

			// NOTE(doyle): Don't set position here, since creation sends
			// a WM_SIZE, we just put all the size and position logic in there.
			////////////////////////////////////////////////////////////////////
			// Create Tab Window
			////////////////////////////////////////////////////////////////////
			Win32Window tabWindow = {};
			tabWindow.handle = CreateWindowExW(
			  WS_EX_COMPOSITED,
			  WC_TABCONTROLW,
			  NULL,
			  WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | WS_BORDER,
			  0,
			  0,
			  0,
			  0,
			  window,
			  NULL,
			  NULL,
			  NULL
			);
			tabWindow.defaultProc =
			    (WNDPROC)SetWindowLongPtrW(tabWindow.handle, GWLP_WNDPROC,
			                               (LONG_PTR)win32_tab_ctrl_callback);
			globalState.window[winjumpwindow_tab] = tabWindow;

			////////////////////////////////////////////////////////////////////
			// Create 1st Tab Window
			////////////////////////////////////////////////////////////////////
			{
				TCITEM tabControlItem  = {};
				tabControlItem.mask    = TCIF_TEXT;
				tabControlItem.pszText = "List";
				i32 tabIndex           = TabCtrl_InsertItem(
				    tabWindow.handle, TabCtrl_GetItemCount(tabWindow.handle),
				    &tabControlItem);

				// Create Edit Window
				Win32Window editWindow = {};
				editWindow.handle      = CreateWindowExW(
				    WS_EX_COMPOSITED | WS_EX_CLIENTEDGE, L"EDIT", NULL,
				    WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, 0, 0, 0, 0,
				    tabWindow.handle, NULL, NULL, NULL);
				editWindow.tabIndex = tabIndex;
				editWindow.defaultProc =
			 	    (WNDPROC)SetWindowLongPtrW(editWindow.handle, GWLP_WNDPROC, (LONG_PTR)win32_edit_box_callback);

				globalState.window[winjumpwindow_input_search_entries] =
				    editWindow;
				SetFocus(editWindow.handle);

				// Create List Window
				Win32Window listWindow = {};
				listWindow.handle = CreateWindowExW(
				    WS_EX_COMPOSITED | WS_EX_CLIENTEDGE, L"LISTBOX", NULL,
				    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
				        WS_HSCROLL | LBS_NOTIFY,
				    0, // x
				    0, // y
				    0, // width
				    0, // height
				    tabWindow.handle, NULL, NULL, NULL);
				listWindow.tabIndex = tabIndex;
				listWindow.defaultProc = (WNDPROC)SetWindowLongPtrW(listWindow.handle, GWLP_WNDPROC, (LONG_PTR)win32_list_box_callback);

				globalState.window[winjumpwindow_list_program_entries] =
				    listWindow;
			}

			////////////////////////////////////////////////////////////////////
			// Create 2nd Tab Window
			////////////////////////////////////////////////////////////////////
			{
				TCITEM tabControlItem  = {};
				tabControlItem.mask    = TCIF_TEXT;
				tabControlItem.pszText = "Options";
				i32 tabIndex           = TabCtrl_InsertItem(
				    tabWindow.handle, TabCtrl_GetItemCount(tabWindow.handle),
				    &tabControlItem);

				// Create change font btn
				Win32Window btnChangeFont = {};
				btnChangeFont.handle      = CreateWindowExW(
				    WS_EX_COMPOSITED,
				    L"BUTTON",      // Predefined class; Unicode assumed
				    L"Change Font", // Button text
				    WS_TABSTOP | WS_CHILD | BS_DEFPUSHBUTTON, // Styles
				    0,                                        // x position
				    0,                                        // y position
				    0,                                        // Button width
				    0,                                        // Button height
				    tabWindow.handle,                         // Parent window
				    NULL,                                     // No menu.
				    NULL,
				    NULL); // Pointer not needed.
				btnChangeFont.tabIndex = tabIndex;
				globalState.window[winjumpwindow_btn_change_font] =
				    btnChangeFont;

				// Create change activation hotkey is valid static text
				Win32Window textHotkeyWinjumpActivate = {};
				textHotkeyWinjumpActivate.handle =
				    CreateWindowExW(WS_EX_COMPOSITED,
				                    L"STATIC",         // class name
				                    L"Winjump Hotkey:", // no title (caption)
				                    WS_CHILD,          // style
				                    0, 0,              // position
				                    0, 0,              // size
				                    tabWindow.handle,  // parent window
				                    NULL,              // uses class menu
				                    NULL,              // instance
				                    NULL);             // no WM_CREATE parameter
				textHotkeyWinjumpActivate.tabIndex = tabIndex;
				globalState.window[winjumpwindow_text_hotkey_winjump_activate] =
				    textHotkeyWinjumpActivate;

				// Create change activation hotkey
				Win32Window hotkeyWinjumpActivate = {};
				hotkeyWinjumpActivate.handle =
				    CreateWindowExW(WS_EX_COMPOSITED,
				                    HOTKEY_CLASSW,       // class name
				                    L"Activate Winjump", // no title (caption)
				                    WS_CHILD,            // style
				                    0, 0,              // position
				                    0, 0,             // size
				                    tabWindow.handle,    // parent window
				                    NULL,                // uses class menu
				                    NULL,                // instance
				                    NULL); // no WM_CREATE parameter
				SendMessageW(hotkeyWinjumpActivate.handle, HKM_SETRULES,
				             HKCOMB_NONE, 0);

				hotkeyWinjumpActivate.tabIndex = tabIndex;
				hotkeyWinjumpActivate.defaultProc = (WNDPROC)SetWindowLongPtrW(
				    hotkeyWinjumpActivate.handle, GWLP_WNDPROC,
				    (LONG_PTR)win32_hotkey_winjump_activate_callback);
				globalState.window[winjumpwindow_hotkey_winjump_activate] =
				    hotkeyWinjumpActivate;

				// Create change activation hotkey is valid static text
				Win32Window textHotkeyIsValid = {};
				textHotkeyIsValid.handle      = CreateWindowExW(
				    WS_EX_COMPOSITED,
				    L"STATIC",                     // class name
				    NULL , // no title (caption)
				    WS_CHILD,                      // style
				    0, 0,                          // position
				    0, 0,                          // size
				    tabWindow.handle,              // parent window
				    NULL,                          // uses class menu
				    NULL,                          // instance
				    NULL);                         // no WM_CREATE parameter
				textHotkeyIsValid.tabIndex = tabIndex;
				globalState.window[winjumpwindow_text_hotkey_is_valid] =
				    textHotkeyIsValid;
			}

			////////////////////////////////////////////////////////////////////
			// Create Status Bar
			////////////////////////////////////////////////////////////////////
			Win32Window statusWindow = {};
			statusWindow.handle      = CreateWindowExW(
			    NULL,
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

#ifdef WINJUMP_DEBUG_MODE
			for (i32 i = 0; i < DQN_ARRAY_COUNT(globalState.window); i++)
			{
				DQN_ASSERT(globalState.window[i].handle);
			}
#endif
		}
		break;

		case WM_NOTIFY:
		{
			NMHDR *notificationMsg = (NMHDR *)lParam;
			if (notificationMsg->hwndFrom ==
			    globalState.window[winjumpwindow_tab].handle)
			{
				switch (notificationMsg->code)
				{
					case TCN_SELCHANGE:
					{
						i32 newTabIndex =
						    TabCtrl_GetCurSel(notificationMsg->hwndFrom);
						if (newTabIndex == -1) return result;

						for (i32 i = 0; i < DQN_ARRAY_COUNT(globalState.window);
						     i++)
						{
							Win32Window win32Window = globalState.window[i];
							if (win32Window.tabIndex == WIN32_NOT_PART_OF_TAB)
								continue;

							if (win32Window.tabIndex == newTabIndex)
							{
								ShowWindow(win32Window.handle, SW_SHOW);
							}
							else
							{
								ShowWindow(win32Window.handle, SW_HIDE);
							}
						}
					}
					break;
				}
			}
			else
			{
				result = DefWindowProcW(window, msg, wParam, lParam);
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
			HWND editBox =
			    globalState.window[winjumpwindow_input_search_entries].handle;
			SetFocus(editBox);
		}
		break;

		case WM_SIZE:
		{
			LONG clientWidth, clientHeight;
			dqn_win32_get_client_dim(window, &clientWidth, &clientHeight);

			// NOTE: This can be called before inner window elements are created
			////////////////////////////////////////////////////////////////////
			// Re-configure Status Bar on Resize
			////////////////////////////////////////////////////////////////////
			HWND status = globalState.window[winjumpwindow_status_bar].handle;
			{
				// Setup the parts of the status bar
				const WPARAM numParts  = 3;
				i32 partsPos[numParts] = {};

				i32 partsInterval = clientWidth / numParts;
				for (i32 i      = 0; i < numParts; i++)
					partsPos[i] = partsInterval * (i + 1);
				SendMessageW(status, SB_SETPARTS, numParts, (LPARAM)partsPos);

				// Pass through message so windows can handle anchoring the bar
				SendMessage(status, WM_SIZE, wParam, lParam);
			}

			////////////////////////////////////////////////////////////////////
			// Setup tab interface
			////////////////////////////////////////////////////////////////////
			HWND tab = globalState.window[winjumpwindow_tab].handle;
			{
				LONG statusHeight;
				dqn_win32_get_client_dim(status, NULL, &statusHeight);

				MoveWindow(tab, 0, 0, clientWidth, clientHeight - statusHeight,
				           TRUE);
			}

			////////////////////////////////////////////////////////////////////
			// Adjust the search box and entry list
			////////////////////////////////////////////////////////////////////
			{
				HWND editWindow =
				    globalState.window[winjumpwindow_input_search_entries]
				        .handle;
				const i32 MIN_SEARCH_HEIGHT = 18;

				HDC deviceContext = GetDC(editWindow);

				LONG searchHeight;
				win32_font_calculate_dim(deviceContext, globalState.font,
				                         WINJUMP_STRING_TO_CALC_HEIGHT, NULL,
				                         &searchHeight);
				searchHeight =
				    DQN_MAX(MIN_SEARCH_HEIGHT, (LONG)(searchHeight * 1.85f));
				ReleaseDC(editWindow, deviceContext);

				LONG searchWidth;
				dqn_win32_get_client_dim(tab, &searchWidth, NULL);
				searchWidth -= (2 * WIN32_UI_MARGIN);

				// NOTE: This also re-positions the list because it depends on
				// the search box position
				winjump_resize_search_box(&globalState, searchWidth,
				                          searchHeight, false, false);
			}

			////////////////////////////////////////////////////////////////////
			// Adjust the options tab elements
			////////////////////////////////////////////////////////////////////
			win32_tab_options_resize_windows(&globalState);

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

FILE_SCOPE bool
winjump_program_array_shallow_copy_array_internal(DqnArray<Win32Program> *src,
                                                  DqnArray<Win32Program> *dest)
{
	if (src && dest)
	{
		// NOTE: Once we take a snapshot, we stop enumerating windows, so we
		// only need to allocate exactly array->count
		if (dqn_array_init(dest, (size_t)src->count))
		{
			DQN_ASSERT(src->data && dest->data);
			memcpy(dest->data, src->data, (size_t)src->count * sizeof(*src->data));
			DQN_ASSERT(src->data && dest->data);
			dest->count = src->count;
			return true;
		}
	}

	return false;
}

FILE_SCOPE bool
winjump_program_array_restore_snapshot(WinjumpState *state,
                                       DqnArray<Win32Program> *snapshot)
{
	bool result = winjump_program_array_shallow_copy_array_internal(
	    snapshot, &state->programArray);
	return result;
}

FILE_SCOPE bool
winjump_program_array_create_snapshot(WinjumpState *state,
                                      DqnArray<Win32Program> *snapshot)
{
	bool result = winjump_program_array_shallow_copy_array_internal(
	    &state->programArray, snapshot);
	return result;
}

void winjump_update(WinjumpState *state)
{
	HWND listBox = state->window[winjumpwindow_list_program_entries].handle;
	i32 firstVisibleIndex = SendMessageW(listBox, LB_GETTOPINDEX, 0, 0);

	// NOTE: Set first char is size of buffer as required by win32
	wchar_t newSearchStr[WIN32_MAX_PROGRAM_TITLE] = {};
	newSearchStr[0]  = DQN_ARRAY_COUNT(newSearchStr);
	HWND editBox     = state->window[winjumpwindow_input_search_entries].handle;
	i32 newSearchLen = (i32)SendMessageW(editBox, EM_GETLINE, 0, (LPARAM)newSearchStr);

	LONG width, height;
	dqn_win32_get_client_dim(editBox, &width, &height);

	///////////////////////////////////////////////////////////////////////////
	// Enumerate windows or initiate state for filtering
	///////////////////////////////////////////////////////////////////////////
	state->isFilteringResults            = (newSearchLen > 0);
	DqnArray<Win32Program> *programArray = &state->programArray;

	// NOTE: If we are filtering, stop clearing out our array and freeze its
	// state by stopping window enumeration on the array and instead work
	// with a stack of snapshots of the state
	if (state->isFilteringResults)
	{
		DQN_ASSERT(newSearchLen > 0);
		wchar_str_to_lower(newSearchStr, newSearchLen);
		if (state->searchStringLen == newSearchLen)
		{
			// NOTE: It's possible to remove more than 1 character from the
			// search string per frame
		}
		else
		{
			bool searchSpaceDecreased = (newSearchLen > state->searchStringLen);
			if (searchSpaceDecreased)
			{
				DqnArray<Win32Program> snapshot = {};
				if (winjump_program_array_create_snapshot(state, &snapshot))
				{
					dqn_array_push(&state->programArraySnapshotStack,
					                snapshot);
				}
				else
				{
					DQN_WIN32_ERROR_BOX(
					    "winjump_program_array_create_snapshot() failed: Out"
					    " of memory ",
					    NULL);
					globalRunning = false;
					return;
				}
			}
			else
			{
				if (state->programArraySnapshotStack.count > 0)
				{
					DqnArray<Win32Program> *snapshot =
					    dqn_array_pop(&state->programArraySnapshotStack);
					winjump_program_array_restore_snapshot(state, snapshot);
					dqn_array_free(snapshot);
				}
			}
		}
	}
	else
	{
		for (i32 i = 0; i < state->programArraySnapshotStack.count; i++)
		{
			DqnArray<Win32Program> *result =
			    &state->programArraySnapshotStack.data[i];
			dqn_array_free(result);
		}
		dqn_array_clear(programArray);
		dqn_array_clear(&state->programArraySnapshotStack);

		EnumWindows(win32_enum_windows_callback, (LPARAM)programArray);
	}
	state->searchStringLen = newSearchLen;

	////////////////////////////////////////////////////////////////////////////
	// Filter program array if user is actively searching
	////////////////////////////////////////////////////////////////////////////
	if (state->isFilteringResults)
	{
		DQN_ASSERT(newSearchLen < WIN32_MAX_PROGRAM_TITLE);

		// NOTE: Really doubt we neeed any more than that
		i32 userSpecifiedIndex      = 0;
		i32 userSpecifiedNumbers[8] = {};

		for (i32 j = 0; j < newSearchLen && newSearchStr[j]; j++)
		{
			if (wchar_is_digit(newSearchStr[j]))
			{
				i32 numberFoundInString =
				    wchar_str_to_i32(&newSearchStr[j], newSearchLen - j);

				// However many number of digits, increment the search ptr,
				// because there may be multiple numbers in the search string
				i32 tmp = userSpecifiedIndex;
				do
				{
					tmp /= 10;
					j++;
				} while (tmp > 0);

				if (userSpecifiedIndex == DQN_ARRAY_COUNT(userSpecifiedNumbers))
				{
					DQN_WIN32_ERROR_BOX(
					    "winjump_update() warning: No more space for user "
					    "specified indexes", NULL);
					break;
				}

				userSpecifiedNumbers[userSpecifiedIndex++] =
				    numberFoundInString;
			}
		}

		u64 arraySize = programArray->count;
		for (i32 index = 0; index < arraySize; index++)
		{
			Win32Program *program = &programArray->data[index];
			wchar_t friendlyName[FRIENDLY_NAME_LEN] = {};
			winjump_get_program_friendly_name(program, friendlyName,
			                                  DQN_ARRAY_COUNT(friendlyName));

			// NOTE: +1 to lastStableIndex since list displays elements starting
			// from 1 and lastStableIndex is zero-based
			i32 programIndex             = program->lastStableIndex + 1;
			bool specifiedNumberWasValid = false;
			for (i32 i = 0; i < userSpecifiedIndex; i++)
			{
				// NOTE: Suppose we have indexes, 1 and 14. If 1 is input, we
				// need both to remain in list entries. We can do this by
				// eliminating digits from 14 by dividing by 10 until it
				// matches.
				i32 specifiedNumber = userSpecifiedNumbers[i];
				i32 programIndexDigitCheck = programIndex;
				do
				{
					if (specifiedNumber == programIndexDigitCheck)
					{
						specifiedNumberWasValid = true;
						break;
					}
					programIndexDigitCheck /= 10;
				} while (programIndexDigitCheck > 0);

			}
			if (specifiedNumberWasValid) continue;

			if (!wchar_has_substring(newSearchStr, newSearchLen, friendlyName,
			                         FRIENDLY_NAME_LEN))
			{
				// If search string doesn't match, delete it from display
				DQN_ASSERT(dqn_array_remove_stable(programArray, index--));
				// Update index so we continue iterating over the correct
				// elements after removing it from the list since the for
				// loop is post increment and we're removing elements from
				// the list
				arraySize--;
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
			winjump_get_program_friendly_name(program, friendlyName,
			                                  DQN_ARRAY_COUNT(friendlyName));

			wchar_t entry[FRIENDLY_NAME_LEN] = {};
			LRESULT entryLen =
			    SendMessageW(listBox, LB_GETTEXT, index, (LPARAM)entry);
			if (wchar_strcmp(friendlyName, entry) != 0)
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
				    program, friendlyName, DQN_ARRAY_COUNT(friendlyName));

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

FILE_SCOPE void debug_unit_test_local_functions()
{
	DQN_ASSERT(wchar_to_lower(L'A') == L'a');
	DQN_ASSERT(wchar_to_lower(L'a') == L'a');
	DQN_ASSERT(wchar_to_lower(L' ') == L' ');

	{
		wchar_t *a = L"Microsoft";
		wchar_t *b = L"icro";
		i32 lenA   = wchar_strlen(a);
		i32 lenB   = wchar_strlen(b);
		DQN_ASSERT(wchar_has_substring(a, lenA, b, lenB) == true);
		DQN_ASSERT(wchar_has_substring(a, lenA, L"iro", wchar_strlen(L"iro")) ==
		           false);
		DQN_ASSERT(wchar_has_substring(b, lenB, a, lenA) == true);
		DQN_ASSERT(wchar_has_substring(L"iro", wchar_strlen(L"iro"), a, lenA) ==
		           false);
		DQN_ASSERT(wchar_has_substring(L"", 0, L"iro", 4) == false);
		DQN_ASSERT(wchar_has_substring(L"", 0, L"", 0) == false);
		DQN_ASSERT(wchar_has_substring(NULL, 0, NULL, 0) == false);
	}

	{
		wchar_t *a = L"Micro";
		wchar_t *b = L"irob";
		i32 lenA   = wchar_strlen(a);
		i32 lenB   = wchar_strlen(b);
		DQN_ASSERT(wchar_has_substring(a, lenA, b, lenB) == false);
		DQN_ASSERT(wchar_has_substring(b, lenB, a, lenA) == false);
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nShowCmd)
{
	////////////////////////////////////////////////////////////////////////////
	// Create Win32 Window
	////////////////////////////////////////////////////////////////////////////
	debug_unit_test_local_functions();

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

	DWORD windowStyle =
	    WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
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

	if (!dqn_array_init(&globalState.programArray, 4))
	{
		DQN_WIN32_ERROR_BOX("dqn_array_init() failed: Not enough memory.",
		                    NULL);
		return -1;
	}

	if (!dqn_array_init(&globalState.programArraySnapshotStack, 4))
	{
		DQN_WIN32_ERROR_BOX("dqn_array_init() failed: Not enough memory.",
		                    NULL);
		return -1;
	}


	RegisterHotKey(mainWindow, WIN32_GUID_HOTKEY_ACTIVATE_APP, MOD_ALT, 'K');

	////////////////////////////////////////////////////////////////////////////
	// Read Configuration if Exist
	////////////////////////////////////////////////////////////////////////////
	HFONT fontDerivedFromConfig = config_read_from_disk(&globalState);
	if (fontDerivedFromConfig)
		winjump_font_change(&globalState, fontDerivedFromConfig);

	////////////////////////////////////////////////////////////////////////////
	// Update loop
	////////////////////////////////////////////////////////////////////////////
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

		winjump_update(&globalState);

		////////////////////////////////////////////////////////////////////////
		// Update Status Bar
		////////////////////////////////////////////////////////////////////////
		HWND status = globalState.window[winjumpwindow_status_bar].handle;
		{
			// Active Windows text in Status Bar
			{
				WPARAM partToDisplayAt = 2;
				char text[32]          = {};
				dqn_sprintf(text, "Active Windows: %d",
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
					dqn_sprintf(text, "Memory: %'dkb",
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
			dqn_sprintf(text, "MsPerFrame: %.2f", (f32)frameTimeInMs);
			SendMessage(status, SB_SETTEXT, partToDisplayAt, (LPARAM)text);
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// Write Config to Disk
	////////////////////////////////////////////////////////////////////////////
	if (globalState.configIsStale) config_write_to_disk(&globalState);

	return 0;
}
