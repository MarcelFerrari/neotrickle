// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cctype>

#include <algorithm>
#include <string>

namespace ntrickle::tui {

/// True when the active locale's codeset is UTF-8.  `mb_cur_max` is MB_CUR_MAX
/// after setlocale(); it is 1 for the plain C locale.
inline bool locale_is_utf8(const char* codeset, int mb_cur_max) {
    if (codeset == nullptr || mb_cur_max <= 1)
        return false;

    std::string lower;
    for (const char* p = codeset; *p != '\0'; ++p)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    return lower.find("utf-8") != std::string::npos || lower.find("utf8") != std::string::npos;
}

/// Select Unicode glyphs only when the locale supports them and the user has
/// not forced ASCII mode.
inline bool use_unicode_glyphs(const char* codeset, int mb_cur_max, bool force_ascii) {
    return !force_ascii && locale_is_utf8(codeset, mb_cur_max);
}

// Gauge glyphs.  Unicode block elements look better; ASCII remains readable on
// terminals whose font lacks them.
inline const char* full(bool unicode) {
    return unicode ? "█" : "#";
}
inline const char* empty(bool unicode) {
    return unicode ? "░" : "-";
}
inline const char* part(bool unicode, int e) {
    if (!unicode) {
        if (e <= 0)
            return " ";
        if (e < 4)
            return ".";
        if (e < 8)
            return "+";
        return "#";
    }
    static const char* t[] = {" ", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"};
    if (e < 0)
        e = 0;
    if (e > 8)
        e = 8;
    return t[e];
}

// History graph cell.  Unicode uses two samples per Braille cell; the ASCII
// fallback uses one coarse level per cell.
inline const char* graph(bool unicode, int l, int r, bool down) {
    if (!unicode) {
        int level = std::max(l, r);
        if (level <= 0)
            return " ";
        if (level == 1)
            return ".";
        if (level == 2)
            return "+";
        if (level == 3)
            return "=";
        return "#";
    }

    static const char* up_tbl[25] = {
        " ", "⢀", "⢠", "⢰", "⢸", "⡀", "⣀", "⣠", "⣰", "⣸", "⡄", "⣄", "⣤",
        "⣴", "⣼", "⡆", "⣆", "⣦", "⣶", "⣾", "⡇", "⣇", "⣧", "⣷", "⣿",
    };
    static const char* down_tbl[25] = {
        " ", "⠈", "⠘", "⠸", "⢸", "⠁", "⠉", "⠙", "⠹", "⢹", "⠃", "⠋", "⠛",
        "⠻", "⢻", "⠇", "⠏", "⠟", "⠿", "⢿", "⡇", "⡏", "⡟", "⡿", "⣿",
    };
    if (l < 0)
        l = 0;
    if (l > 4)
        l = 4;
    if (r < 0)
        r = 0;
    if (r > 4)
        r = 4;
    return down ? down_tbl[l * 5 + r] : up_tbl[l * 5 + r];
}

inline const char* up_down(bool unicode) {
    return unicode ? "↕" : "|";
}
inline const char* up_down_arrows(bool unicode) {
    return unicode ? "↑↓" : "^v";
}
inline const char* connected_dot(bool unicode) {
    return unicode ? "●" : "*";
}
inline const char* em_dash(bool unicode) {
    return unicode ? "—" : "-";
}

} // namespace ntrickle::tui
