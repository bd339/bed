#include "util.h"

#include <string.h>

void*
arena_alloc(arena *arena, isize size, isize align, isize count, int flags) {
	char *p = arena_alignas(arena->begin + arena->offset, align);

	if(flags & ALLOC_RETNULL) {
		if(count > (arena->end - p) / size) {
			return 0;
		}
	} else {
		assert(count <= (arena->end - p) / size);
	}

	isize total = size * count;
	arena->offset = p - arena->begin + total;
	return flags & ALLOC_NOZERO ? p : memset(p, 0, (size_t)total);
}

void*
arena_alignas(void *ptr, isize align) {
	uintptr_t padding = -(uintptr_t)ptr & (uintptr_t)(align - 1);
	return (char*)ptr + padding;
}
