add_executable(hlm_app WIN32 MACOSX_BUNDLE main.cpp)
set_target_properties(hlm_app PROPERTIES
    OUTPUT_NAME Helmsman
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.niliodev.helmsman
    MACOSX_BUNDLE_BUNDLE_NAME Helmsman
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
target_link_libraries(hlm_app PRIVATE helmsman::core SDL3::SDL3)
hlm_set_warnings(hlm_app)
