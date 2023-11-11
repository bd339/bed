#ifndef BED_BUFFER_H
#define BED_BUFFER_H

#include "util.h"

typedef uintptr_t buffer_t;

typedef struct {
	int col;
	int line;
} line_info;

buffer_t buffer_new(arena*, const char*);
void buffer_free(buffer_t);
void buffer_insert(buffer_t, isize, int);
void buffer_insert_runes(buffer_t, isize, s8);
void buffer_erase(buffer_t, isize);
void buffer_erase_runes(buffer_t, isize, isize);
isize buffer_bol(buffer_t, isize);
isize buffer_eol(buffer_t, isize);
isize buffer_length(buffer_t);
int buffer_get(buffer_t, isize);
int buffer_save(buffer_t);
isize buffer_undo(buffer_t);
isize buffer_redo(buffer_t);
const char *buffer_file_path(buffer_t);
line_info   buffer_line_info(buffer_t, isize);
b32         buffer_is_dirty(buffer_t);

#endif // BED_BUFFER_H
