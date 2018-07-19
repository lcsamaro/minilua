#ifndef VALUE_H
#define VALUE_H

#include "common.h"

typedef struct state state;

typedef union {
	u64 u;
	double d;
	i64 i;
	void *p;
} bv;

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


#define SSTR_MAX_LENGTH 5

extern const bv nil;

bv bv_make_double(double d);
bv bv_make_bool(int b);
bv bv_make_str(void *p);
bv bv_make_tbl(void *p);
bv bv_make_ptr(void *p);
bv bv_make_sstr(const char *s, u32 len);

void *bv_get_ptr(bv v);
void *bv_get_ptr_clean(bv v);
u64 bv_get_u64(bv v);

u32 bv_type(bv v);

int bv_is_nil(bv v);
int bv_is_str(bv v);
int bv_is_sstr(bv v);

i64 bv_fast_cmp(bv a, bv b);

bv bv_add(state *L, bv a, bv b);
bv bv_mul(state *L, bv a, bv b);
bv bv_sub(state *L, bv a, bv b);
bv bv_div(state *L, bv a, bv b);
bv bv_pow(state *L, bv a, bv b);
bv bv_mod(state *L, bv a, bv b);
u64 bv_EQ(bv a, bv b);
u64 bv_NE(bv a, bv b);
u64 bv_LE(bv a, bv b);
u64 bv_LT(bv a, bv b);
u64 bv_GE(bv a, bv b);
u64 bv_GT(bv a, bv b);
bv bv_inc(bv a);
bv bv_dec(bv a);

void bv_disp(bv v);

#endif // VALUE_H

