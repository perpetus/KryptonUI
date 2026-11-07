# The contract between the browser and a platform UI module. Plain C, header
# only, so Chromium (or any host) can include it without adopting C++23.
add_library(kr_abi INTERFACE)
target_include_directories(kr_abi INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Compile the header as C once, so a C-only construct can't slip in unnoticed.
add_library(kr_abi_c_check OBJECT "${CMAKE_CURRENT_SOURCE_DIR}/check/abi_c_check.c")
target_link_libraries(kr_abi_c_check PRIVATE kr_abi)
