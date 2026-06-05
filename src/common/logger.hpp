// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <utility>

namespace ntrickle {

// Log levels.  Verbosity count controls the threshold:
//   0       ERROR / WARN only (default)
//   1 (-v)  + INFO
//   2 (-vv) + DEBUG
//   3 (-vvv)+ TRACE
enum class LogLevel { ERROR = 0, WARN = 0, INFO = 1, DEBUG = 2, TRACE = 3 };

class Logger {
public:
    Logger(const char* progname, int verbosity = 0) : progname_(progname), verbosity_(verbosity) {
    }

    void set_verbosity(int v) {
        verbosity_ = v;
    }
    int verbosity() const {
        return verbosity_;
    }
    const char* progname() const {
        return progname_;
    }

    // -- conditionally-printed messages (honour verbosity) ------------------

    template <typename... Args> void info(const char* fmt, Args&&... args) {
        log(LogLevel::INFO, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args> void debug(const char* fmt, Args&&... args) {
        log(LogLevel::DEBUG, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args> void trace(const char* fmt, Args&&... args) {
        log(LogLevel::TRACE, fmt, std::forward<Args>(args)...);
    }

    // -- always-printed messages -------------------------------------------

    template <typename... Args> void error(const char* fmt, Args&&... args) {
        emit("ERROR", fmt, std::forward<Args>(args)...);
    }

    template <typename... Args> void warn(const char* fmt, Args&&... args) {
        emit("WARN", fmt, std::forward<Args>(args)...);
    }

    // Always printed with strerror(errno) appended.
    template <typename... Args> void error_errno(const char* fmt, Args&&... args) {
        emit_errno("ERROR", fmt, std::forward<Args>(args)...);
    }

    // Always printed and exit.
    template <typename... Args>
    [[noreturn]] void fatal(int exit_code, const char* fmt, Args&&... args) {
        emit("FATAL", fmt, std::forward<Args>(args)...);
        std::exit(exit_code);
    }

    // Always printed with strerror(errno) and exit.
    template <typename... Args>
    [[noreturn]] void fatal_errno(int exit_code, const char* fmt, Args&&... args) {
        emit_errno("FATAL", fmt, std::forward<Args>(args)...);
        std::exit(exit_code);
    }

    // -- non-template overloads for plain strings --------------------------

    void info(const char* msg) {
        log(LogLevel::INFO, "%s", msg);
    }
    void debug(const char* msg) {
        log(LogLevel::DEBUG, "%s", msg);
    }
    void trace(const char* msg) {
        log(LogLevel::TRACE, "%s", msg);
    }
    void error(const char* msg) {
        emit("ERROR", "%s", msg);
    }
    void warn(const char* msg) {
        emit("WARN", "%s", msg);
    }
    void error_errno(const char* msg) {
        emit_errno("ERROR", "%s", msg);
    }
    [[noreturn]] void fatal(int exit_code, const char* msg) {
        emit("FATAL", "%s", msg);
        std::exit(exit_code);
    }
    [[noreturn]] void fatal_errno(int exit_code, const char* msg) {
        emit_errno("FATAL", "%s", msg);
        std::exit(exit_code);
    }

private:
    const char* progname_;
    int verbosity_ = 0;

    template <typename... Args> void log(LogLevel lvl, const char* fmt, Args&&... args) {
        if (static_cast<int>(lvl) > verbosity_)
            return;
        const char* tag = level_tag(lvl);
        std::fprintf(stderr, "%s: [%s] ", progname_, tag);
        std::fprintf(stderr, fmt, std::forward<Args>(args)...);
        std::fprintf(stderr, "\n");
    }

    template <typename... Args> void emit(const char* tag, const char* fmt, Args&&... args) {
        std::fprintf(stderr, "%s: [%s] ", progname_, tag);
        std::fprintf(stderr, fmt, std::forward<Args>(args)...);
        std::fprintf(stderr, "\n");
    }

    template <typename... Args> void emit_errno(const char* tag, const char* fmt, Args&&... args) {
        std::fprintf(stderr, "%s: [%s] ", progname_, tag);
        std::fprintf(stderr, fmt, std::forward<Args>(args)...);
        std::fprintf(stderr, ": %s\n", std::strerror(errno));
    }

    static const char* level_tag(LogLevel lvl) {
        switch (lvl) {
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::TRACE:
            return "TRACE";
        case LogLevel::ERROR:
            return "ERROR";
        }
        return "ERROR";
    }
};

} // namespace ntrickle
