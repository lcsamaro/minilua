#ifndef COMMON_H
#define COMMON_H

/* integer typedefs */
#include <stdint.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t  i8;

/* memory */
#include <stdlib.h>

#define ML_MALLOC  malloc
#define ML_REALLOC realloc
#define ML_FREE    free

/* bitset */
#include <string.h>

#define BSET(name, sz) u32 name[(sz) / 32 + 1] = {0};
#define BSET_SET(name, i) (name[i / 32] |= (1 << (i % 32)))
#define BSET_GET(name, i) (name[i / 32] & (1 << (i % 32)))
#define BSET_ZERO(name) memset(name, 0, sizeof name)

/* profiling */
#define ML_MEASURING 0

#if ML_MEASURING
	void prof_begin(const char *tag);
	void prof_end();
	void prof_results();
#else
	#define prof_begin(a) do {} while (0)
	#define prof_end() do {} while (0)
	#define prof_results() do {} while (0)
#endif

/* debug */
//#define DBG

#endif // COMMON_H

