#include "string.h"

#include <string.h>

str *str_new(const char *s, u32 len) {
	str *o = ML_MALLOC(sizeof(str)+len);
	if (o) {
		o->sz = len;
		memcpy(o->data, s, len);
	}
	return o;
}

