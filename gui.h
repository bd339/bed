#ifndef BED_GUI_H
#define BED_GUI_H

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
	kbd_char,
} gui_event;

int gui_font_width(int);
int gui_font_height(void);
dimensions gui_dimensions(void);
void gui_text(int, int, s8);
void gui_redraw(arena*);
arena gui_reflow(arena);
arena push_begin(arena);
void  push_end(arena*);
void gui_mouse(arena, gui_event, int, int);
void gui_keyboard(arena, gui_event);

#endif // BED_GUI_H
