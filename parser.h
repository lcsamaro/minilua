#ifndef PARSER_H
#define PARSER_H

#include "common.h"
#include "ir.h"
#include "rhhm.h"

typedef struct state state;

#define PARSER_SYM_MAX (1<<12)

typedef struct {
	rhhm_value tbl[PARSER_SYM_MAX*2];
	rhhm sym;

	token scope[128]; // lexical scope undo stack
	token *scopep;

	u8 sym_cdepth[IR_OP_MAX];

	u16 assignment[IR_OP_MAX]; // ssa current assignment

	state *L;

	const char *b;
	const char *s;

	token current;

	ir c;
	int depth;

	int cdepth;
} parser;

int parser_init(parser *p, const char *s, state *L);
int parse_chunk(parser *p);

#endif // PARSER_H

