# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later

function(ntrickle_target_sanitizers target_name)
  if(NOT NTRICKLE_ENABLE_SANITIZERS)
    return()
  endif()

  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target_name} PRIVATE
      -fsanitize=address,undefined
      -fno-omit-frame-pointer
    )
    target_link_options(${target_name} PRIVATE
      -fsanitize=address,undefined
    )
  endif()
endfunction()
