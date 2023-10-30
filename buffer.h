#ifndef BED_BUFFER_H
#define BED_BUFFER_H

#include "util.h"

typedef uintptr_t buffer_t;

buffer_t buffer_new(arena*, s8);
void buffer_free(buffer_t);
void buffer_insert(buffer_t, isize, int);
void buffer_insert_string(buffer_t, isize, s8);
void buffer_erase(buffer_t, isize);
void buffer_erase_string(buffer_t, isize, isize);
isize buffer_bol(buffer_t, isize);
isize buffer_eol(buffer_t, isize);
isize buffer_length(buffer_t);
int buffer_get(buffer_t, isize);
int buffer_save(buffer_t);

#endif // BED_BUFFER_H
