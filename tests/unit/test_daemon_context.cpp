// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_daemon_context.cpp — Unit tests for UnixListener cleanup ownership.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "daemon_context.hpp"

#include <string>

static int failures = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Return a unique socket path inside /tmp for this test invocation.
static std::string temp_socket_path(const char* tag) {
    char tmpl[128];
    std::snprintf(tmpl, sizeof(tmpl), "/tmp/ntrickle_test_%s_XXXXXX", tag);
    int fd = mkstemp(tmpl);
    if (fd == -1) {
        std::fprintf(stderr, "FAIL: mkstemp for %s\n", tag);
        std::exit(1);
    }
    close(fd);
    std::string path(tmpl);
    unlink(path.c_str()); // mkstemp created a regular file; remove it
    return path + ".sock"; // use a distinct suffix for the socket
}

/// True if `path` exists on the filesystem.
static bool path_exists(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_construct_destruct() {
    auto path = temp_socket_path("ctor");
    {
        ntrickled::UnixListener l(path.c_str());
        if (l.fd() == -1) {
            std::fprintf(stderr, "FAIL: listener construction failed\n");
            ++failures;
            return;
        }
        if (!path_exists(path)) {
            std::fprintf(stderr, "FAIL: socket path should exist after construction\n");
            ++failures;
            return;
        }
    } // destructor runs
    if (path_exists(path)) {
        std::fprintf(stderr, "FAIL: socket path should be unlinked by destructor\n");
        ++failures;
    }
}

static void test_release_then_destruct() {
    auto path = temp_socket_path("release");
    {
        ntrickled::UnixListener l(path.c_str());
        if (l.fd() == -1) {
            std::fprintf(stderr, "FAIL: listener construction failed\n");
            ++failures;
            return;
        }
        l.release(); // explicit cleanup
        if (path_exists(path)) {
            std::fprintf(stderr, "FAIL: socket path should be gone after release()\n");
            ++failures;
        }
    } // destructor runs — must NOT double-close or double-unlink
    // No crash or errno corruption = pass.
}

static void test_double_release() {
    auto path = temp_socket_path("2xrel");
    {
        ntrickled::UnixListener l(path.c_str());
        if (l.fd() == -1) {
            std::fprintf(stderr, "FAIL: listener construction failed\n");
            ++failures;
            return;
        }
        l.release();
        l.release(); // idempotent — must not crash
    } // destructor runs after double-release — must also be safe
}

static void test_cleanup_list_pattern() {
    // Simulate the pattern used in ntrickled.cpp: a cleanup_list callback
    // calls release(), then the destructor runs.
    auto path = temp_socket_path("clnup");
    {
        ntrickled::UnixListener l(path.c_str());
        if (l.fd() == -1) {
            std::fprintf(stderr, "FAIL: listener construction failed\n");
            ++failures;
            return;
        }

        ntrickled::cleanup_list cl;
        cl.add([&l]() { l.release(); });

        cl.run_all(); // signal-path or normal-path cleanup
        if (path_exists(path)) {
            std::fprintf(stderr, "FAIL: socket path should be gone after cleanup_list\n");
            ++failures;
        }
    } // destructor after cleanup_list — must be harmless
    if (path_exists(path)) {
        std::fprintf(stderr, "FAIL: socket path should remain gone after destructor\n");
        ++failures;
    }
}

static void test_repeated_create_destroy() {
    // Repeatedly create and destroy listeners on the same path.
    auto path = temp_socket_path("repeat");
    for (int i = 0; i < 5; ++i) {
        {
            ntrickled::UnixListener l(path.c_str());
            if (l.fd() == -1) {
                std::fprintf(stderr, "FAIL: listener #%d construction failed\n", i);
                ++failures;
                return;
            }
            if (!path_exists(path)) {
                std::fprintf(stderr, "FAIL: socket should exist after construction #%d\n", i);
                ++failures;
                return;
            }
        } // destructor
        if (path_exists(path)) {
            std::fprintf(stderr,
                         "FAIL: socket should be gone after destruction #%d; "
                         "stale path may indicate double-unlink cleared a "
                         "new listener's socket\n",
                         i);
            ++failures;
            return;
        }
    }
}

// ---------------------------------------------------------------------------

int main() {
    test_construct_destruct();
    test_release_then_destruct();
    test_double_release();
    test_cleanup_list_pattern();
    test_repeated_create_destroy();

    if (failures) {
        std::fprintf(stderr, "%d FAILURES\n", failures);
        return 1;
    }
    std::printf("PASS: daemon_context tests\n");
    return 0;
}
