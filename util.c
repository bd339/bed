#include "util.h"

#include <stdlib.h>
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

void
slice_grow(void *slice, isize sz) {
	struct {
		void  *data;
		isize  length;
		isize  capacity;
	} header;
	memcpy(&header, slice, sizeof(header));

	void *data = header.data;
	header.capacity = header.capacity ? 2 * header.capacity : 1000;
	header.data = calloc((size_t)header.capacity, (size_t)sz);

	if(data) {
		memcpy(header.data, data, (size_t)(header.length * sz));
		free(data);
	}

	memcpy(slice, &header, sizeof(header));
}
