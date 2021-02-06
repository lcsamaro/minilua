#include "value.h"
#include "lapi.h"

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
	memcpy(((char*)&v)+1, s, len);
	return v;
}

char *bv_get_sstr(bv *v) {
	return bv_is_sstr(*v) ? ((char*)v)+1 : NULL;
}

int bv_get_sstr_len(bv v) {
	return bv_is_sstr(v) ? v.u & 0xf : -66;
}


void *bv_get_ptr(bv v) { return (void*)(v.u & bv_value_mask); }
void *bv_get_ptr_clean(bv v) { return (void*)(v.u & (bv_value_mask ^ 3)); }
rhhm *bv_get_tbl(bv v) { return (rhhm*)(v.u & bv_value_mask); }
u64 bv_get_u64(bv v) { return v.u & bv_value_mask; }

u64 bv_type(bv v) { return v.u & bv_type_mask; }

int bv_is_double(bv v) {
	return !isnan(v.d);
}
int bv_is_nil(bv v) { return v.u == bv_nil; }
int bv_is_str(bv v) { return (v.u&bv_type_mask) == bv_str; }
int bv_is_sstr(bv v) { return (v.u&bv_type_mask) == bv_sstr; }

int bv_is_tbl(bv v) {
	return (v.u&bv_type_mask) == bv_tbl;
}

///// new
u32 value_type(value v) {
	return bv_type(v);
}

value box_double(double v) {
	value boxed;
	boxed.d = v;
	return boxed;
}

value box_bool(int v) {
	value boxed;
	boxed.u = bv_bool | (v ? 1 : 0);
	return boxed;
}

value box_ptr(void *v) {
	value boxed;
	boxed.i = 0;
	return boxed;
}

value box_nil() {
	bv boxed;
	boxed.u = bv_nil;
	return boxed;
}

value box_cfunction(void *v) {
	bv boxed;
	boxed.p = v;
	boxed.u |= bv_cfunction;
	return boxed;
}

double unbox_double(value v) {
	return 0.0;
}

int unbox_bool(value v) {
	return 0;
}

void *unbox_ptr(value v) {
	return NULL;
}

/* ops */
bv bv_add(state *L, bv a, bv b) {
	bv r = nil;
	r.d = a.d + b.d;
	return r;
}

bv bv_mul(state *L, bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = a.d * b.d;
	else lua_error(L);
	return r;
}

bv bv_sub(state *L, bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = a.d - b.d;
	else lua_error(L);
	return r;
}

bv bv_div(state *L, bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = a.d / b.d;
	else lua_error(L);
	return r;
}

bv bv_pow(state *L, bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = pow(a.d, b.d);
	else lua_error(L);
	return r;
}

bv bv_mod(state *L, bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = a.d - floor(a.d / b.d) * b.d;
	else lua_error(L);
	return r;
}

u64 bv_EQ(bv a, bv b) { return a.u == b.u ? 1 : 0; }
u64 bv_NE(bv a, bv b) {
	//printf("CHECK %.14g %.14g\n", a.d, b.d);
	return a.u == b.u ? 0 : 1;
}
u64 bv_LE(bv a, bv b) { return a.d <= b.d ? 1 : 0; } // TODO
u64 bv_LT(bv a, bv b) { return a.d < b.d ? 1 : 0; } // TODO
u64 bv_GE(bv a, bv b) { return a.d >= b.d ? 1 : 0; } // TODO
u64 bv_GT(bv a, bv b) { return a.d > b.d ? 1 : 0; } // TODO
bv bv_inc(bv a) { a.d+=1.0; return a; }
bv bv_dec(bv a) { a.d-=1.0; return a; }

void bv_disp(bv v) {
	switch (bv_type(v)) {
	case bv_nil: printf("nil"); break;
	case bv_bool: printf("bool"); break;
	case bv_str: printf("str"); break;
	case bv_sstr:
		printf("%.*s", bv_get_sstr_len(v), bv_get_sstr(&v));
		break;
	case bv_tbl: printf("table: %p", v.p); break;
	case bv_cfunction: printf("cfunction"); break;
	case bv_closure:
	case bv_function: printf("function"); break;
	default: printf("%.14g", v.d);
	}
}

