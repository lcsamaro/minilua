#include "common.h"

#include "cc.h"
#include "env.h"
#include "lex.h"
#include "lapi.h"
#include "value.h"
#include "ir.h"

#include <sys/mman.h>

#include <stdio.h>
#include <string.h>

/* assembler */
enum cc_reg_gen {
	rax = 0, rcx, rdx, rbx, rsp, rbp, rsi, rdi,
	r8, r9, r10, r11, r12, r13, r14, r15
};

enum cc_reg_xmm {
	xmm0 = 0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7,
	xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15
};

typedef struct {
	u8 *s;
	u8 *p;

	void *op_addr[IR_OP_MAX];

	void *fill[IR_OP_MAX];
	int ifill;
} cc;

#define CC_BLOCK_SZ (1<<14)

int cc_init(cc *c) {
	c->s = c->p = mmap(0, CC_BLOCK_SZ,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS
#ifdef linux
		| MAP_32BIT
#endif
		, -1, 0);
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
#ifdef DBG
	printf("gen: %ld bytes\n", c->p - c->s);
	FILE *fp = fopen("dump.bin", "wb");
	fwrite(c->s, 1, c->p-c->s, fp);
	fclose(fp);
#endif

	return mprotect(c->s, CC_BLOCK_SZ, PROT_READ | PROT_EXEC) == -1 ? NULL : c->s;
}


/* w: 64 bit op? 0 or 1, dest, sib? 0 or 1, source */
#define REX(w, r, x, b) (0x40 | (w << 3) | ((r & 0x8)>>1) | (x << 1) | ((b & 0x8) >> 3))

/* mod: 00 indirect, 01 1-byte displa, 10 4-byte displ, 11 reg; r; m */
#define MODRM(mod, reg, rm) (((mod&0x7) << 6) | ((reg&0x7) << 3) | (rm&0x7))

#define EMIT_REX(w, r, b) \
	do { *c->p++ = REX(w, r, 0, b); } while (0)

#define EMIT_OPCODE(op) \
	do { *c->p++ = op; } while (0)

#define EMIT_MODRM(mod, r, m) \
	do { *c->p++ = MODRM(mod, r, m); } while (0)

#define EMIT_SIB() \
	do { *c->p++ = 0x24; } while (0)

#define EMIT_I32(val) \
	do { memcpy(c->p, &val, sizeof(i32)); c->p+=sizeof(i32); } while (0)



void cc_xor_rr(cc *c, i32 a, i32 b);
void cc_mov_rl(cc *c, i32 reg, bv v) { // mov $reg, $v
	if (v.u == 0) {
		cc_xor_rr(c, reg, reg);
		return;
	}

	if (reg >= r8) c->p[0] = 0x49;
	else c->p[0] = 0x48;

	//if (v.u > 255) {
		c->p[1] = (0xb8 | (reg&0x7));
		memcpy(c->p+2, &v, sizeof(bv));
		c->p+=(2+sizeof(bv));
	/*} else {
		c->p[1] = (0xb0 | (reg&0x7));
		c->p[2] = v.u;
		c->p+=3;
	}*/
}

void cc_mov_rs(cc *c, i32 reg, i32 n) { // mov $reg, [rsp+$n]
	EMIT_REX(1, reg, rsp);
	EMIT_OPCODE(0x8b);
	EMIT_MODRM(0x2, reg, rsp);
	EMIT_SIB();
	n*=sizeof(bv);
	EMIT_I32(n);
}

void cc_mov_sr(cc *c, i32 n, i32 reg) { // mov [rsp+$n], $reg
	EMIT_REX(1, reg, rsp);
	EMIT_OPCODE(0x89);
	EMIT_MODRM(0x2, reg, rsp);
	EMIT_SIB();
	n*=sizeof(bv);
	EMIT_I32(n);
}

void cc_mov_rr(cc *c, i32 dest, i32 src) {
	*c->p++ = REX(1, src, 0, dest);
	*c->p++ = 0x89;
	*c->p++ = MODRM(0x3, src, dest);
}

void cc_movq_xr(cc *c, i32 dest, i32 src) {
	*c->p++ = 0x66;
	*c->p++ = REX(1, dest, 0, src);
	*c->p++ = 0x0f;
	*c->p++ = 0x6e;
	*c->p++ = MODRM(0x3, dest, src);
}

void cc_movq_rx(cc *c, i32 dest, i32 src) {
	*c->p++ = 0x66;
	*c->p++ = REX(1, src, 0, dest);
	*c->p++ = 0x0f;
	*c->p++ = 0x7e;
	*c->p++ = MODRM(0x3, src, dest);
}

#define cc_store_result(c, n) cc_mov_sr(c, n, rax)

void cc_push(cc *c, i32 reg) {
	if (reg >= r8) *c->p++ = 0x41; // r8-r15 support
	*c->p++ = 0x50 + (reg & 0x7);
}

void cc_pop(cc *c, i32 reg) {
	if (reg >= r8) *c->p++ = 0x41; // r8-r15 support
	*c->p++ = 0x58 + (reg & 0x7);
}

void cc_xor_rr(cc *c, i32 dest, i32 src) {
	*c->p++ = REX(1, src, 0, dest);
	*c->p++ = 0x31;
	*c->p++ = MODRM(0x3, src, dest);
}

void cc_cmp_rr(cc *c, i32 dest, i32 src) {
	*c->p++ = REX(1, src, 0, dest);
	*c->p++ = 0x39;
	*c->p++ = MODRM(0x3, src, dest);
}

void cc_test_rr(cc *c, i32 dest, i32 src) {
	*c->p++ = REX(1, src, 0, dest);
	*c->p++ = 0x85;
	*c->p++ = MODRM(0x3, src, dest);
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

void cc_mcode(cc *c, u8 *mcode, u32 sz) {
	memcpy(c->p, mcode, sz);
	c->p+=sz;
}

u8 *cc_skip(cc *c, u32 sz) {
	u8 *r = c->p;
	c->p+=sz;
	return r;
}

void cc_subrsp(cc *c, i32 s) {
	if (s <= 127 && s >= -128) {
		cc_mcode(c, (u8*)"\x48\x83\xec", 3); // sub rsp, $s
		*c->p++ = s;
	} else {
		cc_mcode(c, (u8*)"\x48\x81\xec", 3); // sub rsp, $s
		memcpy(c->p, &s, sizeof(i32));
		c->p+=4;
	}
}

void cc_addrsp(cc *c, i32 s) {
	if (s <= 127 && s >= -128) {
		cc_mcode(c, (u8*)"\x48\x83\xc4", 3); // add rsp, $s
		*c->p++ = s;
	} else {
		cc_mcode(c, (u8*)"\x48\x81\xc4", 3); // add rsp, $s
		memcpy(c->p, &s, sizeof(i32));
		c->p+=4;
	}
}

void cc_leave(cc *c) {
	*c->p++ = 0xc9;
}

void cc_ret(cc *c) {
	*c->p++ = 0xc3;
}

void cc_nop(cc *c) {
	*c->p++ = 0x90;
}

void cc_addsd(cc *c, i32 dest, i32 src) {
	cc_mcode(c, (u8*)"\xf2\x0f\x58", 3);
	*c->p++ = MODRM(0x3, dest, src);
}

void cc_subsd(cc *c, i32 dest, i32 src) {
	cc_mcode(c, (u8*)"\xf2\x0f\x5c", 3);
	*c->p++ = MODRM(0x3, dest, src);
}

void cc_mulsd(cc *c, i32 dest, i32 src) {
	cc_mcode(c, (u8*)"\xf2\x0f\x59", 3);
	*c->p++ = MODRM(0x3, dest, src);
}

void cc_divsd(cc *c, i32 dest, i32 src) {
	cc_mcode(c, (u8*)"\xf2\x0f\x5e", 3);
	*c->p++ = MODRM(0x3, dest, src);
}

/* Register allocator */
static int allocate_cmp(const void* a, const void* b) {
    const u32 ai = *(const u32*)a;
    const u32 bi = *(const u32*)b;
    if (ai < bi) return -1;
    return ai > bi ? 1 : 0;
}

static void liveness(ir *c, u32 *ini, u32 *end) {
	prof_begin("liveness_Z");

	u8 loop_depth[IR_OP_MAX] = {0};

	for (int i = 0; i < vsize(c->ops); i++) ini[i] = end[i] = (u32)-1;
	prof_end();
	prof_begin("livenessI");
	for (int i = vsize(c->ops)-1; i >= 0; i--) {
		int op = vget(c->ops, i).op;
		if (op == IR_OP_NOOP || ir_is_mark(op)) continue;
		int target = vget(c->ops, i).target;
		if (!ir_is_jmp(op) && target != IR_NO_TARGET) ini[target] = (i<<16) | target;
	}
	prof_end();
	prof_begin("livenessE");
	u8 depth = 0;
	for (int i = 0; i < vsize(c->ops); i++) {
		int op = vget(c->ops, i).op;

		if (op == IR_LOOP_BEGIN) depth++;
		else if (op == IR_LOOP_END) depth--;
		loop_depth[i] = depth;

		if (op == IR_OP_NOOP || ir_is_mark(op)) continue;
		int a = vget(c->ops, i).a;
		int b = vget(c->ops, i).b;

		if (a >= 0 && a != IR_NO_ARG) end[a] = i;
		if (b >= 0 && b != IR_NO_ARG && op != IR_OP_CALL) end[b] = i;
	}

	prof_end();

	prof_begin("live ext");
	for (int j = 0; j < c->iv; j++) { // extend live ranges
			int b = ini[j] >> 16;
			int e = end[j];

			while (loop_depth[e] > loop_depth[b]) {
				e = ++end[j];
			}
	}

#ifdef DBG
	puts(" * allocation live ranges *");
	printf("  .");
	for (int j = 0; j < c->iv; j++) printf("%2d", j % 10);
	puts("");
	for (int i = 0; i < vsize(c->ops); i++) {
		printf("%2d. ", i);
		for (int j = 0; j < c->iv; j++) {
			int b = ini[j] >> 16;
			int e = end[j];
			if (i >= b && i <= e) {
				printf("@ ");
			} else {
				printf(". ");
			}
		}
		printf("\n");
	}
#endif

	prof_end();

	prof_begin("live sort");

	qsort(ini, vsize(c->ops), sizeof(int), allocate_cmp);

	prof_end();
}

// [ir_begin, ir_end)
static void allocate(ir *c,
	int ir_begin, int ir_end,
	int *regs, int nregs,
	u32 *ini, u32 *end,
	int *assignment, int *allocated, int *spilled) {

	prof_begin("selection");

	int current[nregs];
	int used = 0;
	int spills = 0;
	for (int i = 0; i < vsize(c->ops) + 1; i++) {
		int var = ini[i]&0xffff;
		int var_ini = ini[i]>>16;
		int var_end = end[var];

		if (assignment[var]) { // already pre-allocated, skip
			//puts(" VAR SKIPPED, as already allocated!");
			continue;
		}

		if (var_end == -1) { // unused
			assignment[var] = rax;
			continue;
		}

		// check if there is a free reg
		int spill = 1;
		for (int j = 0; j < used; j++) {
			int oldvar = current[j];
			if (end[oldvar] < var_ini) {
				assignment[var] = assignment[oldvar];
				current[j] = var;
				spill = 0;
				break;
			}
		}

		if (!spill) continue;

		if (used < nregs) {
			current[used] = var;
			assignment[var] = regs[used++];
			continue;
		}


		// spill
		int spilled = var;
		int spilled_idx = 0;
		for (int j = 0; j < used; j++) {
			int oldvar = current[j];
			if (end[oldvar] > end[spilled]) {
				spilled_idx = j;
				spilled = oldvar;
			}
		}

		if (spilled == var) {
			assignment[var] = -(++spills);
		} else {
#ifdef DBG
			printf("RE SPILL %d -> %d\n", var, spilled);
#endif
			current[spilled_idx] = var;
			assignment[var] = assignment[spilled];
			assignment[spilled] = -(++spills);
		}

	}

	*allocated = used;
	*spilled = spills;

	prof_end();
}

/**********************************************************/
/* IR compiler                                            */
/**********************************************************/
#define LOAD(fld, reg, keep) \
	do { keep = reg; \
	if (fld < 0) { \
		cc_mov_rl(&c, reg, vget(o->ctts, -fld -1)); \
	} else { \
		int a = assignment[fld]; \
		if (a < 0) { \
			cc_mov_rs(&c, reg, -a-1); \
		} else if (reg != a) { \
			cc_mov_rr(&c, reg, a); \
		} \
	}} while (0)

#define LOAD_A(reg) LOAD(t->a, reg, ra)
#define LOAD_B(reg) LOAD(t->b, reg, rb)

#define LOAD_RA() \
	do { \
		ra = -1; \
		if (t->a >= 0) ra = assignment[t->a]; \
		if (ra < 0) LOAD_A(rdi); \
	} while (0)

#define LOAD_RB() \
	do { \
		rb = -1; \
		if (t->b >= 0) rb = assignment[t->b]; \
		if (rb < 0) LOAD_B(rsi); \
	} while (0)

#define LOAD_RA_RB() \
	do { LOAD_RA(); LOAD_RB(); } while (0)

#define SAVE_RESULT(fld) \
	do { \
		if (fld == IR_NO_TARGET) break; \
		int a = assignment[fld]; \
		if (a >= 0) { if (a != rax) cc_mov_rr(&c, a, rax); } \
		else cc_store_result(&c, -a-1); \
	} while (0)

static void *compile_chunk(ir *o, int begin, int end, u32 *liv_ini, u32 *liv_end) {
	int regs[] = { rbx, rbp, r12, r13, r14, r15 };
	int assignment[IR_OP_MAX] = {0};
	int spills;
	int allocated;

	// fix params, so they are not allocated
	if (vget(o->ops, begin).op == IR_FUNCTION_BEGIN) {
		int nparams = vget(o->ops, begin).b;
		for (int i = 1; i <= nparams; i++) {
			int var = vget(o->ops, begin - i).a;
			assignment[var] = -1;
		}
	}

	prof_begin("ralloc");
	allocate(o, begin, end, regs, 6, liv_ini, liv_end, assignment, &allocated, &spills);
	prof_end();


#ifdef DBG
	printf("allocated: %d\n", allocated);
	//for (int i = 0; i < o->iv; i++) printf(" [%d] -> %d = %d\n", i, assignment[i], -assignment[i]-1);
	printf("spills: %d\n", spills);
#endif

	// assemble
	int nvars = spills;
	int lvar = nvars++; // for L, TODO: remove?

#ifdef DBG
	printf("L var stack:  %d\n", lvar);
	printf("total vars: %d\n", nvars);
#endif

	if (((allocated + nvars) & 1) == 0) nvars++; // stack 16B alignment
	//if (((nvars) & 1) == 0) nvars++; // stack 16B alignment

#ifdef DBG
	printf("total stack used: %d\n", nvars);
#endif

	if (vget(o->ops, begin).op == IR_FUNCTION_BEGIN) { // fix assignment for parameters
		int nparams = vget(o->ops, begin).b;

		for (int i = 1; i <= nparams; i++) {
			int var = vget(o->ops, begin - i).a;
			int locals = nvars + allocated + 1; // + 1 for ret
			assignment[var] = -(locals+(nparams-i))-1;
			//printf(" ASSIGN    > param: %d to %d -> %d\n", var, assignment[var], -assignment[var]-1);
		}
	}

#ifdef DBG
	printf("total vars (ALIGNED): %d\n", nvars);
	printf("total ops:  %d\n", vsize(o->ops));
#endif

	cc c;
	cc_init(&c);

	for (int i = 0; i < allocated; i++) cc_push(&c, regs[i]);

	if (nvars) cc_subrsp(&c, sizeof(bv)*nvars);

	cc_mov_sr(&c, lvar, rdi); // save L

	int ra, rb;
	for (int i = begin; i < end; i++) {
		c.op_addr[i] = cc_cur(&c);
		tac *t = vbegin(o->ops)+i;

		switch (t->op) {
		case IR_OP_CALL: {
			LOAD_A(rcx);

			cc_mov_rs(&c, rdi, lvar);

			bv v; v.u = t->b;
			cc_mov_rl(&c, rsi, v);

			int nargs = t->b;
			int align = nargs + (nargs & 1 ? 1 : 0);

			if (nargs) cc_subrsp(&c, sizeof(bv)*align);
			for (int j = 0; j < nargs; j++) {
				tac *arg = t-j-1;
				if (arg->a >= 0) {
					ra = assignment[arg->a];
					if (ra < 0) { // on stack
						cc_mov_rs(&c, rax, -ra-1 + align);
						ra = rax;
					}
				} else { // constant
					cc_mov_rl(&c, rax, vget(o->ctts, -arg->a-1));
					ra = rax;
				}
				cc_mov_sr(&c, nargs-j-1, ra);
			}
			cc_call(&c, ml_indirect_call);
			if (nargs) cc_addrsp(&c, sizeof(bv)*align);
			SAVE_RESULT(t->target);
			} break;
		case IR_OP_JE: // cmp + jmp
			LOAD_RA_RB();
			cc_cmp_rr(&c, ra, rb);

			if (t->target <= i) { // back
				cc_jz(&c, c.op_addr[t->target]);
			} else { // forward
				cc_jz(&c, NULL);
				cc_mark(&c, t->target);
			}

			break;
		case IR_OP_JNE: // cmp + jmp
			LOAD_RA_RB();
			cc_cmp_rr(&c, ra, rb);

			if (t->target <= i) { // back
				cc_jnz(&c, c.op_addr[t->target]);
			} else { // forward
				cc_jnz(&c, NULL);
				cc_mark(&c, t->target);
			}

			break;
		case IR_OP_JZ:
			//cc_test_lsb(&c);
			cc_test_rr(&c, rax, rax); // TODO: use target

			if (t->target <= i) { // back
				cc_jz(&c, c.op_addr[t->target]);
			} else { // forward
				cc_jz(&c, NULL);
				cc_mark(&c, t->target);
			}
			break;
		case IR_OP_JNZ:
			//cc_test_lsb(&c);
			cc_test_rr(&c, rax, rax); // TODO: use target

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

			if (t->a < 0) cc_mov_rl(&c, rdx, vget(o->ctts, -t->a-1));
			else cc_mov_rs(&c, rdx, t->a);

			if (t->b < 0) cc_mov_rl(&c, rcx, vget(o->ctts, -t->b-1));
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
			if (assignment[t->target] >= 0) {
				LOAD_A(assignment[t->target]);
			} else {
				LOAD_A(rax);
				SAVE_RESULT(t->target);
			}

			} break;
		case '+': case '*': case '-': case '/':
			LOAD_RA_RB();
			cc_movq_xr(&c, xmm0, ra);
			cc_movq_xr(&c, xmm1, rb);

			switch (t->op) {
			case '+': cc_addsd(&c, xmm0, xmm1); break;
			case '*': cc_mulsd(&c, xmm0, xmm1); break;
			case '-': cc_subsd(&c, xmm0, xmm1); break;
			case '/': cc_divsd(&c, xmm0, xmm1); break;
			}

			if (assignment[t->target] >= 0) {
				cc_movq_rx(&c, assignment[t->target], xmm0);
			} else {
				cc_movq_rx(&c, rax, xmm0);
				SAVE_RESULT(t->target);
			}

			break;
		case '%':
			LOAD_RA_RB();

			cc_movq_xr(&c, xmm0, ra);
			cc_movq_xr(&c, xmm1, rb);

			cc_divsd(&c, xmm0, xmm1);
			cc_mcode(&c, (u8*)"\x66\x0f\x3a\x0b\xc0\x09", 6); // roundsd xmm0, xmm0, 9
			cc_mulsd(&c, xmm0, xmm1);

			cc_movq_xr(&c, xmm1, ra);
			cc_subsd(&c, xmm1, xmm0);

			if (assignment[t->target] >= 0) {
				cc_movq_rx(&c, assignment[t->target], xmm1);
			} else {
				cc_movq_rx(&c, rax, xmm1);
				SAVE_RESULT(t->target);
			}

			break;
		case LEX_EQ:
		case LEX_NE:
			LOAD_A(rdi);
			LOAD_B(rsi);
			switch (t->op) {
			case LEX_EQ: cc_call(&c, (void*)bv_EQ); break;
			case LEX_NE: cc_call(&c, (void*)bv_NE); break;
			}
			break;
		}
	}
	// fill addresses
	c.op_addr[vsize(o->ops)] = cc_cur(&c);
	cc_fill_marks(&c);

	if (nvars) cc_addrsp(&c, sizeof(bv)*nvars);
	for (int i = allocated; i > 0; i--) cc_pop(&c, regs[i-1]);
	cc_ret(&c);
	return cc_done(&c);
}

typedef struct {
	u8 args;
} function_header;

void *compile(ir *I) {
#ifdef DBG
	puts("IR:"); ir_disp(I);
#endif

	prof_begin("opt");
	ir_opt(I);
	prof_end();

#ifdef DBG
	puts("IR:"); ir_disp(I);
#endif

	prof_begin("phi");
	ir_phi_elim(I);
	prof_end();

#ifdef DBG
	puts("IR:"); ir_disp(I);
#endif

	u32 liveness_ini[IR_OP_MAX];
	u32 liveness_end[IR_OP_MAX];
	liveness(I, liveness_ini, liveness_end);

	prof_begin("comp");
	// compile each function bottom-up
	for (int i = vsize(I->ops)-1; i >= 0; i--) {
		tac *t = vbegin(I->ops)+i;
		if (t->op == IR_FUNCTION_BEGIN) {
			int begin = i;
			int end = t->target;
			printf(">> compile fn [%d, %d]\n", begin, end);
			void *fn = compile_chunk(I, begin, end, liveness_ini, liveness_end);
			printf(" addr => %p\n", fn);
			while (begin <= end) {
				vget(I->ops, begin++).op = IR_OP_NOOP;
			}
			vget(I->ops, end+1).a = ir_ctt(I, box_cfunction(fn));
		}
	}

	// compile top-level code
	void *r = compile_chunk(I, 0, vsize(I->ops), liveness_ini, liveness_end);

	prof_end();
	return r;
}
