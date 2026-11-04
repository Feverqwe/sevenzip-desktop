    qt_add_executable(native_toolbar_state_tests tests/platform/native_toolbar_state_tests.cpp)
    target_link_libraries(native_toolbar_state_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME native_toolbar_state COMMAND native_toolbar_state_tests -silent)
    set_tests_properties(native_toolbar_state PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(window_info_tests tests/ui/window_info_tests.cpp)
    qt_add_executable(file_menu_policy_tests tests/ui/file_menu_policy_tests.cpp)
    target_include_directories(file_menu_policy_tests PRIVATE src)
    target_link_libraries(file_menu_policy_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME file_menu_policy COMMAND file_menu_policy_tests -silent)
    qt_add_executable(native_command_dispatch_tests tests/platform/native_command_dispatch_tests.cpp)
    target_include_directories(native_command_dispatch_tests PRIVATE src)
    target_link_libraries(native_command_dispatch_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME native_command_dispatch COMMAND native_command_dispatch_tests -silent)
    qt_add_executable(native_open_commands_tests tests/filemanager/native_open_commands_tests.cpp)
    target_link_libraries(native_open_commands_tests PRIVATE sevenzip_location Qt6::Test)
    add_test(NAME native_open_commands COMMAND native_open_commands_tests -silent)
    set_tests_properties(native_open_commands PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
    target_include_directories(window_info_tests PRIVATE src)
    target_link_libraries(window_info_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME window_info COMMAND window_info_tests -silent)
    target_include_directories(app_lifecycle_tests PRIVATE src)
    target_link_libraries(app_lifecycle_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME app_lifecycle COMMAND app_lifecycle_tests -silent)
    set_tests_properties(app_lifecycle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    target_include_directories(binding_lifecycle_tests PRIVATE src tests)
    target_link_libraries(binding_lifecycle_tests PRIVATE sevenzip_panel sevenzip_filesystem Qt6::Test)
    add_test(NAME binding_lifecycle COMMAND binding_lifecycle_tests -silent)
    set_tests_properties(binding_lifecycle PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/binding-lifecycle-settings.ini")
    qt_add_executable(filesystem_selection_runtime_tests tests/filemanager/filesystem_selection_runtime_tests.cpp)
    target_link_libraries(filesystem_selection_runtime_tests PRIVATE sevenzip_window)
    target_include_directories(filesystem_selection_runtime_tests PRIVATE src tests)
    target_link_libraries(filesystem_selection_runtime_tests PRIVATE sevenzip_panel sevenzip_filesystem Qt6::Test)
    add_test(NAME filesystem_selection_runtime COMMAND filesystem_selection_runtime_tests -silent)
    set_tests_properties(filesystem_selection_runtime PROPERTIES TIMEOUT 45 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/filesystem-selection-runtime-settings.ini")
    target_link_libraries(property_formatting_tests PRIVATE sevenzip_filesystem Qt6::Test)
    add_test(NAME property_formatting COMMAND property_formatting_tests -silent)

    qt_add_executable(property_name_tests tests/archive/property_name_tests.cpp)
    target_link_libraries(property_name_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME property_name COMMAND property_name_tests -silent)

    qt_add_executable(archive_error_properties_tests tests/archive/archive_error_properties_tests.cpp)
    target_link_libraries(archive_error_properties_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME archive_error_properties COMMAND archive_error_properties_tests -silent)

    qt_add_executable(panel_property_tests tests/filemanager/panel_property_tests.cpp
        tests/archive/support/schema_folder.cpp)
    target_link_libraries(panel_property_tests PRIVATE sevenzip_layout_inspection sevenzip_panel_load_fixture)
    target_include_directories(panel_property_tests PRIVATE tests)
    target_link_libraries(panel_property_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME panel_property COMMAND panel_property_tests -silent)
    set_tests_properties(panel_property PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")

    qt_add_executable(archive_property_catalog_tests tests/archive/archive_property_catalog_tests.cpp)
    target_link_libraries(archive_property_catalog_tests PRIVATE sevenzip_location Qt6::Test)
    add_test(NAME archive_property_catalog COMMAND archive_property_catalog_tests -silent)
    add_test(NAME resource_strings_generator COMMAND "${CMAKE_COMMAND}"
        "-DPROJECT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}" "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/resource-strings-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/rc_strings_tests.cmake")
    qt_add_executable(volume_workflow_tests tests/fileops/volume_workflow_tests.cpp)
    target_link_libraries(volume_workflow_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(volume_workflow_tests PRIVATE sevenzip_window)
    target_include_directories(volume_workflow_tests PRIVATE src tests)
    target_link_libraries(volume_workflow_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME volume_workflow COMMAND volume_workflow_tests -silent)
    set_tests_properties(volume_workflow PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/volume-workflow-settings.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(panel_sort_tests tests/filemanager/panel_sort_tests.cpp)
    target_link_libraries(panel_sort_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(panel_sort_tests PRIVATE sevenzip_window)
    target_include_directories(panel_sort_tests PRIVATE src)
    target_link_libraries(panel_sort_tests PRIVATE Qt6::Widgets Qt6::Test sevenzip_common)
    add_test(NAME panel_sort COMMAND panel_sort_tests -silent)
    qt_add_executable(panel_columns_tests tests/filemanager/panel_columns_tests.cpp)
    target_link_libraries(panel_columns_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(panel_columns_tests PRIVATE sevenzip_window)
    target_include_directories(panel_columns_tests PRIVATE src tests)
    target_link_libraries(panel_columns_tests PRIVATE sevenzip_location sevenzip_folder_loader Qt6::Test)

    add_test(NAME panel_columns COMMAND panel_columns_tests -silent)
    qt_add_executable(panel_column_schema_tests tests/filemanager/panel_column_schema_tests.cpp tests/archive/support/schema_folder.cpp)
    target_link_libraries(panel_column_schema_tests PRIVATE sevenzip_layout_inspection sevenzip_panel_load_fixture)
    target_link_libraries(panel_column_schema_tests PRIVATE sevenzip_window)
    target_include_directories(panel_column_schema_tests PRIVATE src tests)
    target_link_libraries(panel_column_schema_tests PRIVATE sevenzip_location sevenzip_folder_loader Qt6::Test)

    add_test(NAME panel_column_schema COMMAND panel_column_schema_tests -silent)
    set_tests_properties(panel_column_schema PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/panel-column-schema.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(panel_column_changes_tests tests/filemanager/panel_column_changes_tests.cpp tests/archive/support/schema_folder.cpp)
    target_link_libraries(panel_column_changes_tests PRIVATE sevenzip_layout_inspection sevenzip_panel_load_fixture)
    target_link_libraries(panel_column_changes_tests PRIVATE sevenzip_window)
    target_include_directories(panel_column_changes_tests PRIVATE src tests)
    target_link_libraries(panel_column_changes_tests PRIVATE sevenzip_location sevenzip_folder_loader Qt6::Test)

    add_test(NAME panel_column_changes COMMAND panel_column_changes_tests -silent)
    set_tests_properties(panel_column_changes PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/panel-column-changes.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(native_column_workflow_tests
        tests/filemanager/native_column_workflow_tests.cpp
        tests/archive/support/schema_folder.cpp)
    target_link_libraries(native_column_workflow_tests PRIVATE sevenzip_layout_inspection sevenzip_panel_load_fixture)
    target_link_libraries(native_column_workflow_tests PRIVATE sevenzip_window)
    target_include_directories(native_column_workflow_tests PRIVATE src tests)
    target_link_libraries(native_column_workflow_tests PRIVATE sevenzip_location sevenzip_folder_loader Qt6::Test)

    if(SEVENZIP_CODEC_MODULE)
        target_compile_definitions(native_column_workflow_tests PRIVATE "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\"")
    endif()
    add_test(NAME native_column_workflow COMMAND native_column_workflow_tests -silent)
    set_tests_properties(native_column_workflow PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native-column-workflow.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(archive_reload_command_tests tests/filemanager/archive_reload_command_tests.cpp)
    target_link_libraries(archive_reload_command_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(archive_reload_command_tests PRIVATE sevenzip_window)
    target_include_directories(archive_reload_command_tests PRIVATE src tests)
    target_link_libraries(archive_reload_command_tests PRIVATE sevenzip_location sevenzip_folder_loader Qt6::Test)

    if(SEVENZIP_CODEC_MODULE)
        target_compile_definitions(archive_reload_command_tests PRIVATE "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\"")
    endif()
    add_test(NAME archive_reload_command COMMAND archive_reload_command_tests -silent)
    set_tests_properties(archive_reload_command PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/archive-reload-command.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    add_test(NAME archive_reload_command_quick COMMAND archive_reload_command_tests -silent
        refreshLoadsBoundProvider:zip-0-0-0-0-0
        refreshLoadsBoundProvider:zip-1-1-1-1-1
        refreshLoadsBoundProvider:7z-0-1-1-0-1
        refreshLoadsBoundProvider:7z-1-0-0-1-0)
    set_tests_properties(archive_reload_command_quick PROPERTIES TIMEOUT 30 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/archive-reload-command-quick.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(native_renderer_failure_tests
        tests/filemanager/native_renderer_failure_tests.cpp
        tests/platform/support/native_property_failure_runtime.cpp)
    target_link_libraries(native_renderer_failure_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(native_renderer_failure_tests PRIVATE sevenzip_window)
    qt_add_executable(native_parent_publication_tests
        tests/filemanager/native_parent_publication_tests.cpp
        tests/platform/support/native_property_failure_runtime.cpp)
    target_link_libraries(native_parent_publication_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(native_parent_publication_tests PRIVATE sevenzip_window)
    target_include_directories(native_parent_publication_tests PRIVATE src tests)
    target_link_libraries(native_parent_publication_tests PRIVATE sevenzip_location sevenzip_folder_loader Qt6::Test)

    target_compile_definitions(native_parent_publication_tests PRIVATE
        "SEVENZIP_EXTERNAL_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_EXTERNAL_FORMAT_FIXTURE=\"$<TARGET_FILE:external_format_fixture>\"")
    add_dependencies(native_parent_publication_tests external_format_fixture)
    add_test(NAME native_parent_publication COMMAND native_parent_publication_tests -silent)
    set_tests_properties(native_parent_publication PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native-parent.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(native_drop_publication_tests
        tests/filemanager/native_drop_publication_tests.cpp
        tests/platform/support/native_property_failure_runtime.cpp)
    target_link_libraries(native_drop_publication_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(native_drop_publication_tests PRIVATE sevenzip_window)
    target_include_directories(native_drop_publication_tests PRIVATE src tests)
    target_link_libraries(native_drop_publication_tests PRIVATE sevenzip_location sevenzip_folder_loader Qt6::Test)

    target_compile_definitions(native_drop_publication_tests PRIVATE
        "SEVENZIP_EXTERNAL_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_EXTERNAL_FORMAT_FIXTURE=\"$<TARGET_FILE:external_format_fixture>\"")
    add_dependencies(native_drop_publication_tests external_format_fixture)
    add_test(NAME native_drop_publication COMMAND native_drop_publication_tests -silent)
    set_tests_properties(native_drop_publication PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native-drop.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(native_rename_publication_tests
        tests/filemanager/native_rename_publication_tests.cpp
        tests/platform/support/native_property_failure_runtime.cpp
        tests/platform/support/external_editor_fixture.cpp)
    target_link_libraries(native_rename_publication_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(native_rename_publication_tests PRIVATE sevenzip_window)
    target_include_directories(native_rename_publication_tests PRIVATE src tests)
    target_link_libraries(native_rename_publication_tests PRIVATE sevenzip_location sevenzip_folder_loader Qt6::Test)

    target_compile_definitions(native_rename_publication_tests PRIVATE
        "SEVENZIP_EXTERNAL_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_EXTERNAL_FORMAT_FIXTURE=\"$<TARGET_FILE:external_format_fixture>\"")
    add_dependencies(native_rename_publication_tests external_format_fixture)
    foreach(rename_rows 2 301)
        add_test(NAME native_rename_${rename_rows} COMMAND native_rename_publication_tests -silent)
        set_tests_properties(native_rename_${rename_rows} PROPERTIES TIMEOUT 360 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_RENAME_ROWS=${rename_rows};SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native-rename-${rename_rows}.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endforeach()
    qt_add_executable(native_external_publication_tests
        tests/filemanager/native_external_publication_tests.cpp
        tests/platform/support/native_property_failure_runtime.cpp
        tests/platform/support/external_editor_fixture.cpp)
    target_link_libraries(native_external_publication_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(native_external_publication_tests PRIVATE sevenzip_window)
    target_include_directories(native_external_publication_tests PRIVATE src tests)
    target_link_libraries(native_external_publication_tests PRIVATE sevenzip_location sevenzip_folder_loader Qt6::Test)

    target_compile_definitions(native_external_publication_tests PRIVATE
        "SEVENZIP_EXTERNAL_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_EXTERNAL_FORMAT_FIXTURE=\"$<TARGET_FILE:external_format_fixture>\"")
    add_dependencies(native_external_publication_tests external_format_fixture)
    foreach(external_rows 2 301)
        add_test(NAME native_external_${external_rows} COMMAND native_external_publication_tests -silent)
        set_tests_properties(native_external_${external_rows} PROPERTIES TIMEOUT 240 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_EXTERNAL_ROWS=${external_rows};SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native-external-${external_rows}.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endforeach()
    add_test(NAME native_external_update COMMAND native_external_publication_tests -silent
        externalUpdatePublishesAfterNativeWriter:2-0-0-0-0-0-0
        externalUpdatePublishesAfterNativeWriter:2-80004005-0-1-1-1-1
        externalUpdatePublishesAfterNativeWriter:2-80004004-0-0-0-0-0
        externalUpdatePublishesAfterNativeWriter:2-0-80004005-1-0-0-0
        externalUpdatePublishesAfterNativeWriter:301-0-0-0-1-0-1)
    set_tests_properties(native_external_update PROPERTIES TIMEOUT 90 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_EXTERNAL_ROWS=0;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native-external-update.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(native_nested_publication_tests
        tests/filemanager/native_nested_publication_tests.cpp
        tests/platform/support/native_property_failure_runtime.cpp)
    target_link_libraries(native_nested_publication_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(native_nested_publication_tests PRIVATE sevenzip_window)
    target_include_directories(native_nested_publication_tests PRIVATE src tests)
    target_link_libraries(native_nested_publication_tests PRIVATE sevenzip_location sevenzip_folder_loader Qt6::Test)
    target_link_libraries(native_nested_publication_tests PRIVATE sevenzip_widget_dialog_fixtures)
    target_compile_definitions(native_nested_publication_tests PRIVATE
        "SEVENZIP_EXTERNAL_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_EXTERNAL_FORMAT_FIXTURE=\"$<TARGET_FILE:external_format_fixture>\"")
    add_dependencies(native_nested_publication_tests external_format_fixture)
    foreach(nested_route stream memory disk)
        add_test(NAME native_nested_${nested_route} COMMAND native_nested_publication_tests -silent)
        set_tests_properties(native_nested_${nested_route} PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_NESTED_ROUTE=${nested_route};SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native-nested-${nested_route}.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
        add_test(NAME native_nested_${nested_route}_quick COMMAND native_nested_publication_tests -silent
            nativeStreamMapsFolderIndicesAndRetainsItsOwner
            nativeOpeningHonorsConfirmedCancel
            nestedPublicationRestoresSourceGuards:${nested_route}-2-0-0-0-0-0
            nestedPublicationRestoresSourceGuards:${nested_route}-301-1-1-1-1-1
            nestedPublicationRestoresSourceGuards:${nested_route}-2-80004005-1-0-1-0
            nestedPublicationRestoresSourceGuards:${nested_route}-301-80004004-0-1-0-1)
        set_tests_properties(native_nested_${nested_route}_quick PROPERTIES TIMEOUT 45 ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;SEVENZIP_NESTED_ROUTE=${nested_route};SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native-nested-${nested_route}-quick.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endforeach()
    target_include_directories(native_renderer_failure_tests PRIVATE src tests)
    target_link_libraries(native_renderer_failure_tests PRIVATE sevenzip_location sevenzip_folder_loader Qt6::Test)

    target_compile_definitions(native_renderer_failure_tests PRIVATE
        "SEVENZIP_EXTERNAL_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_EXTERNAL_FORMAT_FIXTURE=\"$<TARGET_FILE:external_format_fixture>\"")
    add_dependencies(native_renderer_failure_tests external_format_fixture)
    add_test(NAME native_renderer_failure COMMAND native_renderer_failure_tests -silent)
    set_tests_properties(native_renderer_failure PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native-renderer-failure.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
