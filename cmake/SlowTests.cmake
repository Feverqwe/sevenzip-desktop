if(NOT BUILD_TESTING)
    return()
endif()

# Long suites and high-memory boundary checks require explicit validation.
# The FAT maximum check decodes about 1 GiB of metadata from a virtual input.
foreach(sevenzip_slow_test IN ITEMS
        fat_maximum
        encrypted_dmg_network
        native_rename_2 native_rename_301
        native_external_2 native_external_301
        native_renderer_failure native_renderer_copy
        native_nested_stream native_nested_memory native_nested_disk
        interarchive_refresh interarchive_recovery
        panel_creation_publication copy_archive_refresh
        ntfs_stream codec_update
        ui_copy_routing archive_reload_command drag_reload_completion panel_activation panel_key)
    if(TEST ${sevenzip_slow_test})
        set_property(TEST ${sevenzip_slow_test} APPEND PROPERTY LABELS slow)
    endif()
endforeach()

# Linux repeats are below the ordinary-run threshold. Keep the existing
# exclusions on the other platforms until their complete matrix is remeasured.
if(TEST panel_creation_path AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_property(TEST panel_creation_path APPEND PROPERTY LABELS slow)
endif()

if(APPLE AND TEST sparse_image_ui)
    set_property(TEST sparse_image_ui APPEND PROPERTY LABELS slow)
endif()
