#include "log.h"

#include <stdlib.h>

void
log_push_insert(log_t *log, isize at, isize length) {
	log_entry_t *top = log_top(log);

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

void
log_push_erase(log_t *log, isize at, s8 erased) {
	log_entry_t *top = log->stack + log->top;
	top->type = entry_erase;
	top->at = at;
	top->erased = erased;
	log->top = (log->top + 1) % countof(log->stack);
	log->length += log->length < countof(log->stack);
}

log_entry_t*
log_top(log_t *log) {
	if(log->length) {
		return log->stack + (log->top - 1 + countof(log->stack)) % countof(log->stack);
	}

	return 0;
}

void
log_pop(log_t *log) {
	assert(log->length);
	log->top = (log->top - 1 + (int)countof(log->stack)) % (int)countof(log->stack);
	log->length--;
	log_entry_t *top = log->stack + log->top;

	if(top->type == entry_erase) {
		free(top->erased.data);
	}
}

void
log_clear(log_t *log) {
	while(log->length) {
		log_pop(log);
	}
}
