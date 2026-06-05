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

set(ntrickle "${prefix}/bin/ntrickle")
set(ntrickled "${prefix}/bin/ntrickled")
set(preload "${prefix}/${install_libdir}/ntrickle/libntrickle_preload.so")

function(run_required name)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${name} failed with exit code ${result}")
  endif()
endfunction()

if(NOT EXISTS "${ntrickle}")
  message(FATAL_ERROR "installed ntrickle not found at ${ntrickle}")
endif()
if(NOT EXISTS "${ntrickled}")
  message(FATAL_ERROR "installed ntrickled not found at ${ntrickled}")
endif()
if(NOT EXISTS "${preload}")
  message(FATAL_ERROR "installed preload not found at ${preload}")
endif()

run_required("installed ntrickle -V" "${ntrickle}" -V)
run_required("installed ntrickled -V" "${ntrickled}" -V)
run_required("installed preload /bin/true" "${CMAKE_COMMAND}" -E env "LD_PRELOAD=${preload}" /bin/true)
run_required("installed standalone wrap" "${ntrickle}" -m standalone -d 1M -- /bin/true)

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
