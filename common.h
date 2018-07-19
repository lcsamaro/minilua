#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <stdint.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t  i8;

#define ML_MALLOC  malloc
#define ML_REALLOC realloc
#define ML_FREE    free

#define BITFIELD(name, sz) u32 name[(sz) / 32 + 1] = {0};
#define BITFIELD_SET(name, i) (name[i / 32] |= (1 << (i % 32)))
#define BITFIELD_GET(name, i) (name[i / 32] & (1 << (i % 32)))
#define BITFIELD_ZERO(name) memset(name, 0, sizeof name)

#endif // COMMON_H

