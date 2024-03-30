#ifndef BED_SYNTAX_H
#define BED_SYNTAX_H

#include "buffer.h"
#include "util.h"

#include <stdbool.h>

typedef struct syntax    syntax_t;
typedef struct highlight highlight_t;

struct highlight {
	enum {
		syntax_comment,
		syntax_string,
		syntax_keyword,
		syntax_end,
	} event;
	isize begin;
	isize end;
};

syntax_t *syntax_new();
void      syntax_free(syntax_t*);
void      syntax_insert(syntax_t*, buffer*, isize, isize);
void      syntax_delete(syntax_t*, buffer*, isize, isize);
void      syntax_highlight_begin(syntax_t*);
bool      syntax_highlight_next(syntax_t*, buffer*, isize, highlight_t*);
void      syntax_highlight_end(syntax_t*);

#endif // BED_SYNTAX_H
