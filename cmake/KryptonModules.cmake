# Kotlin-Multiplatform-style modules for CMake.
#
# A module lives in modules/<name>/ and is split into source sets:
#
#   src/commonMain/include   public headers, platform-neutral  (KMP: commonMain)
#   src/commonMain/cpp       shared logic
#   src/commonTest/cpp       tests for the shared logic        (KMP: commonTest)
#   src/<platform>Main/...   one per platform: windows, macos, linux, headless
#
# commonMain declares "expect" functions (plain declarations in a header) and
# every <platform>Main defines the matching "actual" ones. Only the active
# platform's source set is compiled, so a platform that forgets an actual fails
# to link, the same way a missing `actual` fails to compile in Kotlin.
#
# headlessMain is the no-UI actual. Tests link against it on every platform,
# which keeps commonTest runnable on CI machines without a desktop.

include_guard(GLOBAL)

set(KR_PLATFORMS windows macos linux headless)

function(kr_resolve_platform)
  set(KR_PLATFORM "auto" CACHE STRING
      "UI platform source set to build: auto, windows, macos, linux or headless")
  set_property(CACHE KR_PLATFORM PROPERTY STRINGS auto ${KR_PLATFORMS})
  set(platform "${KR_PLATFORM}")
  if(platform STREQUAL "auto")
    if(WIN32)
      set(platform windows)
    elseif(APPLE)
      set(platform macos)
    else()
      set(platform linux)
    endif()
  endif()
  if(NOT platform IN_LIST KR_PLATFORMS)
    message(FATAL_ERROR "KR_PLATFORM must be one of: auto;${KR_PLATFORMS}")
  endif()
  set(KR_PLATFORM "${platform}" PARENT_SCOPE)
  set(KR_ACTIVE_PLATFORM "${platform}" CACHE INTERNAL "")
endfunction()

function(kr_compile_options target)
  if(MSVC)
    target_compile_options(${target} PRIVATE
      /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc /bigobj)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  if(WIN32)
    target_compile_definitions(${target} PRIVATE
      NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  endif()
endfunction()

function(_kr_collect_sources out_var dir)
  set(sources)
  if(IS_DIRECTORY "${dir}")
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS
      "${dir}/*.c" "${dir}/*.cc" "${dir}/*.cpp" "${dir}/*.mm"
      "${dir}/*.h" "${dir}/*.hpp" "${dir}/*.manifest" "${dir}/*.rc")
  endif()
  set(${out_var} "${sources}" PARENT_SCOPE)
endfunction()

# kr_module(<name>
#   KIND <STATIC|SHARED|EXECUTABLE>
#   [OUTPUT_NAME <file name>]
#   [COMMON_ONLY]            no platform source sets (e.g. fixtures, testing)
#   [PLATFORM_ONLY]          no commonMain (e.g. the preview app)
#   [WIN32_EXECUTABLE]       GUI subsystem on Windows
#   [DEPENDS <targets...>]   linked publicly into commonMain
#   [TEST_DEPENDS <targets>] extra libraries for the commonTest executable)
#
# Creates:
#   kr_<name>_common    commonMain as a static library (unless PLATFORM_ONLY)
#   kr_<name>           the artifact: commonMain + the active <platform>Main
#   kr_<name>_headless  headlessMain actuals, when the module has them
#   kr_<name>_tests     commonTest, linked against the headless actuals
function(kr_module name)
  cmake_parse_arguments(PARSE_ARGV 1 arg
    "COMMON_ONLY;PLATFORM_ONLY;WIN32_EXECUTABLE"
    "KIND;OUTPUT_NAME"
    "DEPENDS;TEST_DEPENDS")

  if(NOT arg_KIND)
    message(FATAL_ERROR "kr_module(${name}): KIND is required")
  endif()

  set(root "${CMAKE_CURRENT_SOURCE_DIR}/src")
  set(common_target kr_${name}_common)
  set(artifact kr_${name})

  # commonMain -------------------------------------------------------------
  if(NOT arg_PLATFORM_ONLY)
    _kr_collect_sources(common_sources "${root}/commonMain")
    add_library(${common_target} STATIC ${common_sources})
    target_include_directories(${common_target} PUBLIC "${root}/commonMain/include")
    target_link_libraries(${common_target} PUBLIC ${arg_DEPENDS})
    set_target_properties(${common_target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    kr_compile_options(${common_target})
  endif()

  # <platform>Main: the artifact --------------------------------------------
  if(arg_COMMON_ONLY)
    add_library(${artifact} ALIAS ${common_target})
  else()
    set(platform_dir "${root}/${KR_PLATFORM}Main")
    _kr_collect_sources(platform_sources "${platform_dir}")
    if(NOT platform_sources)
      message(FATAL_ERROR
        "kr_module(${name}): no ${KR_PLATFORM}Main source set in ${root}. "
        "Add src/${KR_PLATFORM}Main with the actual implementations.")
    endif()

    if(arg_KIND STREQUAL "EXECUTABLE")
      add_executable(${artifact} ${platform_sources})
      if(arg_WIN32_EXECUTABLE)
        set_target_properties(${artifact} PROPERTIES WIN32_EXECUTABLE ON)
      endif()
    else()
      add_library(${artifact} ${arg_KIND} ${platform_sources})
    endif()

    if(IS_DIRECTORY "${platform_dir}/include")
      target_include_directories(${artifact} PRIVATE "${platform_dir}/include")
    endif()
    if(NOT arg_PLATFORM_ONLY)
      # Whole-archive keeps the expect/actual link honest: commonMain symbols
      # that only the platform code calls must not be dropped from a DLL.
      target_link_libraries(${artifact} PUBLIC
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,${common_target}>")
    endif()
    target_link_libraries(${artifact} PUBLIC ${arg_DEPENDS})
    kr_compile_options(${artifact})
    if(arg_OUTPUT_NAME)
      set_target_properties(${artifact} PROPERTIES OUTPUT_NAME "${arg_OUTPUT_NAME}")
    endif()
  endif()

  # headlessMain: actuals for tests -------------------------------------------
  set(headless_dir "${root}/headlessMain")
  set(test_actuals)
  # Always a static library, even when headless is the active platform: the
  # artifact may be a DLL that doesn't export the test hooks.
  if(IS_DIRECTORY "${headless_dir}" AND NOT arg_PLATFORM_ONLY)
    _kr_collect_sources(headless_sources "${headless_dir}")
    add_library(kr_${name}_headless STATIC ${headless_sources})
    target_link_libraries(kr_${name}_headless PUBLIC ${common_target})
    if(IS_DIRECTORY "${headless_dir}/include")
      target_include_directories(kr_${name}_headless PUBLIC "${headless_dir}/include")
    endif()
    kr_compile_options(kr_${name}_headless)
    set(test_actuals kr_${name}_headless)
  endif()

  # commonTest -----------------------------------------------------------------
  set(test_dir "${root}/commonTest")
  if(KR_BUILD_TESTS AND IS_DIRECTORY "${test_dir}" AND NOT arg_PLATFORM_ONLY)
    _kr_collect_sources(test_sources "${test_dir}")
    add_executable(kr_${name}_tests ${test_sources})
    target_link_libraries(kr_${name}_tests PRIVATE
      ${common_target} ${test_actuals} ${arg_TEST_DEPENDS} kr_testing)
    if(IS_DIRECTORY "${headless_dir}/include")
      target_include_directories(kr_${name}_tests PRIVATE "${headless_dir}/include")
    endif()
    kr_compile_options(kr_${name}_tests)
    add_test(NAME ${name} COMMAND kr_${name}_tests)
  endif()
endfunction()
