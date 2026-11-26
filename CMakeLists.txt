cmake_minimum_required(VERSION 3.22)

project(Helmsman
    VERSION 0.1.0
    DESCRIPTION "Clean-room engine that plays two 1990s starship adventure games from your own CDs"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Without a build type the compiler does not optimise, and the game's room
# code runs in our interpreter: build optimised unless asked otherwise.
get_property(HLM_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT HLM_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(HLM_BUILD_APP "Build the game executable (needs SDL3)" ON)
option(HLM_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(HLM_BUILD_TOOLS "Build the developer tools" ${PROJECT_IS_TOP_LEVEL})
option(HLM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(HlmWarnings)
include(HlmChdr)

# Engine core: platform independent, no SDL. Everything that can be tested
# without a screen lives here.
add_subdirectory(src/core)

if(HLM_BUILD_APP)
    include(HlmSDL3)
    add_subdirectory(src/app)
endif()

if(HLM_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(HLM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
