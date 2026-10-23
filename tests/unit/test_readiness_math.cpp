/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * test_readiness_math.cpp — Timeout arithmetic used by the select/poll wrappers.
 */

#include <climits>
#include <cstdio>
#include <sys/time.h>

#include "readiness_math.h"

static int failures = 0;

static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void test_poll_ms_conversion() {
    struct timeval tv;

    ntrickle_poll_timeout_to_tv(10500, &tv);
    check(tv.tv_sec == 10 && tv.tv_usec == 500000, "10.5 s conversion");

    ntrickle_poll_timeout_to_tv(883512, &tv);
    check(tv.tv_sec == 883 && tv.tv_usec == 512000, "883.512 s conversion");

    ntrickle_poll_timeout_to_tv(999, &tv);
    check(tv.tv_sec == 0 && tv.tv_usec == 999000, "999 ms conversion");
}

static void test_remaining_timeout() {
    struct timeval initial {
        10, 0
    };
    struct timeval elapsed {
        3, 250000
    };
    struct timeval remaining;

    ntrickle_remaining_timeout(&initial, &elapsed, &remaining);
    check(remaining.tv_sec == 6 && remaining.tv_usec == 750000, "10 s - 3.25 s = 6.75 s");

    elapsed = {0, 500000};
    ntrickle_remaining_timeout(&initial, &elapsed, &remaining);
    check(remaining.tv_sec == 9 && remaining.tv_usec == 500000,
          "negative microseconds borrow correctly");

    elapsed = {11, 0};
    ntrickle_remaining_timeout(&initial, &elapsed, &remaining);
    check(remaining.tv_sec == 0 && remaining.tv_usec == 0, "elapsed beyond timeout clamps to zero");
}

static void test_remaining_never_exceeds_original() {
    struct timeval initial {
        10, 0
    };
    struct timeval elapsed {
        - 873, -512324
    }; // simulated backward clock jump
    struct timeval remaining;

    ntrickle_remaining_timeout(&initial, &elapsed, &remaining);
    ntrickle_cap_timeout(&remaining, &initial);

    check(remaining.tv_sec == 10 && remaining.tv_usec == 0,
          "negative elapsed cannot extend the caller timeout");
}

static void test_timeout_to_poll_ms() {
    struct timeval tv {
        883, 512324
    };

    check(ntrickle_timeout_to_poll_ms(&tv) == 883512, "timeval to milliseconds");

    tv = {INT_MAX / 1000L + 1, 0};
    check(ntrickle_timeout_to_poll_ms(&tv) == INT_MAX, "large timeout saturates");
}

int main() {
    test_poll_ms_conversion();
    test_remaining_timeout();
    test_remaining_never_exceeds_original();
    test_timeout_to_poll_ms();

    if (failures) {
        std::fprintf(stderr, "%d FAILURES\n", failures);
        return 1;
    }
    std::printf("PASS: readiness timeout arithmetic tests\n");
    return 0;
}
