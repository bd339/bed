#include "buffer.h"
#include "gui.h"

#include <string.h>

#define MARGIN 5

/* DEFERRED RENDERING API BEGIN */

typedef enum {
	layer_bg,
	layer_text,
	layer_fg,
} layer;

static arena push_begin(arena);
static void  push_rect(arena*, layer, int, int, int, int, color);
static s8*   push_text(arena*, int, int);
static void  push_cursor(arena*, int, int, int);
static void  push_end(arena*);

/* DEFERRED RENDERING API END */

buffer_t buffer;
unsigned *pixels;

/* GUI STATE VARIABLES BEGIN */

static isize cursor_pos;
static int   cursor_x;
static b32   selection_valid;
/*
 * selection[0] is the buffer position of the first rune in the selection.
 * selection[1] is the buffer position of the last rune in the selection.
 * NOTICE: selection[1] < selection[0] is perfectly valid and reasonable.
 * If this is a problem for you, use selection_begin() and selection_end().
 */
static isize selection[2];

/* GUI STATE VARIABLES END */

/*
 * Buffer position of the beginning of every line in the display.
 * Line means display line, i.e. a long line that gets wrapped is two display lines.
 */
static isize display_lines[128];
/* 0 .. num_display_lines - 1 are the visible display lines. */
static int num_display_lines;

static int rune_width(int);
static isize selection_begin(void);
static isize selection_end(void);
static void display_scroll(int);
static isize buffer_pos_at_xy(int, int);

/* GUI IMPLEMENTATION BEGIN */

void
gui_redraw(arena memory) {
	arena cmdbuf = push_begin(memory);
	dimensions dim = gui_dimensions();
	color magenta = rgb(255, 0, 255);
	push_rect(&cmdbuf, layer_bg, MARGIN, MARGIN, dim.w - 2*MARGIN, dim.h - MARGIN, rgb(255, 255, 234));
	push_rect(&cmdbuf, layer_bg, 0, 0, dim.w, MARGIN, magenta);
	push_rect(&cmdbuf, layer_bg, 0, 0, MARGIN, dim.h, magenta);
	push_rect(&cmdbuf, layer_bg, dim.w - MARGIN, 0, MARGIN, dim.h, magenta);

	int x = MARGIN;
	int y = MARGIN;

	for(int i = 0; i < num_display_lines; ++i) {
		s8 *line = push_text(&cmdbuf, x, y);
		b32 cursor_on_line_i = 0;
		int rune;

		for(isize j = display_lines[i]; j < display_lines[i+1]; ++j) {
			rune = buffer_get(buffer, j);
			int width = rune_width(rune);

			if(cursor_pos == j) {
				cursor_on_line_i = 1;
				push_cursor(&cmdbuf, x, y, rune != -1 ? width : 8);
			}

			if(selection_valid) {
				if(selection_begin() <= j && j <= selection_end()) {
					push_rect(&cmdbuf, layer_bg, x, y, width, gui_font_height(), rgb(208, 235, 255));
				}
			}

			if(rune == '\n' || rune == -1) {
				/* highlight trailing whitespace */
				int trailing_x = x;

				for(isize k = j-1; k >= display_lines[i]; --k) {
					int rune = buffer_get(buffer, k);

					if(rune != ' ' && rune != '\t') {
						break;
					}

					trailing_x -= rune_width(rune);
				}

				if(x - trailing_x && !cursor_on_line_i) {
					color red = rgb(255, 0, 0);
					push_rect(&cmdbuf, layer_bg, trailing_x, y, x - trailing_x, gui_font_height(), red);
				}
			} else if(rune == '\t') {
				memcpy(line->data + line->length, "    ", 4);
				line->length += 4;
			} else {
				s8_append(line, rune);
			}

			x += width;
		}

		if(rune != '\n' && rune != -1) {
			/* last rune on current line was not a newline i.e. current line was wrapped */
			color green = rgb(0, 255, 0);
			push_rect(&cmdbuf, layer_fg, dim.w - MARGIN, y, MARGIN, gui_font_height(), green);
		}

		x = MARGIN;
		y += gui_font_height();
	}

	push_end(&cmdbuf);
}

/* Must be called whenever buffer contents change or the dimensions change. */
void
gui_reflow(void) {
	dimensions dim = gui_dimensions();
	int x = MARGIN;
	int y = MARGIN;

	num_display_lines = 0;

	for(isize i = display_lines[0]; y < dim.h - gui_font_height(); ++i) {
		int rune = buffer_get(buffer, i);

		if(rune == -1) {
			display_lines[++num_display_lines] = i+1;
			break;
		}

		int width = rune_width(rune);

		if(rune == '\n') {
			display_lines[++num_display_lines] = i+1;
			x = MARGIN;
			y += gui_font_height();
		} else if(x + width > dim.w - MARGIN) {
			display_lines[++num_display_lines] = i;
			x = MARGIN + width;
			y += gui_font_height();
		} else {
			x += width;
		}
	}
}

void
gui_mouse(gui_event event, int mouse_x, int mouse_y) {
	if(event == mouse_scrolldown || event == mouse_scrollup) {
		display_scroll(event == mouse_scrolldown ? 4 : -4);
	} else if(event == mouse_left) {
		selection[0] = cursor_pos = buffer_pos_at_xy(mouse_x, mouse_y);
		selection_valid = 0;
		selection[0] -= selection[0] == buffer_length(buffer);
		cursor_x = 0;
	} else if(event == mouse_drag) {
		selection_valid = 1;
		selection[1] = buffer_pos_at_xy(mouse_x, mouse_y);
		cursor_pos = selection[1] -= selection[1] == buffer_length(buffer);
	}
}

void
gui_keyboard(arena memory, gui_event event) {
	if(event == kbd_left) {
		cursor_pos -= cursor_pos > 0;
		cursor_x = 0;
	} else if(event == kbd_right) {
		cursor_pos += cursor_pos < buffer_length(buffer);
		cursor_x = 0;
	} else if(event == kbd_down || event == kbd_up) {
		for(int i = 0; i < num_display_lines; ++i) {
			if(cursor_pos < display_lines[i+1]) {
				if(i == 0 && event == kbd_up) {
					display_scroll(-1);
					i = 1;
				} else if(i == num_display_lines - 1 && event == kbd_down) {
					display_scroll(1);
					i = num_display_lines - 2;
				}

				int x = cursor_x ? cursor_x : MARGIN;

				if(!cursor_x) {
					for(isize j = display_lines[i]; j <= cursor_pos; ++j) {
						x += rune_width(buffer_get(buffer, j));
					}
				}

				cursor_pos = event == kbd_up ? display_lines[i-1] : display_lines[i+1];
				isize stop = event == kbd_up ? display_lines[i]   : display_lines[i+2];

				for(int next_x = MARGIN;; ++cursor_pos) {
					if(cursor_pos == stop) {
						cursor_x = x;
						cursor_pos--;
						break;
					}

					next_x += rune_width(buffer_get(buffer, cursor_pos));

					if(next_x >= x) {
						cursor_x = 0;
						break;
					}
				}

				break;
			}
		}
	}

	if(event == kbd_left || event == kbd_right) {
		if(cursor_pos < display_lines[0]) {
			display_lines[0] = buffer_bol(buffer, cursor_pos);
			gui_reflow();
			return;
		} else {
			while(cursor_pos >= display_lines[num_display_lines]) {
				display_scroll(1);
			}
		}
	}

	if(event < kbd_char) {
		return;
	}

	if(selection_valid) {
		buffer_erase_string(buffer, selection_begin(), selection_end() + 1);
		cursor_pos = selection_begin();
		selection_valid = 0;
	}

	enum {
		backspace = 0x08,
		enter     = 0x0D,
	} ch = event - kbd_char;

	if(ch == backspace) {
		if(cursor_pos > 0) {
			buffer_erase(buffer, --cursor_pos);
		}
	} else if(ch == enter || ch == '\n') {
		s8 indent = {0};
		indent.data = arena_alloc(&memory, 1, 1, 80, 0);
		s8_append(&indent, '\n');

		isize bol = buffer_bol(buffer, cursor_pos);
		isize eol = buffer_eol(buffer, cursor_pos);

		for(isize i = bol; i < eol; ++i) {
			int rune = buffer_get(buffer, i);

			if(rune != ' ' && rune != '\t') {
				break;
			}

			s8_append(&indent, rune);
		}

		isize whitespace = cursor_pos;

		for(; whitespace > bol; --whitespace) {
			int rune = buffer_get(buffer, whitespace - 1);

			if(rune != ' ' && rune != '\t') {
				break;
			}
		}

		buffer_erase_string(buffer, whitespace, cursor_pos);
		cursor_pos = whitespace;
		buffer_insert_string(buffer, cursor_pos, indent);
		cursor_pos += indent.length;
	} else if(ch == 0x15) {
		isize bol = buffer_bol(buffer, cursor_pos);
		buffer_erase_string(buffer, bol, cursor_pos);
		cursor_pos = bol;
	} else if(ch == 0x13) { // Ctrl+s
		if(!buffer_save(buffer)) {
			// TODO: handle error
		}
	} else {
		buffer_insert(buffer, cursor_pos++, ch);
	}

	gui_reflow();
}

/* GUI IMPLEMENTATION END */

/* DEFERRED RENDERING IMPLEMENTATION BEGIN */

typedef enum {
	cmd_rect,
	cmd_text,
	cmd_cursor,
} cmd;

typedef struct {
	int meta;
	layer layer;
	int x;
	int y;
	int w;
	int h;
	color rgb;
} rect_cmd;

typedef struct {
	int meta;
	int x;
	int y;
	s8 runes;
} text_cmd;

typedef struct {
	int meta;
	int x;
	int y;
	int w;
} cursor_cmd;

#define CMD_ALIGN (alignof(rect_cmd) < alignof(text_cmd)                                              \
                ? (alignof(text_cmd) < alignof(cursor_cmd) ? alignof(cursor_cmd) : alignof(text_cmd)) \
                : (alignof(rect_cmd) < alignof(cursor_cmd) ? alignof(cursor_cmd) : alignof(rect_cmd)))

static arena
push_begin(arena memory) {
	arena cmdbuf = memory;
	cmdbuf.begin = memory.begin + memory.offset;
	cmdbuf.offset = 0;
	return cmdbuf;
}

static void
push_rect(arena *cmdbuf, layer layer, int x, int y, int w, int h, color rgb) {
	rect_cmd *cmd = arena_alloc(cmdbuf, sizeof *cmd, CMD_ALIGN, 1, 0);
	cmd->meta = sizeof(*cmd) << 2 | cmd_rect;
	cmd->layer = layer;
	cmd->x = x;
	cmd->y = y;
	cmd->w = w;
	cmd->h = h;
	cmd->rgb = rgb;
}

static s8*
push_text(arena *cmdbuf, int x, int y) {
	int cmd_size = sizeof(text_cmd) + 512;
	text_cmd *cmd = arena_alloc(cmdbuf, cmd_size, CMD_ALIGN, 1, 0);
	cmd->meta = cmd_size << 2 | cmd_text;
	cmd->x = x;
	cmd->y = y;
	cmd->runes.data = (u8*)cmd + sizeof(*cmd);
	return &cmd->runes;
}

static void
push_cursor(arena *cmdbuf, int x, int y, int w) {
	cursor_cmd *cmd = arena_alloc(cmdbuf, sizeof *cmd, CMD_ALIGN, 1, 0);
	cmd->meta = sizeof(*cmd) << 2 | cmd_cursor;
	cmd->x = x;
	cmd->y = y;
	cmd->w = w;
}

static void
push_end(arena *cmdbuf) {
	dimensions dim = gui_dimensions();

	/* draw layers from background to foreground */
	for(layer layer = layer_bg; layer <= layer_fg; ++layer) {
		for(char *it = cmdbuf->begin; it < cmdbuf->begin + cmdbuf->offset;) {
			it = arena_alignas(it, CMD_ALIGN);
			cmd cmd = *(int*)it & 3;

			if(cmd == cmd_rect) {
				rect_cmd *rect = (rect_cmd*)it;

				if(rect->layer == layer) {
					unsigned *row = pixels + rect->y * dim.w + rect->x;

					for(int i = 0; i < rect->h; ++i) {
						unsigned *pixel = row;

						for(int j = 0; j < rect->w; ++j) {
							*pixel++ = rect->rgb;
						}

						row += dim.w;
					}
				}
			} else if(cmd == cmd_text && layer == layer_text) {
				text_cmd *text = (text_cmd*)it;
				gui_text(text->x, text->y, text->runes);
			} else if(cmd == cmd_cursor && layer == layer_fg) {
				cursor_cmd *cursor = (cursor_cmd*)it;
				unsigned *row = pixels + cursor->y * dim.w + cursor->x;

				for(int i = 0; i < gui_font_height(); ++i) {
					unsigned *pixel = row;

					for(int j = 0; j < cursor->w; ++j) {
						*pixel = ~*pixel;
						pixel++;
					}

					row += dim.w;
				}
			}

			it += *(int*)it >> 2;
		}
	}
}

/* DEFERRED RENDERING IMPLEMENTATION END */

static int
rune_width(int rune) {
	return rune == '\t' ? 4 * gui_font_width(' ') : gui_font_width(rune);
}

static isize
selection_begin(void) {
	assert(selection_valid);
	return selection[0] < selection[1] ? selection[0] : selection[1];
}

static isize
selection_end(void) {
	assert(selection_valid);
	return selection[0] < selection[1] ? selection[1] : selection[0];
}

static isize
buffer_pos_at_xy(int x, int y) {
	int line = (y - MARGIN) / gui_font_height();
	int line_x = MARGIN;

	if(line >= num_display_lines) {
		line = num_display_lines - 1;
	}

	for(isize i = display_lines[line];; ++i) {
		line_x += rune_width(buffer_get(buffer, i));

		if(i == display_lines[line+1]) {
			return i-1;
		} else if(line_x > x) {
			return i;
		}
	}
}

static void
display_scroll(int num_lines) {
	if(num_lines >= 0) {
		display_lines[0] = display_lines[num_lines < num_display_lines ? num_lines : num_display_lines - 1];
	} else {
		while(display_lines[0] > 0 && num_lines) {
			display_lines[0] = buffer_bol(buffer, display_lines[0] - 1);
			num_lines++;
		}
	}

	gui_reflow();
}
