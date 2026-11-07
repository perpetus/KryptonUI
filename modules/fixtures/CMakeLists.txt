# MockBrowser: a fake host that talks to the UI module only through abi.h,
# exactly like Chromium will. Used by the preview app.
kr_module(fixtures KIND STATIC COMMON_ONLY DEPENDS kr_abi)
if(KR_BUILD_TESTS)
  # The fixture itself remains ABI-only; protocol integration tests use the
  # real headless module rather than duplicating shared reducers in a stub.
  target_link_libraries(kr_fixtures_tests PRIVATE kr_shell_headless)
endif()

# The reference host uses the same appearance tokens without linking the UI
# module or bypassing its C ABI.
get_target_property(KR_SHELL_TOKEN_INCLUDES kr_shell_common INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(kr_fixtures_common PRIVATE ${KR_SHELL_TOKEN_INCLUDES})
add_dependencies(kr_fixtures_common kr_design_tokens)
if(TARGET kr_fixtures_tests)
  target_include_directories(kr_fixtures_tests PRIVATE ${KR_SHELL_TOKEN_INCLUDES})
  add_dependencies(kr_fixtures_tests kr_design_tokens)
endif()
