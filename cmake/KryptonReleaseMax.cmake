# Opt-in Windows production profile. The release-max preset selects the exact
# clang-cl and LLD shipped in the sibling Chromium tree; ordinary builds retain
# their compiler, flags and output directory.
include_guard(GLOBAL)

option(KR_RELEASE_MAX_SYMBOLS "Emit line-table PDBs for optimized UI crash reports" ON)

if(NOT KR_PLATFORM STREQUAL "windows" OR
   NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
   NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  message(FATAL_ERROR "KR_RELEASE_MAX requires Windows clang-cl; use the release-max preset")
endif()

# Chromium ships the LLVM library driver inside lld-link.exe, not as a separate
# llvm-lib.exe. Its /lib mode indexes the same LLVM bitcode used by ThinLTO.
# Keep this override inside the opt-in graph so other profiles keep their ar.
foreach(lang IN ITEMS C CXX)
  set(CMAKE_${lang}_CREATE_STATIC_LIBRARY
    "<CMAKE_AR> /lib /nologo <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
  set(CMAKE_${lang}_CREATE_STATIC_LIBRARY_IPO
    "${CMAKE_${lang}_CREATE_STATIC_LIBRARY}")
endforeach()

function(kr_release_max_options target)
  set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  # clang-cl needs /clang: forwarding for the LLVM -O3 driver option. Omitted
  # aggregate fields intentionally retain their default/value initialization.
  target_compile_options(${target} PRIVATE
    "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:/clang:-O3;/clang:-Wno-missing-field-initializers;/Gy;/Gw>")
  if(KR_RELEASE_MAX_SYMBOLS)
    target_compile_options(${target} PRIVATE
      "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:/Z7;/clang:-gline-tables-only>")
  endif()

  get_target_property(kind ${target} TYPE)
  if(kind STREQUAL "SHARED_LIBRARY")
    # Establish the COM and Win32 include boundary before any native projection.
    # commonMain deliberately receives no Windows headers or forced includes.
    target_compile_options(${target} PRIVATE
      "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:/FI${PROJECT_SOURCE_DIR}/modules/shell/src/windowsMain/cpp/winrt_compat.hpp>")
    target_link_options(${target} PRIVATE
      "$<$<CONFIG:Release>:/lldltocache:${CMAKE_BINARY_DIR}/thinlto-cache>"
      "$<$<CONFIG:Release>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/opt:lldlto=3;/opt:lldltocgo=3;-mllvm:-hot-cold-split=true;-mllvm:-enable-ext-tsp-block-placement=true>")
    if(KR_RELEASE_MAX_SYMBOLS)
      target_link_options(${target} PRIVATE
        "$<$<CONFIG:Release>:/DEBUG:FULL;/PDBALTPATH:krypton_ui.pdb>")
    endif()
  endif()
endfunction()
