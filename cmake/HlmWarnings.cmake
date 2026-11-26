# Applies the project's warning flags to a target.
function(hlm_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        # Standard C functions such as getenv and fopen are fine for us;
        # MSVC's "_s" replacements are not portable.
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        if(HLM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
        # Clang's -Wconversion also warns of every signed/unsigned mix, which
        # GCC's leaves out for C++; keep the two compilers alike.
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${target} PRIVATE -Wno-sign-conversion)
        endif()
        if(HLM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
