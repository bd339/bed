#ifndef BED_UTIL_H
#define BED_UTIL_H

#include <stddef.h>
#include <stdint.h>

#define sizeof(x)   (isize)sizeof(x)
#define countof(a)  (sizeof(a) / sizeof(*(a)))
#define lengthof(s) (countof(s) - 1)
#define alignof(x)  _Alignof(x)

typedef ptrdiff_t isize;
typedef uint8_t   u8;
typedef uint16_t  u16;

typedef struct {
	isize length;
	u8 *data;
} s8;

typedef struct {
	char *begin;
	char *end;
	isize offset;
} arena;

#define ALLOC_NOZERO 1

__attribute__((malloc, alloc_size(2, 4), alloc_align(3)))
void *arena_alloc(arena*, isize, isize, isize, int);
void *arena_alignas(void*, isize);

#define s8(s)           (s8){ lengthof(s), (u8*)(s) }
#define s8_append(s, x) (s)->data[(s)->length++] = (u8)(x)

s8 s8_clone(arena*, s8);

#ifdef NDEBUG
#define assert(c)
#else
#define assert(c) if(!(c)) __builtin_trap()
#endif

#endif // BED_UTIL_H
