// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#include "top_tui.hpp"

#include <cerrno>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "bandwidth.hpp"
#include "ntrickle/version.hpp"

#include <algorithm>
#include <ncurses.h>

namespace ntrickle {

void HistoryRing::push(uint64_t dl, uint64_t ul) {
    dl_samples[head] = dl;
    ul_samples[head] = ul;
    head = (head + 1) % kMaxSamples;
    if (count < kMaxSamples)
        ++count;
}
void HistoryRing::clear() {
    head = count = 0;
    std::memset(dl_samples, 0, sizeof(dl_samples));
    std::memset(ul_samples, 0, sizeof(ul_samples));
}

namespace color {
enum : int { BORDER = 1, GREEN = 2, YELLOW = 3, RED = 4, MAGENTA = 5, DIM = 6 };
}

static constexpr int kMinTermRows = 32;
static constexpr int kMinTermCols = 110;

namespace glyph {
const char* full() {
    return "█";
}
const char* empty() {
    return "░";
}
const char* part(int e) {
    static const char* t[] = {" ", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"};
    if (e < 0)
        e = 0;
    if (e > 8)
        e = 8;
    return t[e];
}

// Pre-computed Braille lookup tables mapping (left_level * 5 + right_level) to a
// Unicode Braille character.  Levels are 0–4 (0 = empty band, 4 = full band).
// Adapted from btop's proven approach.
// braille_up: fill grows upward from the bottom of the character cell.
// braille_down: fill grows downward from the top of the character cell.
const char* braille_up(int l, int r) {
    static const char* tbl[25] = {
        " ", "⢀", "⢠", "⢰", "⢸", "⡀", "⣀", "⣠", "⣰", "⣸", "⡄", "⣄", "⣤",
        "⣴", "⣼", "⡆", "⣆", "⣦", "⣶", "⣾", "⡇", "⣇", "⣧", "⣷", "⣿",
    };
    if (l < 0)
        l = 0;
    if (l > 4)
        l = 4;
    if (r < 0)
        r = 0;
    if (r > 4)
        r = 4;
    return tbl[l * 5 + r];
}
const char* braille_down(int l, int r) {
    static const char* tbl[25] = {
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
    return tbl[l * 5 + r];
}
} // namespace glyph

static std::string fmt_dur(uint32_t s) {
    if (s < 60)
        return std::to_string(s) + "s";
    if (s < 3600) {
        char b[8];
        std::snprintf(b, sizeof(b), "%d:%02d", s / 60, s % 60);
        return b;
    }
    auto h = s / 3600, m = (s % 3600) / 60;
    char b[16];
    std::snprintf(b, sizeof(b), "%d:%02d:%02d", h, m, s % 60);
    return b;
}
static int rate_pct(uint64_t rate, uint64_t limit) {
    if (limit == 0)
        return 0;
    auto p = (int) (rate * 100 / limit);
    return std::min(p, 999);
}
static std::string fmt_pct(uint64_t rate, uint64_t limit) {
    if (limit == 0)
        return "  - ";
    int p = rate_pct(rate, limit);
    if (p >= 100) {
        char b[16];
        std::snprintf(b, sizeof(b), " MAX");
        return b;
    }
    char b[16];
    std::snprintf(b, sizeof(b), "%3d%%", p);
    return b;
}
// Fixed-width rate string.  Unlimited → "       -".
static std::string fmt_rate_fixed(uint64_t bps) {
    if (bps == 0)
        return "       -";
    auto s = format_bandwidth(bps);
    // pad to at least 9 chars so gauges don't jump around
    while (s.size() < 9)
        s = " " + s;
    return s;
}
// Fixed-width compact rate (6 chars, right-aligned) for table cells.
// Produces strings like "     0", "  500K", " 1.50M", " 10.5M", " 100M".
static std::string fmt_rate_short_fixed(uint64_t bps) {
    auto s = format_bandwidth_short(bps);
    while (s.size() < 6)
        s = " " + s;
    return s;
}
// Fixed-width table cell: "<rate6> <pct4>" — 11 characters total.
// Rate is 6 chars right-aligned, pct is always 4 chars.
static std::string fmt_rate_cell(uint64_t rate, uint64_t limit) {
    auto rate_str = fmt_rate_short_fixed(rate);
    auto pct_str = fmt_pct(rate, limit);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%s %s", rate_str.c_str(), pct_str.c_str());
    return buf;
}
static std::string clip(const std::string& s, size_t w) {
    if (w == 0)
        return "";
    if (s.size() <= w)
        return s;
    if (w <= 3)
        return s.substr(0, w);
    return s.substr(0, w - 2) + "..";
}
static int gauge_color(int pct) {
    if (pct >= 90)
        return COLOR_PAIR(color::RED);
    if (pct >= 70)
        return COLOR_PAIR(color::YELLOW);
    return COLOR_PAIR(color::GREEN);
}
// Like gauge_color but takes raw rate+limit
static int rate_color(uint64_t rate, uint64_t limit) {
    return gauge_color(rate_pct(rate, limit));
}

static void draw_gauge(int y, int x, int w, uint64_t rate, uint64_t limit) {
    if (w < 3)
        return;
    int pct = rate_pct(rate, limit);
    double cells = (double) pct / 100.0 * (double) w;
    int full = (int) cells, frac = (int) ((cells - full) * 8.0);
    if (full > w)
        full = w;
    if (full == w)
        frac = 0;
    int attr = gauge_color(pct);
    move(y, x);
    attron(attr);
    for (int i = 0; i < full; ++i)
        addstr(glyph::full());
    if (frac > 0 && full < w) {
        addstr(glyph::part(frac));
        ++full;
    }
    attroff(attr);
    int rem = w - full;
    if (rem > 0) {
        attron(COLOR_PAIR(color::DIM));
        for (int i = 0; i < rem; ++i)
            addstr(glyph::empty());
        attroff(COLOR_PAIR(color::DIM));
    }
}

// Render one row of a Braille history graph.  Each Braille column encodes two
// consecutive time samples: the left half of the glyph = the older sample, the
// right half = the newer sample.  "band_idx" numbers the value bands from the
// baseline (0 = band closest to separator) outward.  Adapted from btop's
// proven Braille graph algorithm.
static void draw_graph_row(int y, int x, int pad_left, int data_cols, const uint64_t* samples,
                           int count, int ring_head, int ring_size, uint64_t max_val, int height,
                           int band_idx, bool /*reversed*/, bool down) {
    if (data_cols < 1 || max_val == 0 || height < 1)
        return;
    move(y, x);
    // The value range this band covers
    double band_h = (double) max_val / height;
    double band_lo = band_h * (double) band_idx;
    double band_hi = band_h * (double) (band_idx + 1);

    for (int ci = 0; ci < pad_left; ++ci)
        addstr(" ");
    for (int ci = 0; ci < data_cols; ++ci) {
        // Each Braille column = 2 consecutive samples.
        // Rightmost column = newest pair (count-2, count-1).
        // When the ring isn't full yet, older columns are empty.
        int i0 = count - 2 * (data_cols - ci);
        int i1 = i0 + 1;
        if (i1 < 0) {
            addstr(" ");
            continue;
        } // no data for this column yet

        // i0 may be -1: oldest sample sits alone in the right half.
        int idx0 = (i0 >= 0) ? (ring_head - count + i0 + ring_size) % ring_size : -1;
        int idx1 = (i1 >= 0 && i1 < count) ? (ring_head - count + i1 + ring_size) % ring_size : -1;

        double v0 = (idx0 >= 0) ? (double) samples[idx0] : 0;
        double v1 = (idx1 >= 0) ? (double) samples[idx1] : 0;

        // Map 0–4 level within this band (btop-style)
        int l, r;
        if (idx0 < 0)
            l = 0;
        else if (v0 >= band_hi)
            l = 4;
        else if (v0 <= band_lo)
            l = 0;
        else
            l = (int) std::round((v0 - band_lo) * 4.0 / (band_hi - band_lo) + 0.1);

        if (idx1 < 0)
            r = 0;
        else if (v1 >= band_hi)
            r = 4;
        else if (v1 <= band_lo)
            r = 0;
        else
            r = (int) std::round((v1 - band_lo) * 4.0 / (band_hi - band_lo) + 0.1);

        addstr(down ? glyph::braille_down(l, r) : glyph::braille_up(l, r));
    }
}

static void hsep(int y, int L, int R) {
    attron(COLOR_PAIR(color::BORDER));
    mvaddch(y, L, ACS_LTEE);
    for (int i = L + 1; i < R; ++i)
        mvaddch(y, i, ACS_HLINE);
    mvaddch(y, R, ACS_RTEE);
    attroff(COLOR_PAIR(color::BORDER));
}

// ============================================================================
// Simplified layout — single table area, no user/session split.
// ============================================================================
struct RC {
    int W, H, L, R;
    int row_hdr, row_info, row_dl, row_ul;
    int row_hist_dl, row_hist_sep, row_hist_ul, hist_h, row_sep;
    int row_tbl_hdr, row_tbl_cols, row_tbl_data, row_tbl_end, row_footer;
    int max_vis;
    bool wide;

    // Table columns (ungrouped)
    int t_pid_x, t_user_x, t_dl_x, t_ul_x, t_conn_x, t_cmd_x;
    int t_pid_w, t_user_w, t_dl_w, t_ul_w, t_conn_w, t_cmd_w;
};

static RC compute_layout(int W, int H, int /*n_sessions*/) {
    RC rc{};
    rc.W = W;
    rc.H = H;
    rc.L = 1;
    rc.R = W - 2;
    rc.wide = (W >= 110);

    // History height: adapt to terminal size
    if (H >= 40)
        rc.hist_h = 4;
    else if (H >= 32)
        rc.hist_h = 3;
    else if (H >= 26)
        rc.hist_h = 2;
    else
        rc.hist_h = 1;

    rc.row_hdr = 1;
    rc.row_info = 2;
    rc.row_dl = 4;
    rc.row_ul = 5;
    rc.row_hist_dl = 7;
    rc.row_hist_sep = rc.row_hist_dl + rc.hist_h;
    rc.row_hist_ul = rc.row_hist_sep + 1;
    rc.row_sep = rc.row_hist_ul + rc.hist_h;
    rc.row_tbl_hdr = rc.row_sep + 1;
    rc.row_tbl_cols = rc.row_tbl_hdr + 1;
    rc.row_tbl_data = rc.row_tbl_cols + 1;
    rc.row_footer = H - 1;
    rc.row_tbl_end = H - 2; // separator above footer
    rc.max_vis = std::max(0, rc.row_tbl_end - rc.row_tbl_data);

    // Single table column layout: PID | User | DL | UL | Conn | Command
    int cw = rc.R - rc.L - 1; // usable width inside borders
    int pad = 1;
    rc.t_pid_w = std::min(6, cw / 10);
    rc.t_user_w = std::min(8, std::max(5, cw / 10));
    rc.t_dl_w = std::max(9, cw / 8);
    rc.t_ul_w = std::max(9, cw / 8);
    rc.t_conn_w = std::min(6, cw / 10);

    int fixed = rc.t_pid_w + rc.t_user_w + rc.t_dl_w + rc.t_ul_w + rc.t_conn_w + pad * 5;
    rc.t_cmd_w = std::max(8, cw - fixed - 2);
    // If we overflow, shrink DL/UL
    if (rc.t_cmd_w < 8) {
        rc.t_cmd_w = 8;
        rc.t_dl_w = std::max(7, rc.t_dl_w - 3);
        rc.t_ul_w = std::max(7, rc.t_ul_w - 3);
    }

    int x = rc.L + 2;
    rc.t_pid_x = x;
    x += rc.t_pid_w + pad;
    rc.t_user_x = x;
    x += rc.t_user_w + pad;
    rc.t_dl_x = x;
    x += rc.t_dl_w + pad;
    rc.t_ul_x = x;
    x += rc.t_ul_w + pad;
    rc.t_conn_x = x;
    x += rc.t_conn_w + pad;
    rc.t_cmd_x = x;

    // Ensure command column doesn't exceed right border
    if (rc.t_cmd_x + rc.t_cmd_w > rc.R)
        rc.t_cmd_w = std::max(4, rc.R - rc.t_cmd_x);

    return rc;
}

TopTui::TopTui(const std::string& ctl_path, bool once, double interval)
    : ctl_path_(ctl_path), once_(once), interval_sec_(std::max(0.25, std::min(interval, 60.0))) {
}
TopTui::~TopTui() = default;

bool TopTui::fetch_data() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        error_msg_ = std::string("socket: ") + std::strerror(errno);
        connected_ = false;
        return false;
    }
    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, ctl_path_.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
        error_msg_ = "cannot reach daemon at " + ctl_path_;
        connected_ = false;
        close(fd);
        return false;
    }
    const char* req = R"({"cmd":"stats"})"
                      "\n";
    size_t req_len = std::strlen(req), sent = 0;
    while (sent < req_len) {
        ssize_t n = write(fd, req + sent, req_len - sent);
        if (n <= 0) {
            error_msg_ = "write error";
            connected_ = false;
            close(fd);
            return false;
        }
        sent += (size_t) n;
    }
    char buf[16384];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        error_msg_ = "no response from daemon";
        connected_ = false;
        return false;
    }
    buf[n] = '\0';
    try {
        auto j = nlohmann::json::parse(buf);
        if (j.contains("error")) {
            error_msg_ = j["error"];
            connected_ = false;
            return false;
        }
        auto& g = j["global"];
        data_.global.dl_limit = g.value("download_limit_bps", 0ULL);
        data_.global.ul_limit = g.value("upload_limit_bps", 0ULL);
        data_.global.fairness_mode = g.value("fairness_mode", std::string("static"));
        data_.global.idle_threshold_sec = g.value("idle_threshold_sec", 3U);
        data_.global.burst_seconds = g.value("burst_seconds", 0U);
        data_.global.active_schedule = g.value("active_schedule", std::string());
        data_.global.uptime_sec = g.value("uptime_sec", 0U);
        data_.users.clear();
        for (auto& u : j["users"]) {
            TopUser tu;
            tu.uid = u.value("uid", 0U);
            tu.name = u.value("name", std::string("?"));
            tu.weight = u.value("weight", 1U);
            tu.dl_rate = u.value("download_rate_bps", 0ULL);
            tu.ul_rate = u.value("upload_rate_bps", 0ULL);
            tu.dl_bytes = u.value("bytes_downloaded", 0ULL);
            tu.ul_bytes = u.value("bytes_uploaded", 0ULL);
            tu.session_count = u.value("session_count", 0U);
            tu.dl_cap = u.value("download_cap_bps", 0ULL);
            tu.ul_cap = u.value("upload_cap_bps", 0ULL);
            tu.is_idle = u.value("is_idle", false);
            data_.users.push_back(tu);
        }
        std::sort(data_.users.begin(), data_.users.end(), [](const TopUser& a, const TopUser& b) {
            return (a.dl_rate + a.ul_rate) > (b.dl_rate + b.ul_rate);
        });
        data_.sessions.clear();
        for (auto& s : j["sessions"]) {
            TopSession ts;
            ts.pid = s.value("pid", 0);
            ts.uid = s.value("uid", 0U);
            ts.command = s.value("argv0", std::string("?"));
            ts.dl_rate = s.value("download_rate_bps", 0ULL);
            ts.ul_rate = s.value("upload_rate_bps", 0ULL);
            ts.dl_bytes = s.value("bytes_downloaded", 0ULL);
            ts.ul_bytes = s.value("bytes_uploaded", 0ULL);
            ts.grant_dl = s.value("grant_download_bps", 0ULL);
            ts.grant_ul = s.value("grant_upload_bps", 0ULL);
            ts.connected_sec = s.value("connected_sec", 0U);
            ts.is_idle = s.value("is_idle", false);
            for (auto& tu : data_.users) {
                if (tu.uid == ts.uid) {
                    ts.user = tu.name;
                    break;
                }
            }
            if (ts.user.empty())
                ts.user = std::to_string(ts.uid);
            data_.sessions.push_back(ts);
        }
        sort_sessions();
        uint64_t td = 0, tu = 0;
        for (auto& u : data_.users) {
            td += u.dl_rate;
            tu += u.ul_rate;
        }
        history_.push(td, tu);
        data_.has_data = true;
        connected_ = true;
        error_msg_.clear();
        return true;
    } catch (const std::exception& e) {
        error_msg_ = std::string("parse: ") + e.what();
        connected_ = false;
        return false;
    }
}

void TopTui::sort_sessions() {
    auto cmp = [this](const TopSession& a, const TopSession& b) {
        bool r;
        switch (sort_col_) {
        case SortCol::DL_RATE:
            r = a.dl_rate < b.dl_rate;
            break;
        case SortCol::UL_RATE:
            r = a.ul_rate < b.ul_rate;
            break;
        case SortCol::PID:
            r = a.pid < b.pid;
            break;
        case SortCol::COMMAND:
            r = a.command < b.command;
            break;
        case SortCol::USER:
            r = a.user < b.user;
            break;
        default:
            r = (a.dl_rate + a.ul_rate) < (b.dl_rate + b.ul_rate);
            break;
        }
        return sort_desc_ ? !r : r;
    };
    std::sort(data_.sessions.begin(), data_.sessions.end(), cmp);
}

// ============================================================================
// Unified table row counting
// ============================================================================
int TopTui::table_row_count() const {
    if (!group_by_user_)
        return (int) data_.sessions.size();
    // Grouped: one header per user + their sessions
    int n = 0;
    for (auto& u : data_.users) {
        ++n; // user header
        for (auto& s : data_.sessions)
            if ((uid_t) s.uid == u.uid)
                ++n;
    }
    return n;
}

bool TopTui::render_display_row(int display_idx, int screen_y, const RC& rc) {
    if (!group_by_user_) {
        // Simple: one row per session
        if (display_idx < 0 || display_idx >= (int) data_.sessions.size())
            return false;
        auto& s = data_.sessions[display_idx];
        if (s.is_idle)
            attron(COLOR_PAIR(color::DIM));
        // PID
        char pb[8];
        std::snprintf(pb, sizeof(pb), "%5d", s.pid);
        mvaddstr(screen_y, rc.t_pid_x, pb);
        // User
        mvaddstr(screen_y, rc.t_user_x, clip(s.user, rc.t_user_w).c_str());
        // DL — colour like gauge, show MAX when ≥100%
        {
            attron(rate_color(s.dl_rate, data_.global.dl_limit));
            auto dl_cell = fmt_rate_cell(s.dl_rate, data_.global.dl_limit);
            mvaddstr(screen_y, rc.t_dl_x, clip(dl_cell, rc.t_dl_w).c_str());
            attroff(rate_color(s.dl_rate, data_.global.dl_limit));
        }
        // UL
        {
            attron(rate_color(s.ul_rate, data_.global.ul_limit));
            auto ul_cell = fmt_rate_cell(s.ul_rate, data_.global.ul_limit);
            mvaddstr(screen_y, rc.t_ul_x, clip(ul_cell, rc.t_ul_w).c_str());
            attroff(rate_color(s.ul_rate, data_.global.ul_limit));
        }
        // Conn
        mvaddstr(screen_y, rc.t_conn_x, clip(fmt_dur(s.connected_sec), rc.t_conn_w).c_str());
        // Command
        mvaddstr(screen_y, rc.t_cmd_x, clip(s.command, rc.t_cmd_w).c_str());
        if (s.is_idle)
            attroff(COLOR_PAIR(color::DIM));
        return true;
    }

    // Grouped: walk users and their sessions to find display_idx
    int idx = 0;
    for (auto& u : data_.users) {
        if (idx == display_idx) {
            // User header row
            attron(A_BOLD);
            uint64_t u_dl = 0, u_ul = 0;
            int u_active = 0, u_total = 0;
            for (auto& s : data_.sessions) {
                if ((uid_t) s.uid != u.uid)
                    continue;
                u_dl += s.dl_rate;
                u_ul += s.ul_rate;
                ++u_total;
                if (!s.is_idle)
                    ++u_active;
            }
            char hdr[128];
            int hdr_w = rc.t_dl_x - rc.t_pid_x;
            std::snprintf(hdr, sizeof(hdr), "▸ %s  w%u", u.name.c_str(), u.weight);
            mvaddstr(screen_y, rc.t_pid_x, clip(hdr, hdr_w).c_str());
            // DL
            {
                attron(rate_color(u_dl, data_.global.dl_limit));
                auto dl_cell = fmt_rate_cell(u_dl, data_.global.dl_limit);
                mvaddstr(screen_y, rc.t_dl_x, clip(dl_cell, rc.t_dl_w).c_str());
                attroff(rate_color(u_dl, data_.global.dl_limit));
            }
            // UL
            {
                attron(rate_color(u_ul, data_.global.ul_limit));
                auto ul_cell = fmt_rate_cell(u_ul, data_.global.ul_limit);
                mvaddstr(screen_y, rc.t_ul_x, clip(ul_cell, rc.t_ul_w).c_str());
                attroff(rate_color(u_ul, data_.global.ul_limit));
            }
            // Session count
            char sb[16];
            std::snprintf(sb, sizeof(sb), "%u/%u", u_active, u_total);
            mvaddstr(screen_y, rc.t_conn_x, sb);
            if (u.is_idle) {
                attron(A_DIM);
                mvaddstr(screen_y, rc.t_cmd_x, "idle");
                attroff(A_DIM);
            }
            attroff(A_BOLD);
            return true;
        }
        ++idx;
        for (auto& s : data_.sessions) {
            if ((uid_t) s.uid != u.uid)
                continue;
            if (idx == display_idx) {
                // Indented session row
                if (s.is_idle)
                    attron(COLOR_PAIR(color::DIM));
                int indent = 2;
                char pb[8];
                std::snprintf(pb, sizeof(pb), "%5d", s.pid);
                mvaddstr(screen_y, rc.t_pid_x + indent, pb);
                {
                    attron(rate_color(s.dl_rate, data_.global.dl_limit));
                    auto dl_cell = fmt_rate_cell(s.dl_rate, data_.global.dl_limit);
                    mvaddstr(screen_y, rc.t_dl_x, clip(dl_cell, rc.t_dl_w).c_str());
                    attroff(rate_color(s.dl_rate, data_.global.dl_limit));
                }
                {
                    attron(rate_color(s.ul_rate, data_.global.ul_limit));
                    auto ul_cell = fmt_rate_cell(s.ul_rate, data_.global.ul_limit);
                    mvaddstr(screen_y, rc.t_ul_x, clip(ul_cell, rc.t_ul_w).c_str());
                    attroff(rate_color(s.ul_rate, data_.global.ul_limit));
                }
                mvaddstr(screen_y, rc.t_conn_x,
                         clip(fmt_dur(s.connected_sec), rc.t_conn_w).c_str());
                mvaddstr(screen_y, rc.t_cmd_x, clip(s.command, rc.t_cmd_w).c_str());
                if (s.is_idle)
                    attroff(COLOR_PAIR(color::DIM));
                return true;
            }
            ++idx;
        }
    }
    return false;
}

// ============================================================================
// Render pipeline
// ============================================================================
void TopTui::render() {
    getmaxyx(stdscr, term_rows_, term_cols_);
    erase();

    if (term_rows_ < kMinTermRows || term_cols_ < kMinTermCols) {
        // Terminal too small — show a centred notice box.
        const char* msg1 = "Terminal too small";
        char msg2[64];
        std::snprintf(msg2, sizeof(msg2), "Need at least %dx%d (current: %dx%d)", kMinTermCols,
                      kMinTermRows, term_cols_, term_rows_);
        const char* msg3 = "Resize the window or increase the font size.";
        int bw = 52, bh = 7;
        int by = std::max(0, (term_rows_ - bh) / 2);
        int bx = std::max(0, (term_cols_ - bw) / 2);
        attron(COLOR_PAIR(color::RED) | A_BOLD);
        mvaddch(by, bx, ACS_ULCORNER);
        mvhline(by, bx + 1, ACS_HLINE, bw - 2);
        mvaddch(by, bx + bw - 1, ACS_URCORNER);
        for (int r = 1; r < bh - 1; ++r) {
            mvaddch(by + r, bx, ACS_VLINE);
            mvaddch(by + r, bx + bw - 1, ACS_VLINE);
        }
        mvaddch(by + bh - 1, bx, ACS_LLCORNER);
        mvhline(by + bh - 1, bx + 1, ACS_HLINE, bw - 2);
        mvaddch(by + bh - 1, bx + bw - 1, ACS_LRCORNER);
        attroff(COLOR_PAIR(color::RED) | A_BOLD);
        mvaddstr(by + 2, bx + (bw - (int) std::strlen(msg1)) / 2, msg1);
        mvaddstr(by + 3, bx + (bw - (int) std::strlen(msg2)) / 2, msg2);
        mvaddstr(by + 4, bx + (bw - (int) std::strlen(msg3)) / 2, msg3);
        mvaddstr(by + 5, bx + (bw - 2) / 2, "q to quit");
        refresh();
        return;
    }

    auto rc = compute_layout(term_cols_, term_rows_, (int) data_.sessions.size());

    // Outer border
    attron(COLOR_PAIR(color::BORDER));
    mvaddch(0, 0, ACS_ULCORNER);
    mvhline(0, 1, ACS_HLINE, term_cols_ - 2);
    mvaddch(0, term_cols_ - 1, ACS_URCORNER);
    for (int r = 1; r < term_rows_ - 1; ++r) {
        mvaddch(r, 0, ACS_VLINE);
        mvaddch(r, term_cols_ - 1, ACS_VLINE);
    }
    mvaddch(term_rows_ - 1, 0, ACS_LLCORNER);
    mvhline(term_rows_ - 1, 1, ACS_HLINE, term_cols_ - 2);
    mvaddch(term_rows_ - 1, term_cols_ - 1, ACS_LRCORNER);
    attroff(COLOR_PAIR(color::BORDER));

    render_header();
    render_info(rc);
    render_gauges(rc);
    render_graphs(rc);
    render_table(rc);
    render_footer();
    if (show_help_)
        render_help();
    if (!connected_ && !error_msg_.empty()) {
        const char* title = "Connection Error";
        int bw = std::max(50, (int) error_msg_.size() + 10);
        int bh = 7;
        int by = std::max(0, (term_rows_ - bh) / 2);
        int bx = std::max(0, (term_cols_ - bw) / 2);

        // Dim the screen behind the box
        attron(COLOR_PAIR(color::DIM));
        for (int r = 0; r < term_rows_; ++r) {
            move(r, 0);
            for (int c = 0; c < term_cols_; ++c)
                addstr(" ");
        }
        attroff(COLOR_PAIR(color::DIM));

        // Red border box
        attron(COLOR_PAIR(color::RED) | A_BOLD);
        mvaddch(by, bx, ACS_ULCORNER);
        mvhline(by, bx + 1, ACS_HLINE, bw - 2);
        mvaddch(by, bx + bw - 1, ACS_URCORNER);
        for (int r = 1; r < bh - 1; ++r) {
            mvaddch(by + r, bx, ACS_VLINE);
            mvaddch(by + r, bx + bw - 1, ACS_VLINE);
        }
        mvaddch(by + bh - 1, bx, ACS_LLCORNER);
        mvhline(by + bh - 1, bx + 1, ACS_HLINE, bw - 2);
        mvaddch(by + bh - 1, bx + bw - 1, ACS_LRCORNER);
        attroff(COLOR_PAIR(color::RED) | A_BOLD);

        mvaddstr(by + 1, bx + (bw - (int) std::strlen(title)) / 2, title);
        mvaddstr(by + 3, bx + (bw - (int) error_msg_.size()) / 2, error_msg_.c_str());
        mvaddstr(by + 4, bx + (bw - 27) / 2, "Is the ntrickled daemon running?");
        attron(A_BOLD);
        mvaddstr(by + 5, bx + (bw - 25) / 2, "Retrying automatically ...");
        attroff(A_BOLD);
    }
    refresh();
}

void TopTui::render_header() {
    move(1, 2);
    if (connected_) {
        attron(COLOR_PAIR(color::GREEN) | A_BOLD);
        addstr("\xe2\x97\x8f");
        attroff(COLOR_PAIR(color::GREEN) | A_BOLD);
    } else {
        attron(COLOR_PAIR(color::RED) | A_BOLD);
        addstr("\xe2\x97\x8f");
        attroff(COLOR_PAIR(color::RED) | A_BOLD);
    }
    attron(A_BOLD);
    addstr(" ntrickle top");
    attroff(A_BOLD);
    addstr(" v" NTRICKLE_PROJECT_VERSION "  ");
    int avail = term_cols_ - 30 - 22;
    if (avail > 5) {
        std::string s = ctl_path_;
        if ((int) s.size() > avail)
            s = s.substr(0, avail - 2) + "..";
        addstr(s.c_str());
    }
    char rb[48];
    std::snprintf(rb, sizeof(rb), "[%.1fs]%s", interval_sec_, paused_ ? " PAUSED" : "");
    int rl = (int) std::strlen(rb);
    if (rl < term_cols_ - 4)
        mvaddstr(1, term_cols_ - 2 - rl, rb);
}

void TopTui::render_info(const RC& rc) {
    (void) rc;
    const char* sched =
        (data_.global.active_schedule.empty() || data_.global.active_schedule == "none")
            ? "none"
            : data_.global.active_schedule.c_str();
    char ibuf[256];
    std::snprintf(ibuf, sizeof(ibuf),
                  "Uptime %s  \xc2\xb7  Fairness %s  \xc2\xb7  Schedule %s  \xc2\xb7  Idle %us  "
                  "\xc2\xb7  Burst %us",
                  fmt_dur(data_.global.uptime_sec).c_str(), data_.global.fairness_mode.c_str(),
                  sched, data_.global.idle_threshold_sec, data_.global.burst_seconds);
    mvaddstr(2, 3, ibuf);
}

void TopTui::render_gauges(const RC& rc) {
    (void) rc;
    uint64_t total_dl = 0, total_ul = 0;
    for (auto& u : data_.users) {
        total_dl += u.dl_rate;
        total_ul += u.ul_rate;
    }

    int gx = 14, gw = std::max(10, term_cols_ - gx - 34);

    mvaddstr(rc.row_dl, 3, "Download  ");
    draw_gauge(rc.row_dl, gx, gw, total_dl, data_.global.dl_limit);
    {
        auto pct = fmt_pct(total_dl, data_.global.dl_limit);
        auto cur = fmt_rate_fixed(total_dl);
        auto lim = fmt_rate_fixed(data_.global.dl_limit);
        char db[64];
        std::snprintf(db, sizeof(db), " %s %s/%s", pct.c_str(), cur.c_str(), lim.c_str());
        attron(rate_color(total_dl, data_.global.dl_limit));
        mvaddstr(rc.row_dl, gx + gw + 1, db);
        attroff(rate_color(total_dl, data_.global.dl_limit));
    }

    mvaddstr(rc.row_ul, 3, "Upload    ");
    draw_gauge(rc.row_ul, gx, gw, total_ul, data_.global.ul_limit);
    {
        auto pct = fmt_pct(total_ul, data_.global.ul_limit);
        auto cur = fmt_rate_fixed(total_ul);
        auto lim = fmt_rate_fixed(data_.global.ul_limit);
        char ub[64];
        std::snprintf(ub, sizeof(ub), " %s %s/%s", pct.c_str(), cur.c_str(), lim.c_str());
        attron(rate_color(total_ul, data_.global.ul_limit));
        mvaddstr(rc.row_ul, gx + gw + 1, ub);
        attroff(rate_color(total_ul, data_.global.ul_limit));
    }
}

void TopTui::render_graphs(const RC& rc) {
    uint64_t total_dl = 0, total_ul = 0;
    for (auto& u : data_.users) {
        total_dl += u.dl_rate;
        total_ul += u.ul_rate;
    }

    uint64_t dl_max = data_.global.dl_limit ? data_.global.dl_limit : total_dl,
             ul_max = data_.global.ul_limit ? data_.global.ul_limit : total_ul;
    if (dl_max == 0)
        dl_max = 1;
    if (ul_max == 0)
        ul_max = 1;
    if (!data_.global.dl_limit) {
        uint64_t om = 0;
        for (int i = 0; i < history_.count; ++i) {
            int idx = (history_.head - history_.count + i + HistoryRing::kMaxSamples) %
                      HistoryRing::kMaxSamples;
            if (history_.dl_samples[idx] > om)
                om = history_.dl_samples[idx];
        }
        if (om > 0)
            dl_max = om * 11 / 10;
    }
    if (!data_.global.ul_limit) {
        uint64_t om = 0;
        for (int i = 0; i < history_.count; ++i) {
            int idx = (history_.head - history_.count + i + HistoryRing::kMaxSamples) %
                      HistoryRing::kMaxSamples;
            if (history_.ul_samples[idx] > om)
                om = history_.ul_samples[idx];
        }
        if (om > 0)
            ul_max = om * 11 / 10;
    }

    // Graph width: each Braille column = 2 seconds.  Max 5 minutes (300 s).
    // Narrow screen → less history shown.  Wide screen → up to 300 s, then
    // the graph is centred with padding.
    constexpr int kMaxHistorySecs = 300;
    int ghx = 9; // space for: 3-pad + "DL" + 2-pad + delimiter (9 cols)
    int graph_area_w = term_cols_ - ghx - 16; // available width, right side needs ~16 cols
    if (graph_area_w < 10)
        graph_area_w = 10;
    int max_braille = kMaxHistorySecs / 2; // 150 cols = 300 s
    int cols = std::min(graph_area_w, max_braille); // never exceed 5 m
    int pad_left = 0;
    if (graph_area_w > cols)
        pad_left = (graph_area_w - cols) / 2; // centre on wide terms
    int disp_count = std::min(history_.count, cols * 2);

    // -- DL graph: bottom-up, green --
    int g_left = ghx + pad_left; // first braille data column
    int g_right = g_left + cols - 1; // last braille data column
    int lbl_x = std::max(1, g_left - 8); // 3-sp-pad + "DL" + 2-sp-pad before delimiter
    attron(COLOR_PAIR(color::GREEN));
    for (int r = 0; r < rc.hist_h; ++r) {
        int yrow = rc.row_hist_dl + r;
        int band = rc.hist_h - 1 - r;
        draw_graph_row(yrow, ghx, pad_left, cols, history_.dl_samples, disp_count, history_.head,
                       HistoryRing::kMaxSamples, dl_max, rc.hist_h, band, true, false);
        // Delimiters + label on top so padding never hides them.
        attroff(COLOR_PAIR(color::GREEN));
        attron(COLOR_PAIR(color::BORDER));
        mvaddch(yrow, g_left - 1, ACS_VLINE);
        mvaddch(yrow, g_right + 1, ACS_VLINE);
        attroff(COLOR_PAIR(color::BORDER));
        // Label in graph colour (green)
        attron(COLOR_PAIR(color::GREEN) | A_BOLD);
        mvaddstr(yrow, lbl_x, r == 0 ? "   DL  " : "       ");
        attroff(COLOR_PAIR(color::GREEN) | A_BOLD);
        attron(COLOR_PAIR(color::GREEN));
    }
    attroff(COLOR_PAIR(color::GREEN));
    char ylbl[16];
    std::snprintf(ylbl, sizeof(ylbl), "%s", format_bandwidth(dl_max).c_str());
    if (g_right + 3 < term_cols_ - 3)
        mvaddstr(rc.row_hist_dl, g_right + 3, ylbl);

    // -- Separator with time labels --
    {
        int sep_y = rc.row_hist_sep;
        attron(COLOR_PAIR(color::BORDER));
        mvaddch(sep_y, 0, ACS_LTEE);
        for (int i = 1; i < term_cols_ - 1; ++i)
            mvaddch(sep_y, i, ACS_HLINE);
        mvaddch(sep_y, term_cols_ - 1, ACS_RTEE);
        attroff(COLOR_PAIR(color::BORDER));
        attron(COLOR_PAIR(color::BORDER));
        attron(A_DIM);
        int max_secs = std::min(cols * 2, kMaxHistorySecs);
        int step = 30;
        if (cols < 20)
            step = 60;
        else if (cols < 45)
            step = 30;
        else if (cols < 90)
            step = 60;
        else
            step = 60;
        for (int secs = step; secs <= max_secs; secs += step) {
            int x = g_left + cols - 1 - secs / 2;
            if (x < g_left)
                break;
            char tl[16];
            if (secs >= 60)
                std::snprintf(tl, sizeof(tl), "%dm", secs / 60);
            else
                std::snprintf(tl, sizeof(tl), "%ds", secs);
            int sl = (int) std::strlen(tl);
            mvaddstr(sep_y, std::max(2, x - sl / 2), tl);
        }
        attroff(COLOR_PAIR(color::BORDER));
        attroff(A_DIM);
    }

    // -- UL graph: top-down, magenta --
    attron(COLOR_PAIR(color::MAGENTA));
    for (int r = 0; r < rc.hist_h; ++r) {
        int yrow = rc.row_hist_ul + r;
        int band = r;
        draw_graph_row(yrow, ghx, pad_left, cols, history_.ul_samples, disp_count, history_.head,
                       HistoryRing::kMaxSamples, ul_max, rc.hist_h, band, true, true);
        attroff(COLOR_PAIR(color::MAGENTA));
        attron(COLOR_PAIR(color::BORDER));
        mvaddch(yrow, g_left - 1, ACS_VLINE);
        mvaddch(yrow, g_right + 1, ACS_VLINE);
        attroff(COLOR_PAIR(color::BORDER));
        attron(COLOR_PAIR(color::MAGENTA) | A_BOLD);
        mvaddstr(yrow, lbl_x, r == 0 ? "   UL  " : "       ");
        attroff(COLOR_PAIR(color::MAGENTA) | A_BOLD);
        attron(COLOR_PAIR(color::MAGENTA));
    }
    attroff(COLOR_PAIR(color::MAGENTA));
    std::snprintf(ylbl, sizeof(ylbl), "%s", format_bandwidth(ul_max).c_str());
    if (g_right + 3 < term_cols_ - 3)
        mvaddstr(rc.row_hist_ul + rc.hist_h - 1, g_right + 3, ylbl);

    // Separator below graphs
    hsep(rc.row_sep, rc.L, rc.R);
}

void TopTui::render_table(const RC& rc) {
    // Section header
    int total_rows = table_row_count();
    const char* sn = "rate";
    switch (sort_col_) {
    case SortCol::DL_RATE:
        sn = "download";
        break;
    case SortCol::UL_RATE:
        sn = "upload";
        break;
    case SortCol::PID:
        sn = "pid";
        break;
    case SortCol::COMMAND:
        sn = "command";
        break;
    case SortCol::USER:
        sn = "user";
        break;
    default:
        break;
    }

    attron(COLOR_PAIR(color::BORDER) | A_BOLD);
    mvaddstr(rc.row_tbl_hdr, rc.L + 2, "Sessions");
    attroff(COLOR_PAIR(color::BORDER) | A_BOLD);
    char hdr_info[64];
    std::snprintf(hdr_info, sizeof(hdr_info), " [sort: %s %s]  group: %s", sn,
                  sort_desc_ ? "\xe2\x96\xbc" : "\xe2\x96\xb2", group_by_user_ ? "on" : "off");
    mvaddstr(rc.row_tbl_hdr, rc.L + 12, hdr_info);

    if (total_rows == 0) {
        attron(A_DIM);
        mvaddstr(rc.row_tbl_cols, rc.L + 4, "(no active sessions)");
        attroff(A_DIM);
        return;
    }

    // Column headers
    attron(A_BOLD);
    if (!group_by_user_) {
        mvaddstr(rc.row_tbl_cols, rc.t_pid_x, "PID");
        mvaddstr(rc.row_tbl_cols, rc.t_user_x, "User");
        mvaddstr(rc.row_tbl_cols, rc.t_dl_x, "DL");
        mvaddstr(rc.row_tbl_cols, rc.t_ul_x, "UL");
        mvaddstr(rc.row_tbl_cols, rc.t_conn_x, "Conn");
        mvaddstr(rc.row_tbl_cols, rc.t_cmd_x, "Command");
    } else {
        mvaddstr(rc.row_tbl_cols, rc.t_pid_x,
                 clip("User / PID", rc.t_dl_x - rc.t_pid_x - 1).c_str());
        mvaddstr(rc.row_tbl_cols, rc.t_dl_x, "DL");
        mvaddstr(rc.row_tbl_cols, rc.t_ul_x, "UL");
        mvaddstr(rc.row_tbl_cols, rc.t_conn_x, clip("Sess", rc.t_conn_w).c_str());
        mvaddstr(rc.row_tbl_cols, rc.t_cmd_x, clip("Command", rc.t_cmd_w).c_str());
    }
    attroff(A_BOLD);

    // Scroll bounds
    int max_vis = rc.max_vis;
    if (max_vis < 1)
        return;
    int max_scroll = std::max(0, total_rows - max_vis);
    if (table_scroll_ < 0)
        table_scroll_ = 0;
    if (table_scroll_ > max_scroll)
        table_scroll_ = max_scroll;

    // Scroll indicator
    if (max_scroll > 0) {
        char b[24];
        std::snprintf(b, sizeof(b), "[%d/%d]", table_scroll_ + 1, total_rows);
        mvaddstr(rc.row_tbl_hdr, rc.R - (int) std::strlen(b) - 1, b);
    }

    // Render visible rows
    for (int i = 0; i < max_vis && table_scroll_ + i < total_rows; ++i) {
        render_display_row(table_scroll_ + i, rc.row_tbl_data + i, rc);
    }

    // Separator above footer
    hsep(rc.row_tbl_end, rc.L, rc.R);
}

void TopTui::render_footer() {
    int fy = term_rows_ - 1;
    move(fy, 2);
    auto key = [&](const char* k) {
        attron(A_BOLD);
        addstr(k);
        attroff(A_BOLD);
    };
    key("q");
    addstr(" quit  ");
    key("p");
    addstr(" pause  ");
    key("g");
    addstr(" group  ");
    key("s");
    addstr(" sort  ");
    key("r");
    addstr(" reverse  ");
    key("h");
    addstr(" help  ");
    key("\xe2\x86\x95");
    addstr("/");
    key("jk");
    addstr(" scroll  ");
    key("\xe2\x86\x91\xe2\x86\x93");
    addstr(" PgUp PgDn Home End");

    const char* st;
    int attr;
    if (paused_) {
        st = " PAUSED ";
        attr = COLOR_PAIR(color::YELLOW) | A_BOLD;
    } else if (connected_) {
        st = " \xe2\x97\x8f connected ";
        attr = COLOR_PAIR(color::GREEN) | A_BOLD;
    } else {
        st = " \xe2\x97\x8f disconnected ";
        attr = COLOR_PAIR(color::RED) | A_BOLD;
    }
    int sl = (int) std::strlen(st);
    attron(attr);
    mvaddstr(fy, term_cols_ - 3 - sl, st);
    attroff(attr);
}

void TopTui::render_help() {
    int h = 30, w = 78;
    int y = std::max(2, (term_rows_ - h) / 2);
    int x = std::max(2, (term_cols_ - w) / 2);

    // Background
    attron(COLOR_PAIR(color::BORDER));
    for (int r = 0; r < h; ++r) {
        move(y + r, x);
        for (int c = 0; c < w; ++c)
            addstr(" ");
    }
    attroff(COLOR_PAIR(color::BORDER));

    // Border
    attron(COLOR_PAIR(color::BORDER) | A_BOLD);
    mvaddch(y, x, ACS_ULCORNER);
    mvhline(y, x + 1, ACS_HLINE, w - 2);
    mvaddch(y, x + w - 1, ACS_URCORNER);
    for (int r = 1; r < h - 1; ++r) {
        mvaddch(y + r, x, ACS_VLINE);
        mvaddch(y + r, x + w - 1, ACS_VLINE);
    }
    mvaddch(y + h - 1, x, ACS_LLCORNER);
    mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2);
    mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
    attroff(COLOR_PAIR(color::BORDER) | A_BOLD);

    auto H = [&](int row, const char* s) { mvaddstr(y + row, x + 2, s); };
    auto B = [&](int row, const char* s) {
        attron(A_BOLD);
        mvaddstr(y + row, x + 2, s);
        attroff(A_BOLD);
    };

    B(1, "ntrickle top  —  help");
    H(2, "");
    H(3, "  Gauges");
    H(4, "    Green / yellow / red = under 70% / 70-90% / 90%+ of limit.");
    H(5, "    The percentage and rate labels are also coloured.");
    H(6, "    \"MAX\" is shown when usage reaches or exceeds the limit.");
    H(7, "");
    H(8, "  Graphs");
    H(9, "    DL = total download bandwidth (green).");
    H(10, "    UL = total upload bandwidth (magenta).");
    H(11, "    Time labels on the separator.  Newest data on the right.");
    H(12, "    Vertical bars mark the graph boundaries.");
    H(13, "");
    H(14, "  Table");
    H(15, "    Columns: PID, User, DL rate, UL rate, Connected time, Command.");
    H(16, "    DL / UL columns are colour-coded like the gauges.");
    H(17, "    Idle sessions are dimmed to dark grey.");
    H(18, "    When grouped by user, user headers show aggregated rates.");
    H(19, "");
    H(20, "  Keys");
    H(21, "    q = quit                  p = pause / resume");
    H(22, "    h = this help screen      s = cycle sort column");
    H(23, "    r = reverse sort order    g = toggle group-by-user mode");
    H(24, "    j / k  or  up / down arrows  = scroll sessions one row");
    H(25, "    PgUp / PgDn = scroll one page");
    H(26, "    Home / End = jump to top / bottom of table");
    H(27, "");
    H(28, "");
    attron(A_BOLD);
    mvaddstr(y + 28, x + 2, "Press any key to close this help screen.");
    attroff(A_BOLD);
}

// ============================================================================
// Input
// ============================================================================
bool TopTui::handle_input(int ch) {
    if (show_help_) {
        show_help_ = false;
        return true;
    }
    switch (ch) {
    case 'q':
    case 'Q':
        return false;
    case 'p':
    case 'P':
        paused_ = !paused_;
        if (!paused_)
            error_msg_.clear();
        return true;
    case 'g':
    case 'G':
        group_by_user_ = !group_by_user_;
        table_scroll_ = 0;
        return true;
    case 's':
    case 'S':
        switch (sort_col_) {
        case SortCol::TOTAL_RATE:
            sort_col_ = SortCol::DL_RATE;
            break;
        case SortCol::DL_RATE:
            sort_col_ = SortCol::UL_RATE;
            break;
        case SortCol::UL_RATE:
            sort_col_ = SortCol::PID;
            break;
        case SortCol::PID:
            sort_col_ = SortCol::COMMAND;
            break;
        case SortCol::COMMAND:
            sort_col_ = SortCol::USER;
            break;
        case SortCol::USER:
            sort_col_ = SortCol::TOTAL_RATE;
            break;
        }
        sort_sessions();
        table_scroll_ = 0;
        return true;
    case 'r':
    case 'R':
        sort_desc_ = !sort_desc_;
        sort_sessions();
        return true;
    case KEY_UP:
    case 'k':
        if (table_scroll_ > 0)
            --table_scroll_;
        return true;
    case KEY_DOWN:
    case 'j':
        ++table_scroll_;
        return true;
    case KEY_PPAGE:
        table_scroll_ -= 10;
        if (table_scroll_ < 0)
            table_scroll_ = 0;
        return true;
    case KEY_NPAGE:
        table_scroll_ += 10;
        return true;
    case KEY_HOME:
        table_scroll_ = 0;
        return true;
    case KEY_END:
        table_scroll_ = 999999;
        return true;
    case 'h':
    case 'H':
    case '?':
        show_help_ = !show_help_;
        return true;
    case '\t':
    case KEY_RESIZE:
        return true;
    default:
        return true;
    }
}

// ============================================================================
// Main loop
// ============================================================================
int TopTui::run() {
    fetch_data();
    std::setlocale(LC_ALL, "");
    if (initscr() == nullptr) {
        std::fprintf(stderr, "ntrickle: failed to init terminal\n");
        return 1;
    }
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    set_escdelay(25);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(color::BORDER, COLOR_CYAN, -1);
        init_pair(color::GREEN, COLOR_GREEN, -1);
        init_pair(color::YELLOW, COLOR_YELLOW, -1);
        init_pair(color::RED, COLOR_RED, -1);
        init_pair(color::MAGENTA, COLOR_MAGENTA, -1);
        init_pair(color::DIM, COLOR_BLACK, -1);
    }
    if (once_) {
        render();
        napms(200);
        endwin();
        return 0;
    }
    timeout((int) (interval_sec_ * 1000.0));
    struct timeval last {};
    gettimeofday(&last, nullptr);
    while (running_) {
        if (!paused_) {
            struct timeval now;
            gettimeofday(&now, nullptr);
            struct timeval diff;
            timersub(&now, &last, &diff);
            if ((double) diff.tv_sec + (double) diff.tv_usec / 1e6 >= interval_sec_) {
                fetch_data();
                gettimeofday(&last, nullptr);
            }
        }
        render();
        int ch = getch();
        if (ch != ERR && !handle_input(ch))
            running_ = false;
        timeout((int) (interval_sec_ * 1000.0));
    }
    endwin();
    return 0;
}
} // namespace ntrickle
