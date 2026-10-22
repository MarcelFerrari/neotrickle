/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * test_tui_text.cpp — Sanitization of command strings for the TUI table.
 */

#include <cstdio>

#include "tui_text.hpp"

#include <string>
#include <string_view>

static int failures = 0;

static void check(const char* label, std::string_view input, const std::string& want) {
    auto got = ntrickle::tui::sanitize_display_text(input);
    if (got != want) {
        std::fprintf(stderr, "FAIL: %s: got '%s', want '%s'\n", label, got.c_str(), want.c_str());
        ++failures;
    }
}

int main() {
    check("plain", "echo hello", "echo hello");
    check("tab", "a\tb", "a b");
    check("newline", "a\nb", "a b");
    check("carriage return", "a\rb", "a b");
    check("backslash", "a\\b", "a b");
    check("nul", std::string_view("a\0b", 3), "a b");
    check("control",
          "a\x01"
          "b",
          "a b");
    check("del",
          "a\x7f"
          "b",
          "a b");
    check("utf8", "naïve", "naïve");
    check("empty", "", "");
    check("only specials", "\n\t\\", "   ");
    check("shell continuation", "a\\\n b", "a   b");

    if (failures) {
        std::fprintf(stderr, "%d FAILURES\n", failures);
        return 1;
    }
    std::printf("PASS: tui text tests\n");
    return 0;
}
