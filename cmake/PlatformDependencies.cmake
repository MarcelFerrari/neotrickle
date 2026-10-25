# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Platform dependency checks for the ntrickle C components (preload and shared C).

include(CheckFunctionExists)
include(CheckIncludeFile)

set(NTRICKLE_CLASSIC_VERSION "1.07" CACHE STRING "Classic trickle compatibility version for provenance")
set(NTRICKLE_PACKAGE_NAME "ntrickle" CACHE STRING "ntrickle package name" FORCE)
set(NTRICKLE_DLOPEN_LIBC "libc.so.6" CACHE STRING "libc soname used by the preload interposer")

check_include_file(asm/poll.h HAVE_ASM_POLL_H)
check_include_file(dlfcn.h HAVE_DLFCN_H)
check_include_file(inttypes.h HAVE_INTTYPES_H)
check_include_file(memory.h HAVE_MEMORY_H)
check_include_file(netinet/in.h HAVE_NETINET_IN_H)
check_include_file(stdint.h HAVE_STDINT_H)
check_include_file(stdlib.h HAVE_STDLIB_H)
check_include_file(string.h HAVE_STRING_H)
check_include_file(strings.h HAVE_STRINGS_H)
check_include_file(sys/ioctl.h HAVE_SYS_IOCTL_H)
check_include_file(sys/stat.h HAVE_SYS_STAT_H)
check_include_file(sys/time.h HAVE_SYS_TIME_H)
check_include_file(sys/types.h HAVE_SYS_TYPES_H)
check_include_file(sys/wait.h HAVE_SYS_WAIT_H)
check_include_file(time.h HAVE_TIME_H)
check_include_file(unistd.h HAVE_UNISTD_H)

check_function_exists(sendfile HAVE_SENDFILE)
check_function_exists(sendfile64 HAVE_SENDFILE64)
check_function_exists(socket HAVE_SOCKET)

if(HAVE_SYS_TIME_H AND HAVE_TIME_H)
  set(TIME_WITH_SYS_TIME 1)
endif()
set(STDC_HEADERS 1)

find_package(Threads REQUIRED)
set(HAVE_PTHREAD 1)

find_path(NTRICKLE_LIBEVENT_INCLUDE_DIR event.h)
# The daemon uses only the core event API.  Prefer libevent_core over the
# libevent meta-library so portable/wheel builds do not pull in optional
# OpenSSL/zlib dependencies they never use.
find_library(NTRICKLE_LIBEVENT_LIBRARY NAMES event_core event)
if(NOT NTRICKLE_LIBEVENT_INCLUDE_DIR OR NOT NTRICKLE_LIBEVENT_LIBRARY)
  message(FATAL_ERROR "libevent development files are required — install libevent-dev")
endif()
set(NTRICKLE_LIBEVENT_INCLUDE_DIRS "${NTRICKLE_LIBEVENT_INCLUDE_DIR}")
set(NTRICKLE_LIBEVENT_LIBRARIES "${NTRICKLE_LIBEVENT_LIBRARY}")
