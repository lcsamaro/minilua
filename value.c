#include "value.h"
#include "state.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

const bv nil  = { bv_nil };

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

u64 bv_EQ(bv a, bv b) { return a.u == b.u ? 1 : 0; }
u64 bv_NE(bv a, bv b) { return a.u == b.u ? 0 : 1; }
u64 bv_LE(bv a, bv b) { return 1; } // TODO
u64 bv_LT(bv a, bv b) { return 1; } // TODO
u64 bv_GE(bv a, bv b) { return 1; } // TODO
u64 bv_GT(bv a, bv b) { return 1; } // TODO
bv bv_inc(bv a) { a.d+=1.0; return a; }
bv bv_dec(bv a) { a.d-=1.0; return a; }

void bv_disp(bv v) { printf("> %f\n", v.d); }

