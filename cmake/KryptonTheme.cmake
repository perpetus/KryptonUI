# Compile the independent theme renderer at build time, using the Windows SDK.
set(KR_THEME_SHADER "${CMAKE_CURRENT_SOURCE_DIR}/src/windowsMain/shaders/theme_gradient.hlsl")
foreach(stage vertex pixel)
  if(stage STREQUAL "vertex")
    set(entry VertexMain)
    set(profile vs_5_0)
  else()
    set(entry GradientMain)
    set(profile ps_5_0)
  endif()
  set(header "${KR_GENERATED_INCLUDE}/krypton/ui/design/theme_${stage}.generated.hpp")
  add_custom_command(OUTPUT "${header}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${KR_GENERATED_INCLUDE}/krypton/ui/design"
    COMMAND "${KR_FXC}" /nologo /O3 /Ges /WX /T ${profile} /E ${entry}
      /Vn g_theme_${stage} /Fh "${header}" "${KR_THEME_SHADER}"
    DEPENDS "${KR_THEME_SHADER}" VERBATIM)
  list(APPEND KR_THEME_SHADERS "${header}")
endforeach()
add_custom_target(kr_theme_shaders DEPENDS ${KR_THEME_SHADERS})
add_dependencies(kr_shell kr_theme_shaders)
