# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Verify that the one authoritative upstream version is consistent across
# CMake, the Python package metadata, and Debian packaging.

file(READ "${source_dir}/CMakeLists.txt" cmake_text)
string(REGEX MATCH "project\\(ntrickle VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)" _ "${cmake_text}")
set(cmake_version "${CMAKE_MATCH_1}")
if("${cmake_version}" STREQUAL "")
  message(FATAL_ERROR "could not find the project version in CMakeLists.txt")
endif()

file(READ "${source_dir}/pyproject.toml" pyproject_text)
if(NOT pyproject_text MATCHES "dynamic = \\[\"version\"\\]")
  message(FATAL_ERROR "pyproject.toml does not use dynamic version metadata")
endif()
if(NOT pyproject_text MATCHES "input = \"CMakeLists.txt\"")
  message(FATAL_ERROR "pyproject.toml version provider is not anchored to CMakeLists.txt")
endif()

file(READ "${source_dir}/debian/changelog" changelog_text)
string(REGEX MATCH "^neotrickle \\(([0-9]+\\.[0-9]+\\.[0-9]+)-[0-9]+" _ "${changelog_text}")
set(debian_version "${CMAKE_MATCH_1}")
if(NOT "${debian_version}" STREQUAL "${cmake_version}")
  message(FATAL_ERROR
    "Debian upstream version ${debian_version} does not match CMake ${cmake_version}")
endif()

message(STATUS "packaging versions agree: ${cmake_version}")
