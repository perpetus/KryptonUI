# Windows App SDK (WinUI 3) + C++/WinRT for CMake, without MSBuild.
#
# Downloads a pinned set of NuGet packages once into KR_DEPS_DIR, verifies
# their SHA-256, and generates C++/WinRT projection headers once. The result
# is one interface target, kr::WindowsAppSDK.
#
# The module is framework-dependent: at runtime it bootstraps the installed
# Windows App Runtime 2.6 (see windowsMain/cpp/winui_runtime.cpp). So only
# headers and metadata are needed here; the bootstrap DLL is the one binary
# we ship next to krypton_ui.dll.

include_guard(GLOBAL)

set(KR_DEPS_DIR "${PROJECT_SOURCE_DIR}/.deps" CACHE PATH
    "Where NuGet packages and generated C++/WinRT headers are cached")

# id | version | sha256 of the .nupkg. These are the component packages the
# Microsoft.WindowsAppSDK 2.6.5 metapackage resolves to. Only the components
# consumed by this native CMake build are downloaded; Base's MSBuild tasks
# and the metapackage's unrelated feature components are not used here.
set(KR_WINAPPSDK_PACKAGES
  "Microsoft.Windows.CppWinRT|2.0.250303.1|955e3051b35db1c00177ed14ab6b7b995c3b32a53fdb6c184ea53b1dba66e439"
  "Microsoft.WindowsAppSDK.Foundation|2.6.5|548033e51be965804f7119e1936fe7f78f324d668becb731fcacb101f3bb18e4"
  "Microsoft.WindowsAppSDK.InteractiveExperiences|2.6.5|3782df5d796dfe872175da0db2b64b616611532fd14b06d9e2102895286df136"
  "Microsoft.WindowsAppSDK.WinUI|2.6.5|8cd902f8abaf4ac0f75bdd187fd420eb98419818f39ab5e853ed126bc26a6af9"
  "Microsoft.Web.WebView2|1.0.3719.77|2f6be3a10a1a8d6d1fde986af4131dab344f8181fffe75590824e0f4b037ed73")

# The runtime the module bootstraps at startup; must match the packages above.
set(KR_WINAPPSDK_RUNTIME_MAJOR_MINOR 0x00020006)
# From the 2.6.5 Runtime package's WindowsAppSDK-VersionInfo.h. Require the
# servicing version that supplies the fixes this module was built against.
set(KR_WINAPPSDK_RUNTIME_MIN_VERSION 0x0002000600050000ULL)

function(_kr_nuget_fetch id version sha256 out_dir_var)
  string(TOLOWER "${id}" lower_id)
  set(dir "${KR_DEPS_DIR}/nuget/${lower_id}.${version}")
  set(stamp "${dir}/.krypton-extracted")
  if(NOT EXISTS "${stamp}")
    set(archive "${KR_DEPS_DIR}/downloads/${lower_id}.${version}.nupkg")
    if(NOT EXISTS "${archive}")
      message(STATUS "Downloading ${id} ${version}")
      file(DOWNLOAD
        "https://api.nuget.org/v3-flatcontainer/${lower_id}/${version}/${lower_id}.${version}.nupkg"
        "${archive}"
        EXPECTED_HASH SHA256=${sha256}
        TLS_VERIFY ON
        STATUS status)
      list(GET status 0 code)
      if(NOT code EQUAL 0)
        file(REMOVE "${archive}")
        message(FATAL_ERROR "Downloading ${id} ${version} failed: ${status}")
      endif()
    else()
      file(SHA256 "${archive}" actual)
      if(NOT actual STREQUAL sha256)
        message(FATAL_ERROR "${archive} does not match its pinned SHA-256")
      endif()
    endif()
    file(REMOVE_RECURSE "${dir}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${dir}")
    file(TOUCH "${stamp}")
  endif()
  set(${out_dir_var} "${dir}" PARENT_SCOPE)
endfunction()

foreach(entry IN LISTS KR_WINAPPSDK_PACKAGES)
  string(REPLACE "|" ";" fields "${entry}")
  list(GET fields 0 id)
  list(GET fields 1 version)
  list(GET fields 2 sha256)
  _kr_nuget_fetch("${id}" "${version}" "${sha256}" dir)
  string(REPLACE "." "_" key "${id}")
  set(KR_PKG_${key} "${dir}")
endforeach()

set(_cppwinrt_pkg "${KR_PKG_Microsoft_Windows_CppWinRT}")
set(_foundation_pkg "${KR_PKG_Microsoft_WindowsAppSDK_Foundation}")
set(_ie_pkg "${KR_PKG_Microsoft_WindowsAppSDK_InteractiveExperiences}")
set(_winui_pkg "${KR_PKG_Microsoft_WindowsAppSDK_WinUI}")
set(_webview2_pkg "${KR_PKG_Microsoft_Web_WebView2}")

# --- C++/WinRT projection, generated once per package set -------------------
set(_inputs
  "sdk"
  "${_foundation_pkg}/metadata"
  "${_ie_pkg}/metadata/10.0.18362.0"
  "${_winui_pkg}/metadata"
  "${_webview2_pkg}/lib/Microsoft.Web.WebView2.Core.winmd")
string(SHA1 _projection_key "${KR_WINAPPSDK_PACKAGES};${_inputs}")
string(SUBSTRING "${_projection_key}" 0 12 _projection_key)
set(KR_CPPWINRT_DIR "${KR_DEPS_DIR}/cppwinrt/${_projection_key}")

if(NOT EXISTS "${KR_CPPWINRT_DIR}/.krypton-generated")
  message(STATUS "Generating C++/WinRT projection (one time, ~30 s)")
  set(_args)
  foreach(input IN LISTS _inputs)
    list(APPEND _args -input "${input}")
  endforeach()
  file(REMOVE_RECURSE "${KR_CPPWINRT_DIR}")
  execute_process(
    COMMAND "${_cppwinrt_pkg}/bin/cppwinrt.exe" ${_args} -output "${KR_CPPWINRT_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "cppwinrt failed (${result}):\n${output}")
  endif()
  file(TOUCH "${KR_CPPWINRT_DIR}/.krypton-generated")
endif()

# --- the target ---------------------------------------------------------------
add_library(kr_windows_app_sdk INTERFACE)
add_library(kr::WindowsAppSDK ALIAS kr_windows_app_sdk)
target_include_directories(kr_windows_app_sdk SYSTEM INTERFACE
  "${KR_CPPWINRT_DIR}"
  "${_foundation_pkg}/include"
  "${_ie_pkg}/include"
  "${_winui_pkg}/include")
target_compile_definitions(kr_windows_app_sdk INTERFACE
    KR_WINAPPSDK_RUNTIME_MAJOR_MINOR=${KR_WINAPPSDK_RUNTIME_MAJOR_MINOR}
    KR_WINAPPSDK_RUNTIME_MIN_VERSION=${KR_WINAPPSDK_RUNTIME_MIN_VERSION})
# C++/WinRT activation goes through the umbrella import library.
target_link_libraries(kr_windows_app_sdk INTERFACE windowsapp.lib)

set(KR_WINAPPSDK_BOOTSTRAP_DLL
    "${_foundation_pkg}/runtimes/win-x64/native/Microsoft.WindowsAppRuntime.Bootstrap.dll"
    CACHE INTERNAL "")
