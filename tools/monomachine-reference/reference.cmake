# SPDX-License-Identifier: AGPL-3.0-only
# Loaded by CMAKE_PROJECT_Monomodule_INCLUDE, leaving the upstream tree intact.
if(MINGW)
  # dsp56300's conditionvariable.h uses std::max without including <algorithm>.
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_LIST_DIR}/compat.h>")
  function(synfact_mingw_base)
    # Expose the Win10 placeholder APIs used by upstream's dynamically loaded
    # MMU support; clang's MS extensions provide its debugger-name SEH guard.
    target_compile_definitions(dsp56kBase PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000006)
    target_compile_options(dsp56kBase PRIVATE -fms-extensions)
    # MinGW uses the Win64 ABI too. Upstream selects SysV argument registers
    # and omits call shadow space when _MSC_VER is absent, crashing its JIT.
    get_target_property(_emu_dir dsp56kEmu SOURCE_DIR)
    get_filename_component(_emu_root "${_emu_dir}/../.." ABSOLUTE)
    find_package(Git REQUIRED)
    set(_patch "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/mingw.patch")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${_emu_root}" apply --unidiff-zero --reverse --check "${_patch}"
                    WORKING_DIRECTORY "${_emu_root}" RESULT_VARIABLE _already OUTPUT_QUIET ERROR_QUIET)
    if(NOT _already EQUAL 0)
      execute_process(COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${_emu_root}" apply --unidiff-zero "${_patch}"
                      WORKING_DIRECTORY "${_emu_root}" COMMAND_ERROR_IS_FATAL ANY)
    endif()
  endfunction()
  cmake_language(DEFER CALL synfact_mingw_base)
endif()
add_executable(synfact-mnm-reference "${CMAKE_CURRENT_LIST_DIR}/reference.cpp")
set_target_properties(synfact-mnm-reference PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(synfact-mnm-reference PRIVATE mnmcore)
