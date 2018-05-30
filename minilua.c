#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef uint64_t u64; typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8;
typedef int64_t i64; typedef int32_t i32; typedef int16_t i16; typedef int8_t i8;


/*
 * Boxed Value: NaN tagging, assumes little endian
 */
typedef union { u64 u; double d; int i; void *p; } bv;
#define bv_value_mask UINT64_C(0x0000ffffffffffff)
#define bv_none       UINT64_C(0x7ff7000000000000)
#define bv_nil        UINT64_C(0x7ff8000000000000)
#define bv_ptr        UINT64_C(0x7ff9000000000000)
#define bv_str        UINT64_C(0x7ffa000000000000)
#define bv_tbl        UINT64_C(0x7ffb000000000000)
/*#define bv_symbol UINT64_C(0x7ffe000000000000)*/
const bv nil  = { bv_nil };
#define GCd u8 mark

u32 bv_type(bv v) {
	return v.u >> 48;
}

int bv_isnil(bv v) {
	return v.u == bv_nil;
}

bv bv_mul(bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = a.d * b.d;
	return r;
}

bv bv_add(bv a, bv b) {
	bv r = nil;
	if (!isnan(a.d) && !isnan(b.d)) r.d = a.d + b.d;
	return r;
}

/*
 * String - simple string
 */
typedef struct { char *data; u32 cap; u32 len; GCd; } str;

int str_maybe_resize(str *s, u32 c) {
	if (c <= s->cap) return 0;
	u32 n = 8; char *d;
	while (n < c) n *= 2;
	d = realloc(s->data, n);
	if (!d) return 1;
	s->data = d; s->cap = n;
	return 0;
}

/*
 * Vector - simple vector
 */
typedef struct { bv *data; u32 cap; u32 sz; } vec;
int vec_maybe_resize(vec *v, u32 c) {
	if (c <= v->cap) return 0;
	u32 n = 8; bv *d;
	while (n < c) n *= 2;
	d = realloc(v->data, n*sizeof(bv));
	if (!d) return 1;
	for (c = v->cap; c < n; c++) d[c] = nil;
	v->data = d; v->cap = n;
	return 0;
}
int vec_init(vec *v, u32 c) {
	v->data = NULL; v->cap = v->sz = 0;
	if (vec_maybe_resize(v, c)) return 1;
	return 0;
}
void vec_destroy(vec *v) { if (v) free(v->data); }
int vec_set(vec *v, u32 i, bv val) {
	if (vec_maybe_resize(v, i)) return 1;
	if (i >= v->sz) v->sz = i+1;
	v->data[i] = val;
	return 0;
}
bv vec_at(vec *v, u32 i) { return i < v->sz ? v->data[i] : nil; }

/*
 * Hashmap - Robin Hood Hashmap (open addressing)
 * TODO: resizing + incremental resizing
 */
typedef struct { const u8 *key; u32 len; } hm_key;
typedef struct { bv value; hm_key key; u32 hash; } rhhm_value;
typedef struct { rhhm_value *table; u32 length; } rhhm;

int hm_key_cmp(hm_key *a, hm_key *b) {
	if (a->len != b->len) return 1;
	return memcmp(a->key, b->key, a->len);
}

u32 djb2(hm_key* k) {
	u32 hash = 5381, i;
	for (i = 0; i < k->len; i++) {
		u32 c = k->key[i];
		hash = ((hash << 5) + hash) + c;
	}
	return hash;
}

#define DISTANCE(p, h) (p >= h ? p-h : p + (hm->length - h))
int rhhm_value_empty(rhhm_value *v) { return v->value.u == bv_none; }

int rhhm_init(rhhm *hm, u32 length) {
	hm->length = length;
	hm->table = malloc(hm->length * sizeof(rhhm_value));
	if (!hm->table) return 1;
	do hm->table[--length].value.u = bv_none; while (length);
	return 0;
}

void rhhm_destroy(rhhm *hm) { if (hm) free(hm->table); }

void rhhm_insert(rhhm *hm, hm_key *key, bv value) {
	rhhm_value entry, tmp;
	entry.key = *key;
	entry.value = value;
	u32 i = entry.hash = djb2(key) % hm->length;
	while (!rhhm_value_empty(hm->table+i) &&
		DISTANCE(i, entry.hash) <= DISTANCE(i, hm->table[i].hash)) {
		if (hm->table[i].hash == entry.hash &&
			!hm_key_cmp(&hm->table[i].key, key)) {
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

bv rhhm_get(rhhm *hm, hm_key *key) {
	u32 i, h; i = h = djb2(key) % hm->length;
	while (!rhhm_value_empty(hm->table+i)) {
		if (bv_isnil(hm->table[i].value)) return nil;
		if (DISTANCE(i, hm->table[i].hash) < DISTANCE(i, h)) return nil;
		if (!hm_key_cmp(&hm->table[i].key, key)) return hm->table[i].value;
		if (++i >= hm->length) i = 0;
	}
	return nil;
}

void rhhm_remove(rhhm *hm, hm_key *key) {
	u32 i, h; i = h = djb2(key) % hm->length;
	while (!rhhm_value_empty(hm->table+i)) {
		if (!hm_key_cmp(&hm->table[i].key, key)) {
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

typedef void(*rhhm_visit_callback)(const rhhm_value *value);
void rhhm_visit(rhhm *hm, rhhm_visit_callback cb) {
	u32 i;
	for (i = 0; i < hm->length; i++)
		if (!rhhm_value_empty(hm->table + i))
			cb(hm->table + i);
}

// TODO

