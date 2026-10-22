# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)

project(ntrickle VERSION 2.0.0 LANGUAGES C CXX)

include(GNUInstallDirs)

option(NTRICKLE_ENABLE_TESTS "Build ntrickle tests" OFF)
option(NTRICKLE_ENABLE_INTEGRATION_TESTS "Run pytest-based ntrickle integration tests through CTest" OFF)
option(NTRICKLE_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(NTRICKLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(NTRICKLE_INSTALL_SYSTEMD_UNIT "Install systemd service unit file" OFF)
option(NTRICKLE_ENABLE_VALGRIND_TESTS "Run a representative subset of tests under Valgrind Memcheck" OFF)
option(NTRICKLE_INSTALL_ALIASES "Install ntrickle/ntrickled command aliases" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  string(TOUPPER "${config}" config_upper)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib")
endforeach()

set(NTRICKLE_DEFAULT_CONFIG_PATH "/etc/ntrickle/ntrickled.toml" CACHE STRING "Default ntrickled config path")
set(NTRICKLE_DEFAULT_PROCESS_CONFIG_PATH "/etc/ntrickle/process.toml" CACHE STRING "Default process.toml path")
set(NTRICKLE_DEFAULT_SCHEDULES_CONFIG_PATH "/etc/ntrickle/schedules.toml" CACHE STRING "Default schedules.toml path")
set(NTRICKLE_DEFAULT_DAEMON_SOCKET "/run/ntrickle/ntrickled.sock" CACHE STRING "Default ntrickled socket path")
set(NTRICKLE_DEFAULT_USAGE_DB "/var/lib/ntrickle/usage.db" CACHE STRING "Default ntrickle usage database path")
set(NTRICKLE_PRELOAD_LIBRARY_PATH "${CMAKE_INSTALL_FULL_LIBDIR}/ntrickle/libntrickle_preload.so" CACHE STRING "Installed preload library path")

if(NTRICKLE_ENABLE_INTEGRATION_TESTS AND NOT NTRICKLE_ENABLE_TESTS)
  message(FATAL_ERROR "NTRICKLE_ENABLE_INTEGRATION_TESTS requires NTRICKLE_ENABLE_TESTS")
endif()

if(NTRICKLE_ENABLE_VALGRIND_TESTS AND NOT NTRICKLE_ENABLE_TESTS)
  message(FATAL_ERROR "NTRICKLE_ENABLE_VALGRIND_TESTS requires NTRICKLE_ENABLE_TESTS")
endif()

if(NTRICKLE_ENABLE_VALGRIND_TESTS)
  find_program(NTRICKLE_VALGRIND_EXECUTABLE valgrind)
  if(NOT NTRICKLE_VALGRIND_EXECUTABLE)
    message(FATAL_ERROR "Valgrind not found — install valgrind or disable NTRICKLE_ENABLE_VALGRIND_TESTS")
  endif()
  set(NTRICKLE_VALGRIND_SUPPRESSIONS "${CMAKE_SOURCE_DIR}/tests/valgrind.supp")
  set(NTRICKLE_VALGRIND_COMMAND
    "${NTRICKLE_VALGRIND_EXECUTABLE}" --tool=memcheck
    --leak-check=full --show-leak-kinds=all
    --errors-for-leak-kinds=definite,indirect
    --track-origins=yes --track-fds=yes
    --trace-children=yes
    --error-exitcode=1
    --suppressions=${NTRICKLE_VALGRIND_SUPPRESSIONS}
  )
  message(STATUS "Valgrind tests enabled: ${NTRICKLE_VALGRIND_EXECUTABLE}")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)
include(PlatformDependencies)

add_subdirectory(src)

if(NTRICKLE_ENABLE_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()

install(FILES config/ntrickled.example.toml
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/ntrickle")
install(FILES config/process.example.toml
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/ntrickle")
install(FILES config/schedules.example.toml
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/ntrickle")

install(FILES docs/man/trickle.1
        DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
install(FILES docs/man/trickled.8
        DESTINATION "${CMAKE_INSTALL_MANDIR}/man8")

# Optionally install the systemd service unit (off by default —
# enable with -DNTRICKLE_INSTALL_SYSTEMD_UNIT=ON).
if(NTRICKLE_INSTALL_SYSTEMD_UNIT)
  configure_file(
    "${CMAKE_SOURCE_DIR}/config/ntrickled.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ntrickled.service"
    @ONLY
  )
  install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/ntrickled.service"
    DESTINATION "lib/systemd/system"
  )
endif()

# Install licensing and attribution documents
set(NTRICKLE_LICENSE_DIR
    "${CMAKE_INSTALL_DATADIR}/licenses/ntrickle")

install(
  FILES
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_SOURCE_DIR}/LICENSING.md"
    "${CMAKE_SOURCE_DIR}/PROVENANCE.md"
    "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    "${CMAKE_SOURCE_DIR}/LICENSES/BSD-3-Clause.txt"
    "${CMAKE_SOURCE_DIR}/LICENSES/BSD-2-Clause.txt"
  DESTINATION "${NTRICKLE_LICENSE_DIR}"
)

install(
  FILES "${CMAKE_SOURCE_DIR}/third_party/argparse/LICENSE.txt"
  DESTINATION "${NTRICKLE_LICENSE_DIR}"
  RENAME "argparse-MIT.txt"
)

install(
  FILES "${CMAKE_SOURCE_DIR}/third_party/tomlplusplus/LICENSE.txt"
  DESTINATION "${NTRICKLE_LICENSE_DIR}"
  RENAME "tomlplusplus-MIT.txt"
)

# ---------------------------------------------------------------------------
# Packaging (CPack)
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "ntrickle")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Marcel Ferrari")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Application-independent bandwidth limiter for Linux workloads")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/mferrari/ntrickle")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_IGNORE_FILES
  "/.git/"
  "/.venv/"
  "/build/"
  "/.claude/"
  "\\\\\\\\.tmp$"
)
set(CPACK_GENERATOR "TGZ")
include(CPack)
