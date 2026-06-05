# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later

if(NOT DEFINED executable)
  message(FATAL_ERROR "executable is required")
endif()

execute_process(
  COMMAND "${executable}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)

if(result EQUAL 0)
  message(FATAL_ERROR "expected ${executable} to fail, but it exited 0")
endif()

set(combined "${stdout}${stderr}")
if(DEFINED expected_regex AND NOT "${combined}" MATCHES "${expected_regex}")
  message(FATAL_ERROR "expected output matching '${expected_regex}', got:
${combined}")
endif()
