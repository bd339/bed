#ifndef BED_VIM_H
#define BED_VIM_H

typedef enum vim_mode_e vim_mode_t;
enum vim_mode_e
{
	VIM_MODE_CMD,
	VIM_MODE_INS,
};

typedef enum vim_parser_state_e vim_parser_state_t;
enum vim_parser_state_e
{
	VIM_PARSER_BUFFER_DQUOTE,
	VIM_PARSER_BUFFER_NAME,
	VIM_PARSER_CMD_CHAR,
	VIM_PARSER_CMD_DOUBLE,
	VIM_PARSER_CMD_ARG,
};

typedef struct vim_cmd_s vim_cmd_t;
struct vim_cmd_s
{
	int count;
	int chr;
	int arg;
};

typedef struct vim_state_s vim_state_t;
struct vim_state_s
{
	vim_mode_t         mode;
	vim_parser_state_t parser_state;
	vim_cmd_t          cmd;
	vim_cmd_t          motion;
	vim_cmd_t         *pcmd;
	int                buf;
};

void vim_init(vim_state_t *vim);
void vim_parse(vim_state_t *vim, int rune);

#endif // BED_VIM_H
