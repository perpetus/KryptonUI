cmake_minimum_required(VERSION 3.28)

project(KryptonUI
  VERSION 0.1.1.2
  DESCRIPTION "Krypton browser UI: one common logic layer, one native UI per platform"
  LANGUAGES C CXX)

# The browser distributes this DLL independently of its own CRT. All module
# object libraries and the preview/tests must agree on the runtime choice.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(KryptonModules)

# The C contract and every module are C++23 internally. Chromium only ever sees
# modules/abi/include/krypton_ui/abi.h, which is plain C, so this choice never
# leaks into the browser build.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No C++ modules here; skip the per-file dependency scan they would need.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_C_STANDARD 11)

# Everything runnable lands in one folder per configuration, so the preview
# finds krypton_ui.dll and its bootstrap DLL next to itself.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(KR_BUILD_TESTS "Build the commonTest suites" ON)
option(KR_BUILD_PREVIEW "Build the preview app for the active platform" ON)

kr_resolve_platform()
message(STATUS "KryptonUI platform: ${KR_PLATFORM}")

if(KR_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(modules/abi)
if(KR_BUILD_TESTS)
  add_subdirectory(modules/testing)
endif()
add_subdirectory(modules/shell)
add_subdirectory(modules/fixtures)
if(KR_BUILD_PREVIEW)
  add_subdirectory(modules/preview)
endif()
