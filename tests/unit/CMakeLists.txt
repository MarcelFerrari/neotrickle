# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_message_codec
  test_message_codec.cpp
  "${CMAKE_SOURCE_DIR}/src/ntrickled/message_codec.cpp"
  "${CMAKE_SOURCE_DIR}/src/preload/codec.c"
)

target_compile_definitions(test_message_codec PRIVATE
  HAVE_CONFIG_H
  _DEFAULT_SOURCE
)

target_include_directories(test_message_codec PRIVATE
  "${CMAKE_SOURCE_DIR}/src/ntrickled"
  "${CMAKE_SOURCE_DIR}/src/preload"
  "${CMAKE_SOURCE_DIR}/src/common"
  "${CMAKE_BINARY_DIR}/src/common/generated"
)

target_link_libraries(test_message_codec PRIVATE
  ntrickle::shared_c
)

ntrickle_target_warnings(test_message_codec)
ntrickle_target_sanitizers(test_message_codec)

add_test(NAME message_codec_unit COMMAND test_message_codec)
set_tests_properties(message_codec_unit PROPERTIES LABELS "unit")

add_executable(test_preload_path test_preload_path.cpp)

target_include_directories(test_preload_path PRIVATE
  "${CMAKE_SOURCE_DIR}/src/ntrickle"
  "${CMAKE_SOURCE_DIR}/src/common"
)

target_link_libraries(test_preload_path PRIVATE
  ntrickle_launcher_support
)

ntrickle_target_warnings(test_preload_path)
ntrickle_target_sanitizers(test_preload_path)

add_test(NAME preload_path_unit COMMAND test_preload_path)
set_tests_properties(preload_path_unit PROPERTIES LABELS "unit")

add_executable(test_readiness_math test_readiness_math.cpp)

target_include_directories(test_readiness_math PRIVATE
  "${CMAKE_SOURCE_DIR}/src/preload"
)

ntrickle_target_warnings(test_readiness_math)
ntrickle_target_sanitizers(test_readiness_math)

add_test(NAME readiness_math_unit COMMAND test_readiness_math)
set_tests_properties(readiness_math_unit PROPERTIES LABELS "unit")

add_executable(test_bandwidth test_bandwidth.cpp)

target_link_libraries(test_bandwidth PRIVATE ntrickle::common)

ntrickle_target_warnings(test_bandwidth)
ntrickle_target_sanitizers(test_bandwidth)

add_test(NAME bandwidth_unit COMMAND test_bandwidth)
set_tests_properties(bandwidth_unit PROPERTIES LABELS "unit")

add_executable(test_scheduler test_scheduler.cpp)

target_compile_definitions(test_scheduler PRIVATE
  HAVE_CONFIG_H
  _DEFAULT_SOURCE
)

target_include_directories(test_scheduler PRIVATE
  "${CMAKE_SOURCE_DIR}/src/common"
  "${CMAKE_BINARY_DIR}/src/common/generated"
)

target_link_libraries(test_scheduler PRIVATE
  ntrickle::shared_c
)

ntrickle_target_warnings(test_scheduler)
ntrickle_target_sanitizers(test_scheduler)

add_test(NAME scheduler_unit COMMAND test_scheduler)
set_tests_properties(scheduler_unit PROPERTIES LABELS "unit")

add_executable(test_shared_config test_shared_config.cpp)

target_include_directories(test_shared_config PRIVATE
  "${CMAKE_SOURCE_DIR}/src/common"
  "${CMAKE_SOURCE_DIR}/third_party/tomlplusplus/include"
)

target_link_libraries(test_shared_config PRIVATE
  ntrickle::config_shared
)

ntrickle_target_warnings(test_shared_config)
ntrickle_target_sanitizers(test_shared_config)

add_test(NAME shared_config_unit COMMAND test_shared_config)
set_tests_properties(shared_config_unit PROPERTIES LABELS "unit")

add_executable(test_daemon_context
  test_daemon_context.cpp
  "${CMAKE_SOURCE_DIR}/src/ntrickled/daemon_context.cpp"
)

target_include_directories(test_daemon_context PRIVATE
  "${CMAKE_SOURCE_DIR}/src/ntrickled"
  "${CMAKE_SOURCE_DIR}/src/common"
  ${NTRICKLE_LIBEVENT_INCLUDE_DIRS}
)

ntrickle_target_warnings(test_daemon_context)
ntrickle_target_sanitizers(test_daemon_context)

add_test(NAME daemon_context_unit COMMAND test_daemon_context)
set_tests_properties(daemon_context_unit PROPERTIES LABELS "unit")

add_executable(test_stats_collector
  test_stats_collector.cpp
  "${CMAKE_SOURCE_DIR}/src/ntrickled/client.cpp"
  "${CMAKE_SOURCE_DIR}/src/ntrickled/message_codec.cpp"
  "${CMAKE_SOURCE_DIR}/src/ntrickled/stats_collector.cpp"
)

target_compile_definitions(test_stats_collector PRIVATE
  HAVE_CONFIG_H
  _DEFAULT_SOURCE
)

target_include_directories(test_stats_collector PRIVATE
  "${CMAKE_SOURCE_DIR}/src/ntrickled"
  "${CMAKE_SOURCE_DIR}/src/common"
  "${CMAKE_BINARY_DIR}/src/common/generated"
  "${CMAKE_SOURCE_DIR}/third_party"
  "${CMAKE_SOURCE_DIR}/third_party/tomlplusplus/include"
  ${NTRICKLE_LIBEVENT_INCLUDE_DIRS}
)

target_link_libraries(test_stats_collector PRIVATE
  ntrickle::shared_c
  ${NTRICKLE_LIBEVENT_LIBRARIES}
)

ntrickle_target_warnings(test_stats_collector)
ntrickle_target_sanitizers(test_stats_collector)

add_test(NAME stats_collector_unit COMMAND test_stats_collector)
set_tests_properties(stats_collector_unit PROPERTIES LABELS "unit")

add_executable(test_tui_text test_tui_text.cpp)

target_include_directories(test_tui_text PRIVATE
  "${CMAKE_SOURCE_DIR}/src/ntrickle"
)

ntrickle_target_warnings(test_tui_text)
ntrickle_target_sanitizers(test_tui_text)

add_test(NAME tui_text_unit COMMAND test_tui_text)
set_tests_properties(tui_text_unit PROPERTIES LABELS "unit")

add_executable(test_tui_glyphs test_tui_glyphs.cpp)

target_include_directories(test_tui_glyphs PRIVATE
  "${CMAKE_SOURCE_DIR}/src/ntrickle"
)

ntrickle_target_warnings(test_tui_glyphs)
ntrickle_target_sanitizers(test_tui_glyphs)

add_test(NAME tui_glyphs_unit COMMAND test_tui_glyphs)
set_tests_properties(tui_glyphs_unit PROPERTIES LABELS "unit")
