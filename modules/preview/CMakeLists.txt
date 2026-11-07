if(NOT KR_PLATFORM STREQUAL "windows")
  return()
endif()

# The preview deliberately links only the C contract and mock browser. Loading
# the shell through its exports exercises the same boundary as Chromium.
kr_module(preview KIND EXECUTABLE PLATFORM_ONLY WIN32_EXECUTABLE
  DEPENDS kr_abi kr_fixtures)
get_target_property(KR_SHELL_PUBLIC_INCLUDES kr_shell_common INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(kr_preview PRIVATE ${KR_SHELL_PUBLIC_INCLUDES})
add_dependencies(kr_preview kr_design_tokens kr_design_grain kr_shell)
target_link_libraries(kr_preview PRIVATE kr::WindowsAppSDK user32 gdi32 shell32 ole32 windowscodecs dwmapi uxtheme msimg32 d3d11 dxgi)

if(KR_BUILD_TESTS)
  add_executable(kr_preview_appearance_tests
    src/windowsTest/cpp/appearance_store_test.cpp
    src/windowsTest/cpp/host_scene_test.cpp
    src/windowsMain/cpp/appearance_store.cpp
    src/windowsMain/cpp/host_scene.cpp
    src/windowsMain/cpp/page_surface.cpp
    src/windowsMain/cpp/theme_backdrop.cpp)
  target_include_directories(kr_preview_appearance_tests PRIVATE
    src/windowsMain/cpp ${KR_SHELL_PUBLIC_INCLUDES})
  target_link_libraries(kr_preview_appearance_tests PRIVATE kr_fixtures kr_testing
    kr::WindowsAppSDK gdi32 msimg32)
  add_dependencies(kr_preview_appearance_tests kr_design_tokens kr_design_grain)
  kr_compile_options(kr_preview_appearance_tests)
  add_test(NAME preview_appearance COMMAND kr_preview_appearance_tests)
endif()
