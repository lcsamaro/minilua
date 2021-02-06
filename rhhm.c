#include "rhhm.h"
#include "string.h"

#include <string.h>

#define DISTANCE(p, h) (p >= h ? p-h : p + (hm->data->cap - h))

static u32 djb2(u8 *key, u32 len) {
	u32 hash = 5381, i;
	for (i = 0; i < len; i++) {
		u32 c = key[i];
		hash = ((hash << 5) + hash) + c;
	}
	return hash;
}

static int rhhm_is_initialized(rhhm *hm) {
	return (((u64)hm->data) & 0x2) ? 0 : 1;
}

static u32 rhhm_hash(rhhm *hm) {
	if (rhhm_is_initialized(hm))
		return hm->data->hash;
	return ((u64)hm->data) >> 32; 
}

static int rhhm_maybe_initialize(rhhm *hm) {
	if (rhhm_is_initialized(hm)) return 0;

	u32 cap = ((u64)hm->data) & 0xfffffffc;
	u32 hash = ((u64)hm->data) >> 32; 

	hm->data = ML_MALLOC(sizeof(rhhm_data) + (cap-1) * sizeof(rhhm_value));
	if (!hm->data) return 1;

	hm->data->cap = cap;
	hm->data->hash = hash;

	do hm->data->table[--cap].value.u = bv_none; while (cap);

	return 0;
}

static int rhhm_value_empty(rhhm_value *v) {
	return v->value.u == bv_none;
}

// length must be a power of two, also >= 4
int rhhm_init(rhhm *hm, u32 length, u32 hash) {
	hm->data = (rhhm_data*)((((u64)hash) << 32) | length | 0x2);
	return 0;
}

void rhhm_destroy(rhhm *hm) {
	if (rhhm_is_initialized(hm)) ML_FREE(hm->data);
}

#define ENTRY_HASH(e) (hfn(e.key) & (hm->data->cap - 1))
void rhhm_set(rhhm *hm, rhhm_hash_fn hfn, rhhm_cmp_fn cfn, bv key, bv value) {
	if (rhhm_maybe_initialize(hm)) return;

	rhhm_value entry, tmp;
	entry.key = key;
	entry.value = value;
	u32 i = ENTRY_HASH(entry);
	u32 entry_hash = i;
	while (!rhhm_value_empty(hm->data->table+i) &&
		DISTANCE(i, entry_hash) <= DISTANCE(i, ENTRY_HASH(hm->data->table[i]))) {
		if (ENTRY_HASH(hm->data->table[i]) == entry_hash &&
			!cfn(hm->data->table[i].key, key)) {
			hm->data->table[i].value = entry.value;
			return;
		}
		i = ((i+1)&(hm->data->cap-1));
	}
	tmp = entry;
	entry = hm->data->table[i];
	hm->data->table[i] = tmp;

	while (!rhhm_value_empty(&entry)) {
		u32 i = entry_hash = ENTRY_HASH(entry);
		while (!rhhm_value_empty(hm->data->table+i) &&
			DISTANCE(i, entry_hash) <= DISTANCE(i, ENTRY_HASH(hm->data->table[i]))) {
			//if (++i >= hm->length) i = 0;
			i = ((i+1)&(hm->data->cap-1));
		}
		tmp = entry;
		entry = hm->data->table[i];
		hm->data->table[i] = tmp;
	}
}
#include <stdio.h>
bv rhhm_get(rhhm *hm, rhhm_hash_fn hfn, rhhm_cmp_fn cfn, bv key) {
	if (!rhhm_is_initialized(hm)) return nil;

	u32 i, h; i = h = hfn(key) & (hm->data->cap-1);
	while (!rhhm_value_empty(hm->data->table+i)) { // TODO: correctly return none
		if (bv_is_nil(hm->data->table[i].value)) return nil; // TODO: check this
		if (DISTANCE(i, ENTRY_HASH(hm->data->table[i])) < DISTANCE(i, h)) return nil;
		if (!cfn(hm->data->table[i].key, key)) return hm->data->table[i].value;
		//if (++i >= hm->length) i = 0;
		i = ((i+1)&(hm->data->cap-1));
	}
	return nil;
}

void rhhm_remove(rhhm *hm, rhhm_hash_fn hfn, rhhm_cmp_fn cfn, bv key) {
	if (!rhhm_is_initialized(hm)) return;

	u32 i, h; i = h = hfn(key) & (hm->data->cap-1);
	while (!rhhm_value_empty(hm->data->table+i)) {
		if (!cfn(hm->data->table[i].key, key)) {
			u32 j = i;
			do {
				j = ((j+1)&(hm->data->cap-1));
				hm->data->table[i] = hm->data->table[j];
				i = j;
			} while (!rhhm_value_empty(hm->data->table+j) &&
				DISTANCE(j, ENTRY_HASH(hm->data->table[j])) != 0);
			return;
		}
		if (ENTRY_HASH(hm->data->table[i]) < h) return;
		i = ((i+1)&(hm->data->cap-1));
	}
}

void rhhm_visit(rhhm *hm, void *context, rhhm_visit_callback cb) {
	if (!rhhm_is_initialized(hm)) return;

	u32 i;
	for (i = 0; i < hm->data->cap; i++)
		if (!rhhm_value_empty(hm->data->table + i))
			cb(context, hm->data->table + i);
}


// bv -> bv
u32 rhhm_bb_hash(bv v) {
	// TODO: better hashing
	return bv_is_tbl(v) ? rhhm_hash(bv_get_tbl(v)) : v.u ^ (v.u>>32);
}

int rhhm_bb_cmp(bv a, bv b) {
	return a.u == b.u ? 0 : 1;
}


// string -> int
int hm_key_cmp(bv a, bv b) {
	int alen = a.u >> 48;
	int blen = b.u >> 48;
	if (alen != blen) return 1;
	return memcmp((void*)(a.u&bv_value_mask), (void*)(b.u&bv_value_mask), alen);
}

u32 hm_key_hash(bv a) {
	int alen = a.u >> 48;

	// TODO: better hash for big strings

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

void rhhm_remove_str(rhhm *hm, const char *s, int len) {
	bv k;
	k.u = ((u64)len << 48) | (u64)s;
	rhhm_remove(hm, hm_key_hash, hm_key_cmp, k);
}

void rhhm_insert_cstr(rhhm *hm, const char *s, int val) {
	rhhm_insert_str(hm, s, strlen(s), val);
}


// interning hash impl

typedef struct {
	u32   len;
	char *data;
} istr;

int hm_sb_cmp(bv a, bv b) { // TODO: improve this -v-
	if (!(a.u&1) && !(b.u&1)) return a.u == b.u ? 0 : 1;

	int alen = a.u & 1 ? ((istr*)(a.u & ~1ULL))->len : ((str*)bv_get_ptr(a))->sz;
	int blen = b.u & 1 ? ((istr*)(b.u & ~1ULL))->len : ((str*)bv_get_ptr(b))->sz;

	if (alen != blen) return 1;

	void *sa = a.u & 1 ? ((istr*)(a.u & ~1ULL))->data : ((str*)bv_get_ptr(a))->data;
	void *sb = b.u & 1 ? ((istr*)(b.u & ~1ULL))->data : ((str*)bv_get_ptr(b))->data;
	return memcmp(sa, sb, alen);
}

u32 hm_sb_hash(bv a) {
	int alen = a.u & 1 ? ((istr*)(a.u & ~1ULL))->len : ((str*)bv_get_ptr(a))->sz;
	void *sa = a.u & 1 ? ((istr*)(a.u & ~1ULL))->data : ((str*)bv_get_ptr(a))->data;
	// TODO: better hash for big strings
	return djb2(sa, alen < 32 ? alen : 32);
}

bv hm_sb_get_str(rhhm *hm, char *s, int len) {
	bv k;
	istr is;
	is.len = len;
	is.data = s;
	k.u = (u64)(&is) | 1;
	return hm_sb_get(hm, k);
}

