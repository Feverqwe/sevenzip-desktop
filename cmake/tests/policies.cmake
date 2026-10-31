if(BUILD_TESTING AND SEVENZIP_CODEC_MODULE)
    foreach(sevenzip_source_add_test IN ITEMS
            filesystem_selection_runtime_tests ui_operations_tests ui_update_modes_tests
            drag_drop_add_ui_tests)
        target_compile_definitions(${sevenzip_source_add_test} PRIVATE
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\"")
    endforeach()
endif()

include(cmake/CompactTestBinaries.cmake)
include(cmake/SlowTests.cmake)
if(BUILD_TESTING)
    add_test(NAME ctest_selection COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/ctest_selection_tests.py")
endif()
if(BUILD_TESTING AND (MSVC OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    add_test(NAME compact_test_binaries COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/compact_test_binaries_tests.py")
endif()

if(BUILD_TESTING)
    qt_add_executable(widget_dialog_fixture_tests tests/ui/support/widget_dialog_fixture_tests.cpp)
    target_link_libraries(widget_dialog_fixture_tests PRIVATE sevenzip_widget_dialog_fixtures)
    target_include_directories(widget_dialog_fixture_tests PRIVATE tests)
    target_link_libraries(widget_dialog_fixture_tests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME widget_dialog_fixture COMMAND widget_dialog_fixture_tests -silent)
    set_tests_properties(widget_dialog_fixture PROPERTIES TIMEOUT 10
        RESOURCE_LOCK sevenzip_native_ui ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    foreach(sevenzip_widget_dialog_target IN ITEMS
            browse_dialog_tests nested_update_tests archive_progress_tests progress_cancel_tests
            nested_edit_chain_tests archive_external_edit_tests archive_external_security_tests
            archive_external_lifetime_tests checksum_tests source_link_storage_tests
            options_dialog_tests system_page_tests flat_view_tests filesystem_reload_tests
            toolbar_archive_workflow_tests shell_update_window_tests shell_menu_window_tests source_drop_add_tests
            native_hash_ui_tests native_rar_ui_tests panel_list_key_tests source_context_menu_tests
            panel_copy_reload_tests native_drop_publication_tests panel_shortcut_tests source_delete_tests
            copy_publication_order_tests native_parent_publication_tests panel_key_source_tests)
        if(TARGET ${sevenzip_widget_dialog_target})
            target_link_libraries(${sevenzip_widget_dialog_target} PRIVATE sevenzip_widget_dialog_fixtures)
        endif()
    endforeach()
endif()

# The Windows offscreen plugin uses FreeType rather than the native Windows
# font database. Without a separately supplied font directory its fallback box
# metrics do not describe the application's controls. Compare these dialog and
# consumer tests using the same platform/fonts/style as the Windows application.
if(BUILD_TESTING AND WIN32)
    foreach(sevenzip_toolbar_test IN ITEMS toolbar_archive_workflow toolbar_archive_batch
            toolbar_creation_workflow manager_hash_workflow operation_launcher operation_extraction
            operation_creation operation_hash operation_benchmark)
      if(TEST ${sevenzip_toolbar_test})
        get_property(sevenzip_toolbar_environment TEST ${sevenzip_toolbar_test} PROPERTY ENVIRONMENT)
        list(FILTER sevenzip_toolbar_environment EXCLUDE REGEX "^QT_QPA_PLATFORM=")
        list(APPEND sevenzip_toolbar_environment "QT_QPA_PLATFORM=windows")
        set_property(TEST ${sevenzip_toolbar_test} PROPERTY ENVIRONMENT
            "${sevenzip_toolbar_environment}")
      endif()
    endforeach()
    foreach(sevenzip_native_ui_test IN ITEMS
            native_parent_publication native_drop_publication native_rename_2 native_rename_301 native_external_2 native_external_301 native_external_update native_nested_stream native_nested_memory native_nested_disk
            native_renderer_failure native_renderer_copy interarchive_refresh panel_creation_publication
            source_compress_dialog source_extract_advanced source_browse_dialog shell_archive_services shell_menu_window shell_update_window
            compression compression_advanced compression_path dialog_path_rules compression_error_policy compression_profile_selection compression_method_state compression_preferences compression_resources language_ui
            codec_commands_ui codec_creation_ui ui_geometry dialog_size
            options_dialog options_integration options_keyboard options_window appearance_settings system_page menu_page help_source help_ui temp_cleanup context_dialog
            filesystem_selection_runtime ui_operations ui_copy_same ui_copy_routing app_copy_routing ui_update_modes archive_metadata_ui source_drop_add
            drag_drop_add_ui drag_drop_add_independent drag_drop_add_temporary dropped_path_choice modal_refresh_window
            main_shell panel_shell source_context_menu panel_key window_placement panel_key_source panel_list_load panel_item_icons panel_name_text archive_rename_window panel_copy_reload copy_publication_order filesystem_reload address_navigation panel_parent_reload panel_folder_sync native_archive_properties filesystem_comment fs_drives_device_ui namespace_hash native_network_navigation native_persisted_locations address_combo panel_address_keys root_command persisted_location ui_navigation ui_archive_states ui_archive_volumes
            rename_lifecycle command_publication large_list_window worker_window_progress panel_sort_commands panel_refresh_drop panel_refresh_completion panel_rename_state deferred_rename navigation_reload panel_deleted_draw panel_column_menu panel_column_initialization panel_column_schema panel_column_sort panel_column_changes panel_cell_text native_column_workflow panel_item_changed
            archive_reload_command drag_reload_completion panel_creation_path copy_archive_refresh interarchive_recovery panel_load_count panel_activation progress_dialog progress_worker progress_cancel archive_progress volume_dialog language_dialog language_standard_buttons fm_settings list_settings_runtime
            language_main_dialog language_focus properties_runtime
            archive_layer_chain archive_layer_lifetime
            archive_item_open archive_external_edit external_editors_ui language_archive_command overwrite_layout
            alt_streams_command alt_streams_archive_ui fs_provider_command native_archive_ads_copy view_menu)
        get_property(sevenzip_native_ui_environment TEST ${sevenzip_native_ui_test}
            PROPERTY ENVIRONMENT)
        list(TRANSFORM sevenzip_native_ui_environment REPLACE
            "^QT_QPA_PLATFORM=offscreen$" "QT_QPA_PLATFORM=windows")
        set_property(TEST ${sevenzip_native_ui_test} PROPERTY ENVIRONMENT
            "${sevenzip_native_ui_environment}")
        # Native popup/focus checks share the logged-on Windows desktop even
        # when they do not write geometry artifacts.
        set_property(TEST ${sevenzip_native_ui_test} APPEND PROPERTY
            RESOURCE_LOCK sevenzip_native_ui)
    endforeach()
    # Full native UI matrices create 128/82/192 independent windows/workflows.
    # Measured Windows totals are 69/130/165s; allow twice that time rounded
    # up to a minute. Individual state waits and performance guards stay intact.
    set_tests_properties(panel_creation_path PROPERTIES TIMEOUT 180)
    set_tests_properties(interarchive_recovery PROPERTIES TIMEOUT 300)
    set_tests_properties(copy_archive_refresh PROPERTIES TIMEOUT 360)
    # Explicit long-matrix run: at the original caps Windows published
    # 188/137 of 192 Rename rows, 149/102 of 192 External rows,
    # 185 of 288 renderer rows and 91 of 256 interarchive rows.
    # Allow about twice the extrapolated whole-matrix time, rounded up.
    # Individual waits and application deadlines remain unchanged.
    set_tests_properties(native_rename_2 PROPERTIES TIMEOUT 720)
    set_tests_properties(native_rename_301 PROPERTIES TIMEOUT 1080)
    set_tests_properties(native_external_2 PROPERTIES TIMEOUT 660)
    set_tests_properties(native_external_301 PROPERTIES TIMEOUT 960)
    set_tests_properties(native_renderer_failure PROPERTIES TIMEOUT 600)
    set_tests_properties(interarchive_refresh PROPERTIES TIMEOUT 720)
    # Corrected creation reached 122/256 rows in 120s; Copy completed all
    # 98 Qt checks in 173.32s. Apply the same whole-matrix reserve.
    set_tests_properties(panel_creation_publication PROPERTIES TIMEOUT 540)
    set_tests_properties(native_renderer_copy PROPERTIES TIMEOUT 360)
    # The complete native context-menu matrix measured 51.6s on Windows.
    # Keep its per-state waits and allow twice the measured aggregate time.
    set_tests_properties(source_context_menu PROPERTIES TIMEOUT 120)
    # All 85 native Copy/Move cases passed in parts: the first 49 within
    # 300s, the remaining 36 in 249s. Reserve twice that aggregate upper
    # bound, rounded up to a minute; retain the individual state deadlines.
    set_tests_properties(ui_copy_routing PROPERTIES TIMEOUT 1140)
    # The complete native address matrix passed in 64s including process
    # startup/shutdown. Give its aggregate the same measured-time reserve.
    set_tests_properties(address_combo PROPERTIES TIMEOUT 180)
    # Native publication phases passed all 12 workflows in 30.6s including
    # process lifetime. Reserve twice that time, rounded up to a minute.
    set_tests_properties(copy_publication_order PROPERTIES TIMEOUT 120)
    # All 20 native navigation workflows passed in 51.1s including process
    # lifetime after the 60s aggregate cap expired in a batch. Reserve twice
    # the measured time; retain individual state waits and all scenarios.
    set_tests_properties(navigation_reload PROPERTIES TIMEOUT 120)
    # The complete native keyboard suite passed in 69.45s after its 90s
    # batch cap expired. Reserve twice the measured process time, rounded
    # up to a minute; keep every scenario and individual state deadline.
    set_tests_properties(panel_key_source PROPERTIES TIMEOUT 180)
    # Full native diagnostic processes passed in 55.39s/623.83s after
    # the 60s/180s batch caps expired. Keep individual waits/scenarios;
    # reserve twice the measured process time, rounded up to a minute.
    set_tests_properties(panel_rename_state PROPERTIES TIMEOUT 120)
    set_tests_properties(panel_activation PROPERTIES TIMEOUT 1260)
endif()

if(APPLE AND TEST copy_archive_refresh)
    # Cocoa completed 89 of 192 workflows in 120s with no row failures.
    # Apply the same twice-extrapolated full-matrix reserve as Windows:
    # 120 * 192 / 89 * 2, rounded up to a minute. This suite is opt-in slow;
    # individual state waits and application deadlines remain unchanged.
    set_tests_properties(copy_archive_refresh PROPERTIES TIMEOUT 540)
endif()

if(BUILD_TESTING)
    foreach(sevenzip_workflow_suite IN ITEMS developer_workflow hyperv_runner runtime_cache module_cache)
        add_test(NAME ${sevenzip_workflow_suite} COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/${sevenzip_workflow_suite}_tests.py")
        set_tests_properties(${sevenzip_workflow_suite} PROPERTIES TIMEOUT 60)
    endforeach()
    # This suite compiles and analyzes real C++ modules, including rejected borrows.
    set_tests_properties(developer_workflow PROPERTIES TIMEOUT 180)
    if(WIN32)
        add_test(NAME windows_reference_builder COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/windows_reference_builder_tests.py")
        set_tests_properties(windows_reference_builder PROPERTIES TIMEOUT 60)
    endif()
endif()
sevenzip_add_window_test_target()
include(cmake/DeveloperWorkflow.cmake)
