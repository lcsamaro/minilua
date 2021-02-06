
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
	printf("gen: %ld bytes\n", c->p - c->s);
	//print_bin(c->s, c->p - c->s);

#if 1
	FILE *fp = fopen("dump.bin", "wb");
	fwrite(c->s, 1, c->p-c->s, fp);
	fclose(fp);
#endif

	return mprotect(c->s, CC_BLOCK_SZ, PROT_READ | PROT_EXEC) == -1 ? NULL : c->s;
}


#define REX(w, r, x, b) (0x40 | (w << 3) | ((r & 0x8)>>1) | (x << 1) | ((b & 0x8) >> 3)) /* w: 64 bit op? 0 or 1, dest, sib? 0 or 1, source */
#define MODRM(mod, reg, rm) (((mod&0x7) << 6) | ((reg&0x7) << 3) | rm) /* mod: 00 indirect, 01 1-byte displa, 10 4-byte displ, 11 reg; dest; source */

#define EMIT_REX(w, reg, rm) \
	do { *c->p++ = REX(1, reg, 0, rm); } while (0)

#define EMIT_OPCODE(op) \
	do { *c->p++ = op; } while (0)

#define EMIT_MODRM(mod, reg, rm) \
	do { *c->p++ = MODRM(mod, reg, rm); } while (0)

#define EMIT_SIB() \
	do { *c->p++ = 0x24; } while (0)

#define EMIT_I32(val) \
	do { memcpy(c->p, &val, sizeof(i32)); c->p+=sizeof(i32); } while (0)



void cc_mov_rl(cc *c, i32 reg, bv v) { // mov $reg, $v
	c->p[0] = 0x48;
	c->p[1] = (0xb8 | reg);
	memcpy(c->p+2, &v, sizeof(bv));
	c->p+=(2+sizeof(bv));
}

void cc_mov_rs(cc *c, i32 reg, i32 n) { // mov $reg, [rsp+$n]
	EMIT_REX(1, reg, rsp);
	EMIT_OPCODE(0x8b);
	EMIT_MODRM(0x2, reg, rsp);
	EMIT_SIB();
	n*=sizeof(bv);
	EMIT_I32(n);
	/*c->p[2] = (0x84 | (reg << 3));
	c->p[3] = 0x24;
	memcpy(c->p+4, &n, sizeof(i32));
	c->p+=8;*/
}

void cc_mov_sr(cc *c, i32 n, i32 reg) { // mov [rsp+$n], $reg
	EMIT_REX(1, reg, rsp);
	EMIT_OPCODE(0x89);
	EMIT_MODRM(0x2, reg, rsp);
	EMIT_SIB();
	n*=sizeof(bv);
	EMIT_I32(n);
	/*memcpy(c->p, "\x89", 1);
	c->p[2] = (0x84 | (reg << 3));
	n *= sizeof(bv);
	c->p[3] = 0x24;
	memcpy(c->p+4, &n, sizeof(i32));
	c->p+=8;*/
}

void cc_mov_rr(cc *c, i32 dest, i32 src) { // :)
	*c->p++ = REX(1, src, 0, dest);
	*c->p++ = 0x89;
	*c->p++ = MODRM(0x3, src, dest);
}

void cc_movq_xr(cc *c, i32 dest, i32 src) {
	*c->p++ = 0x66;
	*c->p++ = REX(1, src, 0, dest);
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

void cc_inc_rbx(cc *c, i32 reg) {
	memcpy(c->p, "\x48\xff\xc3", 3);
	c->p += 3;
}

void cc_inc(cc *c, i32 reg) {
	*c->p++ = REX(1, 0, 0, reg);
	*c->p++ = 0xff;
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

