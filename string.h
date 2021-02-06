#ifndef STRING_H
#define STRING_H

#include "common.h"

typedef struct str {
	struct str *next;
	u32 sz;
	char data[1];
} str;

str *str_new(const char *s, u32 len);
void str_delete(str *s);

#endif // STRING_H

