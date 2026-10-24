# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(ntrickle_launcher_support STATIC user_config.cpp preload_path.cpp)

target_include_directories(ntrickle_launcher_support PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/src/common"
  "${CMAKE_SOURCE_DIR}/third_party/tomlplusplus/include"
  "${CMAKE_SOURCE_DIR}/third_party"
)

ntrickle_target_warnings(ntrickle_launcher_support)
ntrickle_target_sanitizers(ntrickle_launcher_support)

add_executable(ntrickle main.cpp top_tui.cpp)

# The Debian package keeps the classic binary name; the project identity and
# internal target name remain ntrickle.
set_target_properties(ntrickle PROPERTIES OUTPUT_NAME trickle)

target_compile_definitions(ntrickle PRIVATE
  NTRICKLE_PRELOAD_LIBRARY_PATH="${NTRICKLE_PRELOAD_LIBRARY_PATH}"
  NTRICKLE_DEFAULT_DAEMON_SOCKET="${NTRICKLE_DEFAULT_DAEMON_SOCKET}"
  NTRICKLE_DEFAULT_CONFIG_PATH="${NTRICKLE_DEFAULT_CONFIG_PATH}"
  NTRICKLE_DEFAULT_PROCESS_CONFIG_PATH="${NTRICKLE_DEFAULT_PROCESS_CONFIG_PATH}"
  NTRICKLE_DEFAULT_SCHEDULES_CONFIG_PATH="${NTRICKLE_DEFAULT_SCHEDULES_CONFIG_PATH}"
  NTRICKLE_INSTALL_PROFILE="${NTRICKLE_INSTALL_PROFILE}"
)

if(NTRICKLE_INSTALL_RPATH)
  set_target_properties(ntrickle PROPERTIES INSTALL_RPATH "${NTRICKLE_INSTALL_RPATH}")
endif()

target_include_directories(ntrickle PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/third_party/argparse/include"
  "${CMAKE_SOURCE_DIR}/third_party/tomlplusplus/include"
  "${CMAKE_SOURCE_DIR}/third_party"
)

# Find ncursesw for the top TUI
set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)

target_include_directories(ntrickle PRIVATE ${CURSES_INCLUDE_DIRS})

target_link_libraries(ntrickle PRIVATE
  ntrickle::common
  ntrickle_launcher_support
  ntrickle::config_shared
  ${CURSES_LIBRARIES}
)

ntrickle_target_warnings(ntrickle)
ntrickle_target_sanitizers(ntrickle)

install(TARGETS ntrickle
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(NTRICKLE_INSTALL_ALIASES)
  if(IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}")
    install(CODE "
      set(_dir \"\$ENV{DESTDIR}${CMAKE_INSTALL_BINDIR}\")
      file(REMOVE \"\${_dir}/ntrickle\")
      file(CREATE_LINK \"trickle\" \"\${_dir}/ntrickle\" SYMBOLIC)
    ")
  else()
    install(CODE "
      set(_dir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
      file(REMOVE \"\${_dir}/ntrickle\")
      file(CREATE_LINK \"trickle\" \"\${_dir}/ntrickle\" SYMBOLIC)
    ")
  endif()
endif()
