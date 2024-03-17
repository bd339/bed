#include "syntax.h"

#include <tree_sitter/api.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct syntax_node {
	TSNode   node;
	uint32_t i;
};

struct stack {
	struct syntax_node *data;
	isize  length;
	isize  capacity;
};

struct syntax {
	TSParser *parser;
	TSTree   *tree;
	TSSymbol  string_symbol;
	TSSymbol  char_symbol;
	TSSymbol  comment_symbol;
	TSSymbol  type_symbol;
	bool     *keyword_symbols;
	struct stack stack;
};

TSLanguage *tree_sitter_c();

static const char *read(void*, uint32_t, TSPoint, uint32_t*);
static void        edit(syntax_t*, buffer*, TSInputEdit);

syntax_t*
syntax_new() {
	syntax_t   *syn;
	TSLanguage *language = tree_sitter_c();

	if(!(syn = calloc(1, sizeof(*syn)))) {
		goto FAIL;
	}

	if(!(syn->parser = ts_parser_new())) {
		goto FAIL;
	}

	if(!ts_parser_set_language(syn->parser, language)) {
		goto FAIL;
	}

	syn->string_symbol  = ts_language_symbol_for_name(language, "string_literal", 14, true);
	syn->char_symbol    = ts_language_symbol_for_name(language, "char_literal",   12, true);
	syn->comment_symbol = ts_language_symbol_for_name(language, "comment",         7, true);
	syn->type_symbol    = ts_language_symbol_for_name(language, "primitive_type", 14, true);

	static const char *c99_keywords[] = {
	    "auto", "break", "case", "char", "const", "continue", "default", "do",
	    "double", "else", "enum", "extern", "float", "for", "goto", "if",
	    "inline", "int", "long", "register", "restrict", "return", "short", "signed",
	    "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void",
	    "volatile", "while",
	};
	syn->keyword_symbols = calloc(ts_language_symbol_count(language), sizeof(bool));

	for(int i = 0; i < countof(c99_keywords); ++i) {
		uint16_t symbol = ts_language_symbol_for_name(language, c99_keywords[i], (uint32_t)strlen(c99_keywords[i]), false);
		syn->keyword_symbols[symbol] = true;
	}

	return syn;

FAIL:
	syntax_free(syn);
	return 0;
}

void
syntax_free(syntax_t *syn) {
	if(syn->parser) ts_parser_delete(syn->parser);
	if(syn->tree) ts_tree_delete(syn->tree);
	free(syn->keyword_symbols);
	free(syn->stack.data);
	free(syn);
}

void
syntax_insert(syntax_t *syn, buffer *buf, isize begin, isize end) {
	edit(syn, buf, (TSInputEdit) {
		.start_byte   = (uint32_t)begin,
		.old_end_byte = (uint32_t)begin,
		.new_end_byte = (uint32_t)end,
	});
}

void
syntax_delete(syntax_t *syn, buffer *buf, isize begin, isize end) {
	edit(syn, buf, (TSInputEdit) {
		.start_byte   = (uint32_t)begin,
		.old_end_byte = (uint32_t)end,
		.new_end_byte = (uint32_t)begin,
	});
}

bool syntax_verbose;

void
syntax_highlight_begin(syntax_t *syn) {
	*push(&syn->stack) = (struct syntax_node) {
		.node = ts_tree_root_node(syn->tree),
		.i = 0,
	};
}

bool
syntax_highlight_next(syntax_t *syn, isize at, highlight_t *out) {
	while(syn->stack.length) {
		struct syntax_node *top = syn->stack.data + syn->stack.length - 1;

		TSSymbol sym  = ts_node_symbol(top->node);
		bool     emit = true;

		if(sym == syn->comment_symbol) {
			out->event = syntax_comment;
		} else if(sym == syn->string_symbol || sym == syn->char_symbol) {
			out->event = syntax_string;
		} else if(sym == syn->type_symbol) {
			out->event = syntax_keyword;
		} else if(sym < 1000 && syn->keyword_symbols[sym]) { // TODO: get rid of 1000
			out->event = syntax_keyword;
		} else {
			emit = false;
		}

		if(emit) {
			out->begin = at;
			out->end   = ts_node_end_byte(top->node);
			syn->stack.length--;
			return true;
		}

		for(uint32_t children = ts_node_child_count(top->node); top->i < children; top->i++) {
			TSNode child = ts_node_child(top->node, top->i);

			if(ts_node_start_byte(child) <= at && at < ts_node_end_byte(child)) {
				*push(&syn->stack) = (struct syntax_node) {
					.node = child,
					.i = 0,
				};
				top->i++;
				goto NEXT;
			} else if(at < ts_node_start_byte(child)) {
				if(syntax_verbose) {
					printf("STOPPING AT CHILD: %s: %u - %u\n", ts_node_type(child), ts_node_start_byte(child), ts_node_end_byte(child));
				}

				return false;
			}
		}

		syn->stack.length--;

		if(syntax_verbose) {
			printf("%s: %u - %u\n", ts_node_type(top->node), ts_node_start_byte(top->node), ts_node_end_byte(top->node));
		}
NEXT:;
	}

	return false;
}

void
syntax_highlight_end(syntax_t *syn) {
	syn->stack.length = 0;
}

static const char*
read(void *payload, uint32_t byte_index, TSPoint position, uint32_t *bytes_read) {
	return buffer_read(payload, byte_index, bytes_read);
}

static void
edit(syntax_t *syn, buffer *buf, TSInputEdit edit) {
	if(syn->tree) {
		ts_tree_edit(syn->tree, &edit);
	}

	TSTree *tree = ts_parser_parse(syn->parser, syn->tree, (TSInput) {
		.read = read,
		.payload = buf,
		.encoding = TSInputEncodingUTF8,
	});
	assert(tree);

	if(syn->tree) {
		ts_tree_delete(syn->tree);
	}

	syn->tree = tree;
}
