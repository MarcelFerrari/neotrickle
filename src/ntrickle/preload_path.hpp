// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace ntrickle {

enum class PreloadSource {
    None,
    Override,
    ExecutableRelative,
    Compiled,
};

struct PreloadLookup {
    std::string path;
    PreloadSource source = PreloadSource::None;
    std::string error;
};

const char* preload_filename();
const char* preload_source_name(PreloadSource source);

/// Absolute path of the running executable, or an empty string if unavailable.
std::string current_executable_path();

/// Resolve the matching preload library for an executable.
///
/// Search order:
///   1. explicit NTRICKLE_PRELOAD_PATH (an invalid value is an error)
///   2. paths relative to the executable (bin/../lib/ntrickle, bin/../lib)
///   3. the compiled system install path, when non-empty
///
/// The current working directory and LD_LIBRARY_PATH are deliberately not
/// searched: either could let an unrelated library override the packaged one.
PreloadLookup locate_preload(const std::string& executable_path, const std::string& override_path,
                             const std::string& compiled_path);

} // namespace ntrickle
