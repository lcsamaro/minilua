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

#define ML_MALLOC malloc
#define ML_REALLOC realloc
#define ML_FREE free

/*
 * Boxed Value
 */
typedef union { u64 u; double d; i64 i; void *p; } bv;

/* Lua state fwd decls - ini */
typedef struct state state;
bv lua_intern(state *L, const char *s, int len);
void lua_error(state *L);
/* Lua state fwd decls - end */

#define bv_type_mask  UINT64_C(0xffff000000000000)
#define bv_value_mask UINT64_C(0x0000ffffffffffff)


#define bv_nil        UINT64_C(0xfff8000000000000)
#define bv_bool       UINT64_C(0x7ff9000000000000)
//                        ...
#define bv_none       UINT64_C(0xffff000000000000)


#define bv_ptr        UINT64_C(0x7ff8000000000000)
#define bv_str        UINT64_C(0x7ff9000000000000)
#define bv_tbl        UINT64_C(0x7ffa000000000000)
#define bv_sstr       UINT64_C(0x7ffb000000000000)
//                        ...
#define bv_symbol     UINT64_C(0x7fff000000000000)


const bv nil  = { bv_nil };

#define SSTR_MAX_LENGTH 5

bv bv_make_double(double d) { bv v; v.d = d; return v; }
bv bv_make_bool(int b) { bv v; v.u = bv_bool | (b ? 1 : 0); return v; }
bv bv_make_str(void *p) { bv v; v.p = p; v.u |= bv_str; return v; }
bv bv_make_tbl(void *p) { bv v; v.p = p; v.u |= bv_tbl; return v; }
bv bv_make_ptr(void *p) { bv v; v.p = p; v.u |= bv_ptr; return v; }
bv bv_make_sstr(const char *s, u32 len) {
	bv v;
	if (len > SSTR_MAX_LENGTH) len = SSTR_MAX_LENGTH;
	v.u = bv_sstr | len;
	memcpy(((char*)&v)+2, s, len);
	return v;
}

void *bv_get_ptr(bv v) { return (void*)(v.u & bv_value_mask); }
void *bv_get_ptr_clean(bv v) { return (void*)(v.u & (bv_value_mask ^ 3)); }
u64 bv_get_u64(bv v) { return v.u & bv_value_mask; }

u32 bv_type(bv v) { return v.u >> 48; }

int bv_is_nil(bv v) { return v.u == bv_nil; }
int bv_is_str(bv v) { return (v.u&bv_type_mask) == bv_str; }
int bv_is_sstr(bv v) { return (v.u&bv_type_mask) == bv_sstr; }

i64 bv_fast_cmp(bv a, bv b) { return a.i - b.i; }

bv bv_add(state *L, bv a, bv b) {
	bv r = nil;
	r.d = a.d + b.d;

	if (isnan(r.d) && (isnan(a.d) || isnan(b.d))) lua_error(L);

	//if (!isnan(a.d) && !isnan(b.d)) r.d = a.d + b.d;
	//else lua_error(L);
	//printf("add %lf %lf %lf\n", a.d, b.d, r.d);
	return r;
}

bv bv_mul(state *L, bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = a.d * b.d;
	else lua_error(L);
	printf("mul %lf\n", r.d);
	return r;
}

bv bv_sub(state *L, bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = a.d - b.d;
	else lua_error(L);
	printf("sub %lf\n", r.d);
	return r;
}

bv bv_div(state *L, bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = a.d / b.d;
	else lua_error(L);
	printf("div %lf\n", r.d);
	return r;
}

bv bv_pow(state *L, bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = pow(a.d, b.d);
	else lua_error(L);
	printf("pow %lf\n", r.d);
	return r;
}

bv bv_mod(state *L, bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = a.d - floor(a.d / b.d) * b.d;
	else lua_error(L);
	printf("mod %lf\n", r.d);
	return r;
}

bv bv_EQ(bv a, bv b) {
	printf("EQ: %f %f\n", a.d, b.d);
	return bv_make_bool(a.u == b.u ? 1 : 0);
}

bv bv_NE(bv a, bv b) {
	//printf("NE: %f %f\n", a.d, b.d);
	return bv_make_bool(a.u == b.u ? 0 : 1);
}

bv bv_LE(bv a, bv b) {

}

bv bv_LT(bv a, bv b) {

}

void bv_disp(bv v) {
	printf("> %f\n", v.d);
}

//bv bv_cmp(bv a, bv b) {}

/*
 * String - simple string
 */
typedef struct { // 16 B
	u32 sz;
	char data[4];
} str;

str *str_new(const char *s, u32 len) {
	str *o = ML_MALLOC(sizeof(str)+len);
	if (o) {
		o->sz = len;
		memcpy(o->data, s, len);
	}
	return o;
}

/*
 * Vector - simple vector
 */
typedef struct { bv *data; u32 cap; u32 sz; } vec;
int vec_maybe_resize(vec *v, u32 c) {
	if (c <= v->cap) return 0;
	u32 n = 8; bv *d;
	while (n < c) n *= 2;
	d = ML_REALLOC(v->data, n*sizeof(bv));
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
void vec_destroy(vec *v) { if (v) ML_FREE(v->data); }
int vec_set(vec *v, u32 i, bv val) {
	if (vec_maybe_resize(v, i)) return 1;
	if (i >= v->sz) v->sz = i+1;
	v->data[i] = val;
	return 0;
}
bv vec_at(vec *v, u32 i) { return i < v->sz ? v->data[i] : nil; }

/*
 * Hashmap - Robin Hood Hashmap (open addressing)
 * TODO: resizing (incremental?)
 */
#define RHHM_RESIZE_FILL_RATE_THRESHOLD 0.8
typedef struct { bv value; bv key; u32 hash; } rhhm_value;
typedef struct { // 16 B
	rhhm_value *table;
	u32 length;
	u32 padding; // for gc
} rhhm;

typedef u32 (*rhhm_hash_fn)(bv);
typedef int (*rhhm_cmp_fn)(bv, bv);

#define DISTANCE(p, h) (p >= h ? p-h : p + (hm->length - h))
int rhhm_value_empty(rhhm_value *v) { return v->value.u == bv_none; }

int rhhm_init(rhhm *hm, u32 length) {
	hm->length = length;
	hm->table = ML_MALLOC(hm->length * sizeof(rhhm_value));
	if (!hm->table) return 1;
	do hm->table[--length].value.u = bv_none; while (length);
	return 0;
}

// unsafe, destroy shan't be called
int rhhm_init_fixed(rhhm *hm, rhhm_value *table, u32 length) {
	hm->length = length;
	hm->table = table;
	if (!hm->table) return 1;
	do hm->table[--length].value.u = bv_none; while (length);
	return 0;
}

void rhhm_destroy(rhhm *hm) {
	if (hm) ML_FREE(hm->table);
}

void rhhm_set(rhhm *hm, rhhm_hash_fn hfn, rhhm_cmp_fn cfn, bv key, bv value) {
	rhhm_value entry, tmp;
	entry.key = key;
	entry.value = value;
	u32 i = entry.hash = hfn(key) % hm->length;
	while (!rhhm_value_empty(hm->table+i) &&
		DISTANCE(i, entry.hash) <= DISTANCE(i, hm->table[i].hash)) {
		if (hm->table[i].hash == entry.hash &&
			!cfn(hm->table[i].key, key)) {
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

bv rhhm_get(rhhm *hm, rhhm_hash_fn hfn, rhhm_cmp_fn cfn, bv key) {
	u32 i, h; i = h = hfn(key) % hm->length;
	while (!rhhm_value_empty(hm->table+i)) { // TODO: correctly return none
		if (bv_is_nil(hm->table[i].value)) return nil;
		if (DISTANCE(i, hm->table[i].hash) < DISTANCE(i, h)) return nil;
		if (!cfn(hm->table[i].key, key)) return hm->table[i].value;
		if (++i >= hm->length) i = 0;
	}
	return nil;
}

void rhhm_remove(rhhm *hm, rhhm_hash_fn hfn, rhhm_cmp_fn cfn, bv key) {
	u32 i, h; i = h = hfn(key) % hm->length;
	while (!rhhm_value_empty(hm->table+i)) {
		if (!cfn(hm->table[i].key, key)) {
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

// bv -> bv
u32 djb2(u8 *key, u32 len) {
	u32 hash = 5381, i;
	for (i = 0; i < len; i++) {
		u32 c = key[i];
		hash = ((hash << 5) + hash) + c;
	}
	return hash;
}

u32 rhhm_bb_hash(bv v) {
	return djb2((u8*)&v, sizeof(bv));
}

int rhhm_bb_cmp(bv a, bv b) {
	return a.u == b.u ? 0 : 1;
}

#define hm_set(h,k,v)  rhhm_set   (h, rhhm_bb_hash, rhhm_bb_cmp, k, v)
#define hm_get(h,k)    rhhm_get   (h, rhhm_bb_hash, rhhm_bb_cmp, k)
#define hm_remove(h,k) rhhm_remove(h, rhhm_bb_hash, rhhm_bb_cmp, k)

// string -> int
int hm_key_cmp(bv a, bv b) {
	int alen = a.u >> 48;
	int blen = b.u >> 48;
	if (alen != blen) return 1;
	return memcmp((void*)(a.u&bv_value_mask), (void*)(b.u&bv_value_mask), alen);
}

u32 hm_key_hash(bv a) {
	int alen = a.u >> 48;
	return djb2((void*)(a.u&bv_value_mask), alen < 32 ? alen : 32);
}


void rhhm_insert_str(rhhm *hm, const char *s, int len, int val) {
	bv k;
	k.u = ((u64)len << 48) | (u64)s;
	bv v;
	v.i = val;
	rhhm_set(hm, hm_key_hash, hm_key_cmp, k, v);
}

int rhhm_get_str(rhhm *hm, const char *s, int len) {
	bv k;
	k.u = ((u64)len << 48) | (u64)s;
	bv v = rhhm_get(hm, hm_key_hash, hm_key_cmp, k);
	if (bv_is_nil(v)) return -1;
	return v.i;
}

void rhhm_insert_cstr(rhhm *hm, const char *s, int val) {
	rhhm_insert_str(hm, s, strlen(s), val);
}

// interning hash impl
int hm_sb_cmp(bv a, bv b) {
	if (!(a.u&1) && !(b.u&1)) return a.u == b.u ? 0 : 1;
	int alen = a.u & 1 ?  a.u >> 48 : ((str*)bv_get_ptr(a))->sz;
	int blen = b.u & 1 ? b.u >> 48 : ((str*)bv_get_ptr(b))->sz;
	if (alen != blen) return 1;
	void *sa = a.u & 1 ? (void*)(a.u&bv_value_mask & ~1ULL) : ((str*)bv_get_ptr(a))->data;
	void *sb = b.u & 1 ? (void*)(b.u&bv_value_mask & ~1ULL) : ((str*)bv_get_ptr(b))->data;
	return memcmp(sa, sb, alen);
}

u32 hm_sb_hash(bv a) {
	int alen = a.u & 1 ?  a.u >> 48 : ((str*)bv_get_ptr(a))->sz;
	void *sa = a.u & 1 ? (void*)(a.u&bv_value_mask & ~1ULL) : ((str*)bv_get_ptr(a))->data;
	return djb2(sa, alen < 32 ? alen : 32);
}

#define hm_sb_set(h,k,v)  rhhm_set   (h, hm_sb_hash, hm_sb_cmp, k, v)
#define hm_sb_get(h,k)    rhhm_get   (h, hm_sb_hash, hm_sb_cmp, k)
#define hm_sb_remove(h,k) rhhm_remove(h, hm_sb_hash, hm_sb_cmp, k)

bv hm_sb_get_str(rhhm *hm, const char *s, int len) {
	bv k;
	k.u = ((u64)len << 48) | (u64)s | 1;
	return hm_sb_get(hm, k);
}

/*
 * Lexer
 */
typedef struct {
	const char *s;
	int type;
	int length;
} token;

enum { LEX_NUM = 1<<8, LEX_BLANKS, LEX_COMMENT, LEX_ID, LEX_STR, LEX_EQ, LEX_NE, LEX_GE, LEX_LE, LEX_NOT,
	LEX_DO, LEX_END, LEX_WHILE, LEX_REPEAT, LEX_UNTIL, LEX_IF,
	LEX_THEN, LEX_ELSEIF, LEX_ELSE, LEX_FOR, LEX_IN, LEX_FUNCTION,
	LEX_LOCAL, LEX_RETURN, LEX_BREAK, LEX_NIL, LEX_FALSE, LEX_TRUE,
	LEX_AND, LEX_OR, LEX_CAT, LEX_VARARG,
	LEX_DISP /*dbg*/ };

rhhm_value reserved_tbl[64];
rhhm reserved;

void lex_init() {
	rhhm_init_fixed(&reserved, reserved_tbl, 64);
	rhhm_insert_cstr(&reserved, "not", LEX_NOT);
	rhhm_insert_cstr(&reserved, "do", LEX_DO);
	rhhm_insert_cstr(&reserved, "end", LEX_END);
	rhhm_insert_cstr(&reserved, "while", LEX_WHILE);
	rhhm_insert_cstr(&reserved, "repeat", LEX_REPEAT);
	rhhm_insert_cstr(&reserved, "until", LEX_UNTIL);
	rhhm_insert_cstr(&reserved, "if", LEX_IF);
	rhhm_insert_cstr(&reserved, "then", LEX_THEN);
	rhhm_insert_cstr(&reserved, "elseif", LEX_ELSEIF);
	rhhm_insert_cstr(&reserved, "else", LEX_ELSE);
	rhhm_insert_cstr(&reserved, "for", LEX_FOR);
	rhhm_insert_cstr(&reserved, "in", LEX_IN);
	rhhm_insert_cstr(&reserved, "function", LEX_FUNCTION);
	rhhm_insert_cstr(&reserved, "local", LEX_LOCAL);
	rhhm_insert_cstr(&reserved, "return", LEX_RETURN);
	rhhm_insert_cstr(&reserved, "break", LEX_BREAK);
	rhhm_insert_cstr(&reserved, "nil", LEX_NIL);
	rhhm_insert_cstr(&reserved, "false", LEX_FALSE);
	rhhm_insert_cstr(&reserved, "true", LEX_TRUE);
	rhhm_insert_cstr(&reserved, "and", LEX_AND);
	rhhm_insert_cstr(&reserved, "or", LEX_OR);

	rhhm_insert_cstr(&reserved, "disp", LEX_DISP); // dbg
}

int lex(const char *s, token *t) { /* naive lexing: switch on first character */
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
		t->type = rhhm_get_str(&reserved, t->s, s-t->s);
		if (t->type == -1) t->type = LEX_ID;
		break;
	case '\'': case '\"':
		break;
	case '-':
		if (*s != '-') t->type = *t->s;
		else {
			while (*s && *s != '\r' && *s != '\n') s++;
			t->type = LEX_COMMENT;
		}
		break;
	case '.':
		break;
	case '~':
		if (*s != '=') return 1;
		t->type = LEX_NE;
		s++;
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
	case '(': case ')': case '[': case ']': case '{': case '}': case '#':
		t->type = *t->s;
		break;
	default: return 1;
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

enum {
	IR_OP_LLOAD = 1<<10, // local load

	IR_OP_TLOAD, // table load
	IR_OP_TSTORE, // table store

	IR_OP_GLOAD, // global table load
	IR_OP_GSTORE, // global table store

	IR_OP_NEWTBL, // new table

	IR_OP_JMP,
	IR_OP_JZ,
	IR_OP_JNZ,

	IR_OP_DISP, // dbg

	IR_OP_NOOP
};

#define IR_DEPTH_MAX 64
#define IR_SYM_MAX (1<<12)
#define IR_CTT_MAX (1<<12)
#define IR_OP_MAX  (1<<16)
typedef struct {
	rhhm_value tbl[IR_SYM_MAX];
	rhhm sym;

	bv ctts[IR_CTT_MAX];
	tac ops[IR_OP_MAX];

	int ic;
	int io;
	int iv;
} ir;

int ir_current(ir *c) { return c->io; }

int ir_newvar(ir *c) { return c->iv++; }

int ir_newsym(ir *c, const char *id, int len) {
	int r;
	if ((r = rhhm_get_str(&c->sym, id, len)) == -1) {
		r = ir_newvar(c);
		rhhm_insert_str(&c->sym, id, len, r);
	}
	return r;
}

int ir_sym(ir *c, const char *id, int len) {
	return rhhm_get_str(&c->sym, id, len);
}

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
	t = t < 0 ? c->iv++ : t;
	c->ops[i].target = t;
	return t;
}

void ir_disp(ir *c) {
	printf("#\top\ta\tb\tt\n");
	int i = c->ic;
	while (--i >= 0) {
		if (bv_is_sstr(c->ctts[i]) || bv_is_str(c->ctts[i])) {
			printf("\tC\t%s\t-\t%d\n", "str", -i-1);
		} else {
			printf("\tC\t%.1lf\t-\t%d\n", c->ctts[i].d, -i-1);
		}
	}
	for (i = 0; i < c->io; i++) {
		printf("%d\t", i);
		if (c->ops[i].op < 256) {
			printf("%c", c->ops[i].op);
		} else {
			switch (c->ops[i].op) {
			case LEX_EQ: printf("eq"); break;
			case LEX_NE: printf("ne"); break;
			case IR_OP_NEWTBL: printf("tnew"); break;
			case IR_OP_LLOAD: printf("lload"); break;
			case IR_OP_TLOAD: printf("tload"); break;
			case IR_OP_TSTORE: printf("tstor"); break;
			case IR_OP_GLOAD: printf("gload"); break;
			case IR_OP_GSTORE: printf("gstor"); break;
			case IR_OP_JZ: printf("jz"); break;
			case IR_OP_JNZ: printf("jnz"); break;
			case IR_OP_JMP: printf("jmp"); break;
			}
		}
		printf("\t%d\t%d\t%d\n", c->ops[i].a, c->ops[i].b, c->ops[i].target);
	}
}

/*
 * Recursive descent parser
 */
typedef struct {
	const char *b;
	const char *s;
	token current;
	ir c;
	state *L;
} parser;

#define TK (p->current)
#define TP (p->current.type)

int parser_next(parser *p);

int parser_init(parser *p, const char *s, state *L) {
	ir_init(&p->c);
	p->b = p->s = s;
	p->L = L;
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
	int r = ir_op(&p->c, IR_OP_NEWTBL, 0, 0, -1);
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
		r = ir_sym(&p->c, TK.s, TK.length);
		if (r == -1) { // global load
			int field = ir_ctt(&p->c, lua_intern(p->L, TK.s, TK.length));
			r = ir_op(&p->c, IR_OP_GLOAD, field, 0, -1);
		}
		NEXT();
		break;
	case LEX_NIL: r = ir_ctt(&p->c, nil); NEXT(); break;
	case LEX_FALSE: r = ir_ctt(&p->c, bv_make_bool(0)); NEXT(); break;
	case LEX_TRUE: r = ir_ctt(&p->c, bv_make_bool(1)); NEXT(); break;
	case LEX_NUM:
		if (p->current.length >= 32) r = ir_ctt(&p->c, nil);
		else {
			char tmp[32];
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
		abort();
	}
	return r;
}

int parse_factor(parser *p) {
	int r = parse_primary(p);
	while (TP == '*' || TP == '/' || TP == '^' || TP == '%') {
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

int parse_expr(parser *p) { return parse_cmp(p); }

int parse_chunk(parser *p);

int parse_statement(parser *p) {
	int r, a, b, c, local = 0, field;
	switch (TP) {
	case LEX_DISP:
		NEXT();
		r = parse_expr(p);
		ir_op(&p->c, IR_OP_DISP, r, 0, 0);
		break;
	case LEX_LOCAL:
		NEXT();
		local = 1;
	case LEX_ID:
		if (local) r = ir_newsym(&p->c, TK.s, TK.length);
		else {
			r = ir_sym(&p->c, TK.s, TK.length); // TODO lexical scope
			if (r == -1) { // global
				field = ir_ctt(&p->c, lua_intern(p->L, TK.s, TK.length));
				r = ir_newvar(&p->c);
			} else {
				local = 1;
			}
		}
		NEXT();
		EXPECT('=');
		a = parse_expr(p);
		r = ir_op(&p->c, IR_OP_LLOAD, a, 0, r);
		if (!local) {
			r = ir_op(&p->c, IR_OP_GSTORE, field, r, 0); // target ignored
		}
		break;
	case LEX_DO:
		NEXT();
		r = parse_chunk(p);
		EXPECT(LEX_END);
		break;
	case LEX_WHILE:
		NEXT();

		a = ir_current(&p->c);

		r = parse_expr(p);
	
		c = ir_current(&p->c); // so we can fix jz target later
		ir_op(&p->c, IR_OP_JZ, r, 0, 0);

		EXPECT(LEX_DO);
		r = parse_chunk(p);
		EXPECT(LEX_END);

		ir_op(&p->c, IR_OP_JMP, r, 0, a);

		b = ir_current(&p->c);
		p->c.ops[c].target = b;

		break;
	case LEX_REPEAT:
		NEXT();
		r = parse_chunk(p);
		EXPECT(LEX_UNTIL);
		r = parse_expr(p);
		break;
	case LEX_IF:
		NEXT();
		r = parse_expr(p);
		c = ir_current(&p->c); // so we can fix jz target later
		ir_op(&p->c, IR_OP_JZ, r, 0, 0);

		EXPECT(LEX_THEN);
		r = parse_chunk(p);
		EXPECT(LEX_END);

		b = ir_current(&p->c);
		p->c.ops[c].target = b;
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
	}
	return r;
}

/*
 * Primitive Assembler
 */
#include <sys/mman.h>

#define CC_BLOCK_SZ (1<<14)
enum cc_reg {
	rax = 0, rcx, rdx, rbx, rsp, rbp, rsi, rdi,
	r8, r9, r10, r11, r12, r13, r14, r15 /* unused */
};

void print_bin(u8 *p, int sz) {
	while (sz--) {
		printf("%X%X ", *p / 16, *p % 16);
		p++;
	}
	puts("");
}

//#define CC_MAX_ADDRS 4096
typedef struct {
	u8 *s;
	u8 *p;

	void *op_addr[IR_OP_MAX];

	void *fill[IR_OP_MAX];
	int ifill;

	//void *addr[CC_MAX_ADDRS];
	//void **addrp;
} cc;

int cc_init(cc *c) {
	c->s = c->p = mmap(0, CC_BLOCK_SZ,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	if (c->s == MAP_FAILED) return 1;
	memcpy(c->p, "\x55\x48\x89\xe5", 4); // push rbp / mov rbp, rsp
	c->p+=4;
	//c->addrp = c->addr;
	c->ifill = 0;
	return 0;
}

void cc_destroy(cc *c) {
	munmap(c->s, CC_BLOCK_SZ);
}

u8 *cc_cur(cc *c) { return c->p; }

void cc_mark(cc *c, i32 target) {
	u8 *addr = cc_cur(c);
	//*(i32*)addr = target;
	memcpy(addr-4, &target, 4);
	c->fill[c->ifill++] = addr;
}

void cc_fill_marks(cc *c) {
	for (int i = 0; i < c->ifill; i++) {
		u8 *addr = c->fill[i];
		i32 target = *(i32*)(addr-4);
		i32 off = (u8*)c->op_addr[target] - addr;

		printf("subbing %p and %p\n", c->op_addr[target], addr);
		printf("fill target: %d with %d\n", target, off);

		memcpy(addr-4, &off, 4);
	}
}

void* cc_done(cc *c) {
	printf("gen: %ld bytes\n", c->p - c->s);
	print_bin(c->s, c->p - c->s);
	return mprotect(c->s, CC_BLOCK_SZ, PROT_READ | PROT_EXEC) == -1 ? NULL : c->s;
}

void cc_mov_rl(cc *c, i32 reg, bv v) { // mov $reg, $v
	c->p[0] = 0x48;
	c->p[1] = (0xb8 | reg);
	memcpy(c->p+2, &v, sizeof(bv));
	c->p+=(2+sizeof(bv));
}

void cc_mov_rs(cc *c, i32 reg, i32 n) { // mov $reg, [rbp-$n]
	n++;
	memcpy(c->p, "\x48\x8b", 2);
	c->p[2] = (0x85 | (reg << 3));
	n *= -sizeof(bv);
	memcpy(c->p+3, &n, sizeof(i32));
	c->p+=7;
}

void cc_mov_sr(cc *c, i32 n, i32 reg) { // mov [rbp-$n], $reg
	n++;
	memcpy(c->p, "\x48\x89", 2);
	c->p[2] = (0x85 | (reg << 3));
	n *= -sizeof(bv);
	memcpy(c->p+3, &n, sizeof(i32));
	c->p+=7;
}

#define cc_arg_a(c,v) cc_mov_rl(c, rdi, v)
#define cc_arg_b(c,v) cc_mov_rl(c, rsi, v)

#define cc_arg_sa(c,n) cc_mov_rs(c, rdi, n)
#define cc_arg_sb(c,n) cc_mov_rs(c, rsi, n)

#define cc_store_result(c, n) cc_mov_sr(c, n, rax)

void cc_push(cc *c, i32 reg) {
	if (reg >= r8) *c->p++ = 0x41; // r8-r15 support
	*c->p++ = 0x50 + (reg & 0x7);
}

void cc_pop(cc *c, i32 reg) {
	if (reg >= r8) *c->p++ = 0x41; // r8-r15 support
	*c->p++ = 0x58 + (reg & 0x7);
}

void cc_call(cc *c, void *f) {
	*c->p = 0xe8; // call
	i32 offset =  (u8*)f - (u8*)(c->p+5);
	memcpy(c->p+1, &offset, sizeof(i32));
	c->p+=5;
}

void cc_jmp(cc *c, void *f) {
	*c->p = 0xe9;
	i32 offset =  (u8*)f - (u8*)(c->p+5);
	memcpy(c->p+1, &offset, sizeof(i32));
	c->p+=5;
}

void cc_jz(cc *c, void *f) {
	*c->p = 0x0f;
	c->p[1] = 0x84;
	i32 offset =  (u8*)f - (u8*)(c->p+6);
	memcpy(c->p+2, &offset, sizeof(i32));
	c->p+=6;
}

void cc_jnz(cc *c, void *f) {
	*c->p = 0x0f;
	c->p[1] = 0x85;
	i32 offset =  (u8*)f - (u8*)(c->p+6);
	memcpy(c->p+2, &offset, sizeof(i32));
	c->p+=6;
}

void cc_test_lsb(cc *c) { // test rax, 1
	memcpy(c->p, "\x48\xa9\x01\x00\x00\x00", 6);
	c->p+=6;
}

void cc_subrsp(cc *c, i32 s) {
	memcpy(c->p, "\x48\x81\xec", 3); // sub rsp, $s
	memcpy(c->p+3, &s, sizeof(i32));
	c->p+=7;
}

void cc_mcode(cc *c, u8 *mcode, u32 sz) {
	memcpy(c->p, mcode, sz);
	c->p+=sz;
}

u8 *cc_skip(cc *c, u32 sz) {
	u8 *r = c->p;
	c->p+=sz;
	return r;
}

void cc_leave(cc *c) { *c->p++ = 0xc9; }
void cc_ret(cc *c) { *c->p++ = 0xc3; }

/* table */
typedef struct {
	rhhm h;
	//vec  v;
} table;

int table_init(table *t, u32 cap) {
	return rhhm_init(&t->h, cap);
}

void table_destroy(table *t) {
	rhhm_destroy(&t->h);
}

int table_set(table *t, bv k, bv v) {
	hm_set(&t->h, k, v);
	return 0;
}

bv table_get(table *t, bv k) {
	return hm_get(&t->h, k);
}

/* gc object pool */
typedef struct {
	void *exists; // forwarded to fwd if NULL
	void *fwd;
} gcobj;

typedef struct {
	void **data;
	u32 sz;
	u32 cap;
} gc;

int gc_init(gc *p, u32 cap) {
	p->sz = 0;
	p->cap = cap;
	p->data = ML_MALLOC(cap*sizeof(void*));
	return p->data ? 0 : 1;
}

void gc_destroy(gc *p) {
	if (p) ML_FREE(p->data);
}

int gc_insert(gc *p, void *o) {
	if (p->sz+1 >= p->cap) {
		void *d = ML_MALLOC(p->cap*sizeof(void*));
		if (!d) return 1;
		p->data = d;
	}
	p->data[p->sz++] = o;
	return 0;
}

/* state */
#define INITIAL_OBJECT_POOL_SZ 1024
#define INTERN_POOL_INITIAL_SZ 256
#define G_INITIAL_SZ 256
typedef struct state {
	jmp_buf jmpbuf;

	table G;

	rhhm intern_pool;
	gc g;
} state;

void lua_destroy(state *L) {
	table_destroy(&L->G);
	rhhm_destroy(&L->intern_pool);
	gc_destroy(&L->g);
}

int lua_init(state *L) {
	do {
		if (table_init(&L->G, G_INITIAL_SZ)) break;
		if (rhhm_init(&L->intern_pool, INTERN_POOL_INITIAL_SZ)) break;
		if (gc_init(&L->g, INITIAL_OBJECT_POOL_SZ)) break;
		return 0;
	} while (0);
	lua_destroy(L);
	return 1;
}

void lua_setglobal(state *L, bv key, bv value) {
//	printf(">setglobal set: %lf \n", value.d);
	table_set(&L->G, key, value);
}

bv lua_getglobal(state *L, bv key) {
	bv v = table_get(&L->G, key);
//	printf("getglobal got: %lf \n", v.d);
	return v;
}

void lua_setfield(state *L, bv table, bv key, bv value) { // TODO: remove L
	table_set(bv_get_ptr(table), key, value);
}

bv lua_getfield(state *L, bv table, bv key) { // TODO: remove L
	return table_get(bv_get_ptr(table), key);
}

bv lua_intern(state *L, const char *s, int len) {
	bv v;
	if (len > SSTR_MAX_LENGTH) {
		if (bv_is_nil(v = hm_sb_get_str(&L->intern_pool, (char*)s, len))) {
			void *str = str_new(s, len);
			//pool_insert(&L->alive, str);
			v = bv_make_str(str);
			hm_sb_set(&L->intern_pool, v, v);
		}
	} else v = bv_make_sstr(s, len);
	return v;
}

bv lua_newtable(state *L) {
	//str->mark = L->mark;
	//pool_insert(&L->alive, str);
	table *t = ML_MALLOC(sizeof(table));
	if (!t) abort();
	if (table_init(t, 32)) abort();
	return bv_make_tbl(t);
}

void lua_gc(state *L) {

}

void lua_error(state *L) {
	longjmp(L->jmpbuf, 1);
}

void optimize(ir *c) {
	for (int i = 0; i < c->io; i++) {
		


	}
}

void *loadstring(state *L, const char *s) {
	parser p;
	parser_init(&p, s, L);
	parse_chunk(&p);
	ir_disp(&p.c);

	optimize(&p.c);

	// assemble
	int nvars = p.c.iv;
	int lvar = nvars++; // for L
	if (nvars) nvars = (nvars & ~1) + 2; // stack 16B alignment


	printf("total vars: %d\n", nvars);
	printf("total ops:  %d\n", p.c.io);

	cc c;
	cc_init(&c);
	if (nvars) cc_subrsp(&c, sizeof(bv)*nvars);

	cc_mov_sr(&c, lvar, rdi); // save L

	for (int i = 0; i < p.c.io; i++) {
		c.op_addr[i] = cc_cur(&c);
		//printf("op %d : %p\n", i, c.op_addr[i]);
		tac *t = p.c.ops+i;
		switch (t->op) {
		case IR_OP_DISP:
			if (t->a < 0) cc_mov_rl(&c, rdi, p.c.ctts[-t->a-1]);
			else cc_mov_rs(&c, rdi, t->a);
			cc_call(&c, (void*)bv_disp);
			break;
		case IR_OP_JZ:
			cc_test_lsb(&c);
			if (t->target <= i) { // back
				cc_jz(&c, c.op_addr[t->target]);
			} else { // forward
				cc_jz(&c, NULL);
				cc_mark(&c, t->target);
			}
			break;
		case IR_OP_JNZ:
			cc_test_lsb(&c);
			if (t->target <= i) { // back
				cc_jnz(&c, c.op_addr[t->target]);
			} else { // forward
				cc_jnz(&c, NULL);
				cc_mark(&c, t->target);
			}
			break;
		case IR_OP_JMP:
			if (t->target <= i) { // back
				cc_jmp(&c, c.op_addr[t->target]);
			} else { // forward
				cc_jmp(&c, NULL);
				cc_mark(&c, t->target);
			}
			break;
		case IR_OP_NEWTBL:
			cc_mov_rs(&c, rdi, lvar);
			cc_call(&c, (void*)lua_newtable);
			cc_store_result(&c, t->target);
			break;
		case IR_OP_TSTORE:
			cc_mov_rs(&c, rdi, lvar);
			cc_mov_rs(&c, rsi, t->target);

			if (t->a < 0) cc_mov_rl(&c, rdx, p.c.ctts[-t->a-1]);
			else cc_mov_rs(&c, rdx, t->a);

			if (t->b < 0) cc_mov_rl(&c, rcx, p.c.ctts[-t->b-1]);
			else cc_mov_rs(&c, rcx, t->b);

			cc_call(&c, (void*)lua_setfield);

			break;
		case IR_OP_TLOAD:
			cc_mov_rs(&c, rdi, lvar);

			if (t->a < 0) cc_mov_rl(&c, rsi, p.c.ctts[-t->a-1]);
			else cc_mov_rs(&c, rsi, t->a);

			if (t->b < 0) cc_mov_rl(&c, rdx, p.c.ctts[-t->b-1]);
			else cc_mov_rs(&c, rdx, t->b);

			cc_call(&c, (void*)lua_getfield);

			cc_store_result(&c, t->target);
			break;
		case IR_OP_GSTORE:
			cc_mov_rs(&c, rdi, lvar);

			if (t->a < 0) cc_mov_rl(&c, rsi, p.c.ctts[-t->a-1]);
			else cc_mov_rs(&c, rsi, t->a);

			if (t->b < 0) cc_mov_rl(&c, rdx, p.c.ctts[-t->b-1]);
			else cc_mov_rs(&c, rdx, t->b);

			cc_call(&c, (void*)lua_setglobal);
			break;
		case IR_OP_GLOAD:
			cc_mov_rs(&c, rdi, lvar);

			if (t->a < 0) cc_mov_rl(&c, rsi, p.c.ctts[-t->a-1]);
			else cc_mov_rs(&c, rsi, t->a);

			cc_call(&c, (void*)lua_getglobal);

			cc_store_result(&c, t->target);
			break;
		case IR_OP_LLOAD:
			if (t->a < 0) cc_mov_rl(&c, rax, p.c.ctts[-t->a-1]);
			else cc_mov_rs(&c, rax, t->a);

			cc_store_result(&c, t->target);
			break;
		case '+': case '*': case '-': case '/': case '^': case '%':
			cc_mov_rs(&c, rdi, lvar);

			if (t->a < 0) cc_mov_rl(&c, rsi, p.c.ctts[-t->a-1]);
			else cc_mov_rs(&c, rsi, t->a);

			if (t->b < 0) cc_mov_rl(&c, rdx, p.c.ctts[-t->b-1]);
			else cc_mov_rs(&c, rdx, t->b);

			switch (t->op) {
			case '+': cc_call(&c, (void*)bv_add); break;
			case '*': cc_call(&c, (void*)bv_mul); break;
			case '-': cc_call(&c, (void*)bv_sub); break;
			case '/': cc_call(&c, (void*)bv_div); break;
			case '^': cc_call(&c, (void*)bv_pow); break;
			case '%': cc_call(&c, (void*)bv_mod); break;
			}

			cc_store_result(&c, t->target);

			break;
		case LEX_EQ: case LEX_NE:
			if (t->a < 0) cc_mov_rl(&c, rdi, p.c.ctts[-t->a-1]);
			else cc_mov_rs(&c, rdi, t->a);

			if (t->b < 0) cc_mov_rl(&c, rsi, p.c.ctts[-t->b-1]);
			else cc_mov_rs(&c, rsi, t->b);

			switch (t->op) {
			case LEX_EQ: cc_call(&c, (void*)bv_EQ); break;
			case LEX_NE: cc_call(&c, (void*)bv_NE); break;
			}

			cc_store_result(&c, t->target);

			break;
		}
	}
	// fill addresses
	c.op_addr[p.c.io] = cc_cur(&c);
	cc_fill_marks(&c);

	if (nvars) cc_leave(&c);
	else cc_pop(&c, rbp);
	cc_ret(&c);
	return cc_done(&c);
}

typedef void (*fn)(state*);
int lua_pcall(state *L, void *f) {
	if (!L || !f) return 1;
	if (setjmp(L->jmpbuf)) {
		puts(" * RUNTIME ERROR * ");
		return 1;
	}
	((fn)f)(L);
	return 0;
}

int main(int argc, char *argv[]) {
	lex_init();
	state L;
	lua_init(&L);
	
	//lua_pcall(&L, loadstring(&L, "a = 0 while a ~= 100000000 do a = a + 1 end  "));
	lua_pcall(&L, loadstring(&L, "b = 666 local a = 1 if a == 0 then b = 1000 end disp b "));
	//lua_pcall(&L, loadstring(&L, " disp 3 "));
	
	lua_destroy(&L);
	
	return 0;
}

