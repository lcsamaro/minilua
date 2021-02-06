#ifndef PARSER_H
#define PARSER_H

#include "common.h"
#include "lex.h"
#include "ir.h"
#include "rhhm.h"

typedef struct state state;

#define PARSER_SYM_MAX (1<<12)
#define PARSER_MAX_REC_DEPTH (1<<8) /* TODO */

typedef struct {
	state *L;

	// lex
	char *b;
	char *s;
	token current;

	// symbols
	rhhm sym;

	// lexical scope
	token scope[128]; // lexical scope undo stack
	token *scopep;
	int depth;

	// ssa - TODO: extract to ir?
	u8 sym_cdepth[IR_OP_MAX];  // symbol depth
	int cdepth;                // phi depth
	u16 assignment[IR_OP_MAX]; // current assignment

	// ir
	ir *c;
} parser;

int parser_init(parser *p, state *L, ir *I, char *s);
void parser_destroy(parser *p);
int parse_chunk(parser *p);

#endif // PARSER_H

