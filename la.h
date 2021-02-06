#ifndef LA_H
#define LA_H

#define LA_DEBUG
#define LA_IMPL

/* typedefs */
#include <stdint.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t  i8;

/* comparison */
#include <string.h>

#define eq_int(l, r) (l == r ? 1 : 0)
#define eq_u64 eq_int
#define eq_u32 eq_int
#define eq_u16 eq_int
#define eq_u8  eq_int
#define eq_i64 eq_int
#define eq_i32 eq_int
#define eq_i16 eq_int
#define eq_i8  eq_int

#define eq_str(l, r) (l == r ? 1 : !strcmp(l, r))

#define cmp_u64(l, r) (l-r) // TODO
#define cmp_u32(l, r) (l-r) // TODO
#define cmp_u16(l, r) (l-r) // TODO
#define cmp_u8 (l, r) (l-r) // TODO
#define cmp_i64(l, r) (l-r)
#define cmp_i32(l, r) (l-r)
#define cmp_i16(l, r) (l-r)
#define cmp_i8 (l, r) (l-r)

#define cmp_str(l, r) (strcmp(l, r))

#define hash_u64(a) (a) // TODO
#define hash_u32(a) (a) // TODO
#define hash_u16(a) (a) // TODO
#define hash_u8 (a) (a) // TODO
#define hash_i64(a) (a) // TODO
#define hash_i32(a) (a) // TODO
#define hash_i16(a) (a) // TODO
#define hash_i8 (a) (a) // TODO

#define hash_str(a) (a) // TODO

/* data structures */
#ifndef la_malloc
#define la_malloc malloc
#endif

#ifndef la_free
#define la_free   free
#endif

#ifndef la_sz
#define la_sz u32
#endif

#ifndef la_initial_cap
#define la_initial_cap 8
#endif

/* vector */
#define v_def_impl(Name, T)                           \
                                                      \
la_sz Name##_size(Name *o) {                          \
	return o->size;                               \
}                                                     \
                                                      \
int Name##_empty(Name *o) {                           \
	return o->size == 0;                          \
}                                                     \
                                                      \
int Name##_full(Name *o) {                            \
	return o->size == Name##_capacity(o);         \
}                                                     \
                                                      \
void Name##_clear(Name *o) {                          \
	o->size = 0;                                  \
}                                                     \
                                                      \
int Name##_push(Name *o, T v) {                       \
	if (Name##_maybegrow(o)) return 1;            \
	o->data[o->size++] = v;                       \
	return 0;                                     \
}                                                     \
                                                      \
T Name##_pop(Name *o) {                               \
	return o->data[--(o->size)];                  \
}                                                     \
                                                      \
T Name##_get(Name *o, la_sz i) {                      \
	return o->data[i];                            \
}                                                     \
                                                      \
void Name##_set(Name *o, la_sz i, T v) {              \
	o->data[i] = v;                               \
}                                                     \
                                                      \
void Name##_dummy()

/* static vector */
#define sv_def(Name, T, Capacity)           \
typedef struct Name {                       \
	la_sz size;                         \
	T data[Capacity];                   \
} Name;                                     \
int Name##_init(Name *o) {                  \
	o->size = 0;                        \
	return 0;                           \
}                                           \
la_sz Name##_capacity(Name *o) {            \
	return Capacity;                    \
}                                           \
int Name##_maybegrow(Name *o) { return 0; } \
v_def_impl(Name, T)

/* dynamic vector */
#define cv_def(Name, T, Capacity, Alloc, Free)             \
typedef struct Name {                                      \
	la_sz capacity;                                    \
	la_sz size;                                        \
	T *data;                                           \
} Name;                                                    \
int Name##_init(Name *o) {                                 \
	o->capacity = Capacity;                            \
	o->size = 0;                                       \
	o->data = Alloc(Capacity * sizeof(T));             \
	return o->data == NULL;                            \
}                                                          \
void Name##_destroy(Name *o) {                             \
	Free(o->data);                                     \
}                                                          \
la_sz Name##_capacity(Name *o) {                           \
	return o->capacity;                                \
}                                                          \
int Name##_maybegrow(Name *o) {                            \
	la_sz i, n;                                        \
	T *data;                                           \
	if (o->size == o->capacity) {                      \
		n = o->capacity * 2;                       \
		data = Alloc(n * sizeof(T));               \
		if (!data) return 1;                       \
		for (i = 0; i < o->capacity; i++)          \
			data[i] = o->data[i];              \
		Free(o->data);                             \
		o->data = data;                            \
		o->capacity = n;                           \
	}                                                  \
	return 0;                                          \
}                                                          \
v_def_impl(Name, T)

#define v_def(Name, T) cv_def(Name, T, la_initial_cap, la_malloc, la_free)

/* hashmap - rhhm */
#define h_distance(p, h) ((p) >= (h) ? (p)-(h) : (p) + (cap - (h)))

#define h_def_impl(Name, K, V, Hash, Eq, Nil)                       \
la_sz Name##_size(Name *o) {                                        \
	return o->size;                                             \
}                                                                   \
void Name##_clear(Name *o) {                                        \
	o->size = 0;                                                \
	for (la_sz i = 0; i < Name##_capacity(o); i++)              \
		o->data[i].value = Nil;                             \
}                                                                   \
int Name##_empty(Name *o) {                                         \
	return o->size == 0;                                        \
}                                                                   \
int Name##_full(Name *o) {                                          \
	return o->size == Name##_capacity(o);                       \
}                                                                   \
V Name##_get(Name *o, K k) {                                        \
	u32 cap = Name##_capacity(o);                               \
	u32 mask = (cap-1);                                         \
	u32 i, h;                                                   \
	i = h = Hash(k) & mask;                                     \
	while (o->data[i].value != Nil) {                           \
		if (h_distance(i, Hash(o->data[i].key)&mask) <      \
			h_distance(i, h)) return Nil;               \
		if (Eq(o->data[i].key, k))                          \
			return o->data[i].value;                    \
		i = ((i+1)&mask);                                   \
	}                                                           \
	return Nil;                                                 \
}                                                                   \
int Name##_set(Name *o, K key, V value) {                           \
	u32 cap = Name##_capacity(o);                               \
	u32 mask = (cap-1);                                         \
	                                                            \
	Name##_entry entry, tmp;                                    \
	entry.key = key;                                            \
	entry.value = value;                                        \
	u32 i = Hash(key)&mask;                                     \
	u32 entry_hash = i;                                         \
	while (o->data[i].value != Nil &&                           \
		h_distance(i, entry_hash) <=                        \
		h_distance(i, Hash(o->data[i].key)&mask)) {         \
                                                                    \
		if ((Hash(o->data[i].key)&mask) == entry_hash &&    \
			Eq(o->data[i].key, key)) {                  \
			o->data[i].value = entry.value;             \
			return 0;                                   \
		}                                                   \
		i = ((i+1)&mask);                                   \
	}                                                           \
	tmp = entry;                                                \
	entry = o->data[i];                                         \
	o->data[i] = tmp;                                           \
                                                                    \
	while (entry.value != Nil) {                                \
		u32 i = entry_hash = Hash(entry.key)&mask;          \
		while (o->data[i].value != Nil &&                   \
			h_distance(i, entry_hash) <=                \
			h_distance(i, Hash(o->data[i].key)&mask)) { \
			i = ((i+1)&mask);                           \
		}                                                   \
		tmp = entry;                                        \
		entry = o->data[i];                                 \
		o->data[i] = tmp;                                   \
	}                                                           \
	return 0;                                                   \
}                                                                   \
V Name##_remove(Name *o, K k) {                                     \
	return (V)0;                                                \
}                                                                   \
void Name##_dummy()


/* static */
#define sh_def(Name, K, V, Hash, Eq, Nil, Capacity)       \
typedef struct Name##_entry {                             \
	K key;                                            \
	V value;                                          \
} Name##_entry;                                           \
typedef struct Name {                                     \
	la_sz size;                                       \
	Name##_entry data[Capacity];                      \
} Name;                                                   \
int Name##_init(Name *o) {                                \
	o->size = 0;                                      \
	for (la_sz i = 0; i < Capacity; i++) {            \
		o->data[i].value = Nil;                   \
		o->data[i].key = Nil;                     \
	}                                                 \
	return 0;                                         \
}                                                         \
la_sz Name##_capacity(Name *o) {                          \
	return Capacity;                                  \
}                                                         \
int Name##_maybegrow(Name *o) {                           \
	return 0;                                         \
}                                                         \
h_def_impl(Name, K, V, Hash, Eq, Nil)


/* dynamic */
#define ch_def(Name, K, V, Hash, Eq, Nil, Capacity, Alloc, Free)     \
typedef struct Name##_entry {                                        \
	K key;                                                       \
	V value;                                                     \
} Name##_entry;                                                      \
typedef struct Name {                                                \
	la_sz capacity;                                              \
	la_sz size;                                                  \
	Name##_entry *data;                                          \
} Name;                                                              \
int Name##_init(Name *o) {                                           \
	o->capacity = Capacity;                                      \
	o->size = 0;                                                 \
	o->data = Alloc(sizeof(Name##_entry) * Capacity);            \
	if (!o->data) return 1;                                      \
	for (la_sz i = 0; i < Capacity; i++) o->data[i].value = Nil; \
	return 0;                                                    \
}                                                                    \
void Name##_destroy(Name *o) {                                       \
	Free(o->data);                                               \
}                                                                    \
la_sz Name##_capacity(Name *o) {                                     \
	return o->capacity;                                          \
}                                                                    \
int Name##_maybegrow(Name *o) {                                      \
	return 0;                                                    \
}                                                                    \
h_def_impl(Name, K, V, Hash, Eq, Nil)


/* deque */


/* heap */


/* map - avl */


#endif /* LA_H */

