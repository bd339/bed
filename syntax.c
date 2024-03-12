#include "syntax.h"

#include <tree_sitter/api.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct syntax {
	TSParser *parser;
	TSTree   *tree;
};

TSLanguage *tree_sitter_c();

static const char *read(void*, uint32_t, TSPoint, uint32_t*);
static void        edit(syntax_t*, buffer*, TSInputEdit);

syntax_t*
syntax_new() {
	syntax_t *syn;

	if(!(syn = malloc(sizeof(*syn)))) {
		goto FAIL;
	}

	if(!(syn->parser = ts_parser_new())) {
		goto FAIL;
	}

	if(!ts_parser_set_language(syn->parser, tree_sitter_c())) {
		goto FAIL;
	}

	syn->tree = 0;
	return syn;

FAIL:
	syntax_free(syn);
	return 0;
}

void
syntax_free(syntax_t *syn) {
	if(syn->parser) ts_parser_delete(syn->parser);
	if(syn->tree) ts_tree_delete(syn->tree);
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
syntax_erase(syntax_t *syn, buffer *buf, isize begin, isize end) {
	edit(syn, buf, (TSInputEdit) {
		.start_byte   = (uint32_t)begin,
		.old_end_byte = (uint32_t)end,
		.new_end_byte = (uint32_t)begin,
	});
}

bool syntax_verbose;

void
syntax_highlight(syntax_t *syn, isize begin, isize end, highlights_t *out) {
	if(syntax_verbose) {
		printf(">>>>>>>>>>>>>>>>>>>>>SYNTAX HIGHLIGHT %lld - %lld\n", begin, end);
	}
	out->length = 0;
	TSTreeCursor cursor = ts_tree_cursor_new(ts_tree_root_node(syn->tree));

	for(bool recurse = true;;) {
		TSNode node = ts_tree_cursor_current_node(&cursor);

		if(recurse) {
			const char *type  = ts_node_type(node);
			uint32_t    start = ts_node_start_byte(node);
			if(syntax_verbose) {
				printf("%u - %u: %s\n", start, ts_node_end_byte(node), type);
			}

			if(strcmp(type, "comment") == 0) {
				*push(out) = (highlight_t) {
					.event = syntax_comment,
					.at    = start < begin ? begin : start,
				};
				*push(out) = (highlight_t) {
					.event = syntax_end,
					.at    = ts_node_end_byte(node),
				};
			} else if(strcmp(type, "char_literal") == 0 || strcmp(type, "string_literal") == 0) {
				*push(out) = (highlight_t) {
					.event = syntax_string,
					.at    = start < begin ? begin : start,
				};
				*push(out) = (highlight_t) {
					.event = syntax_end,
					.at    = ts_node_end_byte(node),
				};
			}
		}

		if(recurse && ts_tree_cursor_goto_first_child(&cursor)) {
			node = ts_tree_cursor_current_node(&cursor);
			recurse = ts_node_end_byte(node) > begin && ts_node_start_byte(node) < end;
		} else {
			if(ts_tree_cursor_goto_next_sibling(&cursor)) {
				node = ts_tree_cursor_current_node(&cursor);
				recurse = ts_node_end_byte(node) > begin && ts_node_start_byte(node) < end;
			} else if(ts_tree_cursor_goto_parent(&cursor)) {
				recurse = false;
			} else {
				break;
			}
		}
	}

	ts_tree_cursor_delete(&cursor);
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
