# The UI module: krypton_ui.dll (Windows), later a framework on macOS.

# Design tokens: design/tokens.json -> krypton/ui/design/tokens.generated.hpp
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(KR_GENERATED_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
set(KR_TOKENS_HEADER
    "${KR_GENERATED_INCLUDE}/krypton/ui/design/tokens.generated.hpp")
add_custom_command(
  OUTPUT "${KR_TOKENS_HEADER}"
  COMMAND Python3::Interpreter
          "${PROJECT_SOURCE_DIR}/tools/gen_tokens.py"
          "${PROJECT_SOURCE_DIR}/design/tokens.json"
          "${KR_TOKENS_HEADER}"
  DEPENDS "${PROJECT_SOURCE_DIR}/design/tokens.json"
          "${PROJECT_SOURCE_DIR}/tools/gen_tokens.py"
  COMMENT "Generating design tokens"
  VERBATIM)
add_custom_target(kr_design_tokens DEPENDS "${KR_TOKENS_HEADER}")

set(KR_ICONS_HEADER "${KR_GENERATED_INCLUDE}/krypton/ui/design/icons.generated.hpp")
add_custom_command(
  OUTPUT "${KR_ICONS_HEADER}"
  COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/gen_icons.py"
          "${PROJECT_SOURCE_DIR}/design/icons" "${KR_ICONS_HEADER}"
  DEPENDS "${PROJECT_SOURCE_DIR}/tools/gen_icons.py"
          "${PROJECT_SOURCE_DIR}/tools/gen_icon_packs.py"
          "${PROJECT_SOURCE_DIR}/design/icons/sidebar.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/add.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/sparkles.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/face-sun.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/moon-stars.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/algorithm.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/library.svg"
    "${PROJECT_SOURCE_DIR}/design/icons/site-tools.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/dart-down.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/globe.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/back.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/forward.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/close.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/security.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/screen.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/media-play.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/media-pause.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/media-next.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/media-previous.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/media-mute.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/media-unmute.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/microphone-fill.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/microphone-blocked-fill.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/video-fill.svg"
          "${PROJECT_SOURCE_DIR}/design/icons/video-blocked-fill.svg"
  COMMENT "Generating native icon data"
  VERBATIM)
add_custom_target(kr_design_icons DEPENDS "${KR_ICONS_HEADER}")

# Zen's animated library sprites, rewritten for SvgImageSource.
file(GLOB KR_SPRITE_SOURCES CONFIGURE_DEPENDS
     "${PROJECT_SOURCE_DIR}/design/icons/library/*.svg")
set(KR_SPRITES_HEADER
    "${KR_GENERATED_INCLUDE}/krypton/ui/design/sprites.generated.hpp")
add_custom_command(
  OUTPUT "${KR_SPRITES_HEADER}"
  COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/gen_sprites.py"
          "${PROJECT_SOURCE_DIR}/design/icons/library" "${KR_SPRITES_HEADER}"
  DEPENDS "${PROJECT_SOURCE_DIR}/tools/gen_sprites.py" ${KR_SPRITE_SOURCES}
  COMMENT "Generating library sprites"
  VERBATIM)
add_custom_target(kr_design_sprites DEPENDS "${KR_SPRITES_HEADER}")

# Swappable chrome icon packs (design/icons/packs, compiled into the module).
file(GLOB_RECURSE KR_ICON_PACK_SOURCES CONFIGURE_DEPENDS
     "${PROJECT_SOURCE_DIR}/design/icons/packs/*.svg"
     "${PROJECT_SOURCE_DIR}/design/icons/packs/manifest.json")
set(KR_ICON_PACKS_HEADER
    "${KR_GENERATED_INCLUDE}/krypton/ui/design/icon_packs.generated.hpp")
add_custom_command(
  OUTPUT "${KR_ICON_PACKS_HEADER}"
  COMMAND Python3::Interpreter -I "${PROJECT_SOURCE_DIR}/tools/gen_icon_packs.py"
          generate "${PROJECT_SOURCE_DIR}/design/icons/packs"
          "${KR_ICON_PACKS_HEADER}"
  DEPENDS "${PROJECT_SOURCE_DIR}/tools/gen_icon_packs.py"
          ${KR_ICON_PACK_SOURCES}
  COMMENT "Generating chrome icon packs"
  VERBATIM)
add_custom_target(kr_design_icon_packs DEPENDS "${KR_ICON_PACKS_HEADER}")

if(KR_BUILD_TESTS)
  add_test(NAME icon_pack_generator
    COMMAND Python3::Interpreter -I
      "${PROJECT_SOURCE_DIR}/tools/tests/test_gen_icon_packs.py")
endif()

set(KR_GRAIN_HEADER "${KR_GENERATED_INCLUDE}/krypton/ui/design/grain.generated.hpp")
add_custom_command(OUTPUT "${KR_GRAIN_HEADER}"
  COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/gen_theme_grain.py"
          "${PROJECT_SOURCE_DIR}/design/textures/grain-bg.png" "${KR_GRAIN_HEADER}"
  DEPENDS "${PROJECT_SOURCE_DIR}/tools/gen_theme_grain.py" "${PROJECT_SOURCE_DIR}/design/textures/grain-bg.png"
  VERBATIM)
add_custom_target(kr_design_grain DEPENDS "${KR_GRAIN_HEADER}")

# Onboarding brand artwork is embedded once, so preview and browser installs
# need neither an asset directory nor network access to show provider icons.
file(GLOB KR_ONBOARDING_ICON_SOURCES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/design/icons/onboarding/*.svg"
  "${PROJECT_SOURCE_DIR}/design/icons/onboarding/*.png"
  "${PROJECT_SOURCE_DIR}/design/icons/browsers/*.svg"
  "${PROJECT_SOURCE_DIR}/design/icons/browsers/*.png"
  "${PROJECT_SOURCE_DIR}/design/icons/search_engines/*.png")
set(KR_ONBOARDING_ICONS_HEADER
  "${KR_GENERATED_INCLUDE}/krypton/ui/design/onboarding_icons.generated.hpp")
add_custom_command(OUTPUT "${KR_ONBOARDING_ICONS_HEADER}"
  COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/gen_onboarding_icons.py"
    "${PROJECT_SOURCE_DIR}/design/icons/onboarding.json" "${KR_ONBOARDING_ICONS_HEADER}"
  DEPENDS "${PROJECT_SOURCE_DIR}/tools/gen_onboarding_icons.py"
    "${PROJECT_SOURCE_DIR}/tools/gen_icon_packs.py"
    "${PROJECT_SOURCE_DIR}/design/icons/onboarding.json" ${KR_ONBOARDING_ICON_SOURCES}
  COMMENT "Embedding onboarding browser and search icons"
  VERBATIM)
add_custom_target(kr_onboarding_icons DEPENDS "${KR_ONBOARDING_ICONS_HEADER}")

if(KR_PLATFORM STREQUAL "windows")
  include(WindowsAppSDK)
endif()

kr_module(shell
  KIND SHARED
  OUTPUT_NAME krypton_ui
  DEPENDS kr_abi)

target_include_directories(kr_shell_common PUBLIC "${KR_GENERATED_INCLUDE}")
add_dependencies(kr_shell_common kr_design_tokens)
add_dependencies(kr_shell_common kr_design_icons)
add_dependencies(kr_shell_common kr_design_sprites)
add_dependencies(kr_shell_common kr_design_icon_packs)
add_dependencies(kr_shell_common kr_design_grain)
add_dependencies(kr_shell_common kr_onboarding_icons)
# abi_bridge.cpp defines the exported kr_ui_* entry points.
target_compile_definitions(kr_shell_common PRIVATE KR_UI_BUILDING)

if(KR_PLATFORM STREQUAL "windows")
  enable_language(RC)
  configure_file("src/windowsMain/resources/krypton_ui.rc.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/krypton_ui.rc" @ONLY)
  target_sources(kr_shell PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/krypton_ui.rc")
  include(KryptonGlass)
  include(KryptonTheme)
  target_link_libraries(kr_shell PRIVATE kr::WindowsAppSDK dwmapi comctl32
                        shlwapi shcore d3d11 dxgi)
  file(GLOB KR_SETTINGS_IMAGES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/design/settings-layouts/*.png")
  set_property(TARGET kr_shell APPEND PROPERTY LINK_DEPENDS ${KR_SETTINGS_IMAGES})
  add_custom_command(TARGET kr_shell POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:kr_shell>/assets/settings"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${KR_SETTINGS_IMAGES} "$<TARGET_FILE_DIR:kr_shell>/assets/settings"
    VERBATIM)
  # Loaded by path at runtime, from the module's own folder.
  add_custom_command(TARGET kr_shell POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${KR_WINAPPSDK_BOOTSTRAP_DLL}" "$<TARGET_FILE_DIR:kr_shell>"
    VERBATIM)
  install(TARGETS kr_shell RUNTIME DESTINATION . COMPONENT Runtime)
  install(FILES "${KR_WINAPPSDK_BOOTSTRAP_DLL}" DESTINATION .
    COMPONENT Runtime)
  install(FILES ${KR_SETTINGS_IMAGES} DESTINATION assets/settings
    COMPONENT Runtime)
endif()

if(KR_PLATFORM STREQUAL "windows" AND KR_BUILD_TESTS)
  add_executable(kr_native_window_tests
    src/windowsTest/cpp/native_window_test.cpp
    src/windowsMain/cpp/native_frame.cpp
    src/windowsMain/cpp/native_input.cpp)
  target_include_directories(kr_native_window_tests PRIVATE
    src/windowsMain/cpp)
  target_link_libraries(kr_native_window_tests PRIVATE
    kr_shell_headless kr_testing user32 comctl32 dwmapi)
  kr_compile_options(kr_native_window_tests)
  add_test(NAME native_window COMMAND kr_native_window_tests)
  set_tests_properties(native_window PROPERTIES TIMEOUT 20)

  add_executable(kr_native_asset_tests
    src/windowsTest/cpp/native_asset_test.cpp
    src/windowsMain/cpp/icon_view.cpp
    src/windowsMain/cpp/sprite_icon.cpp
    src/windowsMain/cpp/native_motion.cpp
    src/windowsMain/cpp/native_input.cpp
    src/windowsMain/cpp/native_theme.cpp
    src/windowsMain/cpp/xaml_factory.cpp
    src/windowsMain/cpp/winui_runtime.cpp)
  target_include_directories(kr_native_asset_tests PRIVATE src/windowsMain/cpp)
  target_link_libraries(kr_native_asset_tests PRIVATE
    kr_shell_headless kr_testing kr::WindowsAppSDK comctl32 shlwapi shcore)
  kr_compile_options(kr_native_asset_tests)
  add_test(NAME native_assets COMMAND kr_native_asset_tests)
  set_tests_properties(native_assets PROPERTIES TIMEOUT 20)
endif()
