#include "buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* BUFFER IMPLEMENTATION BEGIN */

struct buffer {
	isize  length;
	char  *file_path;
	char   runes[1 << 30];
};

static buffer *buffers[1];

buffer*
buffer_new(arena *arena, const char *file_path) {
	buffer *buf = 0;
	FILE *file = 0;

	for(int i = 0; i < countof(buffers); ++i) {
		if(!buffers[i]) {
			buf = arena_alloc(arena, sizeof(buffer), 1 << 16, 1, ALLOC_NOZERO);
			buf->length = 0;
			buf->file_path = strdup(file_path);

			file = fopen(file_path, "rb");
			if(!file) goto FAIL;
			static char iobuf[8 * 1024];

			while(!feof(file)) {
				size_t read = fread(iobuf, 1, countof(iobuf), file);
				if(read < countof(iobuf) && ferror(file)) goto FAIL;
				memcpy(buf->runes + buf->length, iobuf, read);
				buf->length += (isize)read;
			}

			fclose(file);
			return buffers[i] = buf;
		}
	}

FAIL:
	if(buf)  free(buf->file_path);
	if(file) fclose(file);
	return 0;
}

void
buffer_free(buffer *buf) {
	for(int i = 0; i < countof(buffers); ++i) {
		if(buffers[i] == buf) {
			free(buf->file_path);
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
buffer_erase_runes(buffer *buf, isize begin, isize end) {
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

int
buffer_save(buffer *buf) {
	FILE *file = fopen(buf->file_path, "wb");

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

const char*
buffer_file_path(buffer *buf) {
	return buf->file_path;
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
