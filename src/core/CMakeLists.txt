# Nuked OPL3 (LGPL-2.1-or-later): the FM sound chip the music was written for.
add_library(hlm_opl3 STATIC "${PROJECT_SOURCE_DIR}/third_party/nuked-opl3/opl3.c")
target_include_directories(hlm_opl3 PUBLIC "${PROJECT_SOURCE_DIR}/third_party/nuked-opl3")
set_target_properties(hlm_opl3 PROPERTIES POSITION_INDEPENDENT_CODE ON)

configure_file(version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/helmsman/version.hpp" @ONLY)

file(GLOB_RECURSE HLM_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(hlm_core STATIC ${HLM_CORE_SOURCES})
add_library(helmsman::core ALIAS hlm_core)

target_include_directories(hlm_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(hlm_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(hlm_core PUBLIC cxx_std_20)
set_target_properties(hlm_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(hlm_core PUBLIC Threads::Threads PRIVATE hlm_opl3)
hlm_set_warnings(hlm_core)
