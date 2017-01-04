#ifndef COMMON_H
#define COMMON_H

#include "stdint.h"

#define LOCAL_PERSIST static
#define GLOBAL_VAR    static
#define INTERNAL      static

typedef uint32_t u32;
typedef uint16_t u16;

typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;

typedef float f32;
typedef unsigned char u8;

#define INVALID_CODE_PATH 0

#define ARRAY_COUNT(array) (sizeof(array) / sizeof(array[0]))
#define ASSERT(expr) if (!(expr)) { (*((int *)0)) = 0; }

#define MATH_ABS(x) (((x) < 0) ? (-(x)) : (x))

typedef union v2 {
	struct { f32 x, y; };
	struct { f32 w, h; };
} v2;

v2 V2(f32 x, f32 y)
{
	v2 result = {};
	result.x  = x;
	result.y  = y;

	return result;
}

v2 V2i(i32 x, i32 y)
{
	v2 result = V2((f32)x, (f32)y);
	return result;
}

i32 common_strcmp(const char *a, const char *b)
{
	while ((*a) == (*b))
	{
		if (!(*a)) return 0;
		a++;
		b++;
	}

	return (((*a) < (*b)) ? -1 : 1);
}

i32 common_wstrcmp(const wchar_t *a, const wchar_t *b)
{
	while ((*a) == (*b))
	{
		if (!(*a)) return 0;
		a++;
		b++;
	}

	return (((*a) < (*b)) ? -1 : 1);
}


i32 common_strlen(const char *a)
{
	i32 result = 0;
	while ((*a))
	{
		result++;
		a++;
	}

	return result;
}


#endif
