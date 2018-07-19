#ifndef VEC_H
#define VEC_H

// TODO

typedef struct { bv *data; u32 cap; u32 sz; } vec;
int vec_maybe_resize(vec *v, u32 c) {
	if (c <= v->cap) return 0;
	u32 n = 8; bv *d;
	while (n < c) n *= 2;
	d = ML_REALLOC(v->data, n*sizeof(bv));
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
void vec_destroy(vec *v) { if (v) ML_FREE(v->data); }
int vec_set(vec *v, u32 i, bv val) {
	if (vec_maybe_resize(v, i)) return 1;
	if (i >= v->sz) v->sz = i+1;
	v->data[i] = val;
	return 0;
}
bv vec_at(vec *v, u32 i) { return i < v->sz ? v->data[i] : nil; }

#endif // VEC_H

