#include "parser.h"
#include "lex.h"
#include "state.h"
#include "value.h"

#include <stdio.h>
#include <string.h>

#define TK (p->current)
#define TP (p->current.type)

int parser_sym(parser *p, const char *id, int len) {
	return rhhm_get_str(&p->sym, id, len);
}

int parser_newsym(parser *p, const char *id, int len) {
	int r = ir_newvar(&p->c);
	rhhm_insert_str(&p->sym, id, len, r);
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
#define ENTER() (p->depth++)
#define EXIT() (p->depth--)

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

int parse_primary(parser *p) {
	int r = 0;
	switch (TP) {
	case LEX_ID:
		r = parser_sym(p, TK.s, TK.length);
		if (r == -1) { // global load
			int field = ir_ctt(&p->c, lua_intern(p->L, TK.s, TK.length));
			r = ir_op(&p->c, IR_OP_GLOAD, field, IR_NO_ARG, ir_newvar(&p->c));
		} else {
			r = p->assignment[r];
		}
		NEXT();
		break;
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

int parse_statement(parser *p) {
	int r, a, b, c, n, local = 0, field;
	switch (TP) {
	case LEX_DISP:
		NEXT();
		r = parse_expr(p);
		ir_op(&p->c, IR_OP_DISP, r, IR_NO_ARG, IR_NO_TARGET);
		break;
	case LEX_LOCAL:
		NEXT();
		local = 1;
	case LEX_ID: {
		token t = TK;

		NEXT();
		EXPECT('=');
		a = parse_expr(p);

		if (local) {
			r = parser_newsym(p, t.s, t.length);
			n = p->assignment[r] = r;
		} else {
			r = parser_sym(p, t.s, t.length); // TODO lexical scope
			if (r == -1) { // global
				field = ir_ctt(&p->c, lua_intern(p->L, t.s, t.length));
				n = r = ir_newvar(&p->c);
				p->assignment[r] = r;
			} else {
				local = 1;
				
				// reassign
				n = ir_newvar(&p->c);

				p->assignment[n] = r;

				int old = p->assignment[r];
				p->assignment[r] = n;

				ir_phi_ins(&p->c, n, old, p->assignment);
			}
		}

		r = ir_op(&p->c, IR_OP_LCOPY, a, IR_NO_ARG, n);
		if (!local) {
			r = ir_op(&p->c, IR_OP_GSTORE, field, r, IR_NO_TARGET); // target ignored
		}

		} break;
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
		ir_phi_begin(&p->c, PHI_LOOP);

		a = ir_current(&p->c);

		r = parse_expr(p);
		c = ir_current(&p->c); // so we can fix jz target later
		ir_op(&p->c, IR_OP_JZ, r, IR_NO_ARG, 0);

		EXPECT(LEX_DO);
		r = parse_chunk(p);
		EXPECT(LEX_END);

		ir_op(&p->c, IR_OP_JMP, IR_NO_ARG, IR_NO_ARG, a);
		
		{
		int shift = ir_phi_commit(&p->c, p->assignment);
		p->c.ops[c+shift].target = ir_current(&p->c);

		}
		EXIT();

		break;
	case LEX_REPEAT:
		NEXT();
		ENTER();
		ir_phi_begin(&p->c, PHI_REPEAT);
		
		c = ir_current(&p->c);
		ir_op(&p->c, IR_OP_PHI, IR_NO_ARG, IR_NO_ARG, ir_newvar(&p->c));

		r = parse_chunk(p);
		EXPECT(LEX_UNTIL);
		r = parse_expr(p);

		ir_op(&p->c, IR_OP_JNZ, r, IR_NO_ARG, c);

		EXIT();

		break;
	case LEX_IF:
		NEXT();
		ENTER();
		ir_phi_begin(&p->c, PHI_COND);

		r = parse_expr(p);

		c = ir_current(&p->c); // so we can fix jz target later
		ir_op(&p->c, IR_OP_JZ, r, IR_NO_ARG, 0);

		EXPECT(LEX_THEN);
		parse_chunk(p);

		int elif = 0;
		if (TP == LEX_ELSE || TP == LEX_ELSEIF) {
			if (TP == LEX_ELSEIF) {
				elif = 1;
				TP = LEX_IF;
			}

			// restore current assignments, and backup
			int i = p->c.iphi;
			int tmp = i;
			while (p->c.ops[i].op != IR_OP_NOOP) {
				int old = p->c.ops[i].b;
				p->assignment[p->assignment[old]] = old;
				p->c.ops[i].target = p->c.ops[i].a; // backup
				p->c.ops[i].a = old;
				i++;
			}

			b = ir_current(&p->c); // so we can fix jz target later
			ir_op(&p->c, IR_OP_JMP, IR_NO_ARG, IR_NO_ARG, 0);
			if (!elif) NEXT();

			p->c.ops[c].target = ir_current(&p->c); // fix target

			parse_chunk(p);

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

		ir_phi_commit(&p->c, p->assignment);

		EXIT();

		break;

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
	case LEX_ID:
	case LEX_DO:
	case LEX_WHILE:
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

