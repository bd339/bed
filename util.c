#include "util.h"

#include <string.h>

void*
arena_alloc(arena *arena, isize size, isize align, isize count, int flags) {
	char *p = arena_alignas(arena->begin + arena->offset, align);
	assert(count <= (arena->end - p) / size);
	isize total = size * count;
	arena->offset = p - arena->begin + total;
	return flags & ALLOC_NOZERO ? p : memset(p, 0, (size_t)total);
}

void*
arena_alignas(void *ptr, isize align) {
	uintptr_t padding = -(uintptr_t)ptr & (uintptr_t)(align - 1);
	return (char*)ptr + padding;
}

void
s8_reverse(s8 *s) {
	char *i = s->data;
	char *j = s->data + s->length - 1;

	while(i < j) {
		char tmp = *i;
		*i++ = *j;
		*j-- = tmp;
	}
}

#ifdef TEST

#define CLOVE_SUITE_NAME util
#include "clove-unit.h"

CLOVE_TEST(s8_reverse) {
	char empty[] = "";
	s8 empty_s8 = s8(empty);
	s8_reverse(&empty_s8);
	CLOVE_STRING_EQ("", empty);

	char a[] = "a";
	s8 a_s8 = s8(a);
	s8_reverse(&a_s8);
	CLOVE_STRING_EQ("a", a);

	char ab[] = "ab";
	s8 ab_s8 = s8(ab);
	s8_reverse(&ab_s8);
	CLOVE_STRING_EQ("ba", ab);

	char hej[] = "hej";
	s8 hej_s8 = s8(hej);
	s8_reverse(&hej_s8);
	CLOVE_STRING_EQ("jeh", hej);

	char involution[] = "involution";
	s8 involution_s8 = s8(involution);
	s8_reverse(&involution_s8);
	s8_reverse(&involution_s8);
	CLOVE_STRING_EQ("involution", involution);
}

#endif // TEST
