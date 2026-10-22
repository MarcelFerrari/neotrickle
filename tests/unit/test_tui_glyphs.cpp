/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * test_tui_glyphs.cpp — Terminal-capability detection and glyph fallbacks.
 */

#include <cstdio>

#include "tui_glyphs.hpp"

#include <string>

static int failures = 0;

static void check_bool(const char* label, bool got, bool want) {
    if (got != want) {
        std::fprintf(stderr, "FAIL: %s: got %s, want %s\n", label, got ? "true" : "false",
                     want ? "true" : "false");
        ++failures;
    }
}

static void check_str(const char* label, const char* got, const char* want) {
    if (std::string(got) != want) {
        std::fprintf(stderr, "FAIL: %s: got '%s', want '%s'\n", label, got, want);
        ++failures;
    }
}

int main() {
    check_bool("utf8 codeset", ntrickle::tui::locale_is_utf8("UTF-8", 6), true);
    check_bool("utf8 lowercase", ntrickle::tui::locale_is_utf8("utf8", 4), true);
    check_bool("C locale", ntrickle::tui::locale_is_utf8("ANSI_X3.4-1968", 1), false);
    check_bool("latin1", ntrickle::tui::locale_is_utf8("ISO-8859-1", 1), false);
    check_bool("null codeset", ntrickle::tui::locale_is_utf8(nullptr, 6), false);
    check_bool("forced ascii", ntrickle::tui::use_unicode_glyphs("UTF-8", 6, true), false);
    check_bool("auto unicode", ntrickle::tui::use_unicode_glyphs("UTF-8", 6, false), true);
    check_bool("auto ascii", ntrickle::tui::use_unicode_glyphs("ANSI_X3.4-1968", 1, false), false);

    check_str("unicode full", ntrickle::tui::full(true), "█");
    check_str("ascii full", ntrickle::tui::full(false), "#");
    check_str("unicode empty", ntrickle::tui::empty(true), "░");
    check_str("ascii empty", ntrickle::tui::empty(false), "-");
    check_str("ascii part low", ntrickle::tui::part(false, 1), ".");
    check_str("ascii part mid", ntrickle::tui::part(false, 5), "+");
    check_str("ascii part full", ntrickle::tui::part(false, 8), "#");
    check_str("unicode graph empty", ntrickle::tui::graph(true, 0, 0, false), " ");
    check_str("ascii graph empty", ntrickle::tui::graph(false, 0, 0, false), " ");
    check_str("ascii graph half", ntrickle::tui::graph(false, 2, 0, false), "+");
    check_str("ascii graph full", ntrickle::tui::graph(false, 4, 4, true), "#");
    check_str("ascii up/down", ntrickle::tui::up_down(false), "|");
    check_str("ascii arrows", ntrickle::tui::up_down_arrows(false), "^v");
    check_str("ascii dot", ntrickle::tui::connected_dot(false), "*");
    check_str("ascii dash", ntrickle::tui::em_dash(false), "-");
    check_str("unicode dash", ntrickle::tui::em_dash(true), "—");

    if (failures) {
        std::fprintf(stderr, "%d FAILURES\n", failures);
        return 1;
    }
    std::printf("PASS: tui glyph tests\n");
    return 0;
}
