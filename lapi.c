#include "lapi.h"

#include "cc.h"
#include "common.h"
#include "env.h"
#include "ir.h"
#include "lex.h"
#include "value.h"
#include "parser.h"
#include "rhhm.h"
#include "string.h"

#include <stdio.h>
#include <string.h>

/* gc */
#define gmalloc malloc
#define gfree free

struct gc_object {
	u64 ptr; // lsb == 1 -> broken heart
};

#define GC_INITIAL_SZ 1024
int gc_init(struct gc *gc) {
	gc->cap = GC_INITIAL_SZ;

	gc->from = gmalloc(gc->cap * 2 * sizeof(rhhm));
	if (!gc->from) return 1;
	gc->cur = gc->from;

	gc->to = gc->from + gc->cap;
	gc->writer = gc->reader = gc->to;

	return 0;
}

int gc_grow(state *L) {
	struct gc *gc = &L->gc;

	u64 diff = gc->cur - gc->from;

	rhhm *old = gc->to;
	if (gc->from < gc->to) old = gc->from;

	gc->to = gmalloc(gc->cap * 4 * sizeof(rhhm));
	if (!gc->to) return 1;
	gc->writer = gc->reader = gc->to;

	lua_gc(L);

	gc->cap *= 2;
	gc->from = gc->to + gc->cap;
	gc->cur = gc->from + diff;

	gfree(old);

	return 0;
}

void gc_destroy(struct gc *gc) {
	if (!gc) return;
	if (gc->from < gc->to) gfree(gc->from);
	else gfree(gc->to);
}

void *gc_new(struct gc *gc) {
	if (gc->cur == gc->from + gc->cap) {
		return NULL;
	}
	
	rhhm *t = gc->cur++;
	
	return t;
}

rhhm *gc_evacuate(struct gc *gc, rhhm *obj) {
	if (obj >= gc->from && obj < gc->from + gc->cap) { // move to tospace
		struct gc_object *gobj = (struct gc_object*)obj;
		if (gobj->ptr & 1) { // broken heart
			return (rhhm*)(gobj->ptr & 0xfffffffffffffffe);
		}
		*gc->writer = *obj;
		gobj->ptr = (((u64)gc->writer) | 1);
		return gc->writer++;
	}
	return obj;
}

void gc_scavenge(void *context, rhhm_value *value) {
	struct gc *gc = (struct gc*)context;

	if (bv_is_tbl(value->key))
		value->key = bv_make_tbl(
			gc_evacuate(gc, bv_get_tbl(value->key)));

	if (bv_is_tbl(value->value))
		value->value = bv_make_tbl(
			gc_evacuate(gc, bv_get_tbl(value->value)));
}

void gc_collect(struct gc *gc, rhhm *obj) {
#ifdef DBG
	printf("collect %p\n", obj);
#endif

	gc_evacuate(gc, obj);

	while (gc->reader < gc->writer) {
		rhhm *h = gc->reader++;
		rhhm_visit(h, gc, gc_scavenge);
	}
}

void gc_maybe_collect(struct gc *gc, void *root) {
	if (bv_is_tbl((bv)root))
		gc_collect(gc, bv_get_ptr((bv)root));
}

int gc_flip(struct gc *gc) {

	while (--gc->cur >= gc->from) {
		struct gc_object *gobj = (struct gc_object*)gc->cur;
		if ((gobj->ptr & 1) == 0) { // not forwarded
			rhhm_destroy(gc->cur);
		}
		//gobj->ptr = NULL;
	}

	u64 diff = gc->writer - gc->to;

	rhhm *from = gc->from;
	gc->from = gc->to;
	gc->to = from;

	gc->cur = gc->from + diff;
	gc->reader = gc->writer = gc->to;

	return 0;
}

// table
int table_set(table *t, bv k, bv v) {
	hm_set(t, k, v);
	return 0;
}

bv table_get(table *t, bv k) {
	return hm_get(t, k);
}


/* state */
#define INITIAL_OBJECT_POOL_SZ 1024
#define INTERN_POOL_INITIAL_SZ 256
#define G_INITIAL_SZ 256

void lua_destroy(state *L) {
	gc_destroy(&L->gc);
	rhhm_destroy(&L->intern_pool);
}

void lua_setglobal(state *L, bv key, bv value) {
	table_set(L->G, key, value);
}

bv lua_getglobal(state *L, bv key) {
	return table_get(L->G, key);
}

void lua_setfield(state *L, bv table, bv key, bv value) {
	table_set(bv_get_ptr(table), key, value);
}

bv lua_getfield(state *L, bv table, bv key) { // TODO: remove L
	return table_get(bv_get_ptr(table), key);
}

bv lua_intern(state *L, char *s, int len) {
	//printf(" _ intern _ %.*s : ", len, s);

	bv v;
	if (len > SSTR_MAX_LENGTH) {
		if (bv_is_nil(v = hm_sb_get_str(&L->intern_pool, s, len))) {
			void *str = str_new(s, len);
			v = bv_make_str(str);
			hm_sb_set(&L->intern_pool, v, v);
			//puts("<new>");
		} else {

			//puts("<existing>");
		}

	} else {

		//puts("<small string>");

		v = bv_make_sstr(s, len);
	}
	return v;
}

int lua_init_G(state *L) {
	rhhm *t = gc_new(&L->gc);
	if (!t) return 1;
	L->G = t;

	u32 hash = L->seed;
	u32 c = (u64)t^(((u64)t)>>32);
	L->seed = hash = ((hash << 5) + hash) + c;

	return rhhm_init(t, G_INITIAL_SZ, hash);
}

bv lua_newtable(state *L) {
	table *t = gc_new(&L->gc);
	if (!t) {
		// collect
		if (lua_gc(L)) { // no free space
			if (gc_grow(L)) { // no more mem
				return nil; // TODO: handle it
			}
		}
		t = gc_new(&L->gc);
		if (!t) { // err
			return nil; // TODO: handle it
		}
	}

	u32 hash = L->seed;
	u32 c = (u64)t^(((u64)t)>>32);
	L->seed = hash = ((hash << 5) + hash) + c;

	if (rhhm_init(t, 16, hash)) return nil;
	return bv_make_tbl(t);
}

typedef bv (*lua_function)(state*, int, bv*);
int lua_register(state *L, lua_function f, char *name) {
	lua_setglobal(L, lua_intern(L, name, strlen(name)), box_cfunction(f));
	return 0;
}

bv io_print(state *L, int nargs, bv *args) {
	for (int i = 0; i < nargs; i++) {
		if (i) printf("\t");
		bv_disp(args[i]);
	}
	puts("");
	return nil;
}

bv dbg_dbg(state *L, int nargs, bv *args) {

	u64 *top = ml_get_rsp();

	puts("~~~~~ debug - ini ~~~~~");

	printf("%p to %p : %ld\n", L->top, top, L->top - top);

	puts(" * top *");
	bv *p = (bv*)top;
	while ((u64*)p <= L->top) {
		printf("%02ld\t", p-(bv*)top);
		if ((*(state**)p) == L)
			printf("Lua state -----------------");
		else
			bv_disp(*p);
		p++;
		puts("");
	}

	puts("~~~~~ debug - end ~~~~~");

	return nil;
}

bv dbg_assert(state *L, int nargs, bv *args) {
	puts("assert");
	/*for (int i = 0; i < nargs; i++) {
		if (bv_is_nil(args[i])) {
			puts("* ASSERTION FAILED *");
		}
	}*/
	return nil;
}

bv sys_gc(state *L, int nargs, bv *args) {
	lua_gc(L);
	return nil;
}

int lua_init(state *L) {
	do {
		L->seed = 5381;

		if (gc_init(&L->gc)) break;
		if (!(L->G = gc_new(&L->gc))) break;
		if (rhhm_init(&L->intern_pool, INTERN_POOL_INITIAL_SZ, 0)) break;
		if (lua_init_G(L)) break;

		lua_register(L, io_print, "print");
		lua_register(L, dbg_dbg, "dbg");
		lua_register(L, dbg_assert, "assert");
		lua_register(L, dbg_assert, "ass");
		lua_register(L, sys_gc, "gc");

		return 0;
	} while (0);
	lua_destroy(L);
	return 1;
}

void gc_fix_ptr(struct gc *gc, bv *p) {
	if (bv_is_tbl(*p)) {
		rhhm *obj = bv_get_ptr(*p);
		if (obj >= gc->from && obj < gc->from + gc->cap) {
			struct gc_object *gobj = (struct gc_object*)obj;
			if (gobj->ptr & 1) { // broken heart
				rhhm *fwd = (rhhm*)(gobj->ptr & 0xfffffffffffffffe);
				*p = bv_make_tbl(fwd);
			}
		}
	}
}

int lua_gc_impl(state *L) {
	u64 *top = ml_get_rsp();

	//puts("*gc*");

	gc_collect(&L->gc, L->G);

	//printf("%p to %p : %d\n", L->top, top, L->top - top);

	u64 *p = top;
	while (p <= L->top) gc_maybe_collect(&L->gc, *p++);
	p = top;
	while (p <= L->top) gc_fix_ptr(&L->gc, p++);

	rhhm *fwd = (rhhm*)(((struct gc_object*)L->G)->ptr & 0xfffffffffffffffe);
	L->G = fwd;

	return gc_flip(&L->gc);
}

void lua_error(state *L) {
	longjmp(L->jmpbuf, 1);
}


void *lua_loadstring(state *L, char *s) {
	ir i;
	ir_init(&i);

	parser p;
	parser_init(&p, L, &i, s);

	prof_begin("parse");
	parse_chunk(&p);
	prof_end();

	return compile(p.c);
}

#define PAD_LEFT 2
#define PAD_RIGHT 1
void *lua_loadfile(state *L, char *filename) {
	prof_begin("IO");
	FILE *f = fopen(filename, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	int len = ftell(f);
	rewind(f);
	char *b = malloc(PAD_LEFT + len + PAD_RIGHT); // padding for lexical scope
	if (!b) {
		fclose(f);
		return NULL;
	}
	if (fread(b+PAD_LEFT, 1, len, f) != len) {
		fclose(f);
		free(b);
		return NULL;
	};
	fclose(f);
	prof_end();
	b[PAD_LEFT+len] = '\0';
	void *result = lua_loadstring(L, b+PAD_LEFT);
	free(b);
	return result;
}

typedef void (*fn)(state*);
int lua_pcall(state *L, void *f) {
	if (!L || !f) return 1;

	L->top = ml_get_rsp();
	
	if (setjmp(L->jmpbuf)) {
		puts(" * RUNTIME ERROR * ");
		return 1;
	}
	((fn)f)(L);

	lua_gc(L); // DEBUG ONLY

	return 0;
}
