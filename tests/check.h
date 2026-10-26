#ifndef EXT2SIM_TESTS_CHECK_H
#define EXT2SIM_TESTS_CHECK_H

/* A minimal assertion helper: failures are counted and reported, and the test keeps running. */

#include <stdio.h>
#include <stdlib.h>

static int g_failures;
static int g_checks;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        g_checks++;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            g_failures++;                                                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);                                   \
        }                                                                                                              \
    } while (0)

static inline int check_summary(void) {
    printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

#endif
