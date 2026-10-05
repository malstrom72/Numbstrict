/*
	Numbstrict's own `assert.h` for the builds in this repository. The library includes "assert.h" so that a host
	project can supply its own. In release builds an assert is compiled but never evaluated, so names used only in
	asserts still count as used. Like the standard header, the macro is redefined on every include, so `NDEBUG` may
	change between includes.
*/
#ifndef NumbstrictAssert_h
#define NumbstrictAssert_h
#include <cstdio>
#include <cstdlib>
inline void numbstrictAssertFailure(const char* assertion, const char* file, int line) {
	std::fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", assertion, file, line);
	std::abort();
}
#endif

#undef assert
#if defined(NDEBUG)
	#define assert(a) ((void)(0 && (a)))
#else
	#define assert(a) ((a) ? (void)0 : numbstrictAssertFailure(#a, __FILE__, __LINE__))
#endif
