/*
 * Minimal test harness — no external deps, no allocations in hot paths.
 *
 * Pattern per test binary:
 *
 *   #include "harness.h"
 *   TEST(foo)                  { CHECK(1 + 1 == 2); }
 *   TEST(bar)                  { CHECK_EQ_INT(3, add(1, 2)); }
 *   int main(void) { RUN_TESTS(foo, bar); }
 *
 * Reports pass/fail counts and returns nonzero if any test failed.
 *
 * This is free and unencumbered software released into the public domain.
 */

#ifndef BITCHAT_TEST_HARNESS_H
#define BITCHAT_TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *name; int fails; } bc_test_state_t;

static bc_test_state_t _bc_cur;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL %s:%d in %s: %s\n", \
                __FILE__, __LINE__, _bc_cur.name, #cond); \
        _bc_cur.fails++; \
    } \
} while (0)

#define CHECK_EQ_INT(expected, actual) do { \
    long long _e = (long long)(expected), _a = (long long)(actual); \
    if (_e != _a) { \
        fprintf(stderr, "  FAIL %s:%d in %s: expected %lld, got %lld\n", \
                __FILE__, __LINE__, _bc_cur.name, _e, _a); \
        _bc_cur.fails++; \
    } \
} while (0)

#define CHECK_EQ_MEM(expected, actual, n) do { \
    if (memcmp((expected), (actual), (n)) != 0) { \
        fprintf(stderr, "  FAIL %s:%d in %s: memcmp over %zu bytes differed\n", \
                __FILE__, __LINE__, _bc_cur.name, (size_t)(n)); \
        _bc_cur.fails++; \
    } \
} while (0)

#define CHECK_EQ_STR(expected, actual) do { \
    const char *_e = (expected), *_a = (actual); \
    if (!_e || !_a || strcmp(_e, _a) != 0) { \
        fprintf(stderr, "  FAIL %s:%d in %s: expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, _bc_cur.name, _e ? _e : "(null)", _a ? _a : "(null)"); \
        _bc_cur.fails++; \
    } \
} while (0)

#define TEST(tname) static void _bc_test_##tname(void)

#define RUN(tname) do { \
    _bc_cur.name = #tname; _bc_cur.fails = 0; \
    _bc_test_##tname(); \
    if (_bc_cur.fails == 0) { fprintf(stderr, "  ok  %s\n", #tname); } \
    _total_fails += _bc_cur.fails; _total++; \
} while (0)

#define RUN_TESTS(...) do { \
    int _total = 0, _total_fails = 0; \
    fprintf(stderr, "## %s\n", __FILE__); \
    _RUN_EACH(__VA_ARGS__); \
    fprintf(stderr, "## %s — %d/%d passed\n", __FILE__, _total - _total_fails, _total); \
    return _total_fails == 0 ? 0 : 1; \
} while (0)

/* Invoke RUN() on each argument. Supports up to 16 tests per file. */
#define _NARG(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,N,...) N
#define _RUN_EACH(...) _RUN_EACH_N(_NARG(__VA_ARGS__,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1), __VA_ARGS__)
/* Double indirection so N is fully expanded before pasting. */
#define _RUN_EACH_N(N, ...)  _RUN_EACH_N_(N, __VA_ARGS__)
#define _RUN_EACH_N_(N, ...) _RUN_##N(__VA_ARGS__)
#define _RUN_1(a)              RUN(a);
#define _RUN_2(a,b)            RUN(a); RUN(b);
#define _RUN_3(a,b,c)          _RUN_2(a,b) RUN(c);
#define _RUN_4(a,b,c,d)        _RUN_3(a,b,c) RUN(d);
#define _RUN_5(a,b,c,d,e)      _RUN_4(a,b,c,d) RUN(e);
#define _RUN_6(a,b,c,d,e,f)    _RUN_5(a,b,c,d,e) RUN(f);
#define _RUN_7(a,b,c,d,e,f,g)  _RUN_6(a,b,c,d,e,f) RUN(g);
#define _RUN_8(a,b,c,d,e,f,g,h) _RUN_7(a,b,c,d,e,f,g) RUN(h);
#define _RUN_9(a,b,c,d,e,f,g,h,i) _RUN_8(a,b,c,d,e,f,g,h) RUN(i);
#define _RUN_10(a,b,c,d,e,f,g,h,i,j) _RUN_9(a,b,c,d,e,f,g,h,i) RUN(j);
#define _RUN_11(a,b,c,d,e,f,g,h,i,j,k) _RUN_10(a,b,c,d,e,f,g,h,i,j) RUN(k);
#define _RUN_12(a,b,c,d,e,f,g,h,i,j,k,l) _RUN_11(a,b,c,d,e,f,g,h,i,j,k) RUN(l);
#define _RUN_13(a,b,c,d,e,f,g,h,i,j,k,l,m) _RUN_12(a,b,c,d,e,f,g,h,i,j,k,l) RUN(m);
#define _RUN_14(a,b,c,d,e,f,g,h,i,j,k,l,m,n) _RUN_13(a,b,c,d,e,f,g,h,i,j,k,l,m) RUN(n);
#define _RUN_15(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o) _RUN_14(a,b,c,d,e,f,g,h,i,j,k,l,m,n) RUN(o);
#define _RUN_16(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p) _RUN_15(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o) RUN(p);

#endif /* BITCHAT_TEST_HARNESS_H */
