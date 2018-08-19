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


char *bv_get_sstr(bv *v) {
	if (bv_is_sstr(*v)) return ((char*)v)+2;
	return NULL;
}

int bv_get_sstr_len(bv v) {
	if (bv_is_sstr(v)) return v.u & 0xf;
	return 0;
}


void *bv_get_ptr(bv v) { return (void*)(v.u & bv_value_mask); }
void *bv_get_ptr_clean(bv v) { return (void*)(v.u & (bv_value_mask ^ 3)); }
u64 bv_get_u64(bv v) { return v.u & bv_value_mask; }

u32 bv_type(bv v) { return v.u >> 48; }

int bv_is_double(bv v) {
	return !isnan(v.d);
}
int bv_is_nil(bv v) { return v.u == bv_nil; }
int bv_is_str(bv v) { return (v.u&bv_type_mask) == bv_str; }
int bv_is_sstr(bv v) { return (v.u&bv_type_mask) == bv_sstr; }

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

value box_u64(i64 v) {
	value boxed;
	boxed.i = bv_int | (v ? 1 : 0);
	return boxed;
}

value box_ptr(void *v) {

}

value box_nil() {
	bv boxed;
	boxed.u = bv_nil;
	return boxed;
}


double unbox_double(value v) {

}

int unbox_bool(value v) {

}

u64 unbox_u64(value v) {
	return v.u & bv_value_mask;
}

void *unbox_ptr(value v) {

}


int check_double(value v, double *out) {

}

int check_bool(value v, int *out) {

}

int check_u64(value v, i64 *out) {

}

int check_ptr(value v, void *out) {

}

int check_nil(value v) {

}

int check_none(value v) {

}


/* ops */
i64 bv_fast_cmp(bv a, bv b) { return a.i - b.i; }

bv bv_add(state *L, bv a, bv b) {
	//printf("ADD %.14g %.14g\n", a.d, b.d);
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
	printf("%.14g\n", v.d);
}

