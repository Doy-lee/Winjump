#ifndef COMMON_H
#define COMMON_H

#include "stdint.h"

#define LOCAL_PERSIST static
#define GLOBAL_VAR    static
#define INTERNAL      static

typedef uint32_t u32;
typedef uint16_t u16;

typedef int32_t i32;
typedef int16_t i16;

typedef float f32;
typedef unsigned char u8;

#define INVALID_CODE_PATH 0

#define ARRAY_COUNT(array) (sizeof(array) / sizeof(array[0]))
#define ASSERT(expr) if (!(expr)) { (*((int *)0)) = 0; }

#endif
