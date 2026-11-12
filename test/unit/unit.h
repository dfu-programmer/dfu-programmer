/*
 * Minimal unit-test helpers for dfu-programmer's `make check`.
 * No framework: each test program includes this, calls CHECK* and
 * returns UNIT_RESULT() from main(). Exit status 0 is a pass.
 */
#ifndef DFU_UNIT_H
#define DFU_UNIT_H

#include <inttypes.h>
#include <stdio.h>

static int unit_checks = 0;
static int unit_failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        unit_checks++;                                                      \
        if( !(cond) ) {                                                     \
            unit_failures++;                                                \
            fprintf( stderr, "%s:%d: CHECK failed: %s\n",                   \
                     __FILE__, __LINE__, #cond );                           \
        }                                                                   \
    } while( 0 )

#define CHECK_EQ(actual, expected)                                          \
    do {                                                                    \
        int64_t unit_a = (int64_t) (actual);                                \
        int64_t unit_e = (int64_t) (expected);                              \
        unit_checks++;                                                      \
        if( unit_a != unit_e ) {                                            \
            unit_failures++;                                                \
            fprintf( stderr, "%s:%d: CHECK_EQ failed: %s == %s "            \
                     "(got %" PRId64 " / 0x%" PRIX64                        \
                     ", want %" PRId64 " / 0x%" PRIX64 ")\n",               \
                     __FILE__, __LINE__, #actual, #expected,                \
                     unit_a, (uint64_t) unit_a,                             \
                     unit_e, (uint64_t) unit_e );                           \
        }                                                                   \
    } while( 0 )

#define RUN(test)                                                           \
    do {                                                                    \
        int unit_before = unit_failures;                                    \
        test();                                                             \
        fprintf( stderr, "%s %s\n",                                         \
                 (unit_failures == unit_before) ? "PASS" : "FAIL", #test ); \
    } while( 0 )

#define UNIT_RESULT()                                                       \
    ( fprintf( stderr, "%d checks, %d failed\n", unit_checks, unit_failures ), \
      (unit_failures ? 1 : 0) )

#endif
