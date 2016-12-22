#include "GL/gl3w.h"
#include "imgui.h"

#ifndef VC_EXTRALEAN
  #define VC_EXTRALEAN
#endif

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

// TODO(doyle): Switch to WinAPI?
// TODO(doyle): Minimise memory footprint. OGL is overkill, but then why not?
// Every device has a GPU why not use it? If we keep, then switch to loading the
// libraries ourselves
// TODO(doyle): Organise all our globals siting around.
// TODO(doyle): Get rid of imgui_impl files and write our own (hand-in-hand with
// manually setting up OGL).
// TODO(doyle): Platform layer separation

////////////////////////////////////////////////////////////////////////////////
// GLFW Implementation
////////////////////////////////////////////////////////////////////////////////
#define GLFW_IMPLEMENTATION

#include "GLFW/glfw3.h"

#ifdef GLFW_IMPLEMENTATION
  #include "imgui_impl_glfw_gl3.h"
  #include "stdio.h"
#endif

#include "Common.h"

typedef struct Win32Window
{
	char title[256];
	HWND handle;
} Win32Window;

GLOBAL_VAR Win32Window windowList[128];
GLOBAL_VAR i32 windowListIndex = 0;

#define MAX_WINDOW_TITLE_LEN ARRAY_COUNT(((Win32Window *)0)->title)

typedef struct WinjumpState
{
	bool init;
	bool keyboardActivityThisFrame;
	bool hotkeyPulledFocus;
	ImVec2 frameBufferSize;
} WinjumpState;

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

#ifdef GLFW_IMPLEMENTATION
  #define GLFW_EXPOSE_NATIVE_WIN32
  #include "GLFW/glfw3native.h"
#endif

void winjump_update(GLFWwindow *window, WinjumpState *state)
{
	////////////////////////////////////////////////////////////////////////////
	// Initialise Window/App State for Frame
	////////////////////////////////////////////////////////////////////////////
	if (!state->init)
	{
		HWND mainWindow = 0;

#ifdef GLFW_IMPLEMENTATION
		FreeConsole();
		mainWindow         = glfwGetWin32Window(window);
		originalWindowProc = (WNDPROC) SetWindowLongPtr(
		    mainWindow, GWLP_WNDPROC, (LONG_PTR)hotkeyWindowProcCallback);
		SetWindowLongPtr(mainWindow, GWLP_USERDATA, (LONG_PTR)state);
#endif

		// NOTE(doyle): MSDN Documentation - An application must specify an id
		// value in the range 0x0000 through 0xBFFF.
		i32 hotkeyId = 0x78240000;
		ASSERT(RegisterHotKey(mainWindow, hotkeyId, MOD_ALT, 'K'));
		state->init = true;
	}

	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	EnumWindows(EnumWindowsProcCallback, NULL);

	////////////////////////////////////////////////////////////////////////////
	// Main Window Functionality
	////////////////////////////////////////////////////////////////////////////

#if 0
	ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiSetCond_FirstUseEver);
	ImGui::ShowTestWindow(NULL);
#endif

	ImGuiWindowFlags flags = // ImGuiWindowFlags_NoTitleBar |
	                         ImGuiWindowFlags_NoResize |
	                         ImGuiWindowFlags_NoMove |
	                         // ImGuiWindowFlags_MenuBar |
	                         ImGuiWindowFlags_NoCollapse |
	                         ImGuiWindowFlags_NoBringToFrontOnFocus;

	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiSetCond_Always);
	ImGui::SetNextWindowSize(state->frameBufferSize, ImGuiSetCond_Always);

	ImGui::Begin("Winjump: Alt+K to activate app and search bar", NULL, flags);

	ImGuiTextFilter filter;
	filter.Draw("", state->frameBufferSize.x);
	if (!ImGui::IsItemActive() && state->hotkeyPulledFocus)
	{
		ImGui::SetKeyboardFocusHere(-1);
		state->hotkeyPulledFocus = false;
	}

	i32 windowFilteredIndexedList[ARRAY_COUNT(windowList)] = {};
	i32 windowFilteredIndex                                = 0;

	Win32Window *windowToShow = nullptr;
	for (i32 i = 0; i < windowListIndex; i++)
	{
		if (filter.PassFilter(windowList[i].title))
		{
			if (ImGui::Selectable(windowList[i].title))
			{
				windowToShow = &windowList[i];
			}
			windowFilteredIndexedList[windowFilteredIndex++] = i;
		}
	}
	ImGui::End();

	i32 keyEnterIndex = ImGui::GetKeyIndex(ImGuiKey_Enter);
	if (ImGui::IsKeyPressed(keyEnterIndex))
	{
		// The filtered list is populated, so choose the first from that list
		if (windowFilteredIndex > 0)
		{
			i32 indexToShow = windowFilteredIndexedList[0];
			windowToShow    = &windowList[indexToShow];
		}
		else
		{
			// No filter set by user choose the first from our window list
			windowToShow = &windowList[0];
		}
	}

	if (windowToShow)
	{
		winjump_displayWindow(windowToShow->handle);
	}

	////////////////////////////////////////////////////////////////////////////
	// Frame End Cleanup
	////////////////////////////////////////////////////////////////////////////
	{
		for (i32 i = 0; i < windowListIndex; i++)
		{
			u8 *dataPtr = (u8 *)(&windowList[i]);
			for (i32 j     = 0; j < sizeof(windowList[0]); j++)
				dataPtr[j] = 0;
		}
		windowListIndex = 0;

		state->keyboardActivityThisFrame = false;
	}
}


#ifdef GLFW_IMPLEMENTATION
static void error_callback(int error, const char *description)
{
	fprintf(stderr, "Error %d: %s\n", error, description);
}

INTERNAL void winjump_keyCallback(GLFWwindow *window, int key, int scancode,
                                  int action, int mods)
{
	WinjumpState *state = (WinjumpState *)(glfwGetWindowUserPointer(window));
	state->keyboardActivityThisFrame = true;

	ImGui_ImplGlfwGL3_KeyCallback(window, key, scancode, action, mods);
}

INTERNAL void winjump_mouseButtonCallback(GLFWwindow *window, int button,
                                          int action, int mods)
{
	ImGui_ImplGlfwGL3_MouseButtonCallback(window, button, action, mods);
}

INTERNAL void winjump_scrollCallback(GLFWwindow *window, double xOffset,
                                     double yOffset)
{
	ImGui_ImplGlfwGL3_ScrollCallback(window, xOffset, yOffset);
}


int main(int argc, char *argv[])
{
	// Setup window
	GLFWwindow *window = nullptr;
	{
		glfwSetErrorCallback(error_callback);
		if (!glfwInit()) return 1;

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		// glfwWindowHint(GLFW_DECORATED, false);

#if __APPLE__
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

		window = glfwCreateWindow(450, 180, "Winjump", NULL, NULL);
		glfwMakeContextCurrent(window);
		gl3wInit();

		// Setup ImGui binding
		ImGui_ImplGlfwGL3_Init(window, false);

		// NOTE(doyle): Prevent glfwSwapBuffer until every N vertical retraces
		// has occurred, i.e. 1 to limit to monitor refresh rate
		glfwSwapInterval(2);

		{ // Route GLFW callbacks to winjump
			glfwSetMouseButtonCallback(window, winjump_mouseButtonCallback);
			glfwSetScrollCallback(window, winjump_scrollCallback);
			glfwSetKeyCallback(window, winjump_keyCallback);
			glfwSetCharCallback(window, ImGui_ImplGlfwGL3_CharCallback);
		}
	}

	WinjumpState state = {};
	glfwSetWindowUserPointer(window, (void *)(&state));

	f32 startTime      = (f32)(glfwGetTime());
	f32 secondsElapsed = 0.0f;

	ImVec4 clear_color = ImColor(114, 144, 154);
	while (!glfwWindowShouldClose(window))
	{
		// glfwPollEvents();
		glfwWaitEvents();
		ImGui_ImplGlfwGL3_NewFrame(secondsElapsed);

		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		state.frameBufferSize = ImVec2((f32)display_w, (f32)display_h);

		winjump_update(window, &state);

		{ // Rendering
			glViewport(0, 0, display_w, display_h);
			glClearColor(clear_color.x, clear_color.y, clear_color.z,
			             clear_color.w);
			glClear(GL_COLOR_BUFFER_BIT);

			ImGui::Render();

			glfwSwapBuffers(window);
		}

		f32 endTime    = (f32)(glfwGetTime());
		secondsElapsed = endTime - startTime;
		startTime      = endTime;

		LOCAL_PERSIST f32 titleUpdateFrequencyInSeconds = 0.5f;
		titleUpdateFrequencyInSeconds -= secondsElapsed;
		if (titleUpdateFrequencyInSeconds <= 0)
		{
			f32 msPerFrame      = secondsElapsed * 1000.0f;
			f32 framesPerSecond = 1.0f / secondsElapsed;

			char textBuffer[256] = {};
			snprintf(textBuffer, ARRAY_COUNT(textBuffer),
			         "Winjump | %f ms/f | %f fps", msPerFrame, framesPerSecond);

			glfwSetWindowTitle(window, textBuffer);
			titleUpdateFrequencyInSeconds = 0.5f;
		}
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////
// WINAPI Implementation
// BROKEN - NOT YET COMPLETE!
////////////////////////////////////////////////////////////////////////////////
// #define WINAPI_IMPLEMENTATION

#ifdef WINAPI_IMPLEMENTATION
#include <Windowsx.h>
#include <commctrl.h>

typedef struct ImGuiHelper
{
	bool mousePressed[3]       = {false, false, false};
	f32 mouseWheel             = 0.0f;
	GLuint fontTexture         = 0;
	i32 shaderHandle           = 0;
	i32 vertHandle             = 0;
	i32 fragHandle             = 0;
	i32 attribLocationTex      = 0;
	i32 attribLocationProjMtx  = 0;
	i32 attribLocationPosition = 0;
	i32 attribLocationUV       = 0;
	i32 attribLocationColor    = 0;
	u32 vboHandle              = 0;
	u32 vaoHandle              = 0;
	u32 elementsHandle         = 0;
} ImGuiHelper;

typedef struct Win32Input
{
	i32 prevScrollX;
	i32 prevScrollY;
} Win32Input;

GLOBAL_VAR Win32Input input        = {};
GLOBAL_VAR bool globalRunning      = true;
GLOBAL_VAR ImGuiHelper imGuiHelper = {};

enum Win32Resources
{
	win32resource_list,
	win32resource_static,
};

INTERNAL LRESULT CALLBACK
mainWindowProcCallback(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
{
	LRESULT result = 0;

	switch (msg)
	{
		case WM_CREATE:
		{
			HWND listBoxHandle = CreateWindow(
			    WC_LISTVIEW, NULL, WS_CHILD | WS_VISIBLE | LBS_NOTIFY, 10, 10, 150,
			    120, window, (HMENU)win32resource_list, NULL, NULL);

			EnumWindows(EnumWindowsProcCallback, NULL);
			for (i32 i = 0; i < windowListIndex; i++)
			{
				Win32Window win32Window = windowList[i];
				SendMessage(listBoxHandle, LB_ADDSTRING, 0,
				            (LPARAM)win32Window.title);
			}
			windowListIndex = 0;

		}
		break;

		case WM_DESTROY:
		case WM_CLOSE:
		{
			PostQuitMessage(0);
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

						imGuiHelper.mousePressed[index] = true;
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
				imGuiHelper.mouseWheel = 1;
			}
			else if (deltaY < 0)
			{
				imGuiHelper.mouseWheel = -1;
			}

			break;
		}

		default:
		{
			result = DefWindowProc(window, msg, wParam, lParam);
		}
		break;
	}

	return result;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nShowCmd)
{

	WNDCLASS wc      = {};
	wc.lpfnWndProc   = mainWindowProcCallback;
	wc.hInstance     = hInstance;
	wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
	wc.lpszClassName = "WinjumpWindowClass";

	// Register class (i.e. Declare existence to Windows)
	if (!RegisterClass(&wc)) return -1;

	// NOTE: Regarding Window Sizes
	// If you specify a window size, e.g. 800x600, Windows regards the 800x600
	// region to be inclusive of the toolbars and side borders. So in actuality,
	// when you blit to the screen blackness, the area that is being blitted to
	// is slightly smaller than 800x600. Windows provides a function to help
	// calculate the size you'd need by accounting for the window style.
	RECT r   = {};
	r.right  = 800;
	r.bottom = 600;

	DWORD windowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	AdjustWindowRect(&r, windowStyle, FALSE);

	// Create window according to style
	HWND mainWindowHandle =
	    CreateWindowEx(0, wc.lpszClassName, "Winjump", windowStyle,
	                   CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
	                   r.bottom - r.top, NULL, NULL, hInstance, 0);
	if (!mainWindowHandle) return -1;

#if 0
	// pixel format descriptor struct for the device context
	PIXELFORMATDESCRIPTOR pfd =
	{
	    sizeof(PIXELFORMATDESCRIPTOR), // size of this pfd
	    1,                             // version number
	    PFD_DRAW_TO_WINDOW |           // support window
	    PFD_SUPPORT_OPENGL |           // support OpenGL
	    PFD_DOUBLEBUFFER,              // double buffered
	    PFD_TYPE_RGBA,                 // RGBA type
	    24,                            // 24-bit color depth
	    0,
	    0, 0, 0, 0, 0,                 // color bits ignored
	    0,                             // no alpha buffer
	    0,                             // shift bit ignored
	    0,                             // no accumulation buffer
	    0, 0, 0, 0,                    // accum bits ignored
	    32,                            // 32-bit z-buffer
	    0,                             // no stencil buffer
	    0,                             // no auxiliary buffer
	    PFD_MAIN_PLANE,                // main layer
	    0,                             // reserved
	    0, 0, 0                        // layer masks ignored
	};

	HDC handleDeviceContext    = GetDC(mainWindowHandle);
	i32 targetPixelFormatValue = ChoosePixelFormat(handleDeviceContext, &pfd);
	ASSERT(targetPixelFormatValue != 0);

	SetPixelFormat(handleDeviceContext, targetPixelFormatValue, &pfd);
	HGLRC handleGlRenderingContext = wglCreateContext(handleDeviceContext);
	wglMakeCurrent(handleDeviceContext, handleGlRenderingContext);
	gl3wInit();

	{ // Setup IMGUI
		ImGuiIO &io                    = ImGui::GetIO();
		io.KeyMap[ImGuiKey_Tab]        = VK_TAB;
		io.KeyMap[ImGuiKey_LeftArrow]  = VK_LEFT;
		io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
		io.KeyMap[ImGuiKey_UpArrow]    = VK_UP;
		io.KeyMap[ImGuiKey_DownArrow]  = VK_DOWN;
		io.KeyMap[ImGuiKey_PageUp]     = VK_PRIOR;
		io.KeyMap[ImGuiKey_PageDown]   = VK_NEXT;
		io.KeyMap[ImGuiKey_Home]       = VK_HOME;
		io.KeyMap[ImGuiKey_End]        = VK_END;
		io.KeyMap[ImGuiKey_Delete]     = VK_DELETE;
		io.KeyMap[ImGuiKey_Backspace]  = VK_BACK;
		io.KeyMap[ImGuiKey_Enter]      = VK_RETURN;
		io.KeyMap[ImGuiKey_Escape]     = VK_ESCAPE;
		io.KeyMap[ImGuiKey_A]          = 'A';
		io.KeyMap[ImGuiKey_C]          = 'C';
		io.KeyMap[ImGuiKey_V]          = 'V';
		io.KeyMap[ImGuiKey_X]          = 'X';
		io.KeyMap[ImGuiKey_Y]          = 'Y';
		io.KeyMap[ImGuiKey_Z]          = 'Z';

		// Alternatively you can set this to NULL and call ImGui::GetDrawData()
		// after ImGui::Render() to get the same ImDrawData pointer.
		// io.RenderDrawListsFn  = ImGui_ImplGlfwGL3_RenderDrawLists;

		// TODO(doyle): Clipboard support
		// io.SetClipboardTextFn = ImGui_ImplGlfwGL3_SetClipboardText;
		// io.GetClipboardTextFn = ImGui_ImplGlfwGL3_GetClipboardText;
		// io.ClipboardUserData  = g_Window;

		io.ImeWindowHandle = mainWindowHandle;
	}
#endif

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);

	}

#if 0
	while (globalRunning)
	{
		// Iterate through all OS messages given to the program
		MSG msg;
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
		}

		GLfloat bg[] = {1.0f, 0.0f, 0.0f, 1.0f};
		glClearBufferfv(GL_COLOR, 0, bg);

		SwapBuffers(handleDeviceContext);
	}
#endif

	return 0;
}
#endif
