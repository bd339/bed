#ifndef BED_BUFFER_H
#define BED_BUFFER_H

#include "util.h"

typedef struct buffer buffer;

typedef struct {
	int col;
	int line;
} line_info;

buffer     *buffer_new(arena*);
void        buffer_free(buffer*);
void        buffer_insert_runes(buffer*, isize, s8);
void        buffer_delete_runes(buffer*, isize, isize);
isize       buffer_bol(buffer*, isize);
isize       buffer_eol(buffer*, isize);
isize       buffer_length(buffer*);
int         buffer_get(buffer*, isize);
line_info   buffer_line_info(buffer*, isize);
const char *buffer_read(buffer*, uint32_t, uint32_t*);

#endif // BED_BUFFER_H
