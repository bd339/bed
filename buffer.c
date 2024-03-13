#include "buffer.h"

#include <string.h>

/* BUFFER IMPLEMENTATION BEGIN */

struct buffer {
	isize  length;
	char   runes[1 << 30];
};

static buffer *buffers[1];

buffer*
buffer_new(arena *arena) {
	buffer *buf  = 0;

	for(int i = 0; i < countof(buffers); ++i) {
		if(!buffers[i]) {
			buf = arena_alloc(arena, sizeof(buffer), 1 << 16, 1, ALLOC_NOZERO);
			buf->length = 0;
			return buffers[i] = buf;
		}
	}

	return 0;
}

void
buffer_free(buffer *buf) {
	for(int i = 0; i < countof(buffers); ++i) {
		if(buffers[i] == buf) {
			buffers[i] = 0;
			return;
		}
	}

	assert(0);
}

void
buffer_insert_runes(buffer *buf, isize at, s8 runes) {
	if(runes.length) {
		assert(at <= buf->length);
		memmove(buf->runes + at + runes.length, buf->runes + at, (size_t)(buf->length - at));
		memcpy(buf->runes + at, runes.data, (size_t)runes.length);
		buf->length += runes.length;
	}
}

void
buffer_delete_runes(buffer *buf, isize begin, isize end) {
	if(begin < end) {
		assert(end <= buf->length);
		memmove(buf->runes + begin, buf->runes + end, (size_t)(buf->length - end));
		buf->length -= end - begin;
	}
}

isize
buffer_bol(buffer *buf, isize pos) {
	isize i;

	for(i = pos - 1; i >= 0; --i) {
		if(buf->runes[i] == '\n') {
			break;
		}
	}

	return i+1;
}

isize
buffer_eol(buffer *buf, isize pos) {
	for(isize i = pos; i < buf->length; ++i) {
		if(buf->runes[i] == '\n') {
			return i;
		}
	}

	return buf->length;
}

isize
buffer_length(buffer *buf) {
	return buf->length;
}

int
buffer_get(buffer *buf, isize pos) {
	return pos < buf->length ? buf->runes[pos] & 0xFF : -1;
}

line_info
buffer_line_info(buffer *buf, isize at) {
	line_info li = {0};

	for(isize i = 0; i < at; ++i) {
		li.line += buf->runes[i] == '\n';
		li.col  *= buf->runes[i] != '\n';
		li.col  += buf->runes[i] != '\n';
	}

	li.col++;
	li.line++;
	return li;
}

const char*
buffer_read(buffer *buf, uint32_t byte_index, uint32_t *bytes_read) {
	*bytes_read = (uint32_t)buf->length - byte_index;
	return buf->runes + byte_index;
}

/* BUFFER IMPLEMENTATION END */
