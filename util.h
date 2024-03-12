#ifndef BED_UTIL_H
#define BED_UTIL_H

#include <stddef.h>
#include <stdint.h>

#define sizeof(x)   (isize)sizeof(x)
#define countof(a)  (sizeof(a) / sizeof(*(a)))
#define lengthof(s) (countof(s) - 1)
#define alignof(x)  _Alignof(x)

typedef ptrdiff_t isize;
typedef int32_t   b32;

typedef struct {
	isize length;
	char *data;
} s8;

typedef struct {
	char *begin;
	char *end;
	isize offset;
} arena;

#define ALLOC_NOZERO  1
#define ALLOC_RETNULL 2

__attribute__((alloc_size(2, 4), alloc_align(3)))
void *arena_alloc(arena*, isize, isize, isize, int);
void *arena_alignas(void*, isize);

#define s8(s)           (s8){ lengthof(s), (s) }
#define s8_append(s, x) (s)->data[(s)->length++] = (char)(x)

#ifdef NDEBUG
#define assert(c)
#else
#define assert(c) if(!(c)) __builtin_trap()
#endif

void slice_grow(void*, isize);

#define push(s) ({                                                                                 \
	typeof(s) _s = s;                                                                              \
	if(_s->length >= _s->capacity) {                                                               \
		slice_grow(_s, sizeof(*_s->data));                                                         \
	}                                                                                              \
	_s->data + _s->length++;                                                                       \
})

#endif // BED_UTIL_H
