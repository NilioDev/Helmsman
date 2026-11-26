# Provides the chdr-static target (libchdr, BSD-3-Clause: reads .chd disc
# images) and the miniz target it brings along (MIT: reads .zip files).
#
# Downloads and builds the pinned commit below.

set(HLM_CHDR_COMMIT "47d794151b2b64f6aba60304e6480f4f5a2bc24b" CACHE STRING "libchdr commit to build")

include(FetchContent)
set(CHDR_WANT_TESTS OFF CACHE BOOL "" FORCE)
set(INSTALL_STATIC_LIBS OFF CACHE BOOL "" FORCE)
set(WITH_SYSTEM_ZLIB OFF CACHE BOOL "" FORCE)
set(WITH_SYSTEM_ZSTD OFF CACHE BOOL "" FORCE)
set(CHDR_FLAC_BACKEND "drflac" CACHE STRING "" FORCE)
# Only the static library: nothing of it is installed beside Helmsman.
set(HLM_SAVED_SHARED ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD) # FetchContent_Populate, for EXCLUDE_FROM_ALL on CMake 3.22
endif()
FetchContent_Declare(chdr
    GIT_REPOSITORY https://github.com/rtissera/libchdr.git
    GIT_TAG ${HLM_CHDR_COMMIT}
    GIT_PROGRESS TRUE)
FetchContent_GetProperties(chdr)
if(NOT chdr_POPULATED)
    FetchContent_Populate(chdr)
    add_subdirectory(${chdr_SOURCE_DIR} ${chdr_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
set(BUILD_SHARED_LIBS ${HLM_SAVED_SHARED})
set_target_properties(chdr-static PROPERTIES POSITION_INDEPENDENT_CODE ON)
foreach(dep miniz chdr-lzma zstd)
    if(TARGET ${dep})
        set_target_properties(${dep} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endforeach()
# Its headers are not ours: keep our warning flags away from them.
get_target_property(HLM_CHDR_INC chdr-static INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(chdr-static PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${HLM_CHDR_INC}")
# miniz's own target names no folder for its header.
if(TARGET miniz)
    target_include_directories(miniz SYSTEM INTERFACE "${chdr_SOURCE_DIR}/deps/miniz-3.1.2")
endif()
