# The application and window scenarios consume one production composition.
# Object owners retain their Qt/moc settings; this archive carries their objects
# and required libraries without compiling the implementation in each test.
add_library(sevenzip_window STATIC)
set_target_properties(sevenzip_window PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_compile_features(sevenzip_window PUBLIC cxx_std_23)
target_link_libraries(sevenzip_window
    PUBLIC Qt6::Widgets sevenzip_location sevenzip_folder_loader
    PRIVATE
        sevenzip_main_window sevenzip_native_menu_presentation sevenzip_panel_context_menu
        sevenzip_compression_ui
        sevenzip_panel_selection sevenzip_file_transfer sevenzip_filesystem_scan)

# Register the build-only group after all test consumers have been declared.
function(sevenzip_add_window_test_target)
    if(BUILD_TESTING)
        add_custom_target(sevenzip_window_tests)
        get_property(sevenzip_window_consumers DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
        foreach(consumer IN LISTS sevenzip_window_consumers)
            get_target_property(consumer_type ${consumer} TYPE)
            get_target_property(consumer_libraries ${consumer} LINK_LIBRARIES)
            if(consumer_type STREQUAL "EXECUTABLE" AND "sevenzip_window" IN_LIST consumer_libraries)
                add_dependencies(sevenzip_window_tests ${consumer})
            endif()
        endforeach()
    endif()
endfunction()
