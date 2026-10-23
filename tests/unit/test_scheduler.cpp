/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * test_scheduler.cpp — Deterministic tests for the bwstat token-bucket scheduler.
 *
 * The bwstat algorithm computes per-client bandwidth entitlements from:
 *   - A global rate limit (bytes/sec)
 *   - Per-client weight points (pts)
 *   - Per-client observed winrate (bytes/sec)
 *
 * These tests set up synthetic bwstat state and verify fairness properties
 * without depending on real time or real network I/O.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/queue.h>
#include <sys/time.h>
#include <unistd.h>

#include <initializer_list>

extern "C" {
#include "bwstat.h"
}

static int failures = 0;

// The bwstat_init call creates a singleton totals entry at the head of the
// global TAILQ.  bwstat_free (on individual clients) now frees heap memory
// since the fix in bwstat.c.  We free the totals entry below in main() so
// the test runs cleanly under Valgrind.

// Helper: inject a known winrate into a client's bwstat.
static void set_winrate(struct bwstat* bs, short which, uint32_t rate) {
    bs->data[which].winrate = rate;
    // Also set rate so the "under-limit" fast path in bwstat_getdelay works
    bs->data[which].rate = rate;
}

// Helper: inject a known winrate into the global totals (first TAILQ entry).
static void set_total_winrate(short which, uint32_t rate) {
    struct bwstat* tot = bwstat_gettot();
    tot->data[which].winrate = rate;
    tot->data[which].rate = rate;
}

// Helper: initialise the tick counters on a bwstat so it looks "active."
static void init_timers(struct bwstat* bs) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    bs->data[BWSTAT_SEND].tv = tv;
    bs->data[BWSTAT_SEND].wintv = tv;
    bs->data[BWSTAT_RECV].tv = tv;
    bs->data[BWSTAT_RECV].wintv = tv;
}

// --- Test cases ---

static void test_no_delay_when_under_limit() {
    struct bwstat* cli = bwstat_new();
    cli->pts = 1;
    cli->tsmooth = 5.0;
    cli->lsmooth = 10 * 1024;
    init_timers(cli);

    set_winrate(cli, BWSTAT_RECV, 50000); // 50 KB/s
    set_total_winrate(BWSTAT_RECV, 50000);

    size_t len = 16384;
    struct timeval* tv = bwstat_getdelay(cli, &len, 100000, BWSTAT_RECV);
    // rate (50KB/s) < limit (100KB/s) → no delay
    if (tv != NULL) {
        std::fprintf(stderr, "FAIL: under-limit should return NULL (no delay)\n");
        ++failures;
    }

    bwstat_free(cli);
}

static void test_delay_when_over_limit() {
    struct bwstat* cli = bwstat_new();
    cli->pts = 1;
    cli->tsmooth = 1.0;
    cli->lsmooth = 10 * 1024;
    init_timers(cli);

    set_winrate(cli, BWSTAT_RECV, 200000); // 200 KB/s
    set_total_winrate(BWSTAT_RECV, 200000);

    size_t len = 16384;
    struct timeval* tv = bwstat_getdelay(cli, &len, 100000, BWSTAT_RECV);
    // rate (200KB/s) > limit (100KB/s) → should delay
    if (tv == NULL) {
        std::fprintf(stderr, "FAIL: over-limit should return a delay\n");
        ++failures;
    } else if (tv->tv_sec == 0 && tv->tv_usec == 0) {
        std::fprintf(stderr, "FAIL: over-limit delay should be > 0\n");
        ++failures;
    }

    bwstat_free(cli);
}

static void test_higher_weight_gets_more() {
    // Client A: weight 0 → pts = 21
    struct bwstat* cli_a = bwstat_new();
    cli_a->pts = 21; // max weight
    cli_a->tsmooth = 5.0;
    cli_a->lsmooth = 10 * 1024;
    init_timers(cli_a);

    // Client B: weight 20 → pts = 1
    struct bwstat* cli_b = bwstat_new();
    cli_b->pts = 1; // min weight
    cli_b->tsmooth = 5.0;
    cli_b->lsmooth = 10 * 1024;
    init_timers(cli_b);

    // Both clients are saturating the link
    set_winrate(cli_a, BWSTAT_RECV, 200000);
    set_winrate(cli_b, BWSTAT_RECV, 200000);
    set_total_winrate(BWSTAT_RECV, 400000);

    size_t len_a = 16384;
    size_t len_b = 16384;
    // bwstat_getdelay returns a static timeval; copy before next call
    struct timeval tv_a_val {
    }, tv_b_val{};
    struct timeval* tv_a_ptr = bwstat_getdelay(cli_a, &len_a, 100000, BWSTAT_RECV);
    if (tv_a_ptr)
        tv_a_val = *tv_a_ptr;
    struct timeval* tv_b_ptr = bwstat_getdelay(cli_b, &len_b, 100000, BWSTAT_RECV);
    if (tv_b_ptr)
        tv_b_val = *tv_b_ptr;

    if (tv_a_ptr == NULL || tv_b_ptr == NULL) {
        std::fprintf(stderr, "FAIL: both clients should get delays when over limit\n");
        ++failures;
    } else {
        double delay_a = tv_a_val.tv_sec + tv_a_val.tv_usec / 1000000.0;
        double delay_b = tv_b_val.tv_sec + tv_b_val.tv_usec / 1000000.0;
        if (delay_a >= delay_b) {
            std::fprintf(stderr,
                         "FAIL: high-weight client should have shorter delay "
                         "(got A=%.3fs B=%.3fs)\n",
                         delay_a, delay_b);
            ++failures;
        }
    }

    bwstat_free(cli_a);
    bwstat_free(cli_b);
}

static void test_unused_bandwidth_redistributed() {
    // Client A: idle (low usage)
    struct bwstat* cli_a = bwstat_new();
    cli_a->pts = 1;
    cli_a->tsmooth = 5.0;
    cli_a->lsmooth = 10 * 1024;
    init_timers(cli_a);

    // Client B: active (high usage)
    struct bwstat* cli_b = bwstat_new();
    cli_b->pts = 1;
    cli_b->tsmooth = 5.0;
    cli_b->lsmooth = 10 * 1024;
    init_timers(cli_b);

    // A uses 10KB/s, B uses 150KB/s, total 160KB/s, limit 100KB/s
    set_winrate(cli_a, BWSTAT_RECV, 10000);
    set_winrate(cli_b, BWSTAT_RECV, 150000);
    set_total_winrate(BWSTAT_RECV, 160000);

    size_t len_a = 16384;
    size_t len_b = 16384;
    struct timeval tv_a_val {
    }, tv_b_val{};
    struct timeval* tv_a_ptr = bwstat_getdelay(cli_a, &len_a, 100000, BWSTAT_RECV);
    if (tv_a_ptr)
        tv_a_val = *tv_a_ptr;
    struct timeval* tv_b_ptr = bwstat_getdelay(cli_b, &len_b, 100000, BWSTAT_RECV);
    if (tv_b_ptr)
        tv_b_val = *tv_b_ptr;

    // Client A is under its fair share → should get minimal/no delay
    if (tv_a_ptr != NULL && (tv_a_val.tv_sec > 0 || tv_a_val.tv_usec > 0)) {
        if (tv_b_ptr != NULL) {
            double delay_a = tv_a_val.tv_sec + tv_a_val.tv_usec / 1000000.0;
            double delay_b = tv_b_val.tv_sec + tv_b_val.tv_usec / 1000000.0;
            if (delay_a > delay_b) {
                std::fprintf(stderr,
                             "FAIL: idle client should not be delayed more than active "
                             "(got A=%.3fs B=%.3fs)\n",
                             delay_a, delay_b);
                ++failures;
            }
        }
    }

    bwstat_free(cli_a);
    bwstat_free(cli_b);
}

static void test_high_limit_no_delay() {
    // A limit far above the actual rate should produce no delay
    struct bwstat* cli = bwstat_new();
    cli->pts = 1;
    cli->tsmooth = 5.0;
    cli->lsmooth = 10 * 1024;
    init_timers(cli);

    set_winrate(cli, BWSTAT_RECV, 50000); // 50 KB/s
    set_total_winrate(BWSTAT_RECV, 50000);

    size_t len = 16384;
    struct timeval* tv = bwstat_getdelay(cli, &len, 500000, BWSTAT_RECV);
    // rate (50KB/s) << limit (500KB/s) → no delay
    if (tv != NULL) {
        std::fprintf(stderr, "FAIL: high-limit should return NULL (no delay needed)\n");
        ++failures;
    }

    bwstat_free(cli);
}

static void test_smoothing_caps_delay() {
    struct bwstat* cli = bwstat_new();
    cli->pts = 1;
    cli->tsmooth = 0.5; // short smoothing time
    cli->lsmooth = 1024;
    init_timers(cli);

    set_winrate(cli, BWSTAT_RECV, 1000000); // way over limit
    set_total_winrate(BWSTAT_RECV, 1000000);

    size_t len = 65536; // 64KB request
    struct timeval* tv = bwstat_getdelay(cli, &len, 10000, BWSTAT_RECV);
    // delay should be capped at tsmooth (0.5s)
    if (tv != NULL) {
        double delay = tv->tv_sec + tv->tv_usec / 1000000.0;
        if (delay > cli->tsmooth + 0.01) { // allow tiny rounding
            std::fprintf(stderr, "FAIL: delay %.3fs exceeds smoothing cap %.3fs\n", delay,
                         cli->tsmooth);
            ++failures;
        }
    }

    bwstat_free(cli);
}

static void test_corrupt_tsmooth_is_capped() {
    struct bwstat* cli = bwstat_new();
    cli->pts = 1;
    cli->tsmooth = 883.512324; // simulate a corrupt per-socket value
    cli->lsmooth = 10 * 1024;
    init_timers(cli);

    set_winrate(cli, BWSTAT_RECV, 200000);
    set_total_winrate(BWSTAT_RECV, 200000);

    size_t len = 65536;
    struct timeval* tv = bwstat_getdelay(cli, &len, 100000, BWSTAT_RECV);
    if (tv == NULL) {
        std::fprintf(stderr, "FAIL: corrupt tsmooth should still delay\n");
        ++failures;
    } else {
        double delay = tv->tv_sec + tv->tv_usec / 1000000.0;
        if (delay > BWSTAT_MAX_DELAY_SEC + 0.01) {
            std::fprintf(stderr, "FAIL: delay %.3fs exceeds hard cap %.1fs\n", delay,
                         BWSTAT_MAX_DELAY_SEC);
            ++failures;
        }
    }

    bwstat_free(cli);
}

static void test_invalid_tsmooth_is_bounded() {
    for (double value : {0.0, -1.0, 1.0e300}) {
        struct bwstat* cli = bwstat_new();
        cli->pts = 1;
        cli->tsmooth = value;
        cli->lsmooth = 10 * 1024;
        init_timers(cli);

        set_winrate(cli, BWSTAT_RECV, 200000);
        set_total_winrate(BWSTAT_RECV, 200000);

        size_t len = 65536;
        struct timeval* tv = bwstat_getdelay(cli, &len, 100000, BWSTAT_RECV);
        if (tv == NULL) {
            std::fprintf(stderr, "FAIL: invalid tsmooth %.3g should still delay\n", value);
            ++failures;
        } else {
            double delay = tv->tv_sec + tv->tv_usec / 1000000.0;
            if (delay > BWSTAT_MAX_DELAY_SEC + 0.01) {
                std::fprintf(stderr, "FAIL: invalid tsmooth %.3g produced %.3fs (max %.1fs)\n",
                             value, delay, BWSTAT_MAX_DELAY_SEC);
                ++failures;
            }
        }

        bwstat_free(cli);
    }
}

int main() {
    bwstat_init(1024 * 200);

    test_no_delay_when_under_limit();
    test_delay_when_over_limit();
    test_higher_weight_gets_more();
    test_unused_bandwidth_redistributed();
    test_high_limit_no_delay();
    test_smoothing_caps_delay();
    test_corrupt_tsmooth_is_capped();
    test_invalid_tsmooth_is_bounded();

    // Free the singleton totals entry so the Valgrind run is clean.
    // Individual client entries are freed inside each test case.
    bwstat_free(bwstat_gettot());

    if (failures) {
        std::fprintf(stderr, "%d FAILURES\n", failures);
        return 1;
    }
    std::printf("PASS: scheduler fairness tests\n");
    return 0;
}
