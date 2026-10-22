# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later

add_subdirectory(unit)

add_test(NAME ntrickle_version COMMAND $<TARGET_FILE:ntrickle> -V)
set_tests_properties(ntrickle_version PROPERTIES
  PASS_REGULAR_EXPRESSION "^ntrickle version ${PROJECT_VERSION}"
  LABELS "smoke"
)

add_test(NAME ntrickle_help COMMAND $<TARGET_FILE:ntrickle> -h)
set_tests_properties(ntrickle_help PROPERTIES
  PASS_REGULAR_EXPRESSION "Usage: ntrickle"
  LABELS "smoke"
)

add_test(NAME ntrickled_version COMMAND $<TARGET_FILE:ntrickled> -V)
set_tests_properties(ntrickled_version PROPERTIES
  PASS_REGULAR_EXPRESSION "^ntrickle version ${PROJECT_VERSION}"
  LABELS "smoke"
)

add_test(NAME ntrickled_help COMMAND $<TARGET_FILE:ntrickled> -h)
set_tests_properties(ntrickled_help PROPERTIES
  PASS_REGULAR_EXPRESSION "Usage: ntrickled"
  LABELS "smoke"
)

add_test(NAME ntrickle_missing_command
  COMMAND ${CMAKE_COMMAND}
    -Dexecutable=$<TARGET_FILE:ntrickle>
    -Dexpected_regex=Usage:.*ntrickle
    -P ${CMAKE_CURRENT_SOURCE_DIR}/smoke/expect_failure.cmake
)
set_tests_properties(ntrickle_missing_command PROPERTIES LABELS "smoke")

add_test(NAME preload_load_true
  COMMAND ${CMAKE_COMMAND} -E env "LD_PRELOAD=$<TARGET_FILE:ntrickle_preload>" /bin/true
)
set_tests_properties(preload_load_true PROPERTIES LABELS "smoke;preload")

add_test(NAME ntrickle_standalone_wrap_true
  COMMAND $<TARGET_FILE:ntrickle> -m standalone -d 1M -- /bin/true
)
set_tests_properties(ntrickle_standalone_wrap_true PROPERTIES
  WORKING_DIRECTORY "$<TARGET_FILE_DIR:ntrickle_preload>"
  LABELS "smoke;preload"
)

add_test(NAME install_smoke
  COMMAND ${CMAKE_COMMAND}
    -Dsource_dir=${PROJECT_SOURCE_DIR}
    -Dgenerator=${CMAKE_GENERATOR}
    -Dbuild_type=${CMAKE_BUILD_TYPE}
    -Dc_compiler=${CMAKE_C_COMPILER}
    -Dcxx_compiler=${CMAKE_CXX_COMPILER}
    -Dinstall_libdir=${CMAKE_INSTALL_LIBDIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/smoke/install_smoke.cmake
)
set_tests_properties(install_smoke PROPERTIES LABELS "install;preload")

if(NTRICKLE_ENABLE_INTEGRATION_TESTS)
  if(EXISTS "${PROJECT_SOURCE_DIR}/.venv/bin/python")
    set(Python3_EXECUTABLE "${PROJECT_SOURCE_DIR}/.venv/bin/python" CACHE FILEPATH "Python interpreter for ntrickle integration tests" FORCE)
  endif()
  find_package(Python3 COMPONENTS Interpreter REQUIRED)

  add_test(NAME pytest_integration
    COMMAND ${Python3_EXECUTABLE} -m pytest
      ${CMAKE_CURRENT_SOURCE_DIR}/integration
      --ntrickle-mode=both
      --ntrickle-bin=$<TARGET_FILE:ntrickle>
      --ntrickled-bin=$<TARGET_FILE:ntrickled>
      --ntrickle-preload-lib=$<TARGET_FILE:ntrickle_preload>
  )
  set_tests_properties(pytest_integration PROPERTIES LABELS "integration;preload")
endif()

# ---------------------------------------------------------------------------
# Valgrind Memcheck tests
# ---------------------------------------------------------------------------
if(NTRICKLE_ENABLE_VALGRIND_TESTS)
  # We also need Python3 for the daemon-lifecycle pytest under Valgrind.
  if(NOT Python3_EXECUTABLE)
    if(EXISTS "${PROJECT_SOURCE_DIR}/.venv/bin/python")
      set(Python3_EXECUTABLE "${PROJECT_SOURCE_DIR}/.venv/bin/python" CACHE FILEPATH "Python interpreter for ntrickle valgrind tests" FORCE)
    endif()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
  endif()

  # Unit tests under Valgrind — fast, self-contained, no I/O beyond stdout.
  add_test(NAME valgrind_message_codec_unit
    COMMAND ${NTRICKLE_VALGRIND_COMMAND} $<TARGET_FILE:test_message_codec>)
  set_tests_properties(valgrind_message_codec_unit PROPERTIES LABELS "valgrind;unit")

  add_test(NAME valgrind_bandwidth_unit
    COMMAND ${NTRICKLE_VALGRIND_COMMAND} $<TARGET_FILE:test_bandwidth>)
  set_tests_properties(valgrind_bandwidth_unit PROPERTIES LABELS "valgrind;unit")

  add_test(NAME valgrind_scheduler_unit
    COMMAND ${NTRICKLE_VALGRIND_COMMAND} $<TARGET_FILE:test_scheduler>)
  set_tests_properties(valgrind_scheduler_unit PROPERTIES LABELS "valgrind;unit")

  add_test(NAME valgrind_shared_config_unit
    COMMAND ${NTRICKLE_VALGRIND_COMMAND} $<TARGET_FILE:test_shared_config>)
  set_tests_properties(valgrind_shared_config_unit PROPERTIES LABELS "valgrind;unit")

  add_test(NAME valgrind_stats_collector_unit
    COMMAND ${NTRICKLE_VALGRIND_COMMAND} $<TARGET_FILE:test_stats_collector>)
  set_tests_properties(valgrind_stats_collector_unit PROPERTIES LABELS "valgrind;unit")

  add_test(NAME valgrind_tui_text_unit
    COMMAND ${NTRICKLE_VALGRIND_COMMAND} $<TARGET_FILE:test_tui_text>)
  set_tests_properties(valgrind_tui_text_unit PROPERTIES LABELS "valgrind;unit")

  # Smoke tests under Valgrind — lightweight, validate basic execution paths.
  # The preload library needs the full NTRICKLE_* env-var set to initialise
  # under Valgrind's --trace-children instrumentation, because Valgrind
  # triggers intercepted libc calls during process setup that activate the
  # lazy-init path.
  add_test(NAME valgrind_preload_load_true
    COMMAND ${NTRICKLE_VALGRIND_COMMAND}
      ${CMAKE_COMMAND} -E env
      "LD_PRELOAD=$<TARGET_FILE:ntrickle_preload>"
      "NTRICKLE_WINDOW_SIZE=200"
      "NTRICKLE_DOWNLOAD_LIMIT=1048576"
      "NTRICKLE_UPLOAD_LIMIT=1048576"
      "NTRICKLE_ARGV=/bin/true"
      "NTRICKLE_SOCKNAME="
      "NTRICKLE_TSMOOTH=3.0"
      "NTRICKLE_LSMOOTH=20"
      "NTRICKLE_WEIGHT=1"
      /bin/true)
  set_tests_properties(valgrind_preload_load_true PROPERTIES
    LABELS "valgrind;smoke;preload")

  add_test(NAME valgrind_standalone_wrap_true
    COMMAND ${NTRICKLE_VALGRIND_COMMAND}
      $<TARGET_FILE:ntrickle> -m standalone -d 1M -- /bin/true)
  set_tests_properties(valgrind_standalone_wrap_true PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:ntrickle_preload>"
    LABELS "valgrind;smoke;preload")

  # Daemon and CLI lifecycle under Valgrind (pytest).
  # Starts ntrickled under Valgrind, exercises the control socket, and
  # verifies graceful-shutdown cleanliness.
  add_test(NAME valgrind_lifecycle
    COMMAND ${Python3_EXECUTABLE} -m pytest
      ${CMAKE_CURRENT_SOURCE_DIR}/valgrind
      --ntrickle-bin=$<TARGET_FILE:ntrickle>
      --ntrickled-bin=$<TARGET_FILE:ntrickled>
      --ntrickle-preload-lib=$<TARGET_FILE:ntrickle_preload>
      --valgrind-exe=${NTRICKLE_VALGRIND_EXECUTABLE}
      --valgrind-suppressions=${NTRICKLE_VALGRIND_SUPPRESSIONS}
  )
  set_tests_properties(valgrind_lifecycle PROPERTIES
    TIMEOUT 60
    LABELS "valgrind;integration;preload")
endif()
