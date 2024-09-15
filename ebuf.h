#ifndef BED_EBUF_H
#define BED_EBUF_H

#include "buffer.h"
#include "vim.h"

typedef struct ebuf_s ebuf_t;
struct ebuf_s
{
	vim_state_t vim; /* DO NOT MOVE */
	buffer     *buf;
	int         cursor_pos;
};

void ebuf_ins(ebuf_t *ebuf, int rune);

#endif /* BED_EBUF_H */
