#include <stdio.h>

#include "ebuf.h"

void ebuf_ins(ebuf_t *ebuf, int rune)
{
	s8 tmp;
	tmp.length = 1;
	tmp.data = (char*)&rune;
	buffer_insert_runes(ebuf->buf, ebuf->cursor_pos++, tmp);
}
