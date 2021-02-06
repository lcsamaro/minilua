#include "parser.h"
#include "ir.h"
#include "lapi.h"
#include "lex.h"
#include "value.h"
#include "vector.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define TK (p->current)
#define TP (p->current.type)

#define MAX_KEY 256
#define MAX_ARGS 256

#define PARSE_NONE INT_MIN

// TODO: some guards around this -v-
#define EMIT_OP(op, arg1, arg2, target) ir_op(p->c, op, arg1, arg2, target)

static void parser_phi_begin(parser *p, int type) {
	ir_phi_begin(p->c, type);
	p->cdepth++;
}

static int parser_phi_commit(parser *p) {
	p->cdepth--;
	return ir_phi_commit(p->c, p->assignment);
}

typedef struct {
	char *b;
	char *s;
	token current;
} parser_state; // TODO: rename to lexer_state

static parser_state parser_save(parser *p) {
	parser_state state;

	state.b = p->b;
	state.s = p->s;
	state.current = p->current;

	return state;
}

static void parser_restore(parser *p, parser_state state) {
	p->b = state.b;
	p->s = state.s;
	p->current = state.current;
}

static void parser_enter(parser *p) {
	if (++p->depth > PARSER_MAX_REC_DEPTH) {
		// TODO: error handling
	}
	token *t = p->scopep++;
	t->s = NULL;
	t->length = 0;
}

static void parser_exit(parser *p) {
	p->scopep--;
	while (p->scopep->s) {
		char key[MAX_KEY];
		memcpy(key+1, p->scopep->s, p->scopep->length);
		key[0] = p->depth;
		rhhm_remove_str(&p->sym, key, p->scopep->length+1);
		p->scopep--;
	}
	p->depth--;
}

static int parser_sym(parser *p, const char *id, int len) {
	assert(len+1 <= MAX_KEY);

	char key[MAX_KEY];
	memcpy(key+1, id, len);

	int d = p->depth;
	do {
		key[0] = d;
		int r = rhhm_get_str(&p->sym, key, len+1);
		if (r >= 0) return r;
	} while (d-- > 0);
	return -1;
}

static int parser_newsym(parser *p, char *id, int len) {
	assert(len+1 <= MAX_KEY);

	if (p->depth > 0) {
		token *t = p->scopep++;
		t->s = id;
		t->length = len;
	}

	char *key = id-1;
	key[0] = p->depth;

	int r = ir_newvar(p->c);

	p->sym_cdepth[r] = p->cdepth;

	rhhm_insert_str(&p->sym, key, len+1, r);
	return r;
}

static int parser_next(parser *p);

int parser_init(parser *p, state *L, ir *I, char *s) {
	if (rhhm_init(&p->sym, PARSER_SYM_MAX*2, 0)) return 1;
	memset(p->assignment, 0, sizeof(u16) * IR_OP_MAX);

	p->c = I;
	p->b = p->s = s;
	p->L = L;
	p->depth = 0;
	p->cdepth = 0;

	p->scopep = p->scope;
	return parser_next(p);
}

void parser_destroy(parser *p) {
	rhhm_destroy(&p->sym);
}

static int parser_next(parser *p) {
	do {
		if (lex(p->s, &p->current)) return 1;
		p->s += p->current.length;
	} while (TP == LEX_BLANKS || TP == LEX_COMMENT);

	printf(" next token '%.*s'\n", TK.length, TK.s);

	return 0;
}

static int parser_check(parser *p, int tp) {
	return p->current.type == tp ? 0 : 1;
}

static void parser_expect(parser *p, int tp) {
	if (p->current.type != tp) {
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

		char *bol = TK.s;
		while (*bol != '\r' && *bol != '\n' && bol != p->b) bol--;
		if (bol != p->b) bol++;
		char *cur = bol;
		while (*cur && *cur != '\r' && *cur != '\n') printf("%c", *cur++);
		printf("\n");
		for (int i = 0; i < TK.s - bol; i++) printf(" ");
		printf("^");
		for (int i = 1; i < TK.length; i++) printf("~");
		puts("");
		exit(1); // TODO: that was harsh
	}
}

#define NEXT() parser_next(p)
#define EXPECT(t) do { parser_expect(p, t); parser_next(p); } while (0)
#define CHECK(t) parser_expect(p, t)
#define ENTER() parser_enter(p)
#define EXIT() parser_exit(p)

typedef struct {
	int tp;
	int a;
	int b;
} pfield;

static int parse_expr(parser *p);
static int parse_primary(parser *p);

static pfield parse_field(parser *p) {
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
		f.tp = 2;
		//f.a = parse_primary(p);
		f.a = ir_ctt(p->c, lua_intern(p->L, TK.s, TK.length));
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

static int parse_table(parser *p) {
	int r = EMIT_OP(IR_OP_NEWTBL, 0, 0, ir_newvar(p->c));
	EXPECT('{');
	if (TP == '}') { // empty table
		NEXT();
		return r;
	}

	pfield f;
again:
	f = parse_field(p); // at least one field
	if (f.tp == 2) {
		EMIT_OP(IR_OP_TSTORE, f.a, f.b, r);
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

static int parse_param(parser *p) {
	CHECK(LEX_ID);
	int r = parser_newsym(p, TK.s, TK.length);
	p->assignment[r] = r;
	NEXT();
	return r;
}

// TODO: merge local, is_anon parameters into a single enum
static int parse_function(parser *p, int local, int is_anon) {
	NEXT();
	token t = TK;
	if (!is_anon) EXPECT(LEX_ID);
	EXPECT('(');

	ENTER();

	int nparams = 0, param;
	if (TP != ')') {
again:
		param = parse_param(p);
		EMIT_OP(IR_OP_PARAM, param, IR_NO_ARG, IR_NO_TARGET);
		nparams++;
		if (TP == ',') {
			NEXT();
			goto again;
		}
	}
	EXPECT(')');
	int header = ir_current(p->c);
	EMIT_OP(IR_FUNCTION_BEGIN, IR_NO_ARG, nparams, IR_NO_TARGET);
	parse_chunk(p);
	vget(p->c->ops, header).target = ir_current(p->c);

	EMIT_OP(IR_FUNCTION_END, IR_NO_ARG, IR_NO_ARG, header);

	// we patch this nil after chunk is compiled
	int r = EMIT_OP(IR_OP_LCOPY, ir_ctt(p->c, nil), IR_NO_ARG, ir_newvar(p->c));

	EXIT();

	EXPECT(LEX_END);
	if (!local) {
		int field = ir_ctt(p->c, lua_intern(p->L, t.s, t.length));
		r = EMIT_OP(IR_OP_GSTORE, field, r, IR_NO_TARGET); // target ignored
	}
	return r;
}

static int parse_args(parser *p) {
	int nargs = 0, arg, args[MAX_ARGS];
	if (TP == ')') return 0;
again:
	arg = parse_expr(p);
	if (arg == PARSE_NONE) EXPECT(-1);
	args[nargs++] = arg;
	if (TP == ',') {
		NEXT();
		goto again;
	}
	for (arg = 0; arg < nargs; arg++)
		EMIT_OP(IR_OP_ARG, args[arg], IR_NO_ARG, IR_NO_TARGET);
	return nargs;
}

static int parse_call(parser *p, int id) {
	NEXT();
	int nargs = parse_args(p);
	EXPECT(')');
	return EMIT_OP(IR_OP_CALL, id, nargs, ir_newvar(p->c));
}

static int parse_call_single_arg(parser *p, int id) {
	int arg = parse_expr(p);
	if (arg == PARSE_NONE) EXPECT(-1);
	EMIT_OP(IR_OP_ARG, arg, IR_NO_ARG, IR_NO_TARGET);
	return EMIT_OP(IR_OP_CALL, id, 1, ir_newvar(p->c));
}

static int parse_id(parser *p) {

	puts("PARSE ID");

	int r = parser_sym(p, TK.s, TK.length);

	if (r == -1) { // global load
		int field = ir_ctt(p->c, lua_intern(p->L, TK.s, TK.length));
		r = EMIT_OP(IR_OP_GLOAD, field, IR_NO_ARG, ir_newvar(p->c));
	} else {
		// get current SSA assignment
		r = p->assignment[r];
	}

	NEXT();

	if (TP == '(') {
		return parse_call(p, r);
	} else if (TP == LEX_STR || TP == '{') {
		return parse_call_single_arg(p, r);
	}
	return r;
}

static int parse_primary(parser *p) {
	puts("parse primary");
	int r = PARSE_NONE;
	switch (TP) {
	case LEX_ID: r = parse_id(p); break;
	case LEX_NIL: r = ir_ctt(p->c, nil); NEXT(); break;
	case LEX_FALSE: r = ir_ctt(p->c, bv_make_bool(0)); NEXT(); break;
	case LEX_TRUE: r = ir_ctt(p->c, bv_make_bool(1)); NEXT(); break;
	case LEX_STR:
		r = ir_ctt(p->c, lua_intern(p->L, TK.s+1, TK.length-2));
		NEXT();
		break;
	case LEX_NUM:
		if (p->current.length >= 512) {
			// TODO: explicit error here
			r = ir_ctt(p->c, nil);
		} else {
			char tmp[512], *e;
			memcpy(tmp, p->current.s, p->current.length);
			tmp[p->current.length] = '\0';
			double val = strtod(tmp, &e);
			if (*e) {
				// TODO handle conversion failure
			}
			r = ir_ctt(p->c, bv_make_double(val));
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
	case LEX_FUNCTION:
		r = parse_function(p, 1, 1);
		break;
	//default: EXPECT(-1);
	}


	while (TP == '.') {
		NEXT();
		EXPECT(LEX_ID);

		int field = ir_ctt(p->c, lua_intern(p->L, TK.s, TK.length));
		r = EMIT_OP(IR_OP_TLOAD, r, field, ir_newvar(p->c));
		

		NEXT();
	}

	return r;
}

static int parse_factor(parser *p) {
	int r = parse_primary(p);
	while (TP == '*' || TP == '/' || TP == '^' || TP == '%') {
		token t = p->current;
		NEXT();
		r = EMIT_OP(t.type, r, parse_primary(p), ir_newvar(p->c));
	}
	return r;
}

static int parse_term(parser *p) {
	int r = parse_factor(p);
	while (TP == '+' || TP == '-') {
		token t = p->current;
		NEXT();
		r = EMIT_OP(t.type, r, parse_factor(p), ir_newvar(p->c));
	}
	return r;
}

static int parse_cmp(parser *p) {
	int r = parse_term(p);
	while (TP == '<' || TP == LEX_LE ||
		TP == '>' || TP == LEX_GE ||
		TP == LEX_EQ || TP == LEX_NE) {
		token t = p->current;
		NEXT();
		r = EMIT_OP(t.type, r, parse_term(p), ir_newvar(p->c));
	}
	return r;
}

static int parse_logic(parser *p) {
	int r = parse_cmp(p);

	// TODO: actual or and and logic here

	while (TP == LEX_OR || TP == LEX_AND) {
		token t = p->current;
		NEXT();
		r = EMIT_OP(t.type, r, parse_cmp(p), ir_newvar(p->c));
	}

	return r;
}

static int parse_expr(parser *p) {
	return parse_logic(p);
}

static int parse_assignment(parser *p, int local) {
	int r, a, field, n;

	parser_state state = parser_save(p);
	token t = TK;
	NEXT();

	// local definition
	if (local) {
		if (TP == '=') {
			r = parser_newsym(p, t.s, t.length);
			n = p->assignment[r] = r;
			
			NEXT();
			a = parse_expr(p);
			
			return EMIT_OP(IR_OP_LCOPY, a, IR_NO_ARG, n);
		} else if (TP == '.') {
			puts("ERROR: not implemented");
			return 0;
		} else {
			// ERROR
			puts("ERROR: not implemented");
			return 0;
		}

		return r;
	}

	// not a definition (global or local reassignment)
	if (TP == '=') { // global or redef
		r = parser_sym(p, t.s, t.length);
		if (r == -1) { // global
			field = ir_ctt(p->c, lua_intern(p->L, t.s, t.length));
			n = r = ir_newvar(p->c);
			p->assignment[r] = r;
		} else {
			local = 1;

			n = ir_newvar(p->c);

			int old = p->assignment[r];
			p->assignment[n] = r;

			p->sym_cdepth[n] = p->cdepth;

			if (p->cdepth > 0 && p->sym_cdepth[old] < p->cdepth) {
				ir_phi_ins(p->c, n, old, p->assignment);
				p->sym_cdepth[n] = p->sym_cdepth[old];
			}
			p->assignment[r] = n;
		}
		NEXT();
		a = parse_expr(p);
		if (a != n) r = EMIT_OP(IR_OP_LCOPY, a, IR_NO_ARG, n); // TODO: condition always true?
		if (!local) {
			r = EMIT_OP(IR_OP_GSTORE, field, r, IR_NO_TARGET); // target ignored
		}

		return r;
	}
	
	// expression
	puts("expr");
	parser_restore(p, state);
	r = parse_expr(p);
	if (TP == '=') {
		NEXT();
		a = parse_expr(p);
		return EMIT_OP(IR_OP_LCOPY, a, IR_NO_ARG, r);
	}

	return r;
}

static int parse_if(parser *p) {
	int r, b, c;
	NEXT();
	ENTER();
	parser_phi_begin(p, PHI_COND);

	r = parse_expr(p);

	c = ir_current(p->c); // so we can fix jz target later
	EMIT_OP(IR_OP_JZ, r, IR_NO_ARG, 0);

	EXPECT(LEX_THEN);
	parse_chunk(p);

	int elif = 0;
	if (TP == LEX_ELSE || TP == LEX_ELSEIF) {
		if (TP == LEX_ELSEIF) elif = 1;

		// restore current assignments, and backup
		int i = p->c->iphi;
		int tmp = i;
		while (vget(p->c->ops, i).op != IR_OP_NOOP) {
			int old = vget(p->c->ops, i).b;
			p->assignment[old] = old;
			vget(p->c->ops, i).target = vget(p->c->ops, i).a; // backup
			vget(p->c->ops, i).a = old;
			i++;
		}

		b = ir_current(p->c); // so we can fix jz target later
		EMIT_OP(IR_OP_JMP, IR_NO_ARG, IR_NO_ARG, 0);
		
		vget(p->c->ops, c).target = ir_current(p->c); // fix target
		
		if (!elif) {
			NEXT();
			parse_chunk(p);
		} else {
			parse_if(p);
		}

		c = b;

		i = tmp;
		while (vget(p->c->ops, i).op != IR_OP_NOOP) {
			int old = vget(p->c->ops, i).b;
			vget(p->c->ops, i).b = vget(p->c->ops, i).target;
			vget(p->c->ops, i).target = old;
			i++;
		}
	}

	vget(p->c->ops, c).target = ir_current(p->c); // fix target

	if (!elif) EXPECT(LEX_END);

	parser_phi_commit(p);

	EXIT();
	return r;
}

static int parse_while(parser *p) {
	int r, a, c;

	NEXT();
	ENTER();

	EMIT_OP(IR_LOOP_HEADER, IR_NO_ARG, IR_NO_ARG, IR_NO_TARGET);

	parser_phi_begin(p, PHI_LOOP);

	a = ir_current(p->c);

	r = parse_expr(p);
	c = ir_current(p->c); // so we can fix jz target later
	EMIT_OP(IR_OP_JZ, r, IR_NO_ARG, 0);

	EXPECT(LEX_DO);
	EMIT_OP(IR_LOOP_BEGIN, IR_NO_ARG, IR_NO_ARG, IR_NO_TARGET);

	r = parse_chunk(p);
	EXPECT(LEX_END);

	EMIT_OP(IR_OP_JMP, IR_NO_ARG, IR_NO_ARG, a);

	int shift = parser_phi_commit(p);
	vget(p->c->ops, c+shift).target = ir_current(p->c);

	EMIT_OP(IR_LOOP_END, IR_NO_ARG, IR_NO_ARG, IR_NO_TARGET);

	EXIT();
	return r;
}

static int parse_repeat(parser *p) {
	int r, c;

	NEXT();
	ENTER();
	parser_phi_begin(p, PHI_REPEAT);
	
	c = ir_current(p->c);
	EMIT_OP(IR_OP_PHI, IR_NO_ARG, IR_NO_ARG, ir_newvar(p->c));

	r = parse_chunk(p);
	EXPECT(LEX_UNTIL);
	r = parse_expr(p);

	EMIT_OP(IR_OP_JNZ, r, IR_NO_ARG, c);

	EXIT();

	return r;
}

static int parse_for(parser *p) {
	int r, a, b, c, n;

	NEXT();
	ENTER();
	parser_phi_begin(p, PHI_LOOP);

	CHECK(LEX_ID);
	token t = TK;
	EXPECT(LEX_ID);
	EXPECT('='); // TODO: for in
	a = parse_expr(p);
	if (a < 0) {
		int nvar = ir_newvar(p->c);
		a = EMIT_OP(IR_OP_LCOPY, a, IR_NO_ARG, nvar);
	}
	p->assignment[a] = a;

	EXPECT(',');
	b = parse_expr(p);
	if (TP == ',') {
		NEXT();
		c = parse_expr(p); // INCR
	} else {
		bv v; v.d = 1.0;
		c = ir_ctt(p->c, v);
	}

	r = parser_newsym(p, t.s, t.length);
	n = p->assignment[r] = r;

	int fix = ir_current(p->c); // so we can fix jz target later
	EMIT_OP(IR_OP_JE, a, b, 0);

	int header = ir_current(p->c);

	// create local iterator var
	int v = parser_newsym(p, t.s, t.length);
	p->assignment[v] = v;
	v = EMIT_OP(IR_OP_LCOPY, a, IR_NO_ARG, v);

	EXPECT(LEX_DO);
	r = parse_chunk(p);
	EXPECT(LEX_END);

	int na = ir_newvar(p->c);
	int old = p->assignment[a];
	p->assignment[na] = a;
	ir_phi_ins(p->c, na, old, p->assignment);
	p->assignment[a] = na;

	EMIT_OP('+', a, c, na);
	EMIT_OP(IR_OP_JNE, na, b, header);
	
	int shift = parser_phi_commit(p);
	vget(p->c->ops, fix+shift).target = ir_current(p->c);

	EXIT();

	return r;
}

static int parse_statement(parser *p) {
	int r;
	switch (TP) {
	case LEX_FUNCTION: return parse_function(p, 0, 0);
	case LEX_ID: return parse_assignment(p, 0);
	case LEX_WHILE: return parse_while(p);
	case LEX_FOR: return parse_for(p);
	case LEX_REPEAT: return parse_repeat(p);
	case LEX_IF: return parse_if(p);
	default: return PARSE_NONE;
	case LEX_RETURN:
		NEXT();
		r = parse_expr(p);
		EMIT_OP(IR_OP_RET, r != PARSE_NONE ? r : IR_NO_ARG, IR_NO_ARG, IR_NO_TARGET);
		break; //return r;
	case LEX_LOCAL:
		NEXT();
		switch(TP) {
		case LEX_FUNCTION: return parse_function(p, 1, 0);
		case LEX_ID: return parse_assignment(p, 1);
		}
		abort();
		break;
	case LEX_DO:
		NEXT();
		ENTER();
		r = parse_chunk(p);
		EXPECT(LEX_END);
		EXIT();
		break;
	}
	return r;
}

int parse_chunk(parser *p) {
	int r = PARSE_NONE;
again:
	switch (TP) {
	case LEX_LOCAL:
	case LEX_FUNCTION:
	case LEX_ID:
	case LEX_DO:
	case LEX_WHILE:
	case LEX_FOR:
	case LEX_REPEAT:
	case LEX_IF:
	case LEX_RETURN:
		r = parse_statement(p);
		goto again;
	default:
		// TODO: error handling
		return 0;
	}
	return r;
}

