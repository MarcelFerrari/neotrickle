// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace ntrickle::tui {

/// Sanitize a string for display in a single-line ncurses field.
///
/// Control characters (including NUL, BEL, tab, newline, carriage return, and
/// DEL) and backslashes are replaced with spaces.  Backslashes are treated as
/// special because command lines commonly use them for shell continuations and
/// escaping, which makes table output hard to read.  Printable UTF-8 text is
/// preserved.
inline std::string sanitize_display_text(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char ch : input) {
        if (ch == '\\' || ch == '\t' || ch == '\n' || ch == '\r' || ch < 0x20 || ch == 0x7f)
            out.push_back(' ');
        else
            out.push_back(static_cast<char>(ch));
    }
    return out;
}

} // namespace ntrickle::tui
