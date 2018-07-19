#ifndef STRING_H
#define STRING_H

#include "common.h"

typedef struct {
	u32 sz;
	char data[4];
} str;

str *str_new(const char *s, u32 len);

#endif // STRING_H

