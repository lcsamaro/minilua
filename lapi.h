#ifndef LAPI_H
#define LAPI_H

#include "common.h"
#include "value.h"
#include "rhhm.h"

#include <setjmp.h>

typedef rhhm table;

struct gc {
	rhhm *from;
	rhhm *to;

	rhhm *cur;

	rhhm *writer;
	rhhm *reader;

	u64 cap;
};

int gc_init(struct gc *gc);
void gc_destroy(struct gc *gc);
int gc_resize(struct gc *gc, u64 sz);

typedef struct state {
	// pcall vars
	jmp_buf jmpbuf;
	u64 *top;

	// global table
	rhhm *G;

	// string interning
	rhhm intern_pool;

	// GC
	struct gc gc;

	// table 'hashing'
	u32 seed;
} state;

bv table_get(table *t, bv k);

int table_set(table *t, bv k, bv v);

void lua_destroy(state *L);



void lua_setglobal(state *L, bv key, bv value);

bv lua_getglobal(state *L, bv key);

void lua_setfield(state *L, bv table, bv key, bv value);

bv lua_getfield(state *L, bv table, bv key);

bv lua_intern(state *L, char *s, int len);
bv lua_newtable(state *L);
typedef bv (*lua_function)(state*, int, bv*);
int lua_register(state *L, lua_function f, char *name);

bv io_print(state *L, int nargs, bv *args);

int lua_init(state *L);


void lua_error(state *L);


void *lua_loadstring(state *L, char *s);

void *lua_loadfile(state *L, char *filename);
typedef void (*fn)(state*);
int lua_pcall(state *L, void *f);
int lua_gc(state *L);

#endif // LAPI_H
