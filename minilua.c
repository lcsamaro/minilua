#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef uint64_t u64; typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8;
typedef int64_t i64; typedef int32_t i32; typedef int16_t i16; typedef int8_t i8;


/*
 * Boxed Value: NaN tagging, assumes little endian
 */
typedef union { u64 u; double d; int i; void *p; } bv;
#define bv_value_mask UINT64_C(0x0000ffffffffffff)
#define bv_none       UINT64_C(0x7ff7000000000000)
#define bv_nil        UINT64_C(0x7ff8000000000000)
#define bv_ptr        UINT64_C(0x7ff9000000000000)
#define bv_str        UINT64_C(0x7ffa000000000000)
#define bv_tbl        UINT64_C(0x7ffb000000000000)
/*#define bv_symbol UINT64_C(0x7ffe000000000000)*/
const bv nil  = { bv_nil };
#define GCd u8 mark

bv bv_from_double(double d) {
	bv v;
	v.d = d;
	return v;
}

u32 bv_type(bv v) {
	return v.u >> 48;
}

int bv_isnil(bv v) {
	return v.u == bv_nil;
}

bv bv_add(bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = a.d + b.d;
	printf("add %lf\n", r.d);
	return r;
}

bv bv_mul(bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = a.d * b.d;
	printf("mul %lf\n", r.d);
	return r;
}

bv bv_sub(bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = a.d - b.d;
	printf("sub %lf\n", r.d);
	return r;
}

bv bv_div(bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = a.d / b.d;
	printf("div %lf\n", r.d);
	return r;
}

/*
 * String - simple string
 */
typedef struct { char *data; u32 cap; u32 len; GCd; } str;

int str_maybe_resize(str *s, u32 c) {
	if (c <= s->cap) return 0;
	u32 n = 8; char *d;
	while (n < c) n *= 2;
	d = realloc(s->data, n);
	if (!d) return 1;
	s->data = d; s->cap = n;
	return 0;
}

/*
 * Vector - simple vector
 */
typedef struct { bv *data; u32 cap; u32 sz; } vec;
int vec_maybe_resize(vec *v, u32 c) {
	if (c <= v->cap) return 0;
	u32 n = 8; bv *d;
	while (n < c) n *= 2;
	d = realloc(v->data, n*sizeof(bv));
	if (!d) return 1;
	for (c = v->cap; c < n; c++) d[c] = nil;
	v->data = d; v->cap = n;
	return 0;
}
int vec_init(vec *v, u32 c) {
	v->data = NULL; v->cap = v->sz = 0;
	if (vec_maybe_resize(v, c)) return 1;
	return 0;
}
void vec_destroy(vec *v) { if (v) free(v->data); }
int vec_set(vec *v, u32 i, bv val) {
	if (vec_maybe_resize(v, i)) return 1;
	if (i >= v->sz) v->sz = i+1;
	v->data[i] = val;
	return 0;
}
bv vec_at(vec *v, u32 i) { return i < v->sz ? v->data[i] : nil; }

/*
 * Hashmap - Robin Hood Hashmap (open addressing)
 * TODO: resizing + incremental resizing
 */
typedef struct { const u8 *key; u32 len; } hm_key;
typedef struct { bv value; hm_key key; u32 hash; } rhhm_value;
typedef struct { rhhm_value *table; u32 length; } rhhm;

int hm_key_cmp(hm_key *a, hm_key *b) {
	if (a->len != b->len) return 1;
	return memcmp(a->key, b->key, a->len);
}

u32 djb2(hm_key* k) {
	u32 hash = 5381, i;
	for (i = 0; i < k->len; i++) {
		u32 c = k->key[i];
		hash = ((hash << 5) + hash) + c;
	}
	return hash;
}

#define DISTANCE(p, h) (p >= h ? p-h : p + (hm->length - h))
int rhhm_value_empty(rhhm_value *v) { return v->value.u == bv_none; }

int rhhm_init(rhhm *hm, u32 length) {
	hm->length = length;
	hm->table = malloc(hm->length * sizeof(rhhm_value));
	if (!hm->table) return 1;
	do hm->table[--length].value.u = bv_none; while (length);
	return 0;
}

void rhhm_destroy(rhhm *hm) { if (hm) free(hm->table); }

void rhhm_insert(rhhm *hm, hm_key *key, bv value) {
	rhhm_value entry, tmp;
	entry.key = *key;
	entry.value = value;
	u32 i = entry.hash = djb2(key) % hm->length;
	while (!rhhm_value_empty(hm->table+i) &&
		DISTANCE(i, entry.hash) <= DISTANCE(i, hm->table[i].hash)) {
		if (hm->table[i].hash == entry.hash &&
			!hm_key_cmp(&hm->table[i].key, key)) {
			hm->table[i].value = entry.value;
			return;
		}
		if (++i >= hm->length) i = 0; // TODO: just mask i here
	}
	tmp = entry;
	entry = hm->table[i];
	hm->table[i] = tmp;

	while (!rhhm_value_empty(&entry)) {
		u32 i = entry.hash;
		while (!rhhm_value_empty(hm->table+i) &&
			DISTANCE(i, entry.hash) <= DISTANCE(i, hm->table[i].hash)) {
			if (++i >= hm->length) i = 0;
		}
		tmp = entry;
		entry = hm->table[i];
		hm->table[i] = tmp;
	}
}

bv rhhm_get(rhhm *hm, hm_key *key) {
	u32 i, h; i = h = djb2(key) % hm->length;
	while (!rhhm_value_empty(hm->table+i)) {
		if (bv_isnil(hm->table[i].value)) return nil;
		if (DISTANCE(i, hm->table[i].hash) < DISTANCE(i, h)) return nil;
		if (!hm_key_cmp(&hm->table[i].key, key)) return hm->table[i].value;
		if (++i >= hm->length) i = 0;
	}
	return nil;
}

void rhhm_remove(rhhm *hm, hm_key *key) {
	u32 i, h; i = h = djb2(key) % hm->length;
	while (!rhhm_value_empty(hm->table+i)) {
		if (!hm_key_cmp(&hm->table[i].key, key)) {
			u32 j = i;
			do {
				if (++j >= hm->length) j = 0;
				hm->table[i] = hm->table[j];
				i = j;
			} while (!rhhm_value_empty(hm->table+j) &&
				DISTANCE(j, hm->table[j].hash) != 0);
			return;
		}
		if (hm->table[i].hash < h) return;
		if (++i >= hm->length) i = 0;
	}
}

typedef void(*rhhm_visit_callback)(const rhhm_value *value);
void rhhm_visit(rhhm *hm, rhhm_visit_callback cb) {
	u32 i;
	for (i = 0; i < hm->length; i++)
		if (!rhhm_value_empty(hm->table + i))
			cb(hm->table + i);
}

// unsafe, destroy shan't be called
int rhhm_init_fixed(rhhm *hm, rhhm_value *table, u32 length) {
	hm->length = length;
	hm->table = table;
	if (!hm->table) return 1;
	do hm->table[--length].value.u = bv_none; while (length);
	return 0;
}

/*
 * Lexer
 */
typedef struct {
	const char *s;
	int type;
	int length;
} token;

typedef struct {
	const char *s;
	u32 ln;
	u32 col;
} lexer;

enum { LEX_NUM = 1<<8, LEX_BLANKS, LEX_COMMENT, LEX_ID, LEX_STR, LEX_EQ, LEX_NE, LEX_GE, LEX_LE,
	LEX_DO, LEX_END, LEX_WHILE, LEX_REPEAT, LEX_UNTIL, LEX_IF,
	LEX_THEN, LEX_ELSEIF, LEX_ELSE, LEX_FOR, LEX_IN, LEX_FUNCTION,
	LEX_LOCAL, LEX_RETURN, LEX_BREAK, LEX_NIL, LEX_FALSE, LEX_TRUE,
	LEX_AND, LEX_OR, LEX_CAT, LEX_VARARG };

int lex(const char *s, token *t) { /* naive lexing */
	t->s = s;
	switch(*s++) {
	case ' ': case '\t': case '\r': case '\n':
		while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
		t->type = LEX_BLANKS;
		break;
	case '0': case '1': case '2': case '3': case '4': case '5':
	case '6': case '7': case '8': case '9':
		while (isdigit(*s)) s++;
		t->type = LEX_NUM;
		break;
	case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
	case 'g': case 'h': case 'i': case 'j': case 'k': case 'l':
	case 'm': case 'n': case 'o': case 'p': case 'q': case 'r':
	case 's': case 't': case 'u': case 'v': case 'w': case 'x':
	case 'y': case 'z':
	case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
	case 'G': case 'H': case 'I': case 'J': case 'K': case 'L':
	case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
	case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
	case 'Y': case 'Z': case '_':
		while (isalnum(*s) || *s == '_') s++;
		t->type = LEX_ID;
		break;
	case '\'': case '\"':
		break;
	case '-':
		if (*s != '-') {
			t->type = *t->s;
		} else {

		}
		break;
	case '.':
		break;
	case '~':
		break;
	case '<':
		if (*s == '=') {
			t->type = LEX_LE;
			s++;
		} else t->type = '<';
		break;
	case '>':
		if (*s == '=') {
			t->type = LEX_GE;
			s++;
		} else t->type = '>';
		break;
	case '=':
		if (*s == '=') {
			t->type = LEX_EQ;
			s++;
		} else t->type = '=';
		break;
	case '^': case '%': case '+': case '*': case '/': case ',': case ';':
	case '(': case ')': case '[': case ']':
		t->type = *t->s;
		break;
	default:
		return 1;
	}
	t->length = s - t->s;
	return 0;
}


/*
 * Three-Address Code
 */
typedef struct {
	i16 op;
	i16 a;
	i16 b;
	i16 target;
} tac;

#define IR_SYM_MAX 256
#define IR_CTT_MAX 256
#define IR_OP_MAX 1024
typedef struct {
	rhhm_value tbl[IR_SYM_MAX];
	rhhm sym;

	bv ctts[IR_CTT_MAX];
	tac ops[IR_OP_MAX];

	int ic;
	int io;
	int iv;
} ir;

void ir_init(ir *c) {
	rhhm_init_fixed(&c->sym, c->tbl, IR_SYM_MAX);
	c->ic = c->io = c->iv = 0;
}

int ir_ctt(ir *c, bv v) {
	if (c->ic >= IR_CTT_MAX) abort();
	c->ctts[c->ic++] = v;
	return -c->ic;
}

int ir_op(ir *c, i16 op, i16 a, i16 b, i16 t) {
	if (c->io >= IR_OP_MAX) abort();
	int i = c->io++;
	c->ops[i].op = op;
	c->ops[i].a = a;
	c->ops[i].b = b;
	c->ops[i].target = t < 0 ? c->iv : t;
	return c->iv++;
}

void ir_disp(ir *c) {
	printf("op\ta\tb\tt\n");
	int i = c->ic;
	while (--i >= 0) printf("C\t%.1lf\t-\t%d\n", c->ctts[i].d, -i-1);
	for (i = 0; i < c->io; i++)
		printf("%c\t%d\t%d\t%d\n", c->ops[i].op, c->ops[i].a, c->ops[i].b, c->ops[i].target);
}


/*
 * Recursive descent parser
 */
typedef struct {
	const char *s;
	token current;
	ir c;
} parser;

int parser_next(parser *p);

int parser_init(parser *p, const char *s) {
	ir_init(&p->c);
	p->s = s;
	return parser_next(p);
}

int parser_next(parser *p) {
	do {
		if (lex(p->s, &p->current)) return 1;
		p->s += p->current.length;
	} while (p->current.type == LEX_BLANKS);
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
		puts("unexpected token");
		abort(); // that was harsh
	}
}

#define TK (p->current)
#define TP (p->current.type)
#define NEXT() parser_next(p)
#define EXPECT(t) parser_expect(p, t)
#define ACCEPT(t) parser_accept(p, t)

int parse_expr(parser *p);

int parse_primary(parser *p) {
	int r = 0;
	switch (TP) {
	case LEX_NUM:
		if (p->current.length >= 32)
			r = ir_ctt(&p->c, nil);
		else {
			char tmp[32];
			memcpy(tmp, p->current.s, p->current.length);
			tmp[p->current.length] = '\0';
			r = ir_ctt(&p->c, bv_from_double(atof(tmp)));
		}
		NEXT();
		break;
	case '(':
		NEXT();
		r = parse_expr(p);
		EXPECT(')');
		break;
	default:
		puts(">>>> aborting: invalid primary!");
		abort();
	}
	return r;
}

int parse_factor(parser *p) {
	int r = parse_primary(p);
	while (TP == '*' || TP == '/') {
		token t = p->current;
		NEXT();
		r = ir_op(&p->c, t.type, r, parse_primary(p), -1);
	}
	return r;
}

int parse_term(parser *p) {
	int r = parse_factor(p);
	while (TP == '+' || TP == '-') {
		token t = p->current;
		NEXT();
		r = ir_op(&p->c, t.type, r, parse_factor(p), -1);
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
		r = ir_op(&p->c, t.type, r, parse_term(p), -1);
	}
	return r;
}

int parse_expr(parser *p) {
	return parse_cmp(p);
}

int parse_statement(parser *p) {
	return parse_expr(p);
}

#undef TK
#undef TP
#undef NEXT
#undef EXPECT
#undef ACCEPT

/*
 * Primitive Assembler
 */
#include <sys/mman.h>

#define CC_BLOCK_SZ 4096
typedef void (*fn)(void);

void print_bin(u8 *p, int sz) {
	while (sz--) {
		printf("%X%X ", *p / 16, *p % 16);
		p++;
	}
	puts("");
}

typedef struct {
	u8 *s;
	u8 *p;
} cc;

int cc_init(cc *c) {
	c->s = c->p = mmap(0, CC_BLOCK_SZ,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	if (c->s == MAP_FAILED) return 1;
	c->p[0] = 0x55; // push rbp
	c->p[1] = 0x48; c->p[2] = 0x89; c->p[3] = 0xe5; // mov rbp, rsp
	c->p+=4;
	return 0;
}

void cc_destroy(cc *c) {
	munmap(c->s, CC_BLOCK_SZ);
}

fn cc_done(cc *c) {
	puts("gen:");
	print_bin(c->s, c->p - c->s);
	return mprotect(c->s, CC_BLOCK_SZ, PROT_READ | PROT_EXEC) == -1 ? NULL : (fn)c->s;
}

void cc_arg_a(cc *c, bv v) {
	memcpy(c->p, "\x48\xbf", 2); // movabs rdi, <>
	memcpy(c->p+2, &v, sizeof(bv));
	c->p+=(2+sizeof(bv));
}

void cc_arg_b(cc *c, bv v) {
	memcpy(c->p, "\x48\xbe", 2); // movabs rsi, <>
	memcpy(c->p+2, &v, sizeof(bv));
	c->p+=(2+sizeof(bv));
}

void cc_arg_sa(cc *c, i32 n) {
	memcpy(c->p, "\x48\x8b\xbd", 3); // mov rdi, [rbp-0x...]
	n *= -sizeof(bv);
	memcpy(c->p+3, &n, sizeof(i32));
	c->p+=7;
}

void cc_arg_sb(cc *c, i32 n) {
	memcpy(c->p, "\x48\x8b\xb5", 3); // mov rsi, [rbp-0x...]
	n *= -sizeof(bv);
	memcpy(c->p+3, &n, sizeof(i32));
	c->p+=7;
}

void cc_store_result(cc *c, i32 n) {
	memcpy(c->p, "\x48\x89\x85", 3); // mov [rbp-0x...], rax
	n *= -sizeof(bv);
	memcpy(c->p+3, &n, sizeof(i32));
	c->p+=7;
}

void cc_call(cc *c, void *f) {
	*c->p = 0xe8; // call
	i32 offset =  (u8*)f - (u8*)(c->p+5);
	memcpy(c->p+1, &offset, sizeof(i32));
	c->p+=5;
}

void cc_subrsp(cc *c, i32 s) {
	memcpy(c->p, "\x48\x81\xec", 3); // sub rsp, ...
	memcpy(c->p+3, &s, sizeof(i32));
	c->p+=7;
}

void cc_leave(cc *c) { *c->p++ = 0xc9; }
void cc_poprbp(cc *c) { *c->p++ = 0x5d; }
void cc_ret(cc *c) { *c->p++ = 0xc3; }

/* gc */


/* environment */
typedef struct {
	rhhm h;
	vec  v;
	GCd;
} table;

typedef struct {
	table G;
} L;

int cmp_dec(const void *lhs, const void *rhs) {
	  return *(int*)rhs - *(int*)lhs;
}

void dostring(const char *s) {
	parser p;
	parser_init(&p, s);
	parse_statement(&p);
	ir_disp(&p.c);

	// optimize

	// allocate registers
	int usage[IR_OP_MAX] = {0};
	for (int i = 0; i < p.c.io; i++) {
		if (p.c.ops[i].target >= 0) usage[p.c.ops[i].target]++;
		if (p.c.ops[i].a >= 0) usage[p.c.ops[i].a]++;
		if (p.c.ops[i].b >= 0) usage[p.c.ops[i].b]++;
	}
	for (int i = 0; i < p.c.io; i++) {
		usage[i] <<= 16;
		usage[i] |= i;
	}
	qsort(usage, p.c.io, sizeof(int), cmp_dec);

	puts("var usage:");
	for (int i = 0; i < p.c.io; i++) {
		printf("%d: %d\n", usage[i] & 0xffff, usage[i]>>16);
	}


	// assemble
	int nvars = p.c.io;
	if (nvars) nvars = ((nvars >> 2) << 2) + 4; // stack alignment

	cc c;
	cc_init(&c);
	if (nvars) cc_subrsp(&c, sizeof(bv)*nvars);

	for (int i = 0; i < p.c.io; i++) {
		tac *t = p.c.ops+i;
		switch (t->op) {
		case '+': case '*': case '-': case '/':
			if (t->a < 0) cc_arg_a(&c, p.c.ctts[-t->a-1]);
			else cc_arg_sa(&c, t->a);

			if (t->b < 0) cc_arg_b(&c, p.c.ctts[-t->b-1]);
			else cc_arg_sb(&c, t->b);

			switch (t->op) {
				case '+': cc_call(&c, (void*)bv_add); break;
				case '*': cc_call(&c, (void*)bv_mul); break;
				case '-': cc_call(&c, (void*)bv_sub); break;
				case '/': cc_call(&c, (void*)bv_div); break;
			}

			cc_store_result(&c, t->target);

			break;
		}
	}
	if (nvars) cc_leave(&c);
	else cc_poprbp(&c);
	cc_ret(&c);
	fn f = cc_done(&c);
	f();
}

int main(int argc, char *argv[]) {
	dostring("(10 + 2 + 8 * 10) / 20");
	return 0;
}

