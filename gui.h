#ifndef BED_GUI_H
#define BED_GUI_H

#include "buffer.h"
#include "util.h"

typedef unsigned color;

#define rgb(r, g, b) ((r) << 16 | (g) << 8 | (b))

typedef struct {
	int w;
	int h;
} dimensions;

typedef enum {
	kbd_left,
	kbd_up,
	kbd_right,
	kbd_down,
	mouse_left,
	mouse_scrollup,
	mouse_scrolldown,
	mouse_drag,
	kbd_char, // NOTE: MUST BE LAST
} gui_event;

void       gui_clipboard_put(buffer*, isize, isize);
s8         gui_clipboard_get(void);
int        gui_font_width(int);
int        gui_font_height(void);
dimensions gui_dimensions(void);
void       gui_text(int, int, s8);
void       gui_set_text_color(color);
void       gui_redraw(arena);
void       gui_reflow(void);
void       gui_mouse(gui_event, int, int);
void       gui_keyboard(arena, gui_event, int);
b32        gui_exit(void);
b32        gui_is_active(void);
b32        gui_file_open(arena*, const char*);

#endif // BED_GUI_H
