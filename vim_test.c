#include <stdio.h>
#include <string.h>

#include "ebuf.h"

int main(int argc, char **argv)
{
	ebuf_t       ebuf;
	vim_state_t *vim = &ebuf.vim;

	memset(&ebuf, 0, sizeof(ebuf));
	ebuf.buf = buffer_new(NULL);
	vim_init(vim);

	if(vim->mode != VIM_MODE_CMD) {
		fprintf(stderr, "%s:%d: FAILED: Start in command mode\n", __FILE__, __LINE__);
		return 1;
	}

	if(vim->parser_state != VIM_PARSER_BUFFER_DQUOTE) {
		fprintf(stderr, "%s:%d: FAILED: Parser starts in BUFFER_DQUOTE state\n", __FILE__, __LINE__);
		return 1;
	}

	vim_parse(vim, 'i');
	if(vim->mode != VIM_MODE_INS) {
		fprintf(stderr, "%s:%d: FAILED: 'i' to enter insert mode\n", __FILE__, __LINE__);
		return 1;
	}

	vim_parse(vim, 'a');
	if(buffer_get(ebuf.buf, 0) != 'a') {
		fprintf(stderr, "%s:%d: FAILED: insert 'a' at position 0\n", __FILE__, __LINE__);
		return 1;
	}

	vim_parse(vim, 27);
	if(vim->mode != VIM_MODE_CMD) {
		fprintf(stderr, "%s:%d: FAILED: ESC to enter command mode\n", __FILE__, __LINE__);
		return 1;
	}

	vim_parse(vim, 'h');
	if(ebuf.cursor_pos != 0) {
		fprintf(stderr, "%s:%d: FAILED: 'h' to move left\n", __FILE__, __LINE__);
		return 1;
	}

	vim_parse(vim, 'l');
	if(ebuf.cursor_pos != 1) {
		fprintf(stderr, "%s:%d: FAILED: 'l' to move right\n", __FILE__, __LINE__);
		return 1;
	}

	return 0;
}
