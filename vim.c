#include "vim.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>

enum vim_flag_e
{
	VIM_FLAG_DOUBLE     = 1,  /* is the cmd a double character command */
	VIM_FLAG_ARGS       = 2,  /* does the cmd have args */
	VIM_FLAG_HAS_MOTION = 4,  /* does the cmd expect a motion */
	VIM_FLAG_MOTION     = 8,  /* is the cmd a motion command */
	VIM_FLAG_ZERO_COUNT = 16, /* can this motion have a 0 count */
};

struct vim_key_s
{
	int flags;
};

static struct vim_key_s vim_keys[128];

static void vim_do_cmd(vim_state_t *vim)
{
}

static void vim_parse_cmd(vim_state_t *vim, int rune)
{
	if(rune == 27) { /* ESC */
		goto reset;
	}

	switch(vim->parser_state) {
	case VIM_PARSER_BUFFER_DQUOTE:
		if(rune == '"') {
			vim->parser_state = VIM_PARSER_BUFFER_NAME;
		} else {
			vim->parser_state = VIM_PARSER_CMD_CHAR;
			vim_parse_cmd(vim, rune);
		}
		break;
	case VIM_PARSER_BUFFER_NAME:
		if(('a' <= rune && rune <= 'z') || ('0' <= rune && rune <= '9')) {
			vim->buf = rune;
			vim->parser_state = VIM_PARSER_CMD_CHAR;
			break;
		} else goto err;
	case VIM_PARSER_CMD_CHAR:
		if(rune >= 128) {
			goto err;
		} else if(('0' <= rune && rune <= '9') && (rune != '0' || vim->pcmd->count)) {
			vim->pcmd->count = 10 * vim->pcmd->count + (rune - '0');
		} else {
			if(vim->pcmd->count == 0) {
				vim->pcmd->count = !(vim_keys[rune].flags & VIM_FLAG_ZERO_COUNT);
			}

			vim->pcmd->chr = rune;

			if(vim_keys[vim->pcmd->chr].flags & VIM_FLAG_DOUBLE) {
				vim->parser_state = VIM_PARSER_CMD_DOUBLE;
				break;
			}
gotdbl:
			if(vim_keys[vim->pcmd->chr].flags & VIM_FLAG_ARGS) {
				vim->parser_state = VIM_PARSER_CMD_ARG;
				break;
			}
gotarg:
			if(vim->pcmd == &vim->motion && !(vim_keys[vim->pcmd->chr].flags & VIM_FLAG_MOTION)) {
				goto err;
			}

			if(vim_keys[vim->pcmd->chr].flags & VIM_FLAG_HAS_MOTION) {
				assert(vim->pcmd == &vim->cmd);
				vim->pcmd = &vim->motion;
				break;
			}

			vim_do_cmd(vim);
			goto reset;
		}
		break;
	case VIM_PARSER_CMD_DOUBLE:
		if(rune != vim->pcmd->chr) goto err;
		else goto gotdbl;
	case VIM_PARSER_CMD_ARG:
		vim->pcmd->arg = rune;
		goto gotarg;
	}

	return;

err: /* TODO: error message */
reset:
	memset(vim, 0, sizeof(*vim));
	vim->pcmd = &vim->cmd;
}

void vim_parse(vim_state_t *vim, int rune)
{
	switch(vim->mode) {
	case VIM_MODE_INS: putchar(rune & 0x7F);     break;
	case VIM_MODE_CMD: vim_parse_cmd(vim, rune); break;
	}
}