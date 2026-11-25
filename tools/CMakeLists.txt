# Developer tools. They read the developer's own game files; what they write
# stays on the developer's machine and is never committed.
add_executable(hlm_dump dump.cpp)
set_target_properties(hlm_dump PROPERTIES OUTPUT_NAME helmsman-dump)
target_link_libraries(hlm_dump PRIVATE helmsman::core)
hlm_set_warnings(hlm_dump)
