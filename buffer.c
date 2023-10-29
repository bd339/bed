#include "buffer.h"

#include <stdio.h>
#include <string.h>

typedef struct {
	isize length;
	s8 file_path;
	u16 generation;
	u8 runes[1 << 30];
} buffer;

static buffer_t buffers[1];

static buffer*
handle_lookup(buffer_t handle) {
	buffer *buf = (buffer*)(handle & 0xFFFFFFFFFFFF0000);
	assert(buf->generation == (handle & 0xFFFF));
	return buf;
}

buffer_t
buffer_new(arena *arena, s8 file_path) {
	for(int i = 0; i < countof(buffers); ++i) {
		if(!buffers[i]) {
			buffer *buf = arena_alloc(arena, sizeof(buffer), 1 << 16, 1, ALLOC_NOZERO);
			buf->file_path = s8_clone(arena, file_path);
			buf->length = 0;
			return buffers[i] = (buffer_t)buf | buf->generation;
		}
	}

	return 0;
}

void
buffer_free(buffer_t handle) {
	for(int i = 0; i < countof(buffers); ++i) {
		if(buffers[i] == handle) {
			handle_lookup(handle)->generation++;
			buffers[i] = 0;
			return;
		}
	}

	assert(0);
}

void
buffer_insert(buffer_t handle, isize pos, int rune) {
	buffer *buf = handle_lookup(handle);
	assert(pos <= buf->length);
	assert(buf->length + 1 <= sizeof(buf->runes));
	memmove(buf->runes + pos + 1, buf->runes + pos, (size_t)(buf->length - pos));
	buf->runes[pos] = (u8)(rune & 0xFF);
	buf->length++;
}

void
buffer_insert_string(buffer_t handle, isize pos, s8 str) {
	buffer *buf = handle_lookup(handle);
	assert(pos <= buf->length);
	assert(buf->length + str.length <= sizeof(buf->runes));
	memmove(buf->runes + pos + str.length, buf->runes + pos, (size_t)(buf->length - pos));
	memcpy(buf->runes + pos, str.data, (size_t)str.length);
	buf->length += str.length;
}

void
buffer_erase(buffer_t handle, isize pos) {
	buffer *buf = handle_lookup(handle);
	assert(pos < buf->length);
	memmove(buf->runes + pos, buf->runes + pos + 1, (size_t)(buf->length - (pos + 1)));
	--buf->length;
}

isize
buffer_bol(buffer_t handle, isize pos) {
	buffer *buf = handle_lookup(handle);
	isize i;

	for(i = pos - 1; i >= 0; --i) {
		if(buf->runes[i] == '\n') {
			break;
		}
	}

	return i+1;
}

isize
buffer_eol(buffer_t handle, isize pos) {
	buffer *buf = handle_lookup(handle);

	for(isize i = pos; i < buf->length; ++i) {
		if(buf->runes[i] == '\n') {
			return i;
		}
	}

	return buf->length;
}

isize
buffer_length(buffer_t handle) {
	buffer *buf = handle_lookup(handle);
	return buf->length;
}

int
buffer_get(buffer_t handle, isize pos) {
	buffer *buf = handle_lookup(handle);
	return pos < buf->length ? buf->runes[pos] : -1;
}

int buffer_save(buffer_t handle) {
	buffer *buf = handle_lookup(handle);
	FILE *file = fopen((char*)buf->file_path.data, "wb");

	if(!file) {
		// TODO: propagate error information
		return 0;
	}

	if(fwrite(buf->runes, 1, (size_t)buf->length, file) < (size_t)buf->length) {
		// TODO: propagate error information
		fclose(file);
		return 0;
	}

	fclose(file);
	return 1;
}
