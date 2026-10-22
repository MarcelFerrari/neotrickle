# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Preload/interposition library loaded via LD_PRELOAD into target processes.
# Everything in this directory is C-only, minimal, and defensive.
# Do not add C++ or heavyweight dependencies here.
add_library(ntrickle_preload MODULE
  trickle-overload.c
  state.c
  limiter.c
  wrappers.c
  readiness.c
  ntrickledu.c
  codec.c
  atomicio.c
)

set_target_properties(ntrickle_preload PROPERTIES
  OUTPUT_NAME "ntrickle_preload"
  PREFIX "lib"
  SUFFIX ".so"
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS ON
)

target_compile_options(ntrickle_preload PRIVATE
  -Wall
  -Wextra
)

target_compile_definitions(ntrickle_preload PRIVATE
  HAVE_CONFIG_H
  _DEFAULT_SOURCE
  _LARGEFILE64_SOURCE
)

target_include_directories(ntrickle_preload PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/src/common"
  "${CMAKE_BINARY_DIR}/src/common/generated"
)

target_link_libraries(ntrickle_preload PRIVATE
  ntrickle::shared_c
  ${CMAKE_DL_LIBS}
)

# The preload is a MODULE library loaded via LD_PRELOAD at runtime.
# It must resolve libc symbols against whatever libc the host process
# already has loaded.  Do NOT pass -nostdlib — that would produce
# unresolved symbols that fail to load when the host binary is
# statically linked, stripped, built with -fvisibility=hidden,
# or is a non-C binary (Go, Rust, etc.).  Linking normally with
# -lc ensures the symbols resolve against the system libc.
# -nostartfiles is acceptable if CRT startup must be avoided, but
# the preload has no constructors, so even that should be unnecessary.

install(TARGETS ntrickle_preload
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/ntrickle")
