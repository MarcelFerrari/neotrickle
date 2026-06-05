// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <ctime>

#include <string>
#include <vector>

namespace ntrickle {

struct TopGlobal {
    uint64_t dl_limit = 0;
    uint64_t ul_limit = 0;
    std::string fairness_mode;
    uint32_t idle_threshold_sec = 3;
    uint32_t burst_seconds = 0;
    std::string active_schedule;
    uint32_t uptime_sec = 0;
};

struct TopUser {
    uid_t uid = 0;
    std::string name;
    uint32_t weight = 1;
    uint64_t dl_rate = 0;
    uint64_t ul_rate = 0;
    uint64_t dl_bytes = 0;
    uint64_t ul_bytes = 0;
    uint32_t session_count = 0;
    uint64_t dl_cap = 0;
    uint64_t ul_cap = 0;
    bool is_idle = false;
};

struct TopSession {
    pid_t pid = 0;
    uid_t uid = 0;
    std::string user;
    std::string command;
    uint64_t dl_rate = 0;
    uint64_t ul_rate = 0;
    uint64_t dl_bytes = 0;
    uint64_t ul_bytes = 0;
    uint64_t grant_dl = 0;
    uint64_t grant_ul = 0;
    uint32_t connected_sec = 0;
    bool is_idle = false;
};

struct TopData {
    TopGlobal global;
    std::vector<TopUser> users;
    std::vector<TopSession> sessions;
    bool has_data = false;
};

struct HistoryRing {
    static constexpr int kMaxSamples = 300;
    uint64_t dl_samples[kMaxSamples] = {};
    uint64_t ul_samples[kMaxSamples] = {};
    int head = 0;
    int count = 0;
    void push(uint64_t dl, uint64_t ul);
    void clear();
};

// Forward-declare the layout struct used inside render().
struct RC;

class TopTui {
public:
    TopTui(const std::string& ctl_path, bool once, double interval = 1.0);
    ~TopTui();
    int run();

    TopTui(const TopTui&) = delete;
    TopTui& operator=(const TopTui&) = delete;
    TopTui(TopTui&&) = delete;
    TopTui& operator=(TopTui&&) = delete;

private:
    bool fetch_data();
    void sort_sessions();

    void render();
    void render_header();
    void render_info(const RC& rc);
    void render_gauges(const RC& rc);
    void render_graphs(const RC& rc);
    void render_table(const RC& rc);
    void render_footer();
    void render_help();

    bool handle_input(int ch);

    // --- helpers for unified table ---
    // Number of rows the table needs to display (sessions, plus user headers if grouped).
    int table_row_count() const;
    // Render one logical row at the given screen position. Returns true if something drawn.
    bool render_display_row(int display_idx, int screen_y, const RC& rc);

    std::string ctl_path_;
    bool once_;
    double interval_sec_;
    bool running_ = true;
    bool paused_ = false;

    TopData data_;
    HistoryRing history_;

    int table_scroll_ = 0;

    enum class SortCol { TOTAL_RATE, DL_RATE, UL_RATE, PID, COMMAND, USER };
    SortCol sort_col_ = SortCol::TOTAL_RATE;
    bool sort_desc_ = true;
    bool group_by_user_ = false;
    bool show_help_ = false;

    bool connected_ = false;
    std::string error_msg_;

    int term_rows_ = 24;
    int term_cols_ = 80;
};

} // namespace ntrickle
