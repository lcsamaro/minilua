#include "string.h"

#include <string.h>

#define smalloc malloc
#define sfree free

str *str_new(const char *s, u32 len) {
	str *o = smalloc(sizeof(str)+len);
	if (o) {
		o->sz = len;
		memcpy(o->data, s, len);
	}
	return o;
}

void str_delete(str *s) {
	sfree(s);
}
