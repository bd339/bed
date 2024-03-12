#ifndef BED_SYNTAX_H
#define BED_SYNTAX_H

#include "buffer.h"
#include "util.h"

typedef struct syntax syntax_t;

typedef struct {
	enum {
		syntax_comment,
		syntax_string,
		syntax_end,
	} event;
	isize at;
} highlight_t;

typedef struct {
	highlight_t *data;
	isize        length;
	isize        capacity;
} highlights_t;

syntax_t *syntax_new();
void      syntax_free(syntax_t*);
void      syntax_insert(syntax_t*, buffer*, isize, isize);
void      syntax_erase(syntax_t*, buffer*, isize, isize);
void      syntax_highlight(syntax_t*, isize, isize, highlights_t*);

#endif // BED_SYNTAX_H
