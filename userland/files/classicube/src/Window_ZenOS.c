#include "Core.h"
#include "_WindowBase.h"
#include "Bitmap.h"
#include "Errors.h"
#include "Event.h"
#include "Input.h"
#include "InputHandler.h"
#include "Platform.h"
#include "String_.h"
#include <linux/input.h>
#include <stdlib.h>
#include <string.h>
#include "../../../include/harp_api.h"

struct cc_window Window_Alt;

static harp_window_t* win;
static int mouseX, mouseY;
static cc_bool pendingClose;

static int ClampInt(int v, int min, int max) {
	if (v < min) return min;
	if (v > max) return max;
	return v;
}

static void Cursor_GetRawPos(int* x, int* y) {
	*x = mouseX;
	*y = mouseY;
}

static void Cursor_DoSetVisible(cc_bool visible) { }

static void ShowDialogCore(const char* title, const char* msg) {
	Platform_LogConst(title);
	Platform_LogConst(msg);
}

void Window_PreInit(void) {
	DisplayInfo.CursorVisible = true;
}

void Window_Init(void) {
	Input.Sources = INPUT_SOURCE_NORMAL;
	DisplayInfo.Depth = 32;
	DisplayInfo.ScaleX = 1.0f;
	DisplayInfo.ScaleY = 1.0f;
	DisplayInfo.Width = 1024;
	DisplayInfo.Height = 768;
	Window_Main.UIScaleX = DEFAULT_UI_SCALE_X;
	Window_Main.UIScaleY = DEFAULT_UI_SCALE_Y;
	Platform_Flags |= PLAT_FLAG_SINGLE_PROCESS | PLAT_FLAG_APP_EXIT;
}

void Window_Free(void) {
	if (win) {
		harp_close(win);
		win = NULL;
	}
}

static void DoCreateWindow(int width, int height, cc_bool is3d) {
	if (win) harp_close(win);
	win = harp_open("ClassiCube", 40, 30, width, height);
	if (!win) Process_Abort("Failed to open Harp window");
	Window_Main.Handle.ptr = win;
	Window_Main.Width = width;
	Window_Main.Height = height;
	Window_Main.Exists = true;
	Window_Main.Focused = true;
	Window_Main.Is3D = is3d;
	Window_Main.UIScaleX = DEFAULT_UI_SCALE_X;
	Window_Main.UIScaleY = DEFAULT_UI_SCALE_Y;
	DisplayInfo.Width = width;
	DisplayInfo.Height = height;
	Window_Alt = Window_Main;
	mouseX = width / 2;
	mouseY = height / 2;
	Pointer_SetPosition(0, mouseX, mouseY);
}

void Window_Create2D(int width, int height) {
	DoCreateWindow(width, height, false);
}

void Window_Create3D(int width, int height) {
	DoCreateWindow(width, height, true);
}

void Window_Destroy(void) {
	if (win) {
		harp_close(win);
		win = NULL;
	}
	Window_Main.Exists = false;
}

void Window_SetTitle(const cc_string* title) {
	char raw[128];
	int len = title->length < (int)sizeof(raw) - 1 ? title->length : (int)sizeof(raw) - 1;
	Mem_Copy(raw, title->buffer, len);
	raw[len] = '\0';
	if (win) harp_retitle(win, raw);
}

void Clipboard_GetText(cc_string* value) { }
void Clipboard_SetText(const cc_string* value) { }

int Window_GetWindowState(void) {
	return WINDOW_STATE_NORMAL;
}

cc_result Window_EnterFullscreen(void) {
	return ERR_NOT_SUPPORTED;
}

cc_result Window_ExitFullscreen(void) {
	return 0;
}

int Window_IsObscured(void) {
	return !Window_Main.Focused;
}

void Window_Show(void) { }

void Window_SetSize(int width, int height) {
	if (!win || (win->w == width && win->h == height)) return;
}

void Window_RequestClose(void) {
	pendingClose = true;
}

static int MapKey(const harp_event_t* ev) {
	if (ev->code >= 0 && ev->code < 256 && Hotkeys_LWJGL[ev->code]) return Hotkeys_LWJGL[ev->code];
	switch (ev->key) {
	case '\n': return CCKEY_ENTER;
	case '\r': return CCKEY_ENTER;
	case '\b': return CCKEY_BACKSPACE;
	case '\t': return CCKEY_TAB;
	case 27: return CCKEY_ESCAPE;
	case KEY_ARROW_LEFT: return CCKEY_LEFT;
	case KEY_ARROW_RIGHT: return CCKEY_RIGHT;
	case KEY_ARROW_UP: return CCKEY_UP;
	case KEY_ARROW_DOWN: return CCKEY_DOWN;
	default: break;
	}
	if (ev->key >= 'a' && ev->key <= 'z') return ev->key - 32;
	if (ev->key >= 32 && ev->key < 127) return ev->key;
	return 0;
}

static void HandleKey(const harp_event_t* ev) {
	int key = MapKey(ev);
	int down = ev->value != 0;
	int ch = ev->key;
	if (key) Input_Set(key, down);
	if (!down) return;
	if (ch == '\r') ch = '\n';
	if ((ch >= 32 && ch < 127) || ch == '\n' || ch == '\t' || ch == '\b') {
		Event_RaiseInt(&InputEvents.Press, (cc_unichar)ch);
	}
}

static void HandleMouseMove(const harp_event_t* ev) {
	int x = ClampInt(ev->x, 0, Window_Main.Width - 1);
	int y = ClampInt(ev->y, 0, Window_Main.Height - 1);
	if (Input.RawMode) {
		Event_RaiseRawMove(&PointerEvents.RawMoved, x - mouseX, y - mouseY);
	} else {
		Pointer_SetPosition(0, x, y);
	}
	mouseX = x;
	mouseY = y;
}

static int MapMouseButton(int code) {
	switch (code) {
	case BTN_LEFT: return CCMOUSE_L;
	case BTN_RIGHT: return CCMOUSE_R;
	case BTN_MIDDLE: return CCMOUSE_M;
	default: return 0;
	}
}

static void HandleMouseButton(const harp_event_t* ev) {
	int btn = MapMouseButton(ev->code);
	if (btn) Input_SetNonRepeatable(btn, ev->value != 0);
}

void Window_ProcessEvents(float delta) {
	harp_event_t ev;
	while (win && harp_poll_event(win, &ev)) {
		switch (ev.type) {
		case HARP_EVENT_FOCUS:
			Window_Main.Focused = true;
			break;
		case HARP_EVENT_BLUR:
			Window_Main.Focused = false;
			Input_Clear();
			break;
		case HARP_EVENT_MOUSE_MOVE:
			HandleMouseMove(&ev);
			break;
		case HARP_EVENT_MOUSE_BUTTON:
			HandleMouseButton(&ev);
			break;
		case HARP_EVENT_KEY:
			HandleKey(&ev);
			break;
		case HARP_EVENT_CLOSE_REQ:
			pendingClose = true;
			break;
		default:
			break;
		}
	}
	if (pendingClose) {
		pendingClose = false;
		Event_RaiseVoid(&WindowEvents.Closing);
		Window_Main.Exists = false;
	}
}

void Gamepads_PreInit(void) { }
void Gamepads_Init(void) { }
void Gamepads_Process(float delta) { }

void Cursor_SetPosition(int x, int y) {
	mouseX = ClampInt(x, 0, Window_Main.Width - 1);
	mouseY = ClampInt(y, 0, Window_Main.Height - 1);
	Pointer_SetPosition(0, mouseX, mouseY);
}

cc_result Window_OpenFileDialog(const struct OpenFileDialogArgs* args) {
	return ERR_NOT_SUPPORTED;
}

cc_result Window_SaveFileDialog(const struct SaveFileDialogArgs* args) {
	return ERR_NOT_SUPPORTED;
}

void Window_AllocFramebuffer(struct Bitmap* bmp, int width, int height) {
	bmp->scan0 = (BitmapCol*)Mem_Alloc(width * height, BITMAPCOLOR_SIZE, "window pixels");
	bmp->width = width;
	bmp->height = height;
}

void Window_DrawFramebuffer(Rect2D r, struct Bitmap* bmp) {
	int x0, y0, x1, y1, y;
	if (!win || !bmp || !bmp->scan0) return;
	x0 = ClampInt(r.x, 0, win->w);
	y0 = ClampInt(r.y, 0, win->h);
	x1 = ClampInt(r.x + r.width, 0, win->w);
	y1 = ClampInt(r.y + r.height, 0, win->h);
	for (y = y0; y < y1; y++) {
		Mem_Copy(win->buf + y * win->w + x0, bmp->scan0 + y * bmp->width + x0, (x1 - x0) * sizeof(BitmapCol));
	}
	harp_flush_rect(win, x0, y0, x1 - x0, y1 - y0);
}

void Window_FreeFramebuffer(struct Bitmap* bmp) {
	Mem_Free(bmp->scan0);
	bmp->scan0 = NULL;
}

void OnscreenKeyboard_Open(struct OpenKeyboardArgs* args) { }
void OnscreenKeyboard_SetText(const cc_string* text) { }
void OnscreenKeyboard_Close(void) { }
void Window_LockLandscapeOrientation(cc_bool lock) { }

void Window_EnableRawMouse(void) {
	Input.RawMode = true;
	Cursor_SetVisible(false);
}

void Window_UpdateRawMouse(void) { }

void Window_DisableRawMouse(void) {
	Input.RawMode = false;
	Cursor_SetVisible(true);
}
