    qt_add_executable(ui_copy_routing_tests tests/filemanager/ui_copy_routing_tests.cpp)
    target_link_libraries(ui_copy_routing_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(ui_copy_routing_tests PRIVATE sevenzip_window)
    target_include_directories(ui_copy_routing_tests PRIVATE src tests)
    target_link_libraries(ui_copy_routing_tests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME ui_copy_routing COMMAND ui_copy_routing_tests -silent)
    set_tests_properties(ui_copy_routing PROPERTIES TIMEOUT 120 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/ui-copy-routing-settings.ini")
    # Keep Copy/Move, cancellation, archive routing and modal selection changes
    # in the default run; the complete presentation matrix is separate.
    add_test(NAME ui_copy_routing_quick COMMAND ui_copy_routing_tests -silent
        sourceReloadPublishesNamespace
        outsidePeerStillRefreshesDestination:0-0-copy
        outsidePeerStillRefreshesDestination:1-1-move
        outsidePeerStillRefreshesDestination:0-1-dialog-cancel
        outsidePeerStillRefreshesDestination:1-0-overwrite-cancel
        outsidePeerStillRefreshesDestination:relative-copy
        outsidePeerStillRefreshesDestination:relative-move
        outsidePeerStillRefreshesDestination:link-copy
        unselectedItemsReachSourceSmartCopy:0-0-0---440
        unselectedItemsReachSourceSmartCopy:1-1-1-ru-360
        rejectsDestinationInsideSourceFolder
        capturedItemsSurviveModalSelectionChange:fs-to-fs-copy---0
        capturedItemsSurviveModalSelectionChange:archive-to-fs-move-ru-1
        capturedItemsSurviveModalSelectionChange:fs-to-archive-copy-ru-0
        capturedItemsSurviveModalSelectionChange:archive-to-archive-move---1
        capturedItemsSurviveModalSelectionChange:typed-archive-copy
        capturedItemsSurviveModalSelectionChange:typed-archive-move
        capturedItemsSurviveModalSelectionChange:large-fs-to-archive-move-1)
    set_tests_properties(ui_copy_routing_quick PROPERTIES TIMEOUT 45 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/ui-copy-routing-quick.ini")
    qt_add_executable(ui_update_modes_tests tests/filemanager/ui_update_modes_tests.cpp)
    target_link_libraries(ui_update_modes_tests PRIVATE sevenzip_window)
    target_include_directories(ui_update_modes_tests PRIVATE src tests)
    target_link_libraries(ui_update_modes_tests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME ui_update_modes COMMAND ui_update_modes_tests -silent)
    qt_add_executable(nested_edit_chain_tests
        tests/filemanager/nested_edit_chain_tests.cpp
        tests/platform/support/external_editor_fixture.cpp)
    target_link_libraries(nested_edit_chain_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(nested_edit_chain_tests PRIVATE sevenzip_window)
    target_include_directories(nested_edit_chain_tests PRIVATE src tests)
    target_sources(nested_edit_chain_tests PRIVATE tests/platform/support/native_property_failure_runtime.cpp)
    target_compile_definitions(nested_edit_chain_tests PRIVATE
        "SEVENZIP_EXTERNAL_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_EXTERNAL_FORMAT_FIXTURE=\"$<TARGET_FILE:external_format_fixture>\"")
    add_dependencies(nested_edit_chain_tests external_format_fixture)
    target_link_libraries(nested_edit_chain_tests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME nested_edit_chain COMMAND nested_edit_chain_tests -v1)
    set_tests_properties(nested_edit_chain PROPERTIES TIMEOUT 120 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/nested_edit_chain.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(archive_external_edit_tests
        tests/filemanager/archive_external_edit_tests.cpp
        tests/platform/support/external_editor_fixture.cpp)
    target_link_libraries(archive_external_edit_tests PRIVATE sevenzip_layout_inspection sevenzip_panel_load_fixture)
    target_link_libraries(archive_external_edit_tests PRIVATE sevenzip_window)
    target_include_directories(archive_external_edit_tests PRIVATE src tests)
    target_link_libraries(archive_external_edit_tests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME archive_external_edit COMMAND archive_external_edit_tests -v1)
    qt_add_executable(external_archive_context_tests tests/filemanager/external_archive_context_tests.cpp)
    target_link_libraries(external_archive_context_tests PRIVATE sevenzip_location sevenzip_widget_dialog_fixtures Qt6::Test)
    target_include_directories(external_archive_context_tests PRIVATE src tests)
    add_test(NAME external_archive_context COMMAND external_archive_context_tests updateSurvivesSourceDestruction -silent)
    qt_add_executable(external_task_lifecycle_tests
        tests/filemanager/external_task_lifecycle_tests.cpp
        tests/platform/support/external_editor_fixture.cpp)
    target_link_libraries(external_task_lifecycle_tests PRIVATE sevenzip_window sevenzip_widget_dialog_fixtures sevenzip_layout_inspection Qt6::Test)
    target_include_directories(external_task_lifecycle_tests PRIVATE src tests)
    add_test(NAME external_task_lifecycle COMMAND external_task_lifecycle_tests -v1)
    qt_add_executable(external_copy_lifetime_tests
        tests/filemanager/external_copy_lifetime_tests.cpp
        tests/platform/support/external_editor_fixture.cpp)
    target_link_libraries(external_copy_lifetime_tests PRIVATE sevenzip_window sevenzip_widget_dialog_fixtures Qt6::Test)
    target_include_directories(external_copy_lifetime_tests PRIVATE src tests)
    add_test(NAME external_copy_lifetime COMMAND external_copy_lifetime_tests -v1)
    set_tests_properties(external_copy_lifetime PROPERTIES TIMEOUT 45 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/external-copy-lifetime.ini")
    qt_add_executable(external_session_recovery_tests
        tests/filemanager/external_session_recovery_tests.cpp
        tests/platform/support/external_editor_fixture.cpp)
    target_link_libraries(external_session_recovery_tests PRIVATE sevenzip_window sevenzip_widget_dialog_fixtures Qt6::Test)
    target_include_directories(external_session_recovery_tests PRIVATE src tests)
    add_test(NAME external_session_recovery COMMAND external_session_recovery_tests -v1)
    set_tests_properties(external_session_recovery PROPERTIES TIMEOUT 120 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(external_task_lifecycle PROPERTIES TIMEOUT 60 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/external-task-lifecycle.ini")
    set_tests_properties(external_archive_context PROPERTIES TIMEOUT 45 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/external-archive-context.ini")
    qt_add_executable(archive_external_security_tests
        tests/filemanager/archive_external_security_tests.cpp
        tests/platform/support/external_editor_fixture.cpp)
    target_link_libraries(archive_external_security_tests PRIVATE sevenzip_window)
    target_include_directories(archive_external_security_tests PRIVATE src tests)
    target_link_libraries(archive_external_security_tests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME archive_external_security COMMAND archive_external_security_tests -silent)
    set_tests_properties(archive_external_security PROPERTIES TIMEOUT 90 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/archive-external-security-settings.ini")
    qt_add_executable(archive_external_lifetime_tests
        tests/filemanager/archive_external_lifetime_tests.cpp
        tests/platform/support/external_editor_fixture.cpp)
    target_link_libraries(archive_external_lifetime_tests PRIVATE sevenzip_location)
    target_include_directories(archive_external_lifetime_tests PRIVATE src tests)
    target_link_libraries(archive_external_lifetime_tests PRIVATE Qt6::Test)
    add_test(NAME archive_external_lifetime COMMAND archive_external_lifetime_tests -silent)
    set_tests_properties(archive_external_lifetime PROPERTIES TIMEOUT 60 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/archive-external-lifetime-settings.ini")
    qt_add_executable(external_gui_call_tests tests/filemanager/external_gui_call_tests.cpp)
    target_include_directories(external_gui_call_tests PRIVATE src)
    target_link_libraries(external_gui_call_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME external_gui_call COMMAND external_gui_call_tests -silent)
    set_tests_properties(external_gui_call PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(archive_external_edit PROPERTIES TIMEOUT 90 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/archive-external-edit-settings.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(ui_archive_volumes_tests tests/filemanager/ui_archive_volumes_tests.cpp)
    target_link_libraries(ui_archive_volumes_tests PRIVATE sevenzip_window)
    target_include_directories(ui_archive_volumes_tests PRIVATE src tests)
    target_link_libraries(ui_archive_volumes_tests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME ui_archive_volumes COMMAND ui_archive_volumes_tests -silent)
    set_tests_properties(ui_archive_volumes PROPERTIES TIMEOUT 60 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/ui-archive-volumes-settings.ini")
    set_tests_properties(ui_update_modes PROPERTIES TIMEOUT 30 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/ui-update-modes-settings.ini")
    target_link_libraries(ui_navigation_tests PRIVATE Qt6::Widgets Qt6::Test)
    target_link_libraries(ui_archive_states_tests PRIVATE Qt6::Widgets Qt6::Test)
    qt_add_executable(settings_tests tests/settings/settings_tests.cpp)
    target_link_libraries(settings_tests PRIVATE sevenzip_window)
    target_include_directories(settings_tests PRIVATE src)
    target_link_libraries(settings_tests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME settings COMMAND settings_tests -silent)
    set_tests_properties(settings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/settings-tests.ini")
    qt_add_executable(properties_tests tests/filemanager/properties_tests.cpp)
    target_link_libraries(properties_tests PRIVATE sevenzip_window)
    target_include_directories(properties_tests PRIVATE src)
    target_link_libraries(properties_tests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME properties COMMAND properties_tests -silent)
    qt_add_executable(properties_window_tests tests/filemanager/properties_window_tests.cpp)
    target_include_directories(properties_window_tests PRIVATE src tests)
    target_link_libraries(properties_window_tests PRIVATE sevenzip_window sevenzip_layout_inspection Qt6::Test)
    add_test(NAME properties_window COMMAND properties_window_tests -silent)
    set_tests_properties(properties_window PROPERTIES TIMEOUT 45 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/properties-window-settings.ini")
    qt_add_executable(checksum_tests tests/filemanager/checksum_tests.cpp)
    target_link_libraries(checksum_tests PRIVATE sevenzip_window)
    target_include_directories(checksum_tests PRIVATE src)
    target_link_libraries(checksum_tests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME checksum COMMAND checksum_tests -silent)
    set_tests_properties(checksum PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/checksum-tests.ini")
    set_tests_properties(properties PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/properties-tests.ini")
    add_test(NAME ui_state COMMAND ui_state_tests -silent)
    add_test(NAME ui_operations COMMAND ui_operations_tests -silent)
    set_tests_properties(ui_operations PROPERTIES TIMEOUT 120)
    add_test(NAME ui_navigation COMMAND ui_navigation_tests -silent)
    add_test(NAME ui_archive_states COMMAND ui_archive_states_tests -silent)
    foreach(suite ui_operations ui_navigation ui_archive_states)
        set_tests_properties(${suite} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/${suite}-settings.ini")
    endforeach()
    set_property(TEST ui_operations APPEND PROPERTY ENVIRONMENT "SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    if(APPLE)
        add_test(NAME backend_overwrite_metadata_native
            COMMAND ui_operations_tests backendOverwriteDisplaysNativeMetadata -silent)
        set_tests_properties(backend_overwrite_metadata_native PROPERTIES TIMEOUT 30
            RESOURCE_LOCK sevenzip_native_ui ENVIRONMENT
            "QT_QPA_PLATFORM=cocoa;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/backend-overwrite-metadata.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports-native-overwrite")
    endif()
    set_property(TEST ui_navigation APPEND PROPERTY ENVIRONMENT "SEVENZIP_SCREENSHOT_DIR=${CMAKE_BINARY_DIR}/ui-screenshots")
    set_tests_properties(ui_state PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SCREENSHOT_DIR=${CMAKE_BINARY_DIR}/ui-screenshots;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/ui-state-settings.ini"
    )
    add_test(NAME ui_layout COMMAND ui_layout_tests -silent)
    set_tests_properties(ui_layout PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SCREENSHOT_DIR=${CMAKE_BINARY_DIR}/ui-screenshots;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/ui-layout-settings.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports"
    )
    add_dependencies(archive_backend_tests sevenzip_linux_frames_runtime)
    add_test(NAME archive_backend COMMAND archive_backend_tests)
    qt_add_executable(extract_dialog_tests tests/archive/extract_dialog_tests.cpp)
    target_link_libraries(extract_dialog_tests PRIVATE sevenzip_layout_inspection sevenzip_location)
    target_include_directories(extract_dialog_tests PRIVATE src)
    target_link_libraries(extract_dialog_tests PRIVATE Qt6::Widgets Qt6::Test sevenzip_panel)
    add_test(NAME extract_dialog COMMAND extract_dialog_tests -silent)
    set_tests_properties(extract_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    qt_add_executable(split_destination_tests tests/fileops/split_destination_tests.cpp)
    target_link_libraries(split_destination_tests PRIVATE sevenzip_layout_inspection sevenzip_location)
    target_include_directories(split_destination_tests PRIVATE src)
    target_link_libraries(split_destination_tests PRIVATE Qt6::Widgets Qt6::Test sevenzip_panel)
    add_test(NAME split_destination COMMAND split_destination_tests -silent)
    set_tests_properties(split_destination PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    qt_add_executable(extraction_options_tests tests/archive/extraction_options_tests.cpp)
    target_link_libraries(extraction_options_tests PRIVATE sevenzip_location)
    target_include_directories(extraction_options_tests PRIVATE src tests)
    target_link_libraries(extraction_options_tests PRIVATE Qt6::Test)
    add_test(NAME extraction_options COMMAND extraction_options_tests -silent)
    qt_add_executable(compression_tests tests/archive/compression_tests.cpp)
    target_link_libraries(compression_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_include_directories(compression_tests PRIVATE src)
    target_link_libraries(compression_tests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME compression COMMAND compression_tests -silent)
    target_link_libraries(exfat_backend_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(xfs_backend_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(squashfs_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(ntfs_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(ntfs_security_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(hfs_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(ext_xattr_backend_tests PRIVATE sevenzip_compression_ui)
    target_link_libraries(ext_fscrypt_backend_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(iso_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(udf_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(gpt_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(cpio_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(gzip_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(lzma_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(bzip2_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(xz_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(zstd_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(base64_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(elf_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(pe_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(uefi_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(ihex_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(tar_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(linux_frames_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(apm_stream_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(fat_module_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(erofs_module_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(brotli_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(lz4_frame_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    target_link_libraries(lzip_tests PRIVATE sevenzip_compression_ui sevenzip_location)
    qt_add_executable(compression_error_policy_tests tests/archive/compression_error_policy_tests.cpp)
    target_link_libraries(compression_error_policy_tests PRIVATE sevenzip_compression_ui sevenzip_layout_inspection sevenzip_location)
    target_include_directories(compression_error_policy_tests PRIVATE src tests)
    target_link_libraries(compression_error_policy_tests PRIVATE sevenzip_panel Qt6::Test)
    if(SEVENZIP_CODEC_MODULE)
        target_compile_definitions(compression_error_policy_tests PRIVATE
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\"")
    endif()
    add_test(NAME compression_error_policy COMMAND compression_error_policy_tests -silent)
    set_tests_properties(compression_error_policy PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(dialog_path_rules_tests tests/ui/dialog_path_rules_tests.cpp)
    target_link_libraries(dialog_path_rules_tests PRIVATE sevenzip_compression_ui sevenzip_layout_inspection sevenzip_location)
    target_include_directories(dialog_path_rules_tests PRIVATE src tests)
    target_link_libraries(dialog_path_rules_tests PRIVATE sevenzip_panel Qt6::Test)
    if(SEVENZIP_CODEC_MODULE)
        target_compile_definitions(dialog_path_rules_tests PRIVATE
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\"")
    endif()
    add_test(NAME dialog_path_rules COMMAND dialog_path_rules_tests -silent)
    set_tests_properties(dialog_path_rules PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(compression_advanced_tests tests/ui/compression_advanced_tests.cpp)
    target_link_libraries(compression_advanced_tests PRIVATE sevenzip_compression_ui sevenzip_layout_inspection sevenzip_location)
    target_include_directories(compression_advanced_tests PRIVATE src tests)
    target_link_libraries(compression_advanced_tests PRIVATE Qt6::Widgets Qt6::Test sevenzip_panel)
    target_link_libraries(compression_advanced_tests PRIVATE sevenzip_widget_dialog_fixtures)
    add_test(NAME compression_advanced COMMAND compression_advanced_tests -silent)
    set_tests_properties(compression_advanced PROPERTIES TIMEOUT 60 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
qt_add_executable(about_dialog_tests tests/ui/about_dialog_tests.cpp)
target_compile_definitions(about_dialog_tests PRIVATE SEVENZIP_DESKTOP_VERSION="${PROJECT_VERSION}")
target_link_libraries(about_dialog_tests PRIVATE sevenzip_panel sevenzip_layout_inspection Qt6::Test)
target_include_directories(about_dialog_tests PRIVATE tests)
add_test(NAME about_dialog COMMAND about_dialog_tests -silent)
set_tests_properties(about_dialog PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
