#include "gui.h"
#include "log.h"
#include "syntax.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CURSOR API BEGIN */

/*
 * Buffer position of the cursor.
 * NOTE: ONLY UPDATE THIS WITH set_cursor_pos().
 */
static isize cursor_pos;
static int   cursor_x;     // Target x of the cursor when moving between lines
static int   cursor_state; // Implements blinking of the cursor

static isize set_cursor_pos(isize);

/* CURSOR API END */

/* SELECTION API BEGIN */

/*
 * selection[0] is the buffer position of the first rune in the selection.
 * selection[1] is the buffer position of the last rune in the selection.
 * NOTE: To get the selection use selection_begin and selection_end().
 */
static isize selection[2];
static b32   selection_valid;

static isize selection_begin(void);
static isize selection_end(void);
static void  erase_selection(void);

/* SELECTION API END */

/* DISPLAY API BEGIN */

typedef struct {
	int x;
	int y;
	int w;
} cell;

static struct {
	cell  *data;
	isize  length;
	isize  capacity;
} display;                // Slice of x,y coords of every rune in the display
static isize display_pos; // Buffer position of the first rune visible in the display

static cell  xy_at_buffer_pos(isize);
static isize buffer_pos_at_xy(int, int);
static void  display_scroll(int);

/* DISPLAY API END */

/* GUI IMPLEMENTATION BEGIN */

#define MARGIN_TOP   0
#define MARGIN_BOT   gui_font_height()
#define MARGIN_L     0
#define MARGIN_R     5

unsigned          *pixels;
static buffer     *buf;
static const char *buf_file_path;
static syntax_t   *syntax;
static log_t       undo;
static log_t       redo;
static b32         warn_unsaved_changes;
static struct {
	highlight_t *data;
	isize        length;
	isize        capacity;
} highlights;

static void draw_rect(int, int, int, int, color);
static void draw_cursor(int, int, int);
static void insert_rune(isize, int);
static void insert_runes(isize, s8);
static void insert_runes2(isize, s8, bool);
static void delete_rune(isize);
static void delete_runes(isize, isize);
static void delete_runes2(isize, isize, bool);
static b32  buffer_is_dirty(buffer*);

b32
gui_file_open(arena *memory, const char *file_path) {
	FILE *file = fopen(file_path, "rb");

	if(!file) {
		// TODO: handle error
		goto FAIL;
	}

	if(!(buf = buffer_new(memory))) {
		goto FAIL;
	}

	if(!(syntax = syntax_new())) {
		goto FAIL;
	}

	arena tmp = *memory;
	s8 iobuf = { .length = 8 * 1024 };
	iobuf.data = arena_alloc(&tmp, 1, 1, iobuf.length, 0);

	while(!feof(file)) {
		iobuf.length = (isize)fread(iobuf.data, 1, (size_t)iobuf.length, file);

		if(ferror(file)) {
			// TODO: handle error
			goto FAIL;
		}

		buffer_insert_runes(buf, buffer_length(buf), iobuf);
	}

	buf_file_path = strdup(file_path);
	syntax_insert(syntax, buf, 0, buffer_length(buf));
	fclose(file);
	return 1;

FAIL:
	if(file)   fclose(file);
	if(syntax) syntax_free(syntax);
	if(buf)    buffer_free(buf);
	return 0;
}

void
gui_redraw(arena memory) {
	dimensions dim         = gui_dimensions();
	color      magenta     = rgb(255, 0, 255);
	color      bg_color    = rgb(255, 255, 234);
	int        line_height = gui_font_height();

	{ // Draw background
		draw_rect(0, 0, dim.w, dim.h, bg_color);
		draw_rect(0, 0, dim.w, MARGIN_TOP, magenta);
		draw_rect(0, 0, MARGIN_L, dim.h, magenta);
		draw_rect(dim.w - MARGIN_R, 0, MARGIN_R, dim.h, magenta);
		gui_set_bg_color(bg_color);
	}

	{ // Draw buffer tag line
		color tag_color = warn_unsaved_changes ? rgb(255, 0, 0) : rgb(231, 255, 221);
		draw_rect(0, dim.h - MARGIN_BOT, dim.w, MARGIN_BOT, tag_color);

		s8 buffer_label;
		buffer_label.data   = arena_alloc(&memory, 1, 1, 512, ALLOC_NOZERO);
		buffer_label.length = (isize)strlen(buf_file_path);
		memcpy(buffer_label.data, buf_file_path, (size_t)buffer_label.length);

		if(warn_unsaved_changes) {
			const char warning[] = " has unsaved changes.";
			memcpy(buffer_label.data + buffer_label.length, warning, lengthof(warning));
			buffer_label.length += lengthof(warning);
		} else if(buffer_is_dirty(buf)) {
			s8_append(&buffer_label, '*');
		}

		line_info li = buffer_line_info(buf, cursor_pos);
		s8 line_label;
		line_label.data   = arena_alloc(&memory, 1, 1, 512, ALLOC_NOZERO);
		line_label.length = sprintf(line_label.data, "%d,%d", li.line, li.col);

		gui_set_text_color(warn_unsaved_changes ? rgb(255, 255, 255) : rgb(0, 0, 0));
		gui_text(MARGIN_L, dim.h - gui_font_height(), buffer_label);
		gui_text(dim.w - MARGIN_R - 75, dim.h - gui_font_height(), line_label);
		gui_set_text_color(rgb(0, 0, 0));
	}
#if 0
	{ // Draw selection
		if(selection_valid) {
			for(isize i = selection_begin(); i <= selection_end(); ++i) {
				if(display_pos <= i && i < display_pos + display.length) {
					cell xy = xy_at_buffer_pos(i);
					draw_rect(xy.x, xy.y, xy.w, gui_font_height(), rgb(208, 235, 255));
				}
			}
		}
	}
#endif
	{ // Draw runes
		static const color syntax_colors[syntax_end] = {
			[syntax_comment] = rgb(128, 128, 128),
			[syntax_string]  = rgb(244, 187, 68),
		};
		highlight_t *highlight = highlights.data;

		for(isize i = display_pos; i < display_pos + display.length; ++i) {
			if(highlight < highlights.data + highlights.length && highlight->end == i) {
				gui_set_text_bold(false);
				gui_set_text_color(0);
				highlight++;
			}

			if(highlight < highlights.data + highlights.length && highlight->begin == i) {
				if(highlight->event == syntax_keyword) {
					gui_set_text_bold(true);
				} else {
					gui_set_text_color(syntax_colors[highlight->event]);
				}
			}

			if(selection_valid && i == selection_begin()) {
				gui_set_bg_color(rgb(208, 235, 255));
			} else if(selection_valid && i == selection_end()) {
				gui_set_bg_color(bg_color);
			}

			int rune = buffer_get(buf, i);
			cell xy  = xy_at_buffer_pos(i);

			draw_rect(dim.w - MARGIN_R, xy.y, MARGIN_R, line_height, rgb(0, 255, 0));

			if(rune == -1 || rune == '\n') {
				draw_rect(dim.w - MARGIN_R, xy.y, MARGIN_R, line_height, magenta);

				for(isize j = i-1; j >= display_pos; --j) {
					rune = buffer_get(buf, j);

					if(rune == '\t' || rune == ' ' || rune == '\r') {
						xy = xy_at_buffer_pos(j);
						draw_rect(xy.x, xy.y, xy.w, line_height, rgb(255, 0, 0));
					} else {
						break;
					}
				}
			} else {
				gui_text(xy.x, xy.y, rune == '\t' ? s8("    ") : (s8) { 1, (char*)&rune });
			}
		}
	}

	{ // Draw cursor
		if(gui_is_active()) {
			cursor_state = (cursor_state + 1) % 60;
		}

		if(cursor_state < 15 || (cursor_state >= 30 && cursor_state < 45)) {
			if(display_pos <= cursor_pos && cursor_pos < display_pos + display.length) {
				cell cursor_xy = xy_at_buffer_pos(cursor_pos);
				draw_cursor(cursor_xy.x, cursor_xy.y, cursor_xy.w);
			}
		}
	}
}

/* Must be called whenever buffer contents change or the dimensions change. */
void
gui_reflow(void) {
	{ // Reflow display
		display.length = 0;

		dimensions dim = gui_dimensions();
		int x = MARGIN_L;
		int y = MARGIN_TOP;
		int display_bot = dim.h - MARGIN_BOT - gui_font_height();

		highlights.length = 0;
		syntax_highlight_begin(syntax);

		for(isize i = display_pos; y < display_bot; ++i) {
			int rune = buffer_get(buf, i);

			if(rune == -1) {
				*push(&display) = (cell){ x, y, 8 };
				break;
			}

			if(highlights.length && highlights.data[highlights.length - 1].end == i) {
				gui_set_text_bold(false);
			}

			if(!highlights.length || highlights.data[highlights.length - 1].end <= i) {
				highlight_t highlight;

				if(syntax_highlight_next(syntax, i, &highlight)) {
					if(highlight.event == syntax_keyword) {
						gui_set_text_bold(true);
					}

					*push(&highlights) = highlight;
				}
			}

			int width = rune == '\t' ? 4 * gui_font_width(' ') : gui_font_width(rune);

			if(rune == '\n') {
				*push(&display) = (cell){ x, y, width };
				x = MARGIN_L;
				y += gui_font_height();
			} else if(x + width > dim.w - MARGIN_R) {
				x = MARGIN_L;
				y += gui_font_height();
				*push(&display) = (cell){ x, y, width };
				x += width;
			} else {
				*push(&display) = (cell){ x, y, width };
				x += width;
			}
		}

		syntax_highlight_end(syntax);
	}
}

void
gui_mouse(gui_event event, int mouse_x, int mouse_y) {
	switch(event) {
		case mouse_scrolldown:
			display_scroll(4);
			break;

		case mouse_scrollup:
			display_scroll(-4);
			break;

		case mouse_left:
			selection[0]  = set_cursor_pos(buffer_pos_at_xy(mouse_x, mouse_y));
			selection[0] -= selection[0] == buffer_length(buf);
			break;

		case mouse_drag:
			if(mouse_y < MARGIN_BOT) {
				display_scroll(-1);
			} else if(gui_dimensions().h - MARGIN_BOT < mouse_y) {
				display_scroll(1);
			}

			selection[1]  = buffer_pos_at_xy(mouse_x, mouse_y);
			selection[1] -= selection[1] == buffer_length(buf);
			set_cursor_pos(selection[1]);
			selection_valid = 1;
			break;

		default:;
	}
}

void
gui_keyboard(arena memory, gui_event event, int modifiers) {
	{ // Make sure cursor is visible
		if(cursor_pos < display_pos) {
			display_pos = buffer_bol(buf, cursor_pos);
			gui_reflow();
		} else {
			while(display_pos + display.length <= cursor_pos) {
				display_scroll(1); // TODO: this is very inefficient when the cursor is far away
			}
		}
	}

	if(event == kbd_left) {
		set_cursor_pos(cursor_pos - (cursor_pos > 0));
	} else if(event == kbd_right) {
		set_cursor_pos(cursor_pos + (cursor_pos < buffer_length(buf)));
	} else if(event == kbd_down || event == kbd_up) {
		cell cursor_xy = xy_at_buffer_pos(cursor_pos);
		int target_x = cursor_xy.x < cursor_x ? cursor_x : cursor_xy.x;
		int line_height = gui_font_height();
		int target_y = cursor_xy.y + (event == kbd_down ? line_height : -line_height);

		if(target_y < MARGIN_TOP) {
			display_scroll(-1);
			target_y = MARGIN_TOP;
		} else if(target_y >= gui_dimensions().h - MARGIN_BOT - line_height) {
			display_scroll(0); // TODO: this 0 is weird
			target_y = cursor_xy.y;
		}

		set_cursor_pos(buffer_pos_at_xy(target_x, target_y));
		cursor_x = target_x;
	} else {
		enum {
			ctrl_c    = 0x03,
			backspace = 0x08,
			tab       = 0x09,
			enter     = 0x0D,
			ctrl_s    = 0x13,
			ctrl_u    = 0x15,
			ctrl_v    = 0x16,
			ctrl_w    = 0x17,
			ctrl_x    = 0x18,
			ctrl_y    = 0x19,
			ctrl_z    = 0x1A,
		} ch = event - kbd_char;

		if(ch == backspace) {
			if(selection_valid) {
				erase_selection();
			} else if(cursor_pos > 0) {
				delete_rune(cursor_pos - 1);
			}
		} else if(ch == ctrl_u) {
			if(selection_valid) {
				erase_selection();
			} else {
				delete_runes(buffer_bol(buf, cursor_pos), cursor_pos);
			}
		} else if(ch == ctrl_s) {
			FILE *file = fopen(buf_file_path, "wb");

			if(!file) {
				// TODO: handle error
				return;
			}

			for(uint32_t i = 0;;) {
				const char *runes = buffer_read(buf, i, &i);

				if(!i) {
					break;
				}

				if(fwrite(runes, 1, i, file) < i) {
					// TODO: handle error
					fclose(file);
					return;
				}
			}

			warn_unsaved_changes = 0;
			log_clear(&undo);
			log_clear(&redo);
			fclose(file);
		} else if(ch == ctrl_z || ch == ctrl_y) {
			log_t       *push = ch == ctrl_z ? &redo : &undo;
			log_t       *pop  = ch == ctrl_z ? &undo : &redo;
			log_entry_t *top = log_top(pop);

			if(top) {
				switch(top->type) {
					case entry_insert:
						s8 erased = {0};
						erased.data = malloc((size_t)top->length);
						for(int i = 0; i < top->length; ++i) {
							s8_append(&erased, buffer_get(buf, top->at + i));
						}
						log_push_erase(push, top->at, erased);
						delete_runes2(top->at, top->at + top->length, false);
						break;

					case entry_erase:
						log_push_insert(push, top->at, top->erased.length);
						insert_runes2(top->at, top->erased, false);
						break;
				}

				log_pop(pop);
			}

			if(!buffer_is_dirty(buf)) {
				warn_unsaved_changes = 0;
			}
		} else if(ch == ctrl_c || ch == ctrl_x) {
			if(selection_valid) {
				gui_clipboard_put(buf, selection_begin(), selection_end() + 1);
			}

			if(ch == ctrl_x) {
				erase_selection();
			}
		} else if(ch == ctrl_v) {
			s8 clipboard = gui_clipboard_get();
			erase_selection();
			insert_runes(cursor_pos, clipboard);
		} else if(ch == ctrl_w) {
			isize whitespace = cursor_pos;

			for(isize bol = buffer_bol(buf, cursor_pos); whitespace > bol; --whitespace) {
				int rune = buffer_get(buf, whitespace);

				if(rune == ' ' || rune == '\t') {
					break;
				}
			}

			delete_runes(whitespace, cursor_pos);
		} else if(ch == tab && selection_valid) {
			b32 newline = 1;
			isize begin = selection_begin();
			isize end   = selection_end();

			for(isize i = begin; i <= end; ++i) {
				int rune = buffer_get(buf, i);

				if(newline && rune != '\n') {
					newline = 0;

					if(modifiers & 1) {
						if(rune == '\t') {
							delete_rune(i);
							end--;
						}
					} else {
						insert_rune(i, '\t');
						end++;
					}
				}

				newline = rune == '\n';
			}

			selection[0] = begin;
			selection[1] = end;
			selection_valid = 1;
		} else {
			erase_selection();

			if(ch == enter || ch == '\n') {
				s8 indent = {0};
				indent.data = arena_alloc(&memory, 1, 1, 80, 0);
				s8_append(&indent, '\n');

				isize bol = buffer_bol(buf, cursor_pos);
				isize eol = buffer_eol(buf, cursor_pos);

				for(isize i = bol; i < eol; ++i) {
					int rune = buffer_get(buf, i);

					if(rune != ' ' && rune != '\t') {
						break;
					}

					s8_append(&indent, rune);
				}

				isize whitespace = cursor_pos;

				for(; whitespace > bol; --whitespace) {
					int rune = buffer_get(buf, whitespace - 1);

					if(rune != ' ' && rune != '\t') {
						break;
					}
				}

				delete_runes(whitespace, cursor_pos);
				insert_runes(cursor_pos, indent);
			} else {
				insert_rune(cursor_pos, ch);
			}
		}

		gui_reflow();
	}
}

b32
gui_exit(void) {
	if(buffer_is_dirty(buf)) {
		if(!warn_unsaved_changes) {
			warn_unsaved_changes = 1;
			return 0;
		}
	}

	return 1;
}

static void
clip_rect(int *x, int *y, int *w, int *h) {
	dimensions dim = gui_dimensions();
	int xmin = *x < 0 ? 0 : *x;
	int ymin = *y < 0 ? 0 : *y;
	int xmax = *x + *w < dim.w ? *x + *w : dim.w;
	int ymax = *y + *h < dim.h ? *y + *h : dim.h;
	*x = xmin;
	*y = ymin;
	*w = xmax - xmin;
	*h = ymax - ymin;
}

static void
draw_rect(int x, int y, int w, int h, color rgb) {
	clip_rect(&x, &y, &w, &h);
	dimensions dim = gui_dimensions();
	unsigned *row = pixels + y * dim.w + x;

	for(int i = 0; i < h; ++i) {
		unsigned *pixel = row;

		for(int j = 0; j < w; ++j) {
			*pixel++ = rgb;
		}

		row += dim.w;
	}
}

static void
draw_cursor(int x, int y, int w) {
	int cursor_h   = gui_font_height();
	dimensions dim = gui_dimensions();
	clip_rect(&x, &y, &w, &cursor_h);
	unsigned *row = pixels + y * dim.w + x;

	for(int i = 0; i < cursor_h; ++i) {
		unsigned *pixel = row;

		for(int j = 0; j < w; ++j) {
			*pixel = ~*pixel;
			pixel++;
		}

		row += dim.w;
	}
}

static void
insert_rune(isize at, int rune) {
	insert_runes2(at, (s8) { 1, (char*)&rune }, true);
}

static void
insert_runes(isize at, s8 runes) {
	insert_runes2(at, runes, true);
}

static void
insert_runes2(isize at, s8 runes, bool edit) {
	if(edit) {
		log_push_insert(&undo, at, runes.length);
		log_clear(&redo);
	}

	buffer_insert_runes(buf, at, runes);
	syntax_insert(syntax, buf, at, at + runes.length);
	set_cursor_pos(at + runes.length);
}

static void
delete_rune(isize at) {
	delete_runes2(at, at + 1, true);
}

static void
delete_runes(isize begin, isize end) {
	delete_runes2(begin, end, true);
}

static void
delete_runes2(isize begin, isize end, bool edit) {
	if(edit) {
		s8 erased = {0};
		erased.data = malloc((size_t)(end - begin));
		for(isize i = begin; i < end; ++i) {
			s8_append(&erased, buffer_get(buf, i));
		}
		log_push_erase(&undo, begin, erased);
		log_clear(&redo);
	}

	buffer_delete_runes(buf, begin, end);
	syntax_delete(syntax, buf, begin, end);
	set_cursor_pos(begin);
}

static b32
buffer_is_dirty(buffer *buf) {
	return undo.length != 0;
}

/* GUI IMPLEMENTATION END */

/* CURSOR IMPLEMENTATION BEGIN */

static isize
set_cursor_pos(isize pos) {
	cursor_state = 0;
	cursor_x = 0;
	cursor_pos = pos;
	selection_valid = 0;
	return cursor_pos;
}

/* CURSOR IMPLEMENTATION END */

/* SELECTION IMPLEMENTATION BEGIN */

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

static void
erase_selection(void) {
	if(selection_valid) {
		delete_runes(selection_begin(), selection_end() + 1);
	}
}

/* SELECTION IMPLEMENTATION END */

/* DISPLAY IMPLEMENTATION BEGIN */

static cell
xy_at_buffer_pos(isize pos) {
	assert(display_pos <= pos && pos < display_pos + display.length);
	return display.data[pos - display_pos];
}

static isize
buffer_pos_at_xy(int x, int y) {
	isize lo = 0;
	isize hi = display.length - 1;

	for(int line_height = gui_font_height(); lo < hi;) {
		isize mid = (lo + hi) >> 1;

		if(display.data[mid].y + line_height <= y) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}

	for(isize i = lo; i < display.length; ++i) {
		if(display.data[i].x > x || display.data[i].y > y) {
			return i-1 + display_pos;
		}
	}

	return display_pos + display.length - 1;
}

static void
display_scroll(int num_lines) {
	if(num_lines >= 0) {
		display_pos = buffer_pos_at_xy(display.data[0].x, display.data[0].y + (num_lines + 1) * gui_font_height());
	} else {
		while(display_pos > 0 && num_lines) {
			display_pos = buffer_bol(buf, display_pos - 1);
			num_lines++;
		}
	}

	gui_reflow();
}

/* DISPLAY IMPLEMENTATION END */
