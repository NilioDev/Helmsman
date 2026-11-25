configure_file(version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/helmsman/version.hpp" @ONLY)

file(GLOB_RECURSE HLM_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(hlm_core STATIC ${HLM_CORE_SOURCES})
add_library(helmsman::core ALIAS hlm_core)

target_include_directories(hlm_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(hlm_core PUBLIC cxx_std_20)
set_target_properties(hlm_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(hlm_core PUBLIC Threads::Threads)
hlm_set_warnings(hlm_core)
