// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <cstdlib>

#include "bandwidth.hpp"

#include <string>

static int failures = 0;

#define CHECK_EQ(expr, expected)                                                                   \
    do {                                                                                           \
        auto _chk_opt = (expr);                                                                    \
        auto _chk_exp = static_cast<uint64_t>(expected);                                           \
        if (!_chk_opt.has_value() || _chk_opt.value() != _chk_exp) {                               \
            std::fprintf(                                                                          \
                stderr, "FAIL %s:%d: %s = %llu, expected %llu\n", __FILE__, __LINE__, #expr,       \
                _chk_opt.has_value() ? static_cast<unsigned long long>(_chk_opt.value()) : 0ULL,   \
                static_cast<unsigned long long>(_chk_exp));                                        \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

#define CHECK_NULLOPT(expr)                                                                        \
    do {                                                                                           \
        auto _nopt = (expr);                                                                       \
        if (_nopt.has_value()) {                                                                   \
            std::fprintf(stderr, "FAIL %s:%d: %s should be nullopt (got %llu)\n", __FILE__,        \
                         __LINE__, #expr, static_cast<unsigned long long>(_nopt.value()));         \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

static void test_normal() {
    CHECK_EQ(ntrickle::parse_bandwidth("8M"), 8ULL * 1024 * 1024);
    CHECK_EQ(ntrickle::parse_bandwidth("1.5G"), static_cast<uint64_t>(1.5 * 1024 * 1024 * 1024));
    CHECK_EQ(ntrickle::parse_bandwidth("500K"), 500ULL * 1024);
    CHECK_EQ(ntrickle::parse_bandwidth("10KB"), 10ULL * 1024);
    CHECK_EQ(ntrickle::parse_bandwidth("1MB"), 1ULL * 1024 * 1024);
    CHECK_EQ(ntrickle::parse_bandwidth("2GB"), 2ULL * 1024 * 1024 * 1024);
    CHECK_EQ(ntrickle::parse_bandwidth("10K"), 10ULL * 1024);
    CHECK_EQ(ntrickle::parse_bandwidth("1M"), 1ULL * 1024 * 1024);
    CHECK_EQ(ntrickle::parse_bandwidth("2G"), 2ULL * 1024 * 1024 * 1024);
}

static void test_bits() {
    CHECK_EQ(ntrickle::parse_bandwidth("100Mbps"), 100ULL * 1024 * 1024 / 8);
    CHECK_EQ(ntrickle::parse_bandwidth("1Gbps"), 1ULL * 1024 * 1024 * 1024 / 8);
    CHECK_EQ(ntrickle::parse_bandwidth("500Kbps"), 500ULL * 1024 / 8);
    CHECK_EQ(ntrickle::parse_bandwidth("100mbps"), 100ULL * 1024 * 1024 / 8);
    CHECK_EQ(ntrickle::parse_bandwidth("500kbps"), 500ULL * 1024 / 8);
    CHECK_EQ(ntrickle::parse_bandwidth("8Kb"), 8ULL * 1024 / 8);
    CHECK_EQ(ntrickle::parse_bandwidth("8Mb"), 8ULL * 1024 * 1024 / 8);
    CHECK_EQ(ntrickle::parse_bandwidth("1Gb"), 1ULL * 1024 * 1024 * 1024 / 8);
}

static void test_zero_unlimited() {
    CHECK_EQ(ntrickle::parse_bandwidth("0"), 0ULL);
    CHECK_EQ(ntrickle::parse_bandwidth("unlimited"), 0ULL);
    CHECK_EQ(ntrickle::parse_bandwidth("Unlimited"), 0ULL);
    CHECK_EQ(ntrickle::parse_bandwidth("UNLIMITED"), 0ULL);
}

static void test_slash_s() {
    CHECK_EQ(ntrickle::parse_bandwidth("8MB/s"), 8ULL * 1024 * 1024);
    CHECK_EQ(ntrickle::parse_bandwidth("100KB/s"), 100ULL * 1024);
    CHECK_EQ(ntrickle::parse_bandwidth("1.5Gb/s"),
             static_cast<uint64_t>(1.5 * 1024 * 1024 * 1024 / 8));
    CHECK_EQ(ntrickle::parse_bandwidth("500kbps"), 500ULL * 1024 / 8);
}

static void test_whitespace() {
    CHECK_EQ(ntrickle::parse_bandwidth(" 8M "), 8ULL * 1024 * 1024);
    CHECK_EQ(ntrickle::parse_bandwidth("  1.5G  "),
             static_cast<uint64_t>(1.5 * 1024 * 1024 * 1024));
}

static void test_units_required() {
    // Bare numbers are rejected; this avoids silently interpreting classic
    // trickle's KB/s values as bytes per second.
    CHECK_NULLOPT(ntrickle::parse_bandwidth("1048576"));
    CHECK_NULLOPT(ntrickle::parse_bandwidth("10"));
    CHECK_NULLOPT(ntrickle::parse_bandwidth("1.5"));
    CHECK_EQ(ntrickle::parse_bandwidth("1000B"), 1000ULL);
    CHECK_EQ(ntrickle::parse_bandwidth("0"), 0ULL);
}

static void test_minimum_clamp() {
    // 0.5 Kbps = 64 bytes/s -> should be clamped to 128
    auto val = ntrickle::parse_bandwidth("0.5Kbps");
    if (!val.has_value() || val.value() != 128ULL) {
        std::fprintf(stderr, "FAIL: 0.5Kbps clamp: got %llu, expected 128\n",
                     val.has_value() ? static_cast<unsigned long long>(val.value()) : 0ULL);
        ++failures;
    }
}

static void test_failures() {
    CHECK_NULLOPT(ntrickle::parse_bandwidth(""));
    CHECK_NULLOPT(ntrickle::parse_bandwidth("abc"));
    CHECK_NULLOPT(ntrickle::parse_bandwidth("-5"));
    CHECK_NULLOPT(ntrickle::parse_bandwidth("8X"));
    CHECK_NULLOPT(ntrickle::parse_bandwidth("1.2.3M"));
    CHECK_NULLOPT(ntrickle::parse_bandwidth("M"));
    CHECK_NULLOPT(ntrickle::parse_bandwidth("8GBps"));
}

static void test_format() {
    auto check_fmt = [](uint64_t bytes, const std::string& expected) {
        auto result = ntrickle::format_bandwidth(bytes);
        if (result != expected) {
            std::fprintf(stderr, "FAIL format_bandwidth(%llu) = '%s', expected '%s'\n",
                         static_cast<unsigned long long>(bytes), result.c_str(), expected.c_str());
            ++failures;
        }
    };

    // -- zero / unlimited --
    check_fmt(0, "unlimited");

    // -- sub-KB (bytes) --
    check_fmt(1, "1 B/s");
    check_fmt(500, "500 B/s");
    check_fmt(1023, "1023 B/s");

    // -- exactly 1 KB and just above --
    check_fmt(1024, "1 KB/s");
    // 1025 / 1024 = 1.00098 → fraction < 0.05 → integer format
    check_fmt(1025, "1 KB/s");
    // 1.1 KB = floor(1.1 * 1024) = 1126
    check_fmt(1126, "1.1 KB/s");
    check_fmt(1536, "1.5 KB/s");

    // -- near the KB → MB boundary --
    // 1023 KB
    check_fmt(1047552, "1023 KB/s");
    // exactly 1 MB
    check_fmt(1048576, "1 MB/s");
    // just below 1 MB: 1048575 / 1024 ≈ 1023.999 → rounds to "1024.0 KB/s"
    check_fmt(1048575, "1024.0 KB/s");
    // just above 1 MB: fraction < 0.05 → integer
    check_fmt(1048577, "1 MB/s");
    check_fmt(1572864, "1.5 MB/s");

    // -- near the MB → GB boundary --
    // exactly 1 GB
    check_fmt(1073741824, "1 GB/s");
    // just below 1 GB: 1073741823 / 1048576 ≈ 1023.999 → "1024.0 MB/s"
    check_fmt(1073741823, "1024.0 MB/s");
    // just above 1 GB: fraction < 0.05 → integer
    check_fmt(1073741825, "1 GB/s");
    check_fmt(1610612736, "1.5 GB/s");

    // -- clean integer values (fractional part < 0.05 → integer) --
    // 1.02 MB → "1 MB/s" (fraction 0.02 < 0.05)
    check_fmt(1069547, "1 MB/s"); // 1.02 * 1024*1024
    // 1.06 MB → "1.1 MB/s" (fraction 0.06 ≥ 0.05 → one decimal)
    check_fmt(1111491, "1.1 MB/s"); // 1.06 * 1024*1024

    // -- large values stay in GB --
    check_fmt(10ULL * 1024 * 1024 * 1024, "10 GB/s");
    check_fmt(100ULL * 1024 * 1024 * 1024, "100 GB/s");

    // -- verify unit selection does not overshoot: bytes just below 1 KB stay B/s --
    check_fmt(1023, "1023 B/s");

    // -- verify the table uses the largest suitable unit (GB > MB > KB > B) --
    check_fmt(2ULL * 1024 * 1024 * 1024, "2 GB/s");
    check_fmt(2ULL * 1024 * 1024, "2 MB/s");
    check_fmt(2ULL * 1024, "2 KB/s");
    check_fmt(2, "2 B/s");
}

int main() {
    test_normal();
    test_bits();
    test_zero_unlimited();
    test_slash_s();
    test_whitespace();
    test_units_required();
    test_minimum_clamp();
    test_failures();
    test_format();

    if (failures) {
        std::fprintf(stderr, "%d FAILURES\n", failures);
        return 1;
    }
    std::printf("PASS: bandwidth unit tests\n");
    return 0;
}
