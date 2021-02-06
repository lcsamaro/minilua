#ifndef RHHM_H
#define RHHM_H

#include "common.h"
#include "value.h"

/*
 * Robin Hood Hashmap
 * TODO: resizing
 */
typedef struct {
	bv value;
	bv key;
} rhhm_value;

typedef struct rhhm_data {
	u32 cap;
	u32 hash;
	rhhm_value table[1];
} rhhm_data;

typedef struct rhhm {
	rhhm_data *data; // lsb reserved for GC
} rhhm;

typedef u32 (*rhhm_hash_fn)(bv);
typedef int (*rhhm_cmp_fn)(bv, bv);

int  rhhm_init(rhhm *hm, u32 length, u32 hash);
void rhhm_destroy(rhhm *hm);

void rhhm_set(rhhm *hm, rhhm_hash_fn hfn, rhhm_cmp_fn cfn, bv key, bv value);
bv   rhhm_get(rhhm *hm, rhhm_hash_fn hfn, rhhm_cmp_fn cfn, bv key);
void rhhm_remove(rhhm *hm, rhhm_hash_fn hfn, rhhm_cmp_fn cfn, bv key);


typedef void(*rhhm_visit_callback)(void *context, rhhm_value *value);
void rhhm_visit(rhhm *hm, void *context, rhhm_visit_callback cb);


// bv -> bv
u32 rhhm_bb_hash(bv v);
int rhhm_bb_cmp(bv a, bv b);

#define hm_set(h,k,v)  rhhm_set   (h, rhhm_bb_hash, rhhm_bb_cmp, k, v)
#define hm_get(h,k)    rhhm_get   (h, rhhm_bb_hash, rhhm_bb_cmp, k)
#define hm_remove(h,k) rhhm_remove(h, rhhm_bb_hash, rhhm_bb_cmp, k)

// string -> int
int hm_key_cmp(bv a, bv b);
u32 hm_key_hash(bv a);
void rhhm_insert_str(rhhm *hm, const char *s, int len, int val);
int rhhm_get_str(rhhm *hm, const char *s, int len);
void rhhm_remove_str(rhhm *hm, const char *s, int len);
void rhhm_insert_cstr(rhhm *hm, const char *s, int val);

// interning hash impl
int hm_sb_cmp(bv a, bv b);
u32 hm_sb_hash(bv a);

#define hm_sb_set(h,k,v)  rhhm_set   (h, hm_sb_hash, hm_sb_cmp, k, v)
#define hm_sb_get(h,k)    rhhm_get   (h, hm_sb_hash, hm_sb_cmp, k)
#define hm_sb_remove(h,k) rhhm_remove(h, hm_sb_hash, hm_sb_cmp, k)

bv hm_sb_get_str(rhhm *hm, char *s, int len);

#endif // RHHM_H

