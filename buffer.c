#include "buffer.h"

#include <stdio.h>
#include <string.h>

/* LOG API BEGIN */

typedef struct buffer buffer;

typedef struct {
	enum {
		entry_insert,
		entry_erase,
	} type;
	isize at;
	isize length;
	/* the erased runes IN REVERSE ORDER */
	char erased[128];
} log_entry;

/* A log is a stack of editing operations represented by a ring buffer. */
typedef struct {
	log_entry stack[1000];
	int top;
	int length;
} log;

static void       log_push_insert(log*, isize, isize);
static void       log_push_erase(log*, buffer*, isize, isize);
static log_entry *log_top(log*);
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
	char file_path[512];
	char runes[1 << 30];
};

static buffer_t buffers[1];

static buffer *handle_lookup(buffer_t);
static void insert_runes(buffer*, isize, s8);
static void erase_runes(buffer*, isize, isize);

buffer_t
buffer_new(arena *arena, const char *file_path) {
	for(int i = 0; i < countof(buffers); ++i) {
		if(!buffers[i]) {
			buffer *buf = arena_alloc(arena, sizeof(buffer), 1 << 16, 1, ALLOC_NOZERO);

			if(strlen(file_path) < countof(buf->file_path)) {
				memcpy(buf->file_path, file_path, strlen(file_path) + 1);
			} else {
				return 0;
			}

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
buffer_insert(buffer_t handle, isize at, int rune) {
	buffer *buf = handle_lookup(handle);
	s8 runes = {1, (char*)&rune};
	log_push_insert(&buf->undo, at, 1);
	log_clear(&buf->redo);
	insert_runes(buf, at, runes);
}

void
buffer_insert_runes(buffer_t handle, isize at, s8 str) {
	if(!str.length) {
		return;
	}

	buffer *buf = handle_lookup(handle);
	log_push_insert(&buf->undo, at, str.length);
	log_clear(&buf->redo);
	insert_runes(buf, at, str);
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
	if(begin >= end) {
		return;
	}

	buffer *buf = handle_lookup(handle);
	log_push_erase(&buf->undo, buf, begin, end - begin);
	log_clear(&buf->redo);
	erase_runes(buf, begin, end);
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
	assert(buf->length + 1 <= sizeof(buf->runes));
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
		log_entry *new_entry = log->stack + log->top;
		new_entry->type = entry_insert;
		new_entry->at = at;
		new_entry->length = length;
		log->top = (log->top + 1) % countof(log->stack);
		log->length += log->length < countof(log->stack);
	}
}

static void
log_push_erase(log *log, buffer *buf, isize at, isize length) {
	log_entry *top = log_top(log);

	if(top && top->type == entry_erase && top->at == at + length) {
		top->at = at;

		for(isize end = at + length; at < end;) {
			top->erased[top->length++] = buf->runes[--end];
		}
	} else {
		log_entry *new_entry = log->stack + log->top;
		new_entry->type = entry_erase;
		new_entry->at = at;
		new_entry->length = 0;

		for(isize end = at + length; at < end;) {
			new_entry->erased[new_entry->length++] = buf->runes[--end];
		}

		log->top = (log->top + 1) % countof(log->stack);
		log->length += log->length < countof(log->stack);
	}
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
}

static void
log_clear(log *log) {
	log->top = log->length = 0;
}

static isize
log_undo(log *undo, log *redo, buffer *buf) {
	log_entry *top = log_top(undo);

	if(top) {
		log_pop(undo);

		switch(top->type) {
			case entry_insert:
				log_push_erase(redo, buf, top->at, top->length);
				erase_runes(buf, top->at, top->at + top->length);
				return top->at;

			case entry_erase:
				s8 erased = {top->length, top->erased};
				s8_reverse(&erased);
				log_push_insert(redo, top->at, top->length);
				insert_runes(buf, top->at, erased);
				return top->at + top->length;
		}
	}

	return -1;
}

/* LOG IMPLEMENTATION END */

#ifdef TEST

#define CLOVE_SUITE_NAME buffer.log
#include "clove-unit.h"

#endif // TEST
