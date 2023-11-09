#include "buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* LOG API BEGIN */

typedef struct buffer buffer;

typedef struct {
	enum {
		entry_insert,
		entry_erase,
	} type;
	isize at;
	union {
		isize length;
		s8 erased;
	};
} log_entry;

/* A log is a stack of editing operations represented by a ring buffer. */
typedef struct {
	log_entry stack[1000];
	int top;
	int length;
} log;

static void       log_push_insert(log*, isize, isize);
static void       log_push_erase(log*, buffer*, isize, isize);
static log_entry* log_top(log*);
static void       log_pop(log*);
static void       log_clear(log*);
static isize      log_undo(log*, log*, buffer*);

/* LOG API END */

/* BUFFER IMPLEMENTATION BEGIN */

struct buffer {
	isize length;
	u16 generation;
	log undo;
	log redo;
	char *file_path;
	char runes[];
};

static buffer_t buffers[1];

static buffer *handle_lookup(buffer_t);
static void insert_runes(buffer*, isize, s8);
static void erase_runes(buffer*, isize, isize);

buffer_t
buffer_new(arena *arena, const char *file_path) {
	buffer *buf = 0;
	FILE *file = 0;

	for(int i = 0; i < countof(buffers); ++i) {
		if(!buffers[i]) {
			buf = arena_alloc(arena, sizeof(buffer) + (1 << 30), 1 << 16, 1, ALLOC_NOZERO);
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
			return buffers[i] = (buffer_t)buf | buf->generation;
		}
	}

FAIL:
	if(buf)  free(buf->file_path);
	if(file) fclose(file);
	return 0;
}

void
buffer_free(buffer_t handle) {
	for(int i = 0; i < countof(buffers); ++i) {
		if(buffers[i] == handle) {
			buffer *buf = handle_lookup(handle);
			buf->generation++;
			log_clear(&buf->undo);
			log_clear(&buf->redo);
			free(buf->file_path);
			buffers[i] = 0;
			return;
		}
	}

	assert(0);
}

void
buffer_insert(buffer_t handle, isize at, int rune) {
	buffer *buf = handle_lookup(handle);
	s8 runes = {1, (char*)&rune};
	log_push_insert(&buf->undo, at, 1);
	log_clear(&buf->redo);
	insert_runes(buf, at, runes);
}

void
buffer_insert_runes(buffer_t handle, isize at, s8 str) {
	if(str.length) {
		buffer *buf = handle_lookup(handle);
		log_push_insert(&buf->undo, at, str.length);
		log_clear(&buf->redo);
		insert_runes(buf, at, str);
	}
}

void
buffer_erase(buffer_t handle, isize at) {
	buffer *buf = handle_lookup(handle);
	log_push_erase(&buf->undo, buf, at, 1);
	log_clear(&buf->redo);
	erase_runes(buf, at, at + 1);
}

void
buffer_erase_runes(buffer_t handle, isize begin, isize end) {
	if(begin < end) {
		buffer *buf = handle_lookup(handle);
		log_push_erase(&buf->undo, buf, begin, end - begin);
		log_clear(&buf->redo);
		erase_runes(buf, begin, end);
	}
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
	return pos < buf->length ? buf->runes[pos] & 0xFF : -1;
}

int
buffer_save(buffer_t handle) {
	buffer *buf = handle_lookup(handle);
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

isize
buffer_undo(buffer_t handle) {
	buffer *buf = handle_lookup(handle);
	return log_undo(&buf->undo, &buf->redo, buf);
}

isize
buffer_redo(buffer_t handle) {
	buffer *buf = handle_lookup(handle);
	return log_undo(&buf->redo, &buf->undo, buf);
}

static buffer*
handle_lookup(buffer_t handle) {
	buffer *buf = (buffer*)(handle & 0xFFFFFFFFFFFF0000);
	assert(buf->generation == (handle & 0xFFFF));
	return buf;
}

static void
insert_runes(buffer *buf, isize at, s8 runes) {
	assert(at <= buf->length);
	memmove(buf->runes + at + runes.length, buf->runes + at, (size_t)(buf->length - at));
	memcpy(buf->runes + at, runes.data, (size_t)runes.length);
	buf->length += runes.length;
}

static void
erase_runes(buffer *buf, isize begin, isize end) {
	assert(begin < end);
	assert(end <= buf->length);
	memmove(buf->runes + begin, buf->runes + end, (size_t)(buf->length - end));
	buf->length -= end - begin;
}

/* BUFFER IMPLEMENTATION END */

/* LOG IMPLEMENTATION BEGIN */

static void
log_push_insert(log *log, isize at, isize length) {
	log_entry *top = log_top(log);

	if(top && top->type == entry_insert && top->at + top->length == at) {
		top->length += length;
	} else {
		top = log->stack + log->top;
		top->type = entry_insert;
		top->at = at;
		top->length = length;
		log->top = (log->top + 1) % countof(log->stack);
		log->length += log->length < countof(log->stack);
	}
}

static void
log_push_erase(log *log, buffer *buf, isize at, isize length) {
	log_entry *top = log->stack + log->top;
	top->type = entry_erase;
	top->at = at;
	top->erased.data = malloc((size_t)length);
	top->erased.length = length;
	assert(top->erased.data);
	memcpy(top->erased.data, buf->runes + at, (size_t)length);
	log->top = (log->top + 1) % countof(log->stack);
	log->length += log->length < countof(log->stack);
}

static log_entry*
log_top(log *log) {
	if(log->length) {
		return log->stack + (log->top - 1 + countof(log->stack)) % countof(log->stack);
	}

	return 0;
}

static void
log_pop(log *log) {
	assert(log->length);
	log->top = (log->top - 1 + (int)countof(log->stack)) % (int)countof(log->stack);
	log->length--;
	log_entry *top = log->stack + log->top;

	if(top->type == entry_erase) {
		free(top->erased.data);
	}
}

static void
log_clear(log *log) {
	while(log->length) {
		log_pop(log);
	}
}

static isize
log_undo(log *undo, log *redo, buffer *buf) {
	isize where = -1;
	log_entry *top = log_top(undo);

	if(top) {
		switch(top->type) {
			case entry_insert:
				log_push_erase(redo, buf, top->at, top->length);
				erase_runes(buf, top->at, top->at + top->length);
				where = top->at;
				break;

			case entry_erase:
				log_push_insert(redo, top->at, top->erased.length);
				insert_runes(buf, top->at, top->erased);
				where = top->at + top->erased.length;
				break;
		}

		log_pop(undo);
	}

	return where;
}

/* LOG IMPLEMENTATION END */

#ifdef TEST

#include <stdlib.h>

#define CLOVE_SUITE_NAME buffer_log
#include "clove-unit.h"

#ifdef _WIN32
#define aligned_alloc(alignment, size) _aligned_malloc(size, alignment)
#endif // _WIN32

CLOVE_TEST(undo_empty_nop) {
	buffer *buf = aligned_alloc(1 << 16, sizeof(*buf) + 100);
	CLOVE_IS_TRUE(buf);
}

#endif // TEST
