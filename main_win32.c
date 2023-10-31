#include "buffer.h"
#include "gui.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#define MEM_SIZE 1024 * 1024 * 1024 * 1024ull

static arena memory;
static HWND window;
static HDC backbuffer;

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
	static b32 must_reflow;

	switch(message) {
		case WM_CHAR:
			gui_keyboard(memory, kbd_char + (wParam & 0xFF));
			break;

		case WM_KEYDOWN:
			switch(wParam) {
				case VK_LEFT:
				case VK_UP:
				case VK_RIGHT:
				case VK_DOWN:
					gui_keyboard(memory, kbd_left + (wParam - VK_LEFT));
					break;
			}
			break;

		case WM_LBUTTONDOWN:
			gui_mouse(memory, mouse_left, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			break;

		case WM_MOUSEWHEEL:
			int delta = GET_WHEEL_DELTA_WPARAM(wParam);
			int x = GET_X_LPARAM(lParam);
			int y = GET_Y_LPARAM(lParam);
			gui_mouse(memory, delta > 0 ? mouse_scrollup : mouse_scrolldown, x, y);
			break;

		case WM_MOUSEMOVE:
			if(wParam & MK_LBUTTON) {
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);
				gui_mouse(memory, mouse_drag, x, y);
			}
			break;

		case WM_PAINT:
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(window, &ps);
			BitBlt(hdc,
			       ps.rcPaint.left,
			       ps.rcPaint.top,
			       ps.rcPaint.right  - ps.rcPaint.left,
			       ps.rcPaint.bottom - ps.rcPaint.top,
			       backbuffer,
			       ps.rcPaint.left,
			       ps.rcPaint.top,
			       SRCCOPY);
			EndPaint(window, &ps);
			break;

		case WM_TIMER:
			if(wParam == 1) {
				arena cmdbuf;

				if(must_reflow) {
					must_reflow = 0;
					cmdbuf = gui_reflow(memory);
				} else {
					cmdbuf = push_begin(memory);
				}

				gui_redraw(&cmdbuf);
				push_end(&cmdbuf);
				InvalidateRect(window, 0, 0);
				UpdateWindow(window);
			}
			break;

		case WM_SIZE:
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

			backbuffer = CreateCompatibleDC(0);
			bitmap = CreateDIBSection(0, &bitmap_info, DIB_RGB_COLORS, (void**)&pixels, 0, 0);
			SelectObject(backbuffer, bitmap);
			SelectObject(backbuffer, GetStockObject(SYSTEM_FIXED_FONT));
			SetBkMode(backbuffer, TRANSPARENT);
			must_reflow = 1;
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
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	memory.begin = VirtualAlloc(0, MEM_SIZE, MEM_RESERVE, PAGE_NOACCESS);
	memory.end = memory.begin + MEM_SIZE;

	if(!memory.begin) {
		return 1;
	}

	AddVectoredExceptionHandler(1, &access_violation_handler);

	{
		HANDLE file = CreateFile(lpCmdLine, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);

		if(file == INVALID_HANDLE_VALUE) {
			return 4;
		}

		LARGE_INTEGER file_size;

		if(!GetFileSizeEx(file, &file_size)) {
			return 5;
		}

		s8 file_contents;
		file_contents.data = HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS, (SIZE_T)file_size.QuadPart);
		file_contents.length = file_size.QuadPart;

		unsigned long bytes_read;

		if(!ReadFile(file, file_contents.data, file_size.u.LowPart, &bytes_read, 0)) {
			return 6;
		}

		char file_path[MAX_PATH];
		GetFullPathName(lpCmdLine, MAX_PATH, file_path, 0);

		extern buffer_t buffer;
		buffer = buffer_new(&memory, file_path);
		buffer_insert_string(buffer, 0, file_contents);

		CloseHandle(file);
		HeapFree(GetProcessHeap(), 0, file_contents.data);
	}

	WNDCLASS window_class      = {0};
	window_class.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	window_class.lpfnWndProc   = window_proc;
	window_class.hInstance     = hInstance;
	window_class.hCursor       = LoadCursor(0, IDC_ARROW);
	window_class.lpszClassName = "BedWindowClass";

	if(!RegisterClass(&window_class)) {
		return 2;
	}

	window = CreateWindow(window_class.lpszClassName,
	                      "Benjamin's Editor",
	                      WS_OVERLAPPEDWINDOW,
	                      CW_USEDEFAULT,
	                      CW_USEDEFAULT,
	                      CW_USEDEFAULT,
	                      CW_USEDEFAULT,
	                      0,
	                      0,
	                      hInstance,
	                      0);

	if(!window) {
		return 3;
	}

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
