#ifndef STATE_H
#define STATE_H

#include "value.h"

typedef struct state state;
bv lua_intern(state *L, const char *s, int len);
void lua_error(state *L);

#endif // STATE_H

