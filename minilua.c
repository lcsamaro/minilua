#include "cc.h"
#include "common.h"
#include "ir.h"
#include "lex.h"
#include "value.h"
#include "parser.h"
#include "rhhm.h"
#include "string.h"
#include "state.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


//asm
#include <sys/mman.h>

#define CC_BLOCK_SZ (1<<14)
enum cc_reg_gen {
	rax = 0, rcx, rdx, rbx, rsp, rbp, rsi, rdi,
	r8, r9, r10, r11, r12, r13, r14, r15
};

enum cc_reg_xmm {
	xmm0 = 0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7,
	xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15
};

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

	void *op_addr[IR_OP_MAX];

	void *fill[IR_OP_MAX];
	int ifill;
} cc;

int cc_init(cc *c) {
	c->s = c->p = mmap(0, CC_BLOCK_SZ,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	if (c->s == MAP_FAILED) return 1;
	c->ifill = 0;
	return 0;
}

void cc_destroy(cc *c) {
	munmap(c->s, CC_BLOCK_SZ);
}

u8 *cc_cur(cc *c) { return c->p; }

void cc_mark(cc *c, i32 target) {
	u8 *addr = cc_cur(c);
	memcpy(addr-4, &target, 4);
	c->fill[c->ifill++] = addr;
}

void cc_fill_marks(cc *c) {
	for (int i = 0; i < c->ifill; i++) {
		u8 *addr = c->fill[i];
		i32 target = *(i32*)(addr-4);
		i32 off = (u8*)c->op_addr[target] - addr;
		memcpy(addr-4, &off, 4);
	}
}

void* cc_done(cc *c) {
	printf("gen: %ld bytes\n", c->p - c->s);
	//print_bin(c->s, c->p - c->s);

#if 0
	FILE *fp = fopen("dump.bin", "wb");
	fwrite(c->s, 1, c->p-c->s, fp);
	fclose(fp);
#endif

	return mprotect(c->s, CC_BLOCK_SZ, PROT_READ | PROT_EXEC) == -1 ? NULL : c->s;
}


#define REX(w, r, x, b) (0x40 | (w << 3) | ((r & 0x8)>>1) | (x << 1) | ((b & 0x8) >> 3)) /* w: 64 bit op? 0 or 1, source, sib? 0 or 1, dest */
#define MODRM(mod, reg, rm) ((mod << 6) | (reg << 3) | rm)

void cc_mov_rl(cc *c, i32 reg, bv v) { // mov $reg, $v
	c->p[0] = 0x48;
	c->p[1] = (0xb8 | reg);
	memcpy(c->p+2, &v, sizeof(bv));
	c->p+=(2+sizeof(bv));
}

void cc_mov_rs(cc *c, i32 reg, i32 n) { // mov $reg, [rsp+$n]
	n++;
	memcpy(c->p, "\x48\x8b", 2);
	c->p[2] = (0x84 | (reg << 3));
	n *= sizeof(bv);
	c->p[3] = 0x24;
	memcpy(c->p+4, &n, sizeof(i32));
	c->p+=8;
}

void cc_mov_sr(cc *c, i32 n, i32 reg) { // mov [rsp+$n], $reg
	n++;
	memcpy(c->p, "\x48\x89", 2);
	c->p[2] = (0x84 | (reg << 3));
	n *= sizeof(bv);
	c->p[3] = 0x24;
	memcpy(c->p+4, &n, sizeof(i32));
	c->p+=8;
}

void cc_mov_rr(cc *c, i32 dest, i32 src) { // :)
	*c->p++ = REX(1, src, 0, dest);
	*c->p++ = 0x89;

	dest &= 0x7;
	src  &= 0x7;

	*c->p++ = MODRM(0x3, src, dest);
}

void cc_movq_xr(cc *c, i32 dest, i32 src) {
	*c->p++ = 0x66;
	*c->p++ = REX(1, src, 0, dest);
	*c->p++ = 0x0f;
	*c->p++ = 0x6e;

	dest &= 0x7;
	src  &= 0x7;

	*c->p++ = MODRM(0x3, dest, src);
}

void cc_movq_rx(cc *c, i32 dest, i32 src) {
	*c->p++ = 0x66;
	*c->p++ = REX(1, src, 0, dest);
	*c->p++ = 0x0f;
	*c->p++ = 0x7e;

	dest &= 0x7;
	src  &= 0x7;

	*c->p++ = MODRM(0x3, src, dest);
}

void cc_inc_rbx(cc *c, i32 reg) {
	memcpy(c->p, "\x48\xff\xc3", 3);
	c->p += 3;
}

void cc_inc(cc *c, i32 reg) {
	*c->p++ = REX(1, 0, 0, reg);
	*c->p++ = 0xff;

	reg  &= 0x7;

	*c->p++ = MODRM(0x3, reg, 0);
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

void cc_sub_rr(cc *c, i32 a, i32 b) {
	// TODO
}

void cc_xor_rr(cc *c, i32 a, i32 b) {
	*c->p++ = 0x48;
	*c->p++ = 0x31;
	*c->p++ = 0xc0 | (b << 3) | (a); // cmp
}

void cc_cmp_rr(cc *c, i32 a, i32 b) {
	*c->p++ = 0x48;
	*c->p++ = 0x39;
	*c->p++ = 0xc0 | (b << 3) | (a); // cmp
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

void cc_addrsp(cc *c, i32 s) {
	memcpy(c->p, "\x48\x81\xc4", 3); // add rsp, $s
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

#define EMIT(mcode) cc_mcode(&c, (u8*)mcode, (sizeof mcode)-1)

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
	table_set(&L->G, key, value);
}

bv lua_getglobal(state *L, bv key) {
	return table_get(&L->G, key);
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

#define LOAD(fld, reg, keep) \
	{ keep = reg; \
	if (fld < 0) { \
		if (p.c.ctts[-fld -1].d == 0.0) cc_xor_rr(&c, reg, reg); \
		else cc_mov_rl(&c, reg, p.c.ctts[-fld -1]); \
	} else { \
		int a = assignment[fld]; \
		if (a < 0) { \
			cc_mov_rs(&c, reg, -a); \
		} else if (reg != a) { \
			cc_mov_rr(&c, reg, a); \
		} \
	}}

#define LOAD_A(reg) LOAD(t->a, reg, ra)
#define LOAD_B(reg) LOAD(t->b, reg, rb)
//#define LOAD_T(reg) LOAD(t->target, reg, keep)

#define SAVE_RESULT(fld) \
	{ \
	int a = assignment[fld]; \
	if (a >= 0) { \
		if (a != rax) cc_mov_rr(&c, a, rax); \
	} else { \
		cc_store_result(&c, -a); \
	} }

int cmp(const void* a, const void* b) {
    const u32 ai = *(const u32*)a;
    const u32 bi = *(const u32*)b;
    if (ai < bi) return -1;
    return ai > bi ? 1 : 0;
}

int allocate(ir *c, int *assignment) { // allocate registers
	u32 ini[IR_OP_MAX];
	u32 end[IR_OP_MAX];
	for (int i = 0; i < IR_OP_MAX; i++) ini[i] = -1;
	for (int i = 0; i < IR_OP_MAX; i++) end[i] = -1;
	for (int i = c->io; i-- > 0; ) {
		int op = c->ops[i].op;
		if (op == IR_OP_NOOP) continue;
		int target = c->ops[i].target;
		if (!ir_is_jmp(op) && target != IR_NO_TARGET) ini[target] = (i<<16) | target;
	}
	for (int i = 0; i < c->io; i++) {
		int op = c->ops[i].op;
		if (op == IR_OP_NOOP) continue;
		int a = c->ops[i].a;
		int b = c->ops[i].b;
		int target = c->ops[i].target;

		if (a >= 0 && a != IR_NO_ARG) end[a] = i;
		if (b >= 0 && b != IR_NO_ARG) end[b] = i;
	}

	qsort(ini, IR_OP_MAX, sizeof(int), cmp);

	int regs[] = { rbx, rbp, r12, r13, r14, r15 };
	int scratch[] = { rbx, rbp, r12, r13, r14, r15 };
	int current[sizeof regs];
	int used = 0;
	int spills = 0;
	for (int i = 0; i < IR_OP_MAX; i++) {
		if (ini[i] == -1) break;

		int var = ini[i]&0xffff;
		int var_ini = ini[i]>>16;
		int var_end = end[var];

		printf("%d: [%d, %d]\n", var, var_ini, var_end);

		if (used < sizeof regs) {
			current[used] = var;
			//assignment[var] = -(++spills); //regs[used++];
			assignment[var] = regs[used++];
		} else {
			puts("spill");
			spills++;
		}
	}

	return spills;
}

void *loadstring(state *L, const char *s) {
	parser p;
	parser_init(&p, s, L);
	parse_chunk(&p);

	puts(" ** COMPILING ** ");
	puts(s);
	puts("");
	puts("IR:");
	ir_disp(&p.c);

	ir_opt(&p.c);
	puts("IR:");
	ir_disp(&p.c);

	ir_phi_elim(&p.c);
	puts("IR:");
	ir_disp(&p.c);

	int assignment[IR_OP_MAX];
	allocate(&p.c, assignment);

	// assemble
	int nvars = p.c.iv;
	int lvar = nvars++; // for L
	if (nvars) nvars = (nvars & ~1) + 2; // stack 16B alignment


	printf("total vars: %d\n", nvars);
	printf("total ops:  %d\n", p.c.io);

	cc c;
	cc_init(&c);
	cc_push(&c, rbp);
	if (nvars) cc_subrsp(&c, sizeof(bv)*nvars);

	cc_mov_sr(&c, lvar, rdi); // save L

	int ra, rb;
	for (int i = 0; i < p.c.io; i++) {

		c.op_addr[i] = cc_cur(&c);
		//printf("op %d : %p\n", i, c.op_addr[i]);
		tac *t = p.c.ops+i;

		switch (t->op) {
		case IR_OP_DISP:
			LOAD_A(rdi);
			cc_call(&c, (void*)bv_disp);
			break;
		case IR_OP_JE: // cmp + jmp
			ra = -1;
			if (t->a >= 0) ra = assignment[t->a];
			if (ra < 0) LOAD_A(rdi);

			rb = -1;
			if (t->b >= 0) rb = assignment[t->b];
			if (rb < 0) LOAD_B(rsi);

			cc_cmp_rr(&c, ra, rb);

			if (t->target <= i) { // back
				cc_jz(&c, c.op_addr[t->target]);
			} else { // forward
				cc_jz(&c, NULL);
				cc_mark(&c, t->target);
			}

			break;
		case IR_OP_JNE: // cmp + jmp
			ra = -1;
			if (t->a >= 0) ra = assignment[t->a];
			if (ra < 0) LOAD_A(rdi);

			rb = -1;
			if (t->b >= 0) rb = assignment[t->b];
			if (rb < 0) LOAD_B(rsi);

			cc_cmp_rr(&c, ra, rb);

			if (t->target <= i) { // back
				cc_jnz(&c, c.op_addr[t->target]);
			} else { // forward
				cc_jnz(&c, NULL);
				cc_mark(&c, t->target);
			}

			break;
		case IR_OP_JZ:
			// rax
			cc_test_lsb(&c);
			if (t->target <= i) { // back
				cc_jz(&c, c.op_addr[t->target]);
			} else { // forward
				cc_jz(&c, NULL);
				cc_mark(&c, t->target);
			}
			break;
		case IR_OP_JNZ:
			// rax
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
			SAVE_RESULT(t->target);
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

			LOAD_A(rsi);
			LOAD_B(rdx);

			if (ra != rsi) cc_mov_rr(&c, rsi, ra);
			if (rb != rdx) cc_mov_rr(&c, rdx, rb);

			cc_call(&c, (void*)lua_getfield);

			SAVE_RESULT(t->target);
			break;
		case IR_OP_GSTORE:
			cc_mov_rs(&c, rdi, lvar);

			LOAD_A(rsi);
			LOAD_B(rdx);

			if (ra != rsi) cc_mov_rr(&c, rsi, ra);
			if (rb != rdx) cc_mov_rr(&c, rdx, rb);

			cc_call(&c, (void*)lua_setglobal);
			break;
		case IR_OP_GLOAD:
			cc_mov_rs(&c, rdi, lvar);

			LOAD_A(rsi);
			if (ra != rsi) cc_mov_rr(&c, rsi, ra);

			cc_call(&c, (void*)lua_getglobal);

			SAVE_RESULT(t->target);
			break;
		case IR_OP_LCOPY: {
			if (assignment[t->target] >=0) {
				LOAD_A(assignment[t->target]);
			} else {
				LOAD_A(rax);
				SAVE_RESULT(t->target);
			}

			} break;
		case IR_OP_INC: case IR_OP_DEC:
				  cc_inc_rbx(&c, 0);
				  /*
			LOAD_A(rdi);
			switch (t->op) {
			case IR_OP_INC: cc_call(&c, (void*)bv_inc); break;
			case IR_OP_DEC: cc_call(&c, (void*)bv_dec); break;
			}

			//cc_mov_rr(&c, assignment[t->target], rax);
			SAVE_RESULT(t->target);

			*/
			break;
		case '+':
			/*LOAD_A(rdi);
			LOAD_B(rsi);*/

			ra = -1;
			if (t->a >= 0) ra = assignment[t->a];
			if (ra < 0) LOAD_A(rdi);

			rb = -1;
			if (t->b >= 0) rb = assignment[t->b];
			if (rb < 0) LOAD_B(rsi);

			cc_movq_xr(&c, xmm0, ra);
			cc_movq_xr(&c, xmm1, rb);

			//EMIT("\x66\x48\x0f\x6e\xc7"); // movq xmm0, rdi
			//EMIT("\x66\x48\x0f\x6e\xce"); // movq xmm1, rsi
			EMIT("\xf2\x0f\x58\xc1"); // addsd xmm0, xmm1

			if (assignment[t->target] >= 0) {
				cc_movq_rx(&c, assignment[t->target], xmm0);
			} else {
				cc_movq_rx(&c, rax, xmm0);
				SAVE_RESULT(t->target);
			}

			break;
		case '*': case '-': case '/': case '^': case '%':
			//cc_mov_rs(&c, rdi, lvar);

			LOAD_A(rsi);
			LOAD_B(rdx);

			switch (t->op) {
			case '+': cc_call(&c, (void*)bv_add); break;
			case '*': cc_call(&c, (void*)bv_mul); break;
			case '-': cc_call(&c, (void*)bv_sub); break;
			case '/': cc_call(&c, (void*)bv_div); break;
			case '^': cc_call(&c, (void*)bv_pow); break;
			case '%': cc_call(&c, (void*)bv_mod); break;
			}

			SAVE_RESULT(t->target);

			break;
		case LEX_NE:
		case LEX_EQ:
			LOAD_A(rdi);
			LOAD_B(rsi);

			switch (t->op) {
			case LEX_EQ: cc_call(&c, (void*)bv_EQ); break;
			case LEX_NE: cc_call(&c, (void*)bv_NE); break;
			case LEX_LE: cc_call(&c, (void*)bv_LE); break;
			case '<': cc_call(&c, (void*)bv_LT); break;
			case LEX_GE: cc_call(&c, (void*)bv_GE); break;
			case '>': cc_call(&c, (void*)bv_GT); break;
			}

			SAVE_RESULT(t->target);
			break;
		}
	}
	// fill addresses
	c.op_addr[p.c.io] = cc_cur(&c);
	cc_fill_marks(&c);

	if (nvars) {
		cc_addrsp(&c, sizeof(bv)*nvars);
		cc_pop(&c, rbp);
	} else cc_pop(&c, rbp);
	cc_ret(&c);
	/*cc_movq_xr(&c, xmm0, rax);
	cc_movq_xr(&c, xmm0, rbx);
	cc_movq_xr(&c, xmm1, rax);
	cc_movq_xr(&c, xmm1, rbx);
	cc_movq_xr(&c, xmm2, rax);
	cc_movq_xr(&c, xmm2, rbx);

	cc_movq_rx(&c, xmm2, rax);
	cc_movq_rx(&c, xmm2, rbx);
	cc_inc(&c, rax);
	cc_inc(&c, rbx);
	cc_inc(&c, rdi);
	cc_inc(&c, r14);
	cc_inc(&c, r15);*/
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

// ffi
#include <dlfcn.h>

void hellofun() { puts("hello world!"); }

void ffi() {
	void *handle = dlopen(NULL, RTLD_LAZY);
	if (!handle) {
		puts("error");
		return;
	}
	dlerror();

	void (*fn)(void);
	*(void **) (&fn) = dlsym(handle, "hellofun");
	if (dlerror()) {
		puts("error");
		dlclose(handle);
		return;
	}

	(*fn)();

	dlclose(handle);
}

/*
 *
 * ffi('double f(double, double)', 'lib.so')
 *
 */

// test
typedef void* (*thunk)(void);
void *getrsp() {
	char *t = "\x48\x89\xe0\xc3";
	return ((thunk)t)();
}


i64 *t1_rsp;
i64 test(i64 x, i64 y);
void t1(i64 x) {
	t1_rsp = getrsp();
	test(x, x);
}

void t2() {
	i64 *s = getrsp();
	printf("stack from %p to %p totalling %ld: ", s, t1_rsp, t1_rsp - s);
	while (s < t1_rsp) printf("%ld ", *s++);
	puts("");
}

i64 test(i64 x, i64 y) {
	i64 a[4];
	a[0] = x+y;
	a[1] = x+y+1;
	a[2] = x*y;
	a[3] = x*y+2;
	for (int i = 0; i < 4; i++) a[i] = 666;
	t2();
	return a[(x+y)%4];
}

#define PAD_LEFT 2
#define PAD_RIGHT 1
int main(int argc, char *argv[]) {
	ffi();

	lex_init();
	
	if (argc < 2) return 0;
	
	FILE *f = fopen(argv[1], "rb");
	if (!f) return 1;
	fseek(f, 0, SEEK_END);
	int len = ftell(f);
	rewind(f);
	char *b = malloc(PAD_LEFT + len + PAD_RIGHT);
	if (!b) { fclose(f); return 1; }
	if (fread(b+PAD_LEFT, 1, len, f) != len) { fclose(f); free(b); return 1; };
	b[PAD_LEFT+len] = '\0';
	fclose(f);

	state L;
	lua_init(&L);


	void *s = loadstring(&L, b+PAD_LEFT);
	lua_pcall(&L, s);
	lua_destroy(&L);
	free(b);
	
	printf("parser: %lu KB\n", sizeof(parser)/1024);

	printf("%d\n", 0.0 == 0ULL);

	return 0;
}

