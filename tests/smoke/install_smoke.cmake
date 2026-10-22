# SPDX-FileCopyrightText: 2026 Marcel Ferrari
# SPDX-License-Identifier: GPL-3.0-or-later

if(NOT DEFINED source_dir)
  message(FATAL_ERROR "source_dir is required")
endif()

if(NOT DEFINED build_type OR "${build_type}" STREQUAL "")
  set(build_type "Debug")
endif()

if(NOT DEFINED install_libdir OR "${install_libdir}" STREQUAL "")
  set(install_libdir "lib")
endif()

if(NOT DEFINED install_aliases OR "${install_aliases}" STREQUAL "")
  set(install_aliases ON)
endif()

if(NOT DEFINED project_version OR "${project_version}" STREQUAL "")
  set(project_version "2.0.0")
endif()

set(tmp_base "$ENV{TMPDIR}")
if("${tmp_base}" STREQUAL "")
  set(tmp_base "/tmp")
endif()

string(RANDOM LENGTH 10 ALPHABET "abcdefghijklmnopqrstuvwxyz0123456789" suffix)
set(work_dir "${tmp_base}/ntrickle-install-smoke-${suffix}")
set(build_dir "${work_dir}/build")
set(prefix "${work_dir}/prefix")

file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")

set(configure_args
  -S "${source_dir}"
  -B "${build_dir}"
  -DCMAKE_INSTALL_PREFIX=${prefix}
  -DNTRICKLE_ENABLE_TESTS=OFF
  -DNTRICKLE_ENABLE_INTEGRATION_TESTS=OFF
  -DNTRICKLE_INSTALL_ALIASES=${install_aliases}
)

if(DEFINED generator AND NOT "${generator}" STREQUAL "")
  list(APPEND configure_args -G "${generator}")
endif()
if(DEFINED c_compiler AND NOT "${c_compiler}" STREQUAL "")
  list(APPEND configure_args -DCMAKE_C_COMPILER=${c_compiler})
endif()
if(DEFINED cxx_compiler AND NOT "${cxx_compiler}" STREQUAL "")
  list(APPEND configure_args -DCMAKE_CXX_COMPILER=${cxx_compiler})
endif()
if(NOT "${generator}" MATCHES "Visual Studio|Xcode|Multi-Config")
  list(APPEND configure_args -DCMAKE_BUILD_TYPE=${build_type})
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" ${configure_args}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "install smoke configure failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${build_dir}" --config "${build_type}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "install smoke build failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${build_dir}" --config "${build_type}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "install smoke install failed")
endif()

set(trickle "${prefix}/bin/trickle")
set(trickled "${prefix}/bin/trickled")
set(ntrickle "${prefix}/bin/ntrickle")
set(ntrickled "${prefix}/bin/ntrickled")
set(preload "${prefix}/${install_libdir}/ntrickle/libntrickle_preload.so")

function(run_required name)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${name} failed with exit code ${result}")
  endif()
endfunction()

if(NOT EXISTS "${trickle}")
  message(FATAL_ERROR "installed trickle not found at ${trickle}")
endif()
if(NOT EXISTS "${trickled}")
  message(FATAL_ERROR "installed trickled not found at ${trickled}")
endif()
if(NOT EXISTS "${preload}")
  message(FATAL_ERROR "installed preload not found at ${preload}")
endif()
if(NOT EXISTS "${prefix}/share/man/man1/trickle.1")
  message(FATAL_ERROR "installed trickle.1 man page not found")
endif()
if(NOT EXISTS "${prefix}/share/man/man8/trickled.8")
  message(FATAL_ERROR "installed trickled.8 man page not found")
endif()

if(install_aliases)
  if(NOT IS_SYMLINK "${ntrickle}")
    message(FATAL_ERROR "ntrickle alias is not a symlink: ${ntrickle}")
  endif()
  if(NOT IS_SYMLINK "${ntrickled}")
    message(FATAL_ERROR "ntrickled alias is not a symlink: ${ntrickled}")
  endif()
  run_required("installed ntrickle -V" "${ntrickle}" -V)
  run_required("installed ntrickled -V" "${ntrickled}" -V)
endif()

run_required("installed trickle -V" "${trickle}" -V)
run_required("installed trickled -V" "${trickled}" -V)
run_required("installed preload /bin/true" "${CMAKE_COMMAND}" -E env "LD_PRELOAD=${preload}" /bin/true)

execute_process(
  COMMAND "${trickle}" -m standalone -d 1M -- /bin/true
  WORKING_DIRECTORY "${work_dir}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "installed standalone wrap failed with exit code ${result}")
endif()

execute_process(
  COMMAND "${trickle}" -V
  OUTPUT_VARIABLE version_output
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0 OR NOT version_output MATCHES "^trickle version ${project_version}")
  message(FATAL_ERROR "installed trickle -V output mismatch: ${version_output}")
endif()

# Verify installed licensing files
set(license_dir "${prefix}/share/licenses/ntrickle")
foreach(lic_file
    LICENSE
    LICENSING.md
    PROVENANCE.md
    THIRD_PARTY_NOTICES.md
    BSD-3-Clause.txt
    BSD-2-Clause.txt
    argparse-MIT.txt
    tomlplusplus-MIT.txt
)
  if(NOT EXISTS "${license_dir}/${lic_file}")
    message(FATAL_ERROR "installed license file not found: ${license_dir}/${lic_file}")
  endif()
endforeach()

file(REMOVE_RECURSE "${work_dir}")
