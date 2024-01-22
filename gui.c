#include "buffer.h"
#include "gui.h"

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

/* SLICE API BEGIN */

static void grow(void*, isize, isize);

#define insert(s, i) ({                                                                            \
	typeof(s) _s = s;                                                                              \
	typeof(i) _i = i;                                                                              \
	if(_s->length >= _s->capacity) {                                                               \
		grow(_s, sizeof(*_s->data), _i);                                                           \
	} else {                                                                                       \
		memcpy(_s->data + _i + 1, _s->data + _i, (size_t)(sizeof(*_s->data) * (_s->length - _i))); \
	}                                                                                              \
	_s->length++;                                                                                  \
	_s->data + _i;                                                                                 \
})

#define push(s) insert(s, (s)->length)

/* SLICE API END */

/* DISPLAY API BEGIN */

typedef struct {
	int x;
	int y;
} point;

static struct {
	point* data;
	isize  length;
	isize  capacity;
} display;                // Slice of x,y coords of every rune in the display
static isize display_pos; // Buffer position of the first rune visible in the display


static point xy_at_buffer_pos(isize);
static isize buffer_pos_at_xy(int, int);
static void  display_scroll(int);

/* DISPLAY API END */

/* TOKEN API BEGIN */

typedef struct {
	enum {
		token_space, // TODO: collapse this and tab into a whitespace token
		token_tab,
		token_eol,
		token_word,
		token_comment_begin,
		token_comment_end,
	} type;
	isize start;
	int   length;
} token;

static struct {
	token* data;
	isize  length;
	isize  capacity;
} tokens;

static struct {
	token* data;
	isize  length;
	isize  capacity;
} comments;

static isize comment_ub(token);
static void  comment_insert(token);
static int   token_width(token*); // TODO: get rid of this

/* TOKEN API END */

/* GUI IMPLEMENTATION BEGIN */

#define MARGIN_TOP   0
#define MARGIN_BOT   gui_font_height()
#define MARGIN_L     0
#define MARGIN_R     5

#define TEXT_COLOR    rgb(0, 0, 0)
#define COMMENT_COLOR rgb(128, 128, 128)
#define STRING_COLOR  rgb(244, 187, 68)

buffer_t  buffer;
unsigned* pixels;
static b32 warn_unsaved_changes;

static void draw_rect(int, int, int, int, color);
static void draw_cursor(int, int, int);
static int  rune_width(int);

void
gui_redraw(arena memory) {
	dimensions dim = gui_dimensions();
	color magenta  = rgb(255, 0, 255);

	{ // Draw background
		color bg_color = rgb(255, 255, 234);
		draw_rect(0, 0, dim.w, dim.h, bg_color);
		draw_rect(0, 0, dim.w, MARGIN_TOP, magenta);
		draw_rect(0, 0, MARGIN_L, dim.h, magenta);
		draw_rect(dim.w - MARGIN_R, 0, MARGIN_R, dim.h, magenta);
	}

	{ // Draw buffer tag line
		color tag_color = warn_unsaved_changes ? rgb(255, 0, 0) : rgb(231, 255, 221);
		draw_rect(0, dim.h - MARGIN_BOT, dim.w, MARGIN_BOT, tag_color);

		s8 buffer_label;
		buffer_label.data   = arena_alloc(&memory, 1, 1, 512, ALLOC_NOZERO);
		buffer_label.length = (isize)strlen(buffer_file_path(buffer));
		memcpy(buffer_label.data, buffer_file_path(buffer), (size_t)buffer_label.length);

		if(warn_unsaved_changes) {
			const char warning[] = " has unsaved changes.";
			memcpy(buffer_label.data + buffer_label.length, warning, lengthof(warning));
			buffer_label.length += lengthof(warning);
		} else if(buffer_is_dirty(buffer)) {
			s8_append(&buffer_label, '*');
		}

		line_info li = buffer_line_info(buffer, cursor_pos);
		s8 line_label;
		line_label.data   = arena_alloc(&memory, 1, 1, 512, ALLOC_NOZERO);
		line_label.length = sprintf(line_label.data, "%d,%d", li.line, li.col);

		gui_set_text_color(warn_unsaved_changes ? rgb(255, 255, 255) : rgb(0, 0, 0));
		gui_text(MARGIN_L, dim.h - gui_font_height(), buffer_label);
		gui_text(dim.w - MARGIN_R - 75, dim.h - gui_font_height(), line_label);
	}

	{ // Draw selection
		if(selection_valid) {
			for(isize i = selection_begin(); i <= selection_end(); ++i) {
				if(display_pos <= i && i < display_pos + display.length) {
					point xy = xy_at_buffer_pos(i);
					int width = rune_width(buffer_get(buffer, i));
					draw_rect(xy.x, xy.y, width, gui_font_height(), rgb(208, 235, 255));
				}
			}
		}
	}

	{ // Draw tokens
		int line_height = gui_font_height();
		color text_color = TEXT_COLOR;

		for(int i = 0; i < tokens.length; ++i) {
			gui_set_text_color(text_color);

			s8 token_str = {0};
			token_str.data = arena_alloc(&memory, 1, 1, 512, ALLOC_NOZERO);

			color green = rgb(0, 255, 0);
			draw_rect(dim.w - MARGIN_R, xy_at_buffer_pos(tokens.data[i].start).y, MARGIN_R, line_height, green);

			switch(tokens.data[i].type) {
				case token_eol: {
					draw_rect(dim.w - MARGIN_R, xy_at_buffer_pos(tokens.data[i].start).y, MARGIN_R, line_height, magenta);

					for(int j = i-1; j >= 0 && (tokens.data[j].type == token_space || tokens.data[j].type == token_tab); --j) {
						for(isize k = tokens.data[j].start; k < tokens.data[j].start + tokens.data[j].length; ++k) {
							point xy = xy_at_buffer_pos(k);
							draw_rect(xy.x, xy.y, rune_width(buffer_get(buffer, k)), line_height, rgb(255, 0, 0));
						}
					}
				}
				break;

				case token_tab:
					memset(token_str.data + token_str.length, ' ', 4 * (size_t)tokens.data[i].length);
					token_str.length += 4 * tokens.data[i].length;
				break;

				case token_space:
					memset(token_str.data + token_str.length, ' ', (size_t)tokens.data[i].length);
					token_str.length += tokens.data[i].length;
				break;

#if 1
				case token_comment_begin:
					/* fallthrough */
				case token_comment_end:
					/* fallthrough because we may only reset the text color AFTER the token has been drawn */
#endif
				case token_word:
					// TODO: optimize by copying the entire token instead of rune by rune
					for(isize j = tokens.data[i].start; j < tokens.data[i].start + tokens.data[i].length; ++j) {
						s8_append(&token_str, buffer_get(buffer, j));
					}
				break;
			}

			{ // Color the token
				isize ub = comment_ub(tokens.data[i]);

				if(0 <= ub-1 && comments.data[ub-1].type == token_comment_begin) {
					if(text_color != COMMENT_COLOR) {
						gui_set_text_color(text_color = COMMENT_COLOR);
					}
				} else {
					text_color = TEXT_COLOR;
				}
			}

			/* draw the token */
			if(xy_at_buffer_pos(tokens.data[i].start).x + token_width(&tokens.data[i]) > dim.w - MARGIN_R) {
				for(isize j = tokens.data[i].start; j < tokens.data[i].start + tokens.data[i].length; ++j) {
					char rune = (char)buffer_get(buffer, j);
					point xy = xy_at_buffer_pos(j);
					gui_text(xy.x, xy.y, (s8) { 1, &rune });
				}
			} else {
				point xy = xy_at_buffer_pos(tokens.data[i].start);
				gui_text(xy.x, xy.y, token_str);
			}
		}
	}

	{ // Draw cursor
		if(gui_is_active()) {
			cursor_state = (cursor_state + 1) % 60;
		}

		if(cursor_state < 15 || (cursor_state >= 30 && cursor_state < 45)) {
			if(display_pos <= cursor_pos && cursor_pos < display_pos + display.length) {
				point cursor_xy = xy_at_buffer_pos(cursor_pos);
				int rune = buffer_get(buffer, cursor_pos);
				draw_cursor(cursor_xy.x, cursor_xy.y, rune != -1 ? rune_width(rune) : 8);
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

		for(isize i = display_pos; y < display_bot; ++i) {
			int rune = buffer_get(buffer, i);

			if(rune == -1) {
				*push(&display) = (point){ x, y };
				break;
			}

			int width = rune_width(rune);

			if(rune == '\n') {
				*push(&display) = (point){ x, y };
				x = MARGIN_L;
				y += gui_font_height();
			} else if(x + width > dim.w - MARGIN_R) {
				x = MARGIN_L;
				y += gui_font_height();
				*push(&display) = (point){ x, y };
				x += width;
			} else {
				*push(&display) = (point){ x, y };
				x += width;
			}
		}
	}

	{ // Tokenize display
		tokens.length = 0;

		b32 inside_line_comment  = 0;
		b32 inside_block_comment = 0;

		for(isize i = display_pos; i < display_pos + display.length; ++i) {
			int rune = buffer_get(buffer, i);
			b32 inside_comment = inside_line_comment || inside_block_comment;

			switch(rune) {
				case ' ':
					if(tokens.length && tokens.data[tokens.length-1].type == token_space) {
						tokens.data[tokens.length-1].length++;
					} else {
						*push(&tokens) = (token) {
							.type   = token_space,
							.start  = i,
							.length = 1,
						};
					}
					break;

				case '\t':
					if(tokens.length && tokens.data[tokens.length-1].type == token_tab) {
						tokens.data[tokens.length-1].length++;
					} else {
						*push(&tokens) = (token) {
							.type   = token_tab,
							.start  = i,
							.length = 1,
						};
					}
					break;

				case -1:
				case '\n':
					if(inside_line_comment) {
						inside_line_comment = 0;
						*push(&tokens) = (token) {
							.type   = token_comment_end,
							.start  = i,
							.length = 0,
						};
					}

					if(i > 0 && buffer_get(buffer, i-1) == '\r') {
						*push(&tokens) = (token) {
							.type   = token_eol,
							.start  = i-1,
							.length = 2,
						};
					} else {
						*push(&tokens) = (token) {
							.type   = token_eol,
							.start  = i,
							.length = 1,
						};
					}
					break;

				case '/':
				case '#':
					if(!inside_comment) {
						int next = buffer_get(buffer, i+1);

						if(next == '/' || next == '*') {
							inside_line_comment  = next == '/';
							inside_block_comment = next == '*';
							token tok = (token) {
								.type = token_comment_begin,
								.start  = i++,
								.length = 2,
							};

							if(inside_block_comment) {
								comment_insert(tok);
							}

							*push(&tokens) = tok;
							break;
						} else if(rune == '#') {
							char next[4];
							for(int j = 0; j < 4; ++j) next[j] = (char)buffer_get(buffer, i + 1 + j);
							if(memcmp(next, "if 0", 4) == 0) {
								inside_block_comment = 1;
								*push(&tokens) = (token) {
									.type   = token_comment_begin,
									.start  = i,
									.length = 5,
								};
								i += 4;
								break;
							}
						}
					} else if(inside_block_comment && rune == '#') {
						char next[5];
						for(int j = 0; j < 5; ++j) next[j] = (char)buffer_get(buffer, i + 1 + j);
						if(memcmp(next, "endif", 5)  == 0) {
							inside_block_comment = 0;
							*push(&tokens) = (token) {
								.type   = token_comment_end,
								.start  = i,
								.length = 6,
							};
							i += 5;
							break;
						}
					}
					goto TOKEN_WORD;

				case '*':
					if(inside_block_comment && buffer_get(buffer, i+1) == '/') {
						inside_block_comment = 0;
						token tok = (token) {
							.type   = token_comment_end,
							.start  = i++,
							.length = 2,
						};
						comment_insert(tok);
						*push(&tokens) = tok;
						break;
					}
					goto TOKEN_WORD;

				default:
				TOKEN_WORD:
					if(tokens.length && tokens.data[tokens.length-1].type == token_word) {
						tokens.data[tokens.length-1].length++;
					} else {
						*push(&tokens) = (token) {
							.type   = token_word,
							.start  = i,
							.length = 1,
						};
					}
					break;
			}
		}
	}
}

void
gui_mouse(gui_event event, int mouse_x, int mouse_y) {
	switch(event) {
		case mouse_scrolldown:
		case mouse_scrollup:
			display_scroll(event == mouse_scrolldown ? 4 : -4);
			break;

		case mouse_left:
			selection[0]  = set_cursor_pos(buffer_pos_at_xy(mouse_x, mouse_y));
			selection[0] -= selection[0] == buffer_length(buffer);
			break;

		case mouse_drag:
			if(mouse_y < MARGIN_BOT) {
				display_scroll(-1);
			} else if(gui_dimensions().h - MARGIN_BOT < mouse_y) {
				display_scroll(1);
			}

			selection[1]  = buffer_pos_at_xy(mouse_x, mouse_y);
			selection[1] -= selection[1] == buffer_length(buffer);
			set_cursor_pos(selection[1]);
			selection_valid = 1;
			break;

		default:;
	}
}

void
gui_keyboard(arena memory, gui_event event, int modifiers) {
	/* make sure cursor is visible */
	if(cursor_pos < display_pos) {
		display_pos = buffer_bol(buffer, cursor_pos);
		gui_reflow();
	} else {
		while(display_pos + display.length <= cursor_pos) {
			display_scroll(1); // TODO: this is very inefficient when the cursor is far away
		}
	}

	if(event == kbd_left) {
		set_cursor_pos(cursor_pos - (cursor_pos > 0));
	} else if(event == kbd_right) {
		set_cursor_pos(cursor_pos + (cursor_pos < buffer_length(buffer)));
	} else if(event == kbd_down || event == kbd_up) {
		point cursor_xy = xy_at_buffer_pos(cursor_pos);
		int target_x = cursor_xy.x < cursor_x ? cursor_x : cursor_xy.x;
		int line_height = gui_font_height();
		int target_y = cursor_xy.y + (event == kbd_down ? line_height : -line_height);

		if(target_y < MARGIN_TOP) {
			display_scroll(-1);
			target_y = MARGIN_TOP;
		} else if(target_y >= gui_dimensions().h - MARGIN_BOT - line_height) {
			display_scroll(1);
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
				buffer_erase(buffer, set_cursor_pos(cursor_pos - 1));
			}
		} else if(ch == ctrl_u) {
			if(selection_valid) {
				erase_selection();
			} else {
				isize bol = buffer_bol(buffer, cursor_pos);
				buffer_erase_runes(buffer, bol, cursor_pos);
				set_cursor_pos(bol);
			}
		} else if(ch == ctrl_s) {
			if(!buffer_save(buffer)) {
				// TODO: handle error
			}

			warn_unsaved_changes = 0;
		} else if(ch == ctrl_z || ch == ctrl_y) {
			isize where = ch == ctrl_z ? buffer_undo(buffer) : buffer_redo(buffer);

			if(where != -1) {
				set_cursor_pos(where);
			}

			if(!buffer_is_dirty(buffer)) {
				warn_unsaved_changes = 0;
			}
		} else if(ch == ctrl_c || ch == ctrl_x) {
			if(selection_valid) {
				gui_clipboard_copy(buffer, selection_begin(), selection_end() + 1);
			}

			if(ch == ctrl_x) {
				erase_selection();
			}
		} else if(ch == ctrl_v) {
			s8 clipboard = gui_clipboard_get();
			erase_selection();
			buffer_insert_runes(buffer, cursor_pos, clipboard);
			set_cursor_pos(cursor_pos + clipboard.length);
		} else if(ch == ctrl_w) {
			isize whitespace = cursor_pos;

			for(isize bol = buffer_bol(buffer, cursor_pos); whitespace > bol; --whitespace) {
				int rune = buffer_get(buffer, whitespace);

				if(rune == ' ' || rune == '\t') {
					break;
				}
			}

			buffer_erase_runes(buffer, whitespace, cursor_pos);
			set_cursor_pos(whitespace);
		} else if(ch == tab && selection_valid) {
			b32 newline = 1;
			isize begin = selection_begin();
			isize end   = selection_end();

			for(isize i = begin; i <= end; ++i) {
				int rune = buffer_get(buffer, i);

				if(newline && rune != '\n') {
					newline = 0;

					if(modifiers & 1) {
						if(buffer_get(buffer, i) == '\t') {
							buffer_erase(buffer, i);
							end--;
						}
					} else {
						buffer_insert(buffer, i, '\t');
						end++;
					}
				}

				newline = rune == '\n';
			}

			selection[0] = begin;
			selection[1] = end;
		} else {
			erase_selection();

			if(ch == enter || ch == '\n') {
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

				buffer_erase_runes(buffer, whitespace, cursor_pos);
				set_cursor_pos(whitespace);
				buffer_insert_runes(buffer, cursor_pos, indent);
				set_cursor_pos(cursor_pos + indent.length);
			} else {
				buffer_insert(buffer, cursor_pos, ch);
				set_cursor_pos(cursor_pos + 1);
			}
		}

		gui_reflow();
	}
}

b32
gui_exit(void) {
	if(buffer_is_dirty(buffer)) {
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

static void draw_cursor(int x, int y, int w) {
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

static int
rune_width(int rune) {
	return rune == '\t' ? 4 * gui_font_width(' ') : gui_font_width(rune);
}

/* GUI IMPLEMENTATION END */

/* SLICE IMPLEMENTATION BEGIN */

typedef struct {
	void* data;
	isize length;
	isize capacity;
} slice;

static void
grow(void *slize, isize sz, isize hint) {
	slice header;
	memcpy(&header, slize, sizeof(header));

	void *data = header.data;
	header.capacity = header.capacity ? 2 * header.capacity : 1000;
	header.data = calloc((size_t)header.capacity, (size_t)sz);

	if(data) {
		memcpy(header.data, data, (size_t)(hint * sz));
		memcpy(header.data + hint + 1, data + hint + 1, (size_t)(header.length - hint));
		free(data);
	}

	memcpy(slize, &header, sizeof(header));
}

/* SLICE IMPLEMENTATION END */

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
		buffer_erase_runes(buffer, selection_begin(), selection_end() + 1);
		set_cursor_pos(selection_begin());
	}
}

/* SELECTION IMPLEMENTATION END */

/* DISPLAY IMPLEMENTATION BEGIN */

static point
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
			display_pos = buffer_bol(buffer, display_pos - 1);
			num_lines++;
		}
	}

	gui_reflow();
}

/* DISPLAY IMPLEMENTATION END */

/* TOKEN IMPLEMENTATION BEGIN */

/*
 * Compute the upper bound of the given token.
 * ub is the first index such that: comments.data[ub].start > tok.start
 * If no such index exists, ub is comments.length
 */
static isize
comment_ub(token tok) {
	isize lo = 0;
	isize hi = comments.length;

	for(; lo < hi;) {
		isize m = (lo + hi) >> 1;

		if(comments.data[m].start <= tok.start) {
			lo = m+1;
		} else {
			hi = m;
		}
	}

	return lo;
}

static void
comment_insert(token tok) {
	isize insert_pos = comment_ub(tok);

	if(insert_pos - 1 < 0 || comments.data[insert_pos - 1].start < tok.start) {
		*insert(&comments, insert_pos) = tok;
	}
}

static int
token_width(token *tok) {
	switch(tok->type) {
		case token_space: return tok->length * rune_width(' ');
		case token_tab:   return tok->length * rune_width(' ') * 4;

		case token_comment_begin:
		case token_comment_end:
		case token_word: {
			int width = 0;

			for(isize i = tok->start; i < tok->start + tok->length; ++i) {
				width += rune_width(buffer_get(buffer, i));
			}

			return width;
		}

		case token_eol:
			return 0;
	}

	assert(0);
}

/* TOKEN IMPLEMENTATION END */
