#ifndef BED_LOG_H
#define BED_LOG_H

#include "util.h"

typedef struct log_entry log_entry_t;
typedef struct log       log_t;

struct log_entry {
	enum {
		entry_insert,
		entry_erase,
	} type;
	isize at;
	union {
		isize length;
		s8    erased;
	};
};

/* A log is a stack of editing operations represented by a ring buffer. */
struct log {
	log_entry_t stack[1000];
	int         top;
	int         length;
};

void         log_push_insert(log_t*, isize, isize);
void         log_push_erase(log_t*, isize, s8);
log_entry_t* log_top(log_t*);
void         log_pop(log_t*);
void         log_clear(log_t*);

#endif // BED_LOG_H
