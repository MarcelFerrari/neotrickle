# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later

# Generated headers
configure_file(
  version.hpp.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/ntrickle/version.hpp"
  @ONLY
)
configure_file(
  config.h.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/config.h"
  @ONLY
)

# Header-only C++ library (bandwidth.hpp, daemon_mode.hpp, shared_config.hpp)
add_library(ntrickle_common INTERFACE)
add_library(ntrickle::common ALIAS ntrickle_common)

target_include_directories(ntrickle_common INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
)

# Shared C library — bandwidth statistics, linked into both preload and daemon
add_library(ntrickle_shared_c OBJECT bwstat.c)
add_library(ntrickle::shared_c ALIAS ntrickle_shared_c)

set_target_properties(ntrickle_shared_c PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS ON
  POSITION_INDEPENDENT_CODE ON
)

target_compile_options(ntrickle_shared_c PRIVATE -Wall -Wextra)

target_compile_definitions(ntrickle_shared_c PUBLIC
  HAVE_CONFIG_H
  _DEFAULT_SOURCE
)

target_include_directories(ntrickle_shared_c PUBLIC
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ntrickle_shared_c PUBLIC Threads::Threads)

# Shared C++ config parsing library (process.toml + schedules.toml)
add_library(ntrickle_config_shared STATIC shared_config.cpp)
add_library(ntrickle::config_shared ALIAS ntrickle_config_shared)

target_include_directories(ntrickle_config_shared PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/third_party/tomlplusplus/include"
)

target_compile_features(ntrickle_config_shared PUBLIC cxx_std_17)

ntrickle_target_warnings(ntrickle_config_shared)
ntrickle_target_sanitizers(ntrickle_config_shared)
