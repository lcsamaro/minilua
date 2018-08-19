#include "parser.h"
#include "lex.h"
#include "state.h"
#include "value.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TK (p->current)
#define TP (p->current.type)

#define MAX_KEY 256

void parser_phi_begin(parser *p, int type) {
	ir_phi_begin(&p->c, type);
	p->cdepth++;
}

int parser_phi_commit(parser *p) {
	p->cdepth--;
	return ir_phi_commit(&p->c, p->assignment);
}

void parser_enter(parser *p) {
	p->depth++;
	//printf("ENTER depth: %d\n", p->depth);
	token *t = p->scopep++;
	t->s = NULL;
	t->length = 0;
}

void parser_exit(parser *p) {
	p->scopep--;
	while (p->scopep->s) {
		char key[MAX_KEY];
		memcpy(key+1, p->scopep->s, p->scopep->length);
		key[0] = p->depth;
		//printf("removing %.*s at %d\n", p->scopep->length, key+1, p->depth);
		rhhm_remove_str(&p->sym, key, p->scopep->length+1);
		p->scopep--;
	}

	//printf("EXIT depth: %d\n", p->depth);
	p->depth--;
}

int parser_sym(parser *p, const char *id, int len) {
	assert(len+1 <= MAX_KEY);

	char key[MAX_KEY];
	memcpy(key+1, id, len);

	int d = p->depth;
	do {
		//printf("searching sym %.*s at %d\n", len, id, d);
		key[0] = d;
		int r = rhhm_get_str(&p->sym, key, len+1);
		if (r >= 0) {
			//printf("sym found %.*s : %d\n", len, id, r);
			return r;
		}
	} while (d-- > 0);
	//puts("sym not found");
	return -1;
}

int parser_newsym(parser *p, const char *id, int len) {
	assert(len+1 <= MAX_KEY);

	if (p->depth > 0) {
		token *t = p->scopep++;
		t->s = id;
		t->length = len;
	}

	char * key = malloc(len+1);
	assert(key);
	key[0] = p->depth;
	memcpy(key+1, id, len);

	int r = ir_newvar(&p->c);
	//printf("newsym %d %.*s depth: %d\n", r, len, id, p->depth);

	p->sym_cdepth[r] = p->cdepth;

	rhhm_insert_str(&p->sym, key, len+1, r);
	return r;
}

int parser_next(parser *p);

int parser_init(parser *p, const char *s, state *L) {
	rhhm_init_fixed(&p->sym, p->tbl, PARSER_SYM_MAX*2);
	memset(p->assignment, 0, sizeof(u16) * IR_OP_MAX);

	ir_init(&p->c);
	p->b = p->s = s;
	p->L = L;
	p->depth = 0;
	p->cdepth = 0;

	p->scopep = p->scope;
	return parser_next(p);
}

int parser_next(parser *p) {
	do {
		if (lex(p->s, &p->current)) return 1;
		p->s += p->current.length;
	} while (TP == LEX_BLANKS || TP == LEX_COMMENT);
	return 0;
}

int parser_accept(parser *p, int tp) {
	if (p->current.type == tp) {
		parser_next(p);
		return 0;
	}
	return 1;
}

void parser_expect(parser *p, int tp) {
	if (parser_accept(p, tp)) {
		printf("unexpected token '%.*s' @ %ld, expecting '", TK.length, TK.s, TK.s-p->b);
		if (tp < 256) printf("%c", tp);
		else switch (tp) {
		case LEX_DO: printf("do"); break;
		case LEX_END: printf("end"); break;
		case LEX_UNTIL: printf("until"); break;
		case LEX_THEN: printf("then"); break;
		case LEX_FOR: printf("for"); break;
		case LEX_IN: printf("in"); break;
		}
		puts("'");
		puts(p->b);
		for (int i = 0; i < TK.s - p->b; i++) printf(" ");
		printf("^");
		for (int i = 1; i < TK.length; i++) printf("~");
		puts("");
		exit(1); // that was harsh
	}
}

#define NEXT() parser_next(p)
#define EXPECT(t) parser_expect(p, t)
#define ENTER() parser_enter(p)
#define EXIT() parser_exit(p)

int parse_expr(parser *p);

typedef struct {
	int tp;
	int a;
	int b;
} pfield;

int parse_primary(parser *p);

pfield parse_field(parser *p) {
	pfield f;
	switch (TP) {
	case '[':
		f.tp = 2;
		NEXT();
		f.a = parse_expr(p);
		EXPECT(']');
		EXPECT('=');
		f.b = parse_expr(p);
		break;
	case LEX_ID:
		puts(">> parse field ID");
		f.tp = 2;
		//f.a = parse_primary(p);
		f.a = ir_ctt(&p->c, lua_intern(p->L, TK.s, TK.length));
		NEXT();
		EXPECT('=');
		f.b = parse_expr(p);
		break;
	default:
		f.tp = 1;
		f.a = parse_expr(p);
	}
	return f;
}

int parse_table(parser *p) {
	int r = ir_op(&p->c, IR_OP_NEWTBL, 0, 0, ir_newvar(&p->c));
	EXPECT('{');
	if (TP == '}') { // empty table
		NEXT();
		return r;
	}

	pfield f;
again:
	f = parse_field(p); // at least one field
	if (f.tp == 2) {
		ir_op(&p->c, IR_OP_TSTORE, f.a, f.b, r);
	} else {
		// TODO
	}

	if (TP != ',' && TP != ';') {
		EXPECT('}');
		return r;
	}
	NEXT();
	if (TP != '}') goto again;

	NEXT();
	return r;
}


int parse_id(parser *p) {
	int r = parser_sym(p, TK.s, TK.length);
	if (r == -1) { // global load
		int field = ir_ctt(&p->c, lua_intern(p->L, TK.s, TK.length));
		r = ir_op(&p->c, IR_OP_GLOAD, field, IR_NO_ARG, ir_newvar(&p->c));
	} else {
		// get current SSA assignment
		r = p->assignment[r];
	}
	NEXT();

	if (TP == '.') {
		puts("DOT!");
	}
	return r;
}

int parse_primary(parser *p) {
	int r = 0;
	switch (TP) {
	case LEX_ID: return parse_id(p);
	case LEX_NIL: r = ir_ctt(&p->c, nil); NEXT(); break;
	case LEX_FALSE: r = ir_ctt(&p->c, bv_make_bool(0)); NEXT(); break;
	case LEX_TRUE: r = ir_ctt(&p->c, bv_make_bool(1)); NEXT(); break;
	case LEX_NUM:
		if (p->current.length >= 512) r = ir_ctt(&p->c, nil);
		else {
			char tmp[512];
			memcpy(tmp, p->current.s, p->current.length);
			tmp[p->current.length] = '\0';
			r = ir_ctt(&p->c, bv_make_double(atof(tmp)));
		}
		NEXT();
		break;
	case '{':
		r = parse_table(p);
		break;
	case '(':
		NEXT();
		r = parse_expr(p);
		EXPECT(')');
		break;
	default:
		EXPECT(-1);
	}
	return r;
}

int parse_factor(parser *p) {
	int r = parse_primary(p);
	while (TP == '*' || TP == '/' || TP == '^' || TP == '%') {
		token t = p->current;
		NEXT();
		r = ir_op(&p->c, t.type, r, parse_primary(p), ir_newvar(&p->c));
	}
	return r;
}

int parse_term(parser *p) {
	int r = parse_factor(p);
	while (TP == '+' || TP == '-') {
		token t = p->current;
		NEXT();
		r = ir_op(&p->c, t.type, r, parse_factor(p), ir_newvar(&p->c));
	}
	return r;
}

int parse_cmp(parser *p) {
	int r = parse_term(p);
	while (TP == '<' || TP == LEX_LE ||
		TP == '>' || TP == LEX_GE ||
		TP == LEX_EQ || TP == LEX_NE) {
		token t = p->current;
		NEXT();
		r = ir_op(&p->c, t.type, r, parse_term(p), ir_newvar(&p->c));
	}
	return r;
}

int parse_expr(parser *p) {
	return parse_cmp(p);
}

int parse_call(parser *p) {

}

int parse_function(parser *p, int local) {

}

int parse_assignment(parser *p, int local) {
	int r, a, field, n;
	token t = TK;

	NEXT();
	EXPECT('=');
	a = parse_expr(p);

	if (local) {
		r = parser_newsym(p, t.s, t.length);
		n = p->assignment[r] = r;
	} else {
		r = parser_sym(p, t.s, t.length);
		if (r == -1) { // global
			field = ir_ctt(&p->c, lua_intern(p->L, t.s, t.length));
			n = r = ir_newvar(&p->c);
			p->assignment[r] = r;
		} else {
			local = 1;
			
			// reassign
			if (a < 0) n = ir_newvar(&p->c);
			else n = a;

			//printf("OLD> %d\n", p->assignment[r]);
			int old = p->assignment[r];
			p->assignment[n] = r;

			p->sym_cdepth[n] = p->cdepth;

			if (p->cdepth > 0 && p->sym_cdepth[old] < p->cdepth) {
				ir_phi_ins(&p->c, n, old, p->assignment);
				p->sym_cdepth[n] = p->sym_cdepth[old];
			}
			p->assignment[r] = n;
		}
	}

	if (a != n) r = ir_op(&p->c, IR_OP_LCOPY, a, IR_NO_ARG, n);
	if (!local) {
		r = ir_op(&p->c, IR_OP_GSTORE, field, r, IR_NO_TARGET); // target ignored
	}

	return r;
}

int parse_if(parser *p) {
	int r, a, b, c, n;
	NEXT();
	ENTER();
	parser_phi_begin(p, PHI_COND);

	r = parse_expr(p);

	c = ir_current(&p->c); // so we can fix jz target later
	ir_op(&p->c, IR_OP_JZ, r, IR_NO_ARG, 0);

	EXPECT(LEX_THEN);
	parse_chunk(p);

	int elif = 0;
	if (TP == LEX_ELSE || TP == LEX_ELSEIF) {
		if (TP == LEX_ELSEIF) elif = 1;

		// restore current assignments, and backup
		int i = p->c.iphi;
		int tmp = i;
		while (p->c.ops[i].op != IR_OP_NOOP) {
			int old = p->c.ops[i].b;
			p->assignment[old] = old;
			p->c.ops[i].target = p->c.ops[i].a; // backup
			p->c.ops[i].a = old;
			i++;
		}

		b = ir_current(&p->c); // so we can fix jz target later
		ir_op(&p->c, IR_OP_JMP, IR_NO_ARG, IR_NO_ARG, 0);
		
		p->c.ops[c].target = ir_current(&p->c); // fix target
		
		if (!elif) {
			NEXT();
			parse_chunk(p);
		} else {
			parse_if(p);
		}

		c = b;

		i = tmp;
		while (p->c.ops[i].op != IR_OP_NOOP) {
			int old = p->c.ops[i].b;
			p->c.ops[i].b = p->c.ops[i].target;
			p->c.ops[i].target = old;
			i++;
		}
	}

	p->c.ops[c].target = ir_current(&p->c); // fix target

	if (!elif) EXPECT(LEX_END);

	parser_phi_commit(p);

	EXIT();
	return r;
}

int parse_statement(parser *p) {
	int r, a, b, c, n;
	switch (TP) {
	case LEX_DISP:
		NEXT();
		r = parse_expr(p);
		ir_op(&p->c, IR_OP_DISP, r, IR_NO_ARG, IR_NO_TARGET);
		break;
	case LEX_FUNCTION: return parse_function(p, 0);
	case LEX_ID: return parse_assignment(p, 0);
	case LEX_LOCAL:
		NEXT();
		switch(TP) {
		case LEX_FUNCTION: return parse_function(p, 1);
		case LEX_ID: return parse_assignment(p, 1);
		}
		puts("invalid statement");
		abort();
		break;
	case LEX_DO:
		NEXT();
		ENTER();
		r = parse_chunk(p);
		EXPECT(LEX_END);
		EXIT();
		break;
	case LEX_WHILE:
		NEXT();
		ENTER();

		ir_op(&p->c, IR_LOOP_HEADER, IR_NO_ARG, IR_NO_ARG, IR_NO_TARGET);

		parser_phi_begin(p, PHI_LOOP);

		a = ir_current(&p->c);

		r = parse_expr(p);
		c = ir_current(&p->c); // so we can fix jz target later
		ir_op(&p->c, IR_OP_JZ, r, IR_NO_ARG, 0);

		EXPECT(LEX_DO);
		ir_op(&p->c, IR_LOOP_BEGIN, IR_NO_ARG, IR_NO_ARG, IR_NO_TARGET);

		r = parse_chunk(p);
		EXPECT(LEX_END);

		ir_op(&p->c, IR_OP_JMP, IR_NO_ARG, IR_NO_ARG, a);
		
		{
		int shift = parser_phi_commit(p);
		p->c.ops[c+shift].target = ir_current(&p->c);
		}

		EXIT();

		break;
	case LEX_FOR: {
		NEXT();
		ENTER();
		parser_phi_begin(p, PHI_LOOP);

		token t = TK;
		EXPECT(LEX_ID);
		EXPECT('='); // TODO: for in
		a = parse_expr(p);
		if (a < 0) {
			int n = ir_newvar(&p->c);
			a = ir_op(&p->c, IR_OP_LCOPY, a, IR_NO_ARG, n);
		}
		p->assignment[a] = a;

		EXPECT(',');
		b = parse_expr(p);
		if (TP == ',') {
			NEXT();
			c = parse_expr(p); // INCR
		} else {
			bv v; v.d = 1.0;
			c = ir_ctt(&p->c, v);
		}

		r = parser_newsym(p, t.s, t.length);
		n = p->assignment[r] = r;

		int fix = ir_current(&p->c); // so we can fix jz target later
		ir_op(&p->c, IR_OP_JE, a, b, 0);

		int header = ir_current(&p->c);

		{
			// create local iterator var
			int v = parser_newsym(p, t.s, t.length);
			p->assignment[v] = v;
			v = ir_op(&p->c, IR_OP_LCOPY, a, IR_NO_ARG, v);
		}


		EXPECT(LEX_DO);
		r = parse_chunk(p);
		EXPECT(LEX_END);


		int na = ir_newvar(&p->c);
		int old = p->assignment[a];
		p->assignment[na] = a;
		ir_phi_ins(&p->c, na, old, p->assignment);
		p->assignment[a] = na;

		ir_op(&p->c, '+', a, c, na);
		ir_op(&p->c, IR_OP_JNE, na, b, header);
		
		{
		int shift = parser_phi_commit(p);
		p->c.ops[fix+shift].target = ir_current(&p->c);
		}
		EXIT();

		} break;
	case LEX_REPEAT:
		NEXT();
		ENTER();
		parser_phi_begin(p, PHI_REPEAT);
		
		c = ir_current(&p->c);
		ir_op(&p->c, IR_OP_PHI, IR_NO_ARG, IR_NO_ARG, ir_newvar(&p->c));

		r = parse_chunk(p);
		EXPECT(LEX_UNTIL);
		r = parse_expr(p);

		ir_op(&p->c, IR_OP_JNZ, r, IR_NO_ARG, c);

		EXIT();

		break;
	case LEX_IF: return parse_if(p);

	default:
		puts("invalid statement");
		abort();
	}
	return r;
}

int parse_chunk(parser *p) {
	int r = -1;
again:
	switch (TP) {
	case LEX_DISP:
	case LEX_LOCAL:
	case LEX_FUNCTION:
	case LEX_ID:
	case LEX_DO:
	case LEX_WHILE:
	case LEX_FOR:
	case LEX_REPEAT:
	case LEX_IF:
		r = parse_statement(p);
		goto again;
	case '\0':
		puts("DONE");
		return 0;
	}
	return r;
}

