/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * test_shared_config.cpp — Unit tests for process and schedule config parsing.
 */

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

#include "shared_config.hpp"

#include <fstream>
#include <string>

static int failures = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string write_temp(const std::string& content) {
    char tmpl[] = "/tmp/ntrickle_test_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd == -1) {
        std::fprintf(stderr, "FAIL: mkstemp\n");
        std::exit(1);
    }
    std::string path(tmpl);
    std::ofstream ofs(path);
    ofs << content;
    ofs.close();
    close(fd);
    return path;
}

// ---------------------------------------------------------------------------
// Process config tests
// ---------------------------------------------------------------------------

static void test_process_parse_valid() {
    std::string content = R"(
[process]
[process."curl.*"]
weight = 3
download = "100M"
upload = "50M"

[process."wget.*"]
weight = 2
)";
    auto path = write_temp(content);
    auto entries = ntrickle::parse_process_config(path);
    std::remove(path.c_str());

    if (entries.size() != 2) {
        std::fprintf(stderr, "FAIL: expected 2 entries, got %zu\n", entries.size());
        ++failures;
        return;
    }
    if (entries[0].weight.value_or(0) != 3) {
        std::fprintf(stderr, "FAIL: expected weight=3\n");
        ++failures;
    }
    if (entries[0].download.value_or(0) == 0) {
        std::fprintf(stderr, "FAIL: expected download limit\n");
        ++failures;
    }
}

static void test_process_match() {
    std::string content = R"(
[process]
[process."curl.*"]
weight = 5

[process."python.*"]
weight = 2
)";
    auto path = write_temp(content);
    auto entries = ntrickle::parse_process_config(path);
    std::remove(path.c_str());

    auto* m1 = ntrickle::match_process(entries, "curl-7.88");
    if (m1 == nullptr) {
        std::fprintf(stderr, "FAIL: curl should match 'curl.*'\n");
        ++failures;
    } else if (m1->weight.value_or(0) != 5) {
        std::fprintf(stderr, "FAIL: matched weight should be 5\n");
        ++failures;
    }

    auto* m2 = ntrickle::match_process(entries, "wget");
    if (m2 != nullptr) {
        std::fprintf(stderr, "FAIL: wget should not match any pattern\n");
        ++failures;
    }

    auto* m3 = ntrickle::match_process(entries, "python3.12");
    if (m3 == nullptr || m3->weight.value_or(0) != 2) {
        std::fprintf(stderr, "FAIL: python3.12 should match 'python.*' weight=2\n");
        ++failures;
    }
}

static void test_process_empty_file() {
    auto entries = ntrickle::parse_process_config("/tmp/ntrickle_nonexistent_process.toml");
    if (!entries.empty()) {
        std::fprintf(stderr, "FAIL: nonexistent file should return empty vector\n");
        ++failures;
    }
}

// ---------------------------------------------------------------------------
// Schedule config tests
// ---------------------------------------------------------------------------

static void test_schedule_parse_valid() {
    std::string content = R"(
[schedules]
[schedules.night]
download = "1G"
upload = "500M"
start = "22:00"
end = "06:00"
days = ["Mon", "Tue", "Wed", "Thu", "Fri"]

[schedules.weekend]
download = "2G"
start = "09:00"
end = "18:00"
days = ["Sat", "Sun"]
)";
    auto path = write_temp(content);
    auto entries = ntrickle::parse_schedules_config(path);
    std::remove(path.c_str());

    if (entries.size() != 2) {
        std::fprintf(stderr, "FAIL: expected 2 schedule entries, got %zu\n", entries.size());
        ++failures;
        return;
    }
    // Check first entry
    auto& e1 = entries[0];
    if (e1.name != "night") {
        std::fprintf(stderr, "FAIL: expected first entry 'night', got '%s'\n", e1.name.c_str());
        ++failures;
    }
    if (e1.download.value_or(0) == 0) {
        std::fprintf(stderr, "FAIL: night should have download\n");
        ++failures;
    }
    if (e1.start_minutes != 22 * 60 || e1.end_minutes != 6 * 60) {
        std::fprintf(stderr, "FAIL: night time range incorrect (%u-%u)\n", e1.start_minutes,
                     e1.end_minutes);
        ++failures;
    }
    // days_bitmask: Mon=0x01 Tue=0x02 Wed=0x04 Thu=0x08 Fri=0x10
    if (e1.days_bitmask != 0x1F) {
        std::fprintf(stderr, "FAIL: night days_bitmask should be 0x1F, got 0x%02X\n",
                     e1.days_bitmask);
        ++failures;
    }
}

static void test_schedule_evaluate() {
    // Create a schedule that covers the current time
    std::string content = R"(
[schedules]
[schedules.always]
download = "42M"
start = "00:00"
end = "23:59"
days = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
)";
    auto path = write_temp(content);
    auto entries = ntrickle::parse_schedules_config(path);
    std::remove(path.c_str());

    auto active = ntrickle::evaluate_schedules(entries, true); // suppress warnings
    if (!active.has_value()) {
        std::fprintf(stderr, "FAIL: 'always' schedule should be active\n");
        ++failures;
    } else {
        if (active->download.value_or(0) == 0) {
            std::fprintf(stderr, "FAIL: active schedule should have download=42M\n");
            ++failures;
        }
    }
}

static void test_schedule_overnight_range() {
    // Test that 22:00–06:00 overnight matches at 02:00
    std::string content = R"(
[schedules]
[schedules.night]
download = "100M"
start = "22:00"
end = "06:00"
)";
    auto path = write_temp(content);
    auto entries = ntrickle::parse_schedules_config(path);
    std::remove(path.c_str());

    // Build a tm for 02:00 on a Monday
    struct tm tm_buf {};
    tm_buf.tm_year = 2025 - 1900;
    tm_buf.tm_mon = 0; // January
    tm_buf.tm_mday = 6; // Jan 6, 2025 is a Monday
    tm_buf.tm_hour = 2;
    tm_buf.tm_min = 0;
    tm_buf.tm_wday = 1; // Monday
    tm_buf.tm_isdst = -1;
    time_t t = mktime(&tm_buf);

    auto active = ntrickle::evaluate_schedules(entries, true, t);
    if (!active.has_value()) {
        std::fprintf(stderr, "FAIL: overnight schedule should be active at 02:00\n");
        ++failures;
    }
}

// ---------------------------------------------------------------------------
// Config path resolution precedence tests
// ---------------------------------------------------------------------------

static void unset_env(const char* name) {
    ::unsetenv(name);
}

static void set_env(const char* name, const char* value) {
    ::setenv(name, value, 1);
}

static void test_config_path_cli_wins() {
    // CLI flag overrides everything, even when env and user config exist.
    unset_env("NTRICKLE_TEST_ENV");
    auto result = ntrickle::resolve_config_path("/explicit/path.toml", "NTRICKLE_TEST_ENV",
                                                "test.toml", "/etc/default.toml");
    if (result != "/explicit/path.toml") {
        std::fprintf(stderr, "FAIL: CLI path should win, got '%s'\n", result.c_str());
        ++failures;
    }
}

static void test_config_path_env_wins_over_user() {
    // Env var overrides user config (even if user config exists).
    unset_env("NTRICKLE_TEST_ENV");
    set_env("NTRICKLE_TEST_ENV", "/env/path.toml");

    // Create a user config so we can verify env still wins.
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        // Can't create user config without HOME; env should still work.
        auto result = ntrickle::resolve_config_path("", "NTRICKLE_TEST_ENV", "test.toml",
                                                    "/etc/default.toml");
        if (result != "/env/path.toml") {
            std::fprintf(stderr, "FAIL: env should win, got '%s'\n", result.c_str());
            ++failures;
        }
        unset_env("NTRICKLE_TEST_ENV");
        return;
    }

    std::string user_dir = std::string(home) + "/.config/ntrickle/";
    std::string user_file = user_dir + "test.toml";
    ::mkdir(user_dir.c_str(), 0755);
    {
        std::ofstream ofs(user_file);
        ofs << "# test\n";
    }

    auto result =
        ntrickle::resolve_config_path("", "NTRICKLE_TEST_ENV", "test.toml", "/etc/default.toml");
    if (result != "/env/path.toml") {
        std::fprintf(stderr, "FAIL: env should win over user config, got '%s'\n", result.c_str());
        ++failures;
    }

    std::remove(user_file.c_str());
    unset_env("NTRICKLE_TEST_ENV");
}

static void test_config_path_user_wins_over_system() {
    // User config (if it exists on disk) overrides the system default.
    unset_env("NTRICKLE_TEST_ENV");
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        // Without HOME, there is no user config → system default is used.
        auto result = ntrickle::resolve_config_path("", "NTRICKLE_TEST_ENV", "test_user.toml",
                                                    "/etc/default.toml");
        if (result != "/etc/default.toml") {
            std::fprintf(stderr, "FAIL: system default should be used when HOME unset, got '%s'\n",
                         result.c_str());
            ++failures;
        }
        return;
    }

    std::string user_dir = std::string(home) + "/.config/ntrickle/";
    std::string user_file = user_dir + "test_user.toml";
    ::mkdir(user_dir.c_str(), 0755);
    {
        std::ofstream ofs(user_file);
        ofs << "# test\n";
    }

    auto result = ntrickle::resolve_config_path("", "NTRICKLE_TEST_ENV", "test_user.toml",
                                                "/etc/default.toml");
    if (result != user_file) {
        std::fprintf(stderr, "FAIL: user config should win over system default, got '%s'\n",
                     result.c_str());
        ++failures;
    }

    std::remove(user_file.c_str());
}

static void test_config_path_system_fallback() {
    // When nothing else is set, the system default is returned.
    unset_env("NTRICKLE_TEST_ENV");
    auto result = ntrickle::resolve_config_path("", "NTRICKLE_TEST_ENV",
                                                "nonexistent_file_xyz.toml", "/etc/default.toml");
    if (result != "/etc/default.toml") {
        std::fprintf(stderr, "FAIL: system default should be fallback, got '%s'\n", result.c_str());
        ++failures;
    }
}

static void test_config_path_empty_env_skipped() {
    // An empty env var is treated as "not set" and skipped.
    unset_env("NTRICKLE_TEST_ENV");
    set_env("NTRICKLE_TEST_ENV", ""); // empty
    auto result = ntrickle::resolve_config_path("", "NTRICKLE_TEST_ENV", "nonexistent_test.toml",
                                                "/etc/default.toml");
    if (result != "/etc/default.toml") {
        std::fprintf(stderr, "FAIL: empty env var should be skipped, got '%s'\n", result.c_str());
        ++failures;
    }
    unset_env("NTRICKLE_TEST_ENV");
}

static void test_config_path_user_missing_skipped() {
    // When the user config path doesn't exist on disk, fall through to system.
    unset_env("NTRICKLE_TEST_ENV");
    auto result = ntrickle::resolve_config_path(
        "", "NTRICKLE_TEST_ENV", "definitely_does_not_exist_12345.toml", "/etc/default.toml");
    if (result != "/etc/default.toml") {
        std::fprintf(stderr, "FAIL: missing user config should fall through, got '%s'\n",
                     result.c_str());
        ++failures;
    }
}

// ---------------------------------------------------------------------------

int main() {
    test_process_parse_valid();
    test_process_match();
    test_process_empty_file();
    test_schedule_parse_valid();
    test_schedule_evaluate();
    test_schedule_overnight_range();
    test_config_path_cli_wins();
    test_config_path_env_wins_over_user();
    test_config_path_user_wins_over_system();
    test_config_path_system_fallback();
    test_config_path_empty_env_skipped();
    test_config_path_user_missing_skipped();

    if (failures) {
        std::fprintf(stderr, "%d FAILURES\n", failures);
        return 1;
    }
    std::printf("PASS: shared_config tests\n");
    return 0;
}
