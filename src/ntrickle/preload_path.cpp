// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preload_path.hpp"

#include <filesystem>
#include <system_error>
#include <vector>

namespace ntrickle {
namespace {

bool regular_file_exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::string normalized(const std::filesystem::path& path) {
    std::error_code ec;
    auto result = std::filesystem::weakly_canonical(path, ec);
    return ec ? path.string() : result.string();
}

} // namespace

const char* preload_filename() {
    return "libntrickle_preload.so";
}

const char* preload_source_name(PreloadSource source) {
    switch (source) {
    case PreloadSource::Override:
        return "NTRICKLE_PRELOAD_PATH";
    case PreloadSource::ExecutableRelative:
        return "executable-relative";
    case PreloadSource::Compiled:
        return "compiled install path";
    case PreloadSource::None:
    default:
        return "none";
    }
}

std::string current_executable_path() {
    std::error_code ec;
    auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::string{} : path.string();
}

PreloadLookup locate_preload(const std::string& executable_path, const std::string& override_path,
                             const std::string& compiled_path) {
    PreloadLookup result;

    if (!override_path.empty()) {
        std::filesystem::path override_file(override_path);
        if (regular_file_exists(override_file)) {
            result.path = normalized(override_file);
            result.source = PreloadSource::Override;
        } else {
            result.error = "NTRICKLE_PRELOAD_PATH does not point to a file: " + override_path;
        }
        return result;
    }

    std::vector<std::filesystem::path> candidates;
    if (!executable_path.empty()) {
        std::error_code ec;
        auto exe = std::filesystem::weakly_canonical(executable_path, ec);
        if (ec)
            exe = executable_path;
        const auto dir = exe.parent_path();
        if (!dir.empty()) {
            candidates.push_back(dir / ".." / "lib" / "ntrickle" / preload_filename());
            candidates.push_back(dir / ".." / "lib" / preload_filename());
            candidates.push_back(dir / ".." / "lib64" / "ntrickle" / preload_filename());
            candidates.push_back(dir / ".." / "lib64" / preload_filename());
            candidates.push_back(dir / "lib" / "ntrickle" / preload_filename());
            candidates.push_back(dir / "lib" / preload_filename());
        }
    }

    for (const auto& candidate : candidates) {
        if (regular_file_exists(candidate)) {
            result.path = normalized(candidate);
            result.source = PreloadSource::ExecutableRelative;
            return result;
        }
    }

    if (!compiled_path.empty() && regular_file_exists(compiled_path)) {
        result.path = normalized(compiled_path);
        result.source = PreloadSource::Compiled;
        return result;
    }

    result.error = "Could not find " + std::string(preload_filename()) +
                   " next to the executable or at the compiled install path";
    return result;
}

} // namespace ntrickle
