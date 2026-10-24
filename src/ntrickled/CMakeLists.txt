# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later

set(NTRICKLED_SOURCES
  ntrickled.cpp
  client.cpp
  config.cpp
  daemon_context.cpp
  message_codec.cpp
  stats_collector.cpp
)

add_executable(ntrickled ${NTRICKLED_SOURCES})

# See src/ntrickle/CMakeLists.txt: installed name is classic-compatible, while
# the internal target and project names stay ntrickle.
set_target_properties(ntrickled PROPERTIES OUTPUT_NAME trickled)

set_target_properties(ntrickled PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS ON
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)

target_compile_options(ntrickled PRIVATE
  -Wconversion
)

ntrickle_target_warnings(ntrickled)

target_compile_definitions(ntrickled PRIVATE
  HAVE_CONFIG_H
  _DEFAULT_SOURCE
  NTRICKLE_PROJECT_VERSION="${PROJECT_VERSION}"
  NTRICKLE_DEFAULT_PROCESS_CONFIG_PATH="${NTRICKLE_DEFAULT_PROCESS_CONFIG_PATH}"
  NTRICKLE_DEFAULT_SCHEDULES_CONFIG_PATH="${NTRICKLE_DEFAULT_SCHEDULES_CONFIG_PATH}"
  NTRICKLE_INSTALL_PROFILE="${NTRICKLE_INSTALL_PROFILE}"
)

if(NTRICKLE_INSTALL_RPATH)
  set_target_properties(ntrickled PROPERTIES INSTALL_RPATH "${NTRICKLE_INSTALL_RPATH}")
endif()

if(NTRICKLE_INSTALL_PROFILE STREQUAL "portable" AND
   CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_link_options(ntrickled PRIVATE -static-libstdc++ -static-libgcc)
endif()

target_include_directories(ntrickled PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  ${NTRICKLE_LIBEVENT_INCLUDE_DIRS}
  "${CMAKE_SOURCE_DIR}/third_party/tomlplusplus/include"
  "${CMAKE_SOURCE_DIR}/third_party/argparse/include"
  "${CMAKE_SOURCE_DIR}/third_party"
  "${CMAKE_SOURCE_DIR}/src/common"
  "${CMAKE_BINARY_DIR}/src/common/generated"
)

target_link_libraries(ntrickled PRIVATE
  ntrickle::common
  ntrickle::shared_c
  ntrickle::config_shared
  ${NTRICKLE_LIBEVENT_LIBRARIES}
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  target_link_libraries(ntrickled PRIVATE stdc++fs)
endif()

ntrickle_target_sanitizers(ntrickled)

install(TARGETS ntrickled
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(NTRICKLE_INSTALL_ALIASES)
  if(IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}")
    install(CODE "
      set(_dir \"\$ENV{DESTDIR}${CMAKE_INSTALL_BINDIR}\")
      file(REMOVE \"\${_dir}/ntrickled\")
      file(CREATE_LINK \"trickled\" \"\${_dir}/ntrickled\" SYMBOLIC)
    ")
  else()
    install(CODE "
      set(_dir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
      file(REMOVE \"\${_dir}/ntrickled\")
      file(CREATE_LINK \"trickled\" \"\${_dir}/ntrickled\" SYMBOLIC)
    ")
  endif()
endif()
