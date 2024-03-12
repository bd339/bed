#include "buffer.h"
#include "gui.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

//#include <stdio.h>

#define MEM_SIZE 1024 * 1024 * 1024 * 1024ull

static arena memory;
static HWND  window;
static HDC   backbuffer;
static HFONT font;
static HFONT bold_font;

LONG CALLBACK
access_violation_handler(EXCEPTION_POINTERS *ExceptionInfo) {
	if(ExceptionInfo->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	ULONG_PTR addr = ExceptionInfo->ExceptionRecord->ExceptionInformation[1];

	if(addr >= (ULONG_PTR)memory.begin && addr < (ULONG_PTR)memory.begin + MEM_SIZE) {
		if(VirtualAlloc((LPVOID)addr, 4096, MEM_COMMIT, PAGE_READWRITE)) {
			return EXCEPTION_CONTINUE_EXECUTION;
		}
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

LRESULT CALLBACK
window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	static int drag_x;
	static int drag_y;
	static b32 typing;

	switch(message) {
		case WM_CHAR:
			BOOL hide_mouse;
			SystemParametersInfo(SPI_GETMOUSEVANISH, 0, &hide_mouse, 0);

			if(hide_mouse && !typing) {
				ShowCursor(FALSE);
				typing = 1;
			}

			b32 shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
			gui_keyboard(memory, kbd_char + (wParam & 0xFF), shift);
			break;

		case WM_SETCURSOR:
			if(typing) {
				ShowCursor(TRUE);
				typing = 0;
			}
			break;

		case WM_KEYDOWN:
			switch(wParam) {
				case VK_LEFT:
				case VK_UP:
				case VK_RIGHT:
				case VK_DOWN:
					b32 shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
					gui_keyboard(memory, kbd_left + (wParam - VK_LEFT), shift);
					break;
			}
			break;

		case WM_LBUTTONDOWN:
			drag_x = GET_X_LPARAM(lParam);
			drag_y = GET_Y_LPARAM(lParam);
			gui_mouse(mouse_left, drag_x, drag_y);
			break;

		case WM_MOUSEWHEEL: {
			int delta = GET_WHEEL_DELTA_WPARAM(wParam);
			int x = GET_X_LPARAM(lParam);
			int y = GET_Y_LPARAM(lParam);
			gui_mouse(delta > 0 ? mouse_scrollup : mouse_scrolldown, x, y);
			break;
		}

		case WM_MOUSEMOVE:
			if(wParam & MK_LBUTTON) {
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);
				int drag_w = GetSystemMetrics(SM_CXDRAG);
				int drag_h = GetSystemMetrics(SM_CYDRAG);

				if(x < drag_x - drag_w || x > drag_x + drag_w || y < drag_y - drag_h || y > drag_y + drag_h) {
					gui_mouse(mouse_drag, x, y);
				}
			}
			break;

		case WM_PAINT: {
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(window, &ps);
			BitBlt(
				hdc,
				ps.rcPaint.left,
				ps.rcPaint.top,
				ps.rcPaint.right  - ps.rcPaint.left,
				ps.rcPaint.bottom - ps.rcPaint.top,
				backbuffer,
				ps.rcPaint.left,
				ps.rcPaint.top,
				SRCCOPY
			);
			EndPaint(window, &ps);
			break;
		}

		case WM_TIMER:
			if(wParam == 1) {
				gui_redraw(memory);
				InvalidateRect(window, 0, 0);
				UpdateWindow(window);
			}
			break;

		case WM_SIZE: {
			extern unsigned *pixels;
			static HBITMAP bitmap;

			if(bitmap) {
				DeleteObject(bitmap);
				DeleteDC(backbuffer);
			}

			BITMAPINFO bitmap_info = {0};
			bitmap_info.bmiHeader.biSize        = sizeof(bitmap_info.bmiHeader);
			bitmap_info.bmiHeader.biPlanes      = 1;
			bitmap_info.bmiHeader.biBitCount    = 32;
			bitmap_info.bmiHeader.biCompression = BI_RGB;
			bitmap_info.bmiHeader.biWidth       =  LOWORD(lParam);
			bitmap_info.bmiHeader.biHeight      = -HIWORD(lParam);

			void *rgb;
			backbuffer = CreateCompatibleDC(0);
			bitmap = CreateDIBSection(0, &bitmap_info, DIB_RGB_COLORS, &rgb, 0, 0);
			SelectObject(backbuffer, bitmap);
			SelectObject(backbuffer, font);
			SetBkMode(backbuffer, TRANSPARENT);

			if(rgb) {
				pixels = rgb;
			}

			gui_reflow();
			break;
		}

		case WM_CREATE:
			SetCursor(LoadCursor(0, IDC_ARROW));
			break;

		case WM_CLOSE:
			if(gui_exit()) {
				DestroyWindow(window);
			}
			break;

		case WM_DESTROY:
			PostQuitMessage(0);
			break;

		default:
			return DefWindowProc(window, message, wParam, lParam);
	}

	return 0;
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdline, int nCmdShow) {
#if 0
	AllocConsole();
	freopen("CONOUT$", "w", stdout);
#endif

	memory.begin = VirtualAlloc(0, MEM_SIZE, MEM_RESERVE, PAGE_NOACCESS);
	memory.end = memory.begin + MEM_SIZE;

	if(!memory.begin) {
		return 1;
	}

	AddVectoredExceptionHandler(1, &access_violation_handler);

	char file_path[MAX_PATH] = {0};
	GetFullPathName(lpCmdline, MAX_PATH, file_path, 0);

	if(!gui_file_open(&memory, file_path)) {
		return 2;
	}

	WNDCLASS window_class      = {0};
	window_class.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	window_class.lpfnWndProc   = window_proc;
	window_class.hInstance     = hInstance;
	window_class.lpszClassName = "BedWindowClass";

	if(!RegisterClass(&window_class)) {
		return 3;
	}

	window = CreateWindow(
		window_class.lpszClassName,
		"Benjamin's Editor",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		0,
		0,
		hInstance,
		0
	);

	if(!window) {
		return 4;
	}

	font = GetStockObject(SYSTEM_FIXED_FONT);
	LOGFONT lf;
	GetObject(font, sizeof(LOGFONT), &lf);
	lf.lfWeight = FW_BOLD;
	bold_font = CreateFontIndirect(&lf);

	ShowWindow(window, SW_MAXIMIZE);
	SetTimer(window, 1, 1000 / 60, 0);

	/* Main event loop */
	MSG msg;

	while(GetMessage(&msg, 0, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int)msg.wParam;
}

/* GUI IMPLEMENTATION BEGIN */

void
gui_clipboard_put(buffer *buffer, isize begin, isize end) {
	OpenClipboard(window);
	EmptyClipboard();
	HGLOBAL mem = GlobalAlloc(GHND, (SIZE_T)(end - begin + 1));
	char *ptr = GlobalLock(mem);

	for(isize i = begin; i < end; ++i) {
		*ptr++ = (char)buffer_get(buffer, i);
	}

	GlobalUnlock(mem);
	SetClipboardData(CF_TEXT, mem);
	CloseClipboard();
}

s8
gui_clipboard_get(void) {
	s8 contents = {0};
	OpenClipboard(window);
	HGLOBAL mem = GetClipboardData(CF_TEXT);
	if(!mem) return contents;
	contents.data = GlobalLock(mem);
	contents.length = (isize)strlen(contents.data);
	GlobalUnlock(mem);
	CloseClipboard();
	return contents;
}

int
gui_font_width(int rune) {
	int width;
	GetCharWidth32(backbuffer, (unsigned)rune, (unsigned)rune, &width);
	return width;
}

int
gui_font_height(void) {
	TEXTMETRIC metric;
	GetTextMetrics(backbuffer, &metric);
	return metric.tmHeight;
}

dimensions
gui_dimensions(void) {
	RECT rect;
	GetClientRect(window, &rect);
	return (dimensions){ .w = rect.right, .h = rect.bottom };
}

void
gui_text(int x, int y, s8 str) {
	TextOut(backbuffer, x, y, (char*)str.data, (int)str.length);
}

void
gui_set_text_color(color rgb) {
	DWORD r = rgb >> 16 & 0xFF;
	DWORD g = rgb >>  8 & 0xFF;
	DWORD b = rgb >>  0 & 0xFF;
	SetTextColor(backbuffer, RGB(r, g, b));
}

b32 gui_is_active(void) {
	return window == GetActiveWindow();
}

/* GUI IMPLEMENTATION END */
