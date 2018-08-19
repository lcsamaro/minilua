#include "rhhm.h"
#include "string.h"

#include <string.h>

#define RHHM_RESIZE_FILL_RATE_THRESHOLD 0.8
#define DISTANCE(p, h) (p >= h ? p-h : p + (hm->length - h))

static u32 djb2(u8 *key, u32 len) {
	u32 hash = 5381, i;
	for (i = 0; i < len; i++) {
		u32 c = key[i];
		hash = ((hash << 5) + hash) + c;
	}
	return hash;
}

int rhhm_value_empty(rhhm_value *v) {
	return v->value.u == bv_none;
}

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

void rhhm_visit(rhhm *hm, rhhm_visit_callback cb) {
	u32 i;
	for (i = 0; i < hm->length; i++)
		if (!rhhm_value_empty(hm->table + i))
			cb(hm->table + i);
}


// bv -> bv
u32 rhhm_bb_hash(bv v) {
	//return djb2((u8*)&v, sizeof(bv));
	return v.u ^ (v.u>>32);
}

int rhhm_bb_cmp(bv a, bv b) {
	//return a.u ^ b.u; //a.u == b.u ? 0 : 1;
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
int hm_sb_cmp(bv a, bv b) { // TODO: improve this -v-
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

bv hm_sb_get_str(rhhm *hm, const char *s, int len) {
	bv k;
	k.u = ((u64)len << 48) | (u64)s | 1;
	return hm_sb_get(hm, k);
}

