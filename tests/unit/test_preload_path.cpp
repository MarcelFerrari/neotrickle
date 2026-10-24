// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_preload_path.cpp — Relocatable preload discovery.

#include <cstdio>
#include <unistd.h>

#include "preload_path.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;

static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void write_file(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << "test";
}

static void test_executable_relative() {
    const fs::path root =
        fs::temp_directory_path() / ("ntrickle-preload-" + std::to_string(getpid()));
    fs::remove_all(root);
    const fs::path exe = root / "bin" / "trickle";
    const fs::path lib = root / "lib" / "ntrickle" / ntrickle::preload_filename();
    write_file(exe);
    write_file(lib);

    const auto result = ntrickle::locate_preload(exe.string(), "", "");
    check(!result.path.empty(), "finds preload relative to executable");
    check(result.source == ntrickle::PreloadSource::ExecutableRelative,
          "reports executable-relative source");
    check(fs::equivalent(result.path, lib), "returns the packaged preload path");

    fs::remove_all(root);
}

static void test_symlink_invocation() {
    const fs::path root =
        fs::temp_directory_path() / ("ntrickle-preload-link-" + std::to_string(getpid()));
    fs::remove_all(root);
    const fs::path exe = root / "bin" / "trickle";
    const fs::path link = root / "bin" / "ntrickle";
    const fs::path lib = root / "lib" / "ntrickle" / ntrickle::preload_filename();
    write_file(exe);
    write_file(lib);
    fs::create_symlink("trickle", link);

    const auto result = ntrickle::locate_preload(link.string(), "", "");
    check(!result.path.empty(), "resolves preload for a symlink invocation");
    check(fs::equivalent(result.path, lib), "symlink invocation uses the real executable");

    fs::remove_all(root);
}

static void test_override_and_invalid_override() {
    const fs::path root =
        fs::temp_directory_path() / ("ntrickle-preload-env-" + std::to_string(getpid()));
    fs::remove_all(root);
    const fs::path exe = root / "bin" / "trickle";
    const fs::path packaged = root / "lib" / "ntrickle" / ntrickle::preload_filename();
    const fs::path alternate = root / "alternate" / ntrickle::preload_filename();
    write_file(exe);
    write_file(packaged);
    write_file(alternate);

    const auto override_result = ntrickle::locate_preload(exe.string(), alternate.string(), "");
    check(override_result.source == ntrickle::PreloadSource::Override, "explicit override wins");
    check(fs::equivalent(override_result.path, alternate), "explicit override path is used");

    const auto invalid = ntrickle::locate_preload(exe.string(), (root / "missing.so").string(), "");
    check(invalid.path.empty(), "invalid override does not fall back");
    check(!invalid.error.empty(), "invalid override reports an error");

    fs::remove_all(root);
}

static void test_compiled_fallback() {
    const fs::path root =
        fs::temp_directory_path() / ("ntrickle-preload-compiled-" + std::to_string(getpid()));
    fs::remove_all(root);
    const fs::path exe = root / "bin" / "trickle";
    const fs::path compiled = root / "system" / "lib" / ntrickle::preload_filename();
    write_file(exe);
    write_file(compiled);

    const auto result = ntrickle::locate_preload(exe.string(), "", compiled.string());
    check(result.source == ntrickle::PreloadSource::Compiled, "uses compiled fallback");
    check(fs::equivalent(result.path, compiled), "compiled fallback path is used");

    fs::remove_all(root);
}

static void test_cwd_is_not_searched() {
    const fs::path root =
        fs::temp_directory_path() / ("ntrickle-preload-cwd-" + std::to_string(getpid()));
    fs::remove_all(root);
    const fs::path work = root / "work";
    const fs::path exe = root / "elsewhere" / "bin" / "trickle";
    write_file(exe);
    write_file(work / ntrickle::preload_filename());

    const fs::path previous = fs::current_path();
    fs::current_path(work);
    const auto result = ntrickle::locate_preload(exe.string(), "", "");
    fs::current_path(previous);

    check(result.path.empty(), "current directory is not searched for the preload");
    fs::remove_all(root);
}

int main() {
    test_executable_relative();
    test_symlink_invocation();
    test_override_and_invalid_override();
    test_compiled_fallback();
    test_cwd_is_not_searched();

    if (failures) {
        std::fprintf(stderr, "%d FAILURES\n", failures);
        return 1;
    }
    std::printf("PASS: preload path tests\n");
    return 0;
}
