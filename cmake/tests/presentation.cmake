foreach(target IN ITEMS archive_proxy_tests archive_backend_tests ui_layout_tests ui_state_tests ui_operations_tests ui_copy_same_tests ui_copy_routing_tests ui_update_modes_tests ui_archive_volumes_tests external_editors_ui_tests verctrl_tests nested_edit_chain_tests archive_external_edit_tests archive_external_security_tests ui_navigation_tests ui_archive_states_tests flat_view_tests settings_tests properties_tests checksum_tests extraction_options_tests compression_tests archive_item_open_tests nested_archive_tests volume_operations_tests)
    if(TARGET ${target})
        target_link_libraries(${target} PRIVATE sevenzip_common)
    endif()
endforeach()

# Reuse the complete MainWindow fixture component, including its narrow adapters.
if(BUILD_TESTING)
    qt_add_executable(folder_size_view_tests tests/filemanager/folder_size_view_tests.cpp)
    target_link_libraries(folder_size_view_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(folder_size_view_tests PRIVATE sevenzip_window)
    target_include_directories(folder_size_view_tests PRIVATE src tests)
    target_link_libraries(folder_size_view_tests PRIVATE Qt6::Test sevenzip_location sevenzip_folder_loader)
    add_test(NAME folder_size_view COMMAND folder_size_view_tests -silent)
    set_tests_properties(folder_size_view PROPERTIES TIMEOUT 90 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/folder_size_view.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(drag_drop_ui_tests tests/filemanager/drag_drop_ui_tests.cpp)
    target_link_libraries(drag_drop_ui_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(drag_drop_ui_tests PRIVATE sevenzip_window)
    target_include_directories(drag_drop_ui_tests PRIVATE src tests)
    target_link_libraries(drag_drop_ui_tests PRIVATE Qt6::Test sevenzip_location sevenzip_folder_loader)
    add_test(NAME drag_drop_ui COMMAND drag_drop_ui_tests -silent)
    set_tests_properties(drag_drop_ui PROPERTIES TIMEOUT 60 RESOURCE_LOCK sevenzip_ui_artifacts ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/drag_drop_ui.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(drag_drop_add_ui_tests tests/filemanager/drag_drop_add_ui_tests.cpp)
    target_link_libraries(drag_drop_add_ui_tests PRIVATE sevenzip_window)
    target_include_directories(drag_drop_add_ui_tests PRIVATE src tests)
    target_link_libraries(drag_drop_add_ui_tests PRIVATE Qt6::Test sevenzip_location sevenzip_folder_loader)
    target_compile_definitions(drag_drop_add_ui_tests PRIVATE
        "SEVENZIP_OPERATION_HOST=\"$<TARGET_FILE:operation_host>\"")
    add_dependencies(drag_drop_add_ui_tests operation_host)
    add_test(NAME drag_drop_add_ui COMMAND drag_drop_add_ui_tests
        externalArchiveDrop externalFilesystemDropCreatesArchiveWithNativeName
        launchFailureReleasesTemporarySource -silent)
    add_test(NAME drag_drop_add_independent COMMAND drag_drop_add_ui_tests independentCompletion -silent)
    add_test(NAME drag_drop_add_temporary COMMAND drag_drop_add_ui_tests temporarySourceLifetime -silent)
    set_tests_properties(drag_drop_add_ui drag_drop_add_independent drag_drop_add_temporary PROPERTIES
        TIMEOUT 60 RESOURCE_LOCK sevenzip_native_ui ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/drag_drop_add_ui.ini")
    if(SEVENZIP_CODEC_MODULE)
        qt_add_executable(dropped_path_choice_tests tests/filemanager/dropped_path_choice_tests.cpp)
        target_link_libraries(dropped_path_choice_tests PRIVATE sevenzip_window sevenzip_layout_inspection
            Qt6::Test sevenzip_location sevenzip_folder_loader)
        target_include_directories(dropped_path_choice_tests PRIVATE src tests)
        target_compile_definitions(dropped_path_choice_tests PRIVATE
            "SEVENZIP_OPERATION_HOST=\"$<TARGET_FILE:operation_host>\""
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\"")
        add_dependencies(dropped_path_choice_tests operation_host)
        add_test(NAME dropped_path_choice COMMAND dropped_path_choice_tests -silent)
        set_tests_properties(dropped_path_choice PROPERTIES TIMEOUT 60 RESOURCE_LOCK sevenzip_native_ui
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/dropped_path_choice.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endif()
endif()

# Window runtime uses the authoritative source-owned archive location factory.
if(TARGET flat_view_tests)
    set_property(TEST flat_view APPEND PROPERTY ENVIRONMENT
        "SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
endif()
if(TARGET ui_navigation_tests)
    target_link_libraries(ui_navigation_tests PRIVATE sevenzip_layout_inspection)
    set_property(TEST ui_navigation APPEND PROPERTY ENVIRONMENT
        "SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
endif()

if(BUILD_TESTING)
    qt_add_executable(source_properties_tests tests/filemanager/source_properties_tests.cpp)
    target_link_libraries(source_properties_tests PRIVATE sevenzip_layout_inspection sevenzip_location)
    target_include_directories(source_properties_tests PRIVATE src tests)
    target_link_libraries(source_properties_tests PRIVATE Qt6::Test)
    add_test(NAME source_properties COMMAND source_properties_tests -silent)
    set_tests_properties(source_properties PROPERTIES TIMEOUT 90
        RESOURCE_LOCK sevenzip_ui_artifacts ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/source_properties.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(agent_property_projection_tests tests/ui/agent_property_projection_tests.cpp)
    target_link_libraries(agent_property_projection_tests PRIVATE sevenzip_location)
    target_include_directories(agent_property_projection_tests PRIVATE src tests)
    target_link_libraries(agent_property_projection_tests PRIVATE Qt6::Test)
    add_test(NAME agent_property_projection COMMAND agent_property_projection_tests -silent)
    set_tests_properties(agent_property_projection PROPERTIES TIMEOUT 60 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/agent-property-projection.ini")
endif()

if(BUILD_TESTING)
    qt_add_executable(language_tests tests/localization/language_tests.cpp)
    target_link_libraries(language_tests PRIVATE sevenzip_layout_inspection)
    qt_add_executable(native_language_host_tests tests/platform/native_language_host_tests.cpp)
    target_include_directories(native_language_host_tests PRIVATE src)
    target_link_libraries(native_language_host_tests PRIVATE sevenzip_location Qt6::Test)
    add_test(NAME native_language_host COMMAND native_language_host_tests -silent)
    qt_add_executable(language_source_update_tests tests/localization/language_source_update_tests.cpp)
    target_link_libraries(language_source_update_tests PRIVATE sevenzip_location)
    target_include_directories(language_source_update_tests PRIVATE src)
    target_link_libraries(language_source_update_tests PRIVATE Qt6::Test)
    add_test(NAME language_source_update COMMAND language_source_update_tests -silent)
    set_tests_properties(language_source_update PROPERTIES TIMEOUT 60)
    target_include_directories(language_tests PRIVATE src ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP/7zip/UI/Common tests)
    target_link_libraries(language_tests PRIVATE sevenzip_location Qt6::Test)
    add_test(NAME language COMMAND language_tests -silent)
    set_tests_properties(language PROPERTIES TIMEOUT 60 RESOURCE_LOCK sevenzip_ui_artifacts
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(menu_page_tests tests/ui/menu_page_tests.cpp)
    target_link_libraries(menu_page_tests PRIVATE sevenzip_layout_inspection)
    qt_add_executable(shell_context_tests tests/filemanager/shell_context_tests.cpp)
    target_include_directories(shell_context_tests PRIVATE src)
    target_link_libraries(shell_context_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME shell_context COMMAND shell_context_tests -silent)
    if(WIN32)
        qt_add_executable(native_system_menu_tests tests/filemanager/native_system_menu_tests.cpp)
        qt_add_executable(native_menu_integration_tests tests/platform/native_menu_integration_tests.cpp)
        target_link_libraries(native_menu_integration_tests PRIVATE sevenzip_location)
        target_include_directories(native_menu_integration_tests PRIVATE src)
        target_link_libraries(native_menu_integration_tests PRIVATE Qt6::Test Qt6::Test)
        add_dependencies(native_menu_integration_tests sevenzip_shell)
        string(TOUPPER "$ENV{COMPUTERNAME}" sevenzip_test_computer)
        string(TOUPPER "$ENV{SEVENZIP_WINDOWS_RUNNER_HOST}" sevenzip_test_host)
        if(sevenzip_test_host AND sevenzip_test_computer STREQUAL sevenzip_test_host)
            add_test(NAME native_menu_integration COMMAND native_menu_integration_tests -silent)
            set_tests_properties(native_menu_integration PROPERTIES TIMEOUT 60
                RESOURCE_LOCK sevenzip_shell_registration ENVIRONMENT "QT_QPA_PLATFORM=windows")
        endif()
        qt_add_executable(native_menu_presentation_tests
            tests/platform/native_menu_presentation_tests.cpp
            tests/support/native_menu_provider.cpp
            tests/support/native_menu_provider.h)
        target_link_libraries(native_menu_presentation_tests PRIVATE sevenzip_window)
        target_include_directories(native_menu_presentation_tests PRIVATE src)
        target_link_libraries(native_menu_presentation_tests PRIVATE sevenzip_panel Qt6::Test)
        add_test(NAME native_menu_presentation COMMAND native_menu_presentation_tests -silent)
        file(GLOB sevenzip_native_reference_manifest CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/build/windows-reference/reference.json")
        if(sevenzip_native_reference_manifest)
            qt_add_executable(native_rendered_system_menu_tests tests/filemanager/native_rendered_system_menu_tests.cpp)
            target_link_libraries(native_rendered_system_menu_tests PRIVATE sevenzip_location sevenzip_window)
            target_include_directories(native_rendered_system_menu_tests PRIVATE src)
            target_link_libraries(native_rendered_system_menu_tests PRIVATE Qt6::Test Qt6::Test advapi32)
            target_compile_definitions(native_rendered_system_menu_tests PRIVATE
                "SEVENZIP_NATIVE_REFERENCE_DIR=\"${CMAKE_SOURCE_DIR}/build/windows-reference\""
                "SEVENZIP_UPSTREAM_REVISION=\"${sfx_upstream_revision}\"")
            add_test(NAME native_rendered_system_menu COMMAND native_rendered_system_menu_tests -silent)
            set_tests_properties(native_rendered_system_menu PROPERTIES TIMEOUT 60 ENVIRONMENT
                "QT_QPA_PLATFORM=windows;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
        endif()
        qt_add_executable(native_shell_data_tests tests/platform/native_shell_data_tests.cpp)
        target_sources(native_shell_data_tests PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/native_context_menu.rc")
        target_include_directories(native_shell_data_tests PRIVATE src)
        target_link_libraries(native_shell_data_tests PRIVATE sevenzip_panel Qt6::Test)
        add_test(NAME native_shell_data COMMAND native_shell_data_tests -silent)
        qt_add_executable(native_archive_command_line_tests tests/archive/native_archive_command_line_tests.cpp)
        target_include_directories(native_archive_command_line_tests PRIVATE src
            ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP/7zip/UI/Common ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP/Common)
        target_link_libraries(native_archive_command_line_tests PRIVATE sevenzip_panel Qt6::Test)
        add_test(NAME native_archive_command_line COMMAND native_archive_command_line_tests -silent)
        target_compile_definitions(native_archive_command_line_tests PRIVATE
            "NATIVE_GUI_EXECUTABLE=\"$<TARGET_FILE_DIR:sevenzip_desktop>/7zG.exe\"")
        add_dependencies(native_archive_command_line_tests sevenzip_desktop)
        qt_add_executable(native_gui_workflow_tests tests/platform/native_gui_workflow_tests.cpp)
        target_include_directories(native_gui_workflow_tests PRIVATE src tests)
        target_link_libraries(native_gui_workflow_tests PRIVATE sevenzip_panel Qt6::Test)
        target_compile_definitions(native_gui_workflow_tests PRIVATE
            "NATIVE_GUI_EXECUTABLE=\"$<TARGET_FILE_DIR:sevenzip_desktop>/7zG.exe\"")
        add_dependencies(native_gui_workflow_tests sevenzip_desktop)
        add_test(NAME native_gui_workflow COMMAND native_gui_workflow_tests -silent)
        set_tests_properties(native_gui_workflow PROPERTIES TIMEOUT 180 ENVIRONMENT
            "SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native_gui_workflow.ini"
            RESOURCE_LOCK sevenzip_native_ui)
        qt_add_executable(native_shell_com_tests tests/platform/native_shell_com_tests.cpp)
        add_executable(native_shell_host tests/support/native_shell_host.cpp)
        set_target_properties(native_shell_host PROPERTIES AUTOMOC OFF AUTOUIC OFF)
        target_link_libraries(native_shell_host PRIVATE ole32 shell32 uuid)
        target_include_directories(native_shell_com_tests PRIVATE src)
        target_link_libraries(native_shell_com_tests PRIVATE sevenzip_panel Qt6::Test)
        target_compile_definitions(native_shell_com_tests PRIVATE
            "NATIVE_SHELL_LIBRARY=\"$<TARGET_FILE:sevenzip_shell>\""
            "NATIVE_SHELL_HOST=\"$<TARGET_FILE:native_shell_host>\"")
        add_dependencies(native_shell_com_tests sevenzip_shell native_shell_host)
        add_test(NAME native_shell_com COMMAND native_shell_com_tests -silent)
        qt_add_executable(native_shell_launch_tests tests/platform/native_shell_launch_tests.cpp)
        add_executable(native_shell_launch_host tests/support/native_shell_launch_host.cpp)
        set_target_properties(native_shell_launch_host PROPERTIES AUTOMOC OFF AUTOUIC OFF)
        target_link_libraries(native_shell_launch_host PRIVATE ole32 shell32 uuid advapi32)
        target_include_directories(native_shell_launch_tests PRIVATE tests)
        target_link_libraries(native_shell_launch_tests PRIVATE Qt6::Test advapi32 ole32 oleaut32)
        target_compile_definitions(native_shell_launch_tests PRIVATE
            "NATIVE_SHELL_LIBRARY=\"$<TARGET_FILE:sevenzip_shell>\""
            "NATIVE_SHELL_LAUNCH_HOST=\"$<TARGET_FILE:native_shell_launch_host>\"")
        add_dependencies(native_shell_launch_tests sevenzip_shell native_shell_launch_host sevenzip_desktop)
        add_test(NAME native_shell_launch COMMAND native_shell_launch_tests -silent)
        set_tests_properties(native_shell_launch PROPERTIES TIMEOUT 120 ENVIRONMENT
            "SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native_shell_launch.ini")
        set_tests_properties(native_shell_com PROPERTIES ENVIRONMENT
            "SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native_shell_com.ini")
        target_include_directories(native_system_menu_tests PRIVATE src)
        target_link_libraries(native_system_menu_tests PRIVATE sevenzip_panel Qt6::Test)
        add_test(NAME native_system_menu COMMAND native_system_menu_tests -silent)
        set_tests_properties(native_system_menu PROPERTIES TIMEOUT 30)
    endif()
    qt_add_executable(shell_registry_tests tests/platform/shell_registry_tests.cpp)
    qt_add_executable(menu_policy_tests tests/ui/menu_policy_tests.cpp)
    target_include_directories(menu_policy_tests PRIVATE src)
    target_link_libraries(menu_policy_tests PRIVATE sevenzip_panel sevenzip_location Qt6::Test)
    add_test(NAME menu_policy COMMAND menu_policy_tests -silent)
    set_tests_properties(menu_policy PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    target_include_directories(shell_registry_tests PRIVATE src)
    target_link_libraries(shell_registry_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME shell_registry COMMAND shell_registry_tests -silent)
    set_tests_properties(shell_registry PROPERTIES TIMEOUT 15)
    set_tests_properties(shell_context PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(system_page_tests tests/settings/system_page_tests.cpp)
    target_link_libraries(system_page_tests PRIVATE sevenzip_layout_inspection)
    if(WIN32)
        qt_add_executable(windows_system_registration_tests tests/ui/windows_system_registration_tests.cpp)
        target_include_directories(windows_system_registration_tests PRIVATE src tests)
        target_link_libraries(windows_system_registration_tests PRIVATE sevenzip_location Qt6::Test)
        add_test(NAME windows_system_registration COMMAND windows_system_registration_tests -silent)
        set_tests_properties(windows_system_registration PROPERTIES TIMEOUT 30
            ENVIRONMENT "QT_QPA_PLATFORM=windows")
    endif()
    qt_add_executable(association_services_tests tests/platform/association_services_tests.cpp)
    qt_add_executable(association_file_open_tests tests/platform/association_file_open_tests.cpp)
    if(APPLE)
        qt_add_executable(mac_system_page_tests tests/platform/mac_system_page_tests.cpp)
        target_link_libraries(mac_system_page_tests PRIVATE sevenzip_layout_inspection)
        target_include_directories(mac_system_page_tests PRIVATE src tests)
        target_link_libraries(mac_system_page_tests PRIVATE sevenzip_location Qt6::Test)
        set_target_properties(mac_system_page_tests PROPERTIES MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_GUI_IDENTIFIER "org.sevenzip-desktop.tests.system-page")
        add_test(NAME mac_system_page COMMAND mac_system_page_tests -silent)
        set_tests_properties(mac_system_page PROPERTIES TIMEOUT 30
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endif()
    target_include_directories(association_file_open_tests PRIVATE src)
    target_link_libraries(association_file_open_tests PRIVATE sevenzip_location Qt6::Test)
    add_test(NAME association_file_open COMMAND association_file_open_tests -silent)
    set_tests_properties(association_file_open PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(APPLE)
        add_test(NAME macos_association_bundle COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/macos_association_bundle_tests.py
            $<TARGET_BUNDLE_DIR:sevenzip_desktop> ${codec_icon_resource})
    endif()
    target_include_directories(association_services_tests PRIVATE src tests)
    target_link_libraries(association_services_tests PRIVATE sevenzip_location Qt6::Test)
    add_test(NAME association_services COMMAND association_services_tests -silent)
    set_tests_properties(association_services PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        qt_add_executable(linux_system_page_tests tests/ui/linux_system_page_tests.cpp)
        target_link_libraries(linux_system_page_tests PRIVATE sevenzip_layout_inspection sevenzip_widget_dialog_fixtures)
        target_include_directories(linux_system_page_tests PRIVATE src tests)
        target_link_libraries(linux_system_page_tests PRIVATE sevenzip_location Qt6::Test)
        foreach(language - ru)
            foreach(width 640 520)
                add_test(NAME linux_system_page_${language}_${width}
                    COMMAND linux_system_page_tests "nativeIconsAndPendingCancel:${language}-${width}" -silent)
                set_tests_properties(linux_system_page_${language}_${width} PROPERTIES TIMEOUT 30
                    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
            endforeach()
        endforeach()
        qt_add_executable(linux_association_clear_tests tests/ui/linux_association_clear_tests.cpp)
        target_include_directories(linux_association_clear_tests PRIVATE src)
        target_link_libraries(linux_association_clear_tests PRIVATE sevenzip_location Qt6::Test)
        foreach(other 0 1)
            add_test(NAME linux_association_clear_${other} COMMAND linux_association_clear_tests -silent)
            set_tests_properties(linux_association_clear_${other} PROPERTIES TIMEOUT 15
                ENVIRONMENT "SEVENZIP_ASSOC_OTHER=${other}")
        endforeach()
    endif()
    target_include_directories(system_page_tests PRIVATE src tests)
    target_link_libraries(system_page_tests PRIVATE sevenzip_location Qt6::Test)
    add_test(NAME system_page COMMAND system_page_tests -silent)
    set_tests_properties(system_page PROPERTIES TIMEOUT 60 RESOURCE_LOCK sevenzip_ui_artifacts
        FIXTURES_SETUP ui_reference_system
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    add_test(NAME ui_parity_system COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_ui_parity.py compare
        --reference ${CMAKE_CURRENT_SOURCE_DIR}/docs/ui-reference/system-page.reference.json
        --contract ${CMAKE_CURRENT_SOURCE_DIR}/docs/ui-reference/system-page.contract.json
        --reports ${CMAKE_BINARY_DIR}/ui-reports
        --output ${CMAKE_BINARY_DIR}/ui-reports/system-parity.json)
    set_tests_properties(ui_parity_system PROPERTIES FIXTURES_REQUIRED ui_reference_system)
    target_include_directories(menu_page_tests PRIVATE src tests)
    target_link_libraries(menu_page_tests PRIVATE sevenzip_location Qt6::Test)
    add_test(NAME menu_page COMMAND menu_page_tests -silent)
    set_tests_properties(menu_page PROPERTIES TIMEOUT 60
        RESOURCE_LOCK sevenzip_ui_artifacts ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/menu-page-settings.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(options_dialog_tests tests/settings/options_dialog_tests.cpp)
    target_link_libraries(options_dialog_tests PRIVATE sevenzip_layout_inspection)
    target_include_directories(options_dialog_tests PRIVATE src tests)
    target_link_libraries(options_dialog_tests PRIVATE sevenzip_location Qt6::Test)
    add_test(NAME options_dialog COMMAND options_dialog_tests -silent)
    qt_add_executable(options_integration_tests tests/settings/options_integration_tests.cpp)
    target_link_libraries(options_integration_tests PRIVATE sevenzip_layout_inspection)
    qt_add_executable(appearance_settings_tests tests/ui/appearance_settings_tests.cpp)
    target_link_libraries(appearance_settings_tests PRIVATE sevenzip_layout_inspection)
    target_include_directories(appearance_settings_tests PRIVATE src tests)
    target_link_libraries(appearance_settings_tests PRIVATE sevenzip_location Qt6::Test)
    add_test(NAME appearance_settings COMMAND appearance_settings_tests -silent)
    set_tests_properties(appearance_settings PROPERTIES TIMEOUT 30 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    target_include_directories(options_integration_tests PRIVATE src)
    target_link_libraries(options_integration_tests PRIVATE sevenzip_location Qt6::Test)
    add_test(NAME options_integration COMMAND options_integration_tests -silent)
    qt_add_executable(options_keyboard_tests tests/settings/options_keyboard_tests.cpp)
    target_link_libraries(options_keyboard_tests PRIVATE sevenzip_layout_inspection)
    target_include_directories(options_keyboard_tests PRIVATE src)
    target_link_libraries(options_keyboard_tests PRIVATE sevenzip_location Qt6::Test)
    add_test(NAME options_keyboard COMMAND options_keyboard_tests -silent)
    set_tests_properties(options_keyboard PROPERTIES TIMEOUT 30
        RESOURCE_LOCK sevenzip_ui_artifacts ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    set_tests_properties(options_integration PROPERTIES TIMEOUT 30
        RESOURCE_LOCK sevenzip_ui_artifacts ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    set_tests_properties(options_dialog PROPERTIES TIMEOUT 60
        RESOURCE_LOCK sevenzip_ui_artifacts ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    if(SEVENZIP_CODEC_MODULE)
        qt_add_executable(toolbar_archive_workflow_tests tests/filemanager/toolbar_archive_workflow_tests.cpp)
        target_link_libraries(toolbar_archive_workflow_tests PRIVATE sevenzip_layout_inspection)
        target_link_libraries(toolbar_archive_workflow_tests PRIVATE sevenzip_window)
        target_include_directories(toolbar_archive_workflow_tests PRIVATE src tests)
        target_link_libraries(toolbar_archive_workflow_tests PRIVATE Qt6::Test)
        target_compile_definitions(toolbar_archive_workflow_tests PRIVATE
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\""
            "SEVENZIP_NATIVE_GUI=\"$<TARGET_FILE_DIR:sevenzip_desktop>/7zG${CMAKE_EXECUTABLE_SUFFIX}\"")
        add_dependencies(toolbar_archive_workflow_tests sevenzip_desktop)
        target_compile_definitions(toolbar_archive_workflow_tests PRIVATE
            "SEVENZIP_OPERATION_HOST=\"$<TARGET_FILE:operation_host>\"")
        add_dependencies(toolbar_archive_workflow_tests operation_host)
        add_test(NAME toolbar_archive_workflow COMMAND toolbar_archive_workflow_tests
            repeatedArchiveTesting filesystemToolbarFailureRestoresSource archiveToolbarUsesSourceCopyTo
            filesystemShellCommandsUseIndependentOperations extractionRefreshUsesCurrentLocation -silent)
        add_test(NAME toolbar_archive_batch COMMAND toolbar_archive_workflow_tests
            filesystemToolbarUsesSourceBatch -silent)
        set_tests_properties(toolbar_archive_workflow toolbar_archive_batch PROPERTIES TIMEOUT 120
            RESOURCE_LOCK "sevenzip_native_ui;sevenzip_ui_artifacts" ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/toolbar-archive-workflow.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
        qt_add_executable(gui_output_refresh_tests
            tests/filemanager/gui_output_refresh_tests.cpp
            src/filemanager/window/qtguioutputrefresh.cpp)
        target_include_directories(gui_output_refresh_tests PRIVATE src)
        target_link_libraries(gui_output_refresh_tests PRIVATE Qt6::Core Qt6::Test)
        add_test(NAME gui_output_refresh COMMAND gui_output_refresh_tests -silent)
        set_tests_properties(gui_output_refresh PROPERTIES TIMEOUT 15)

        qt_add_executable(toolbar_creation_workflow_tests tests/filemanager/toolbar_creation_workflow_tests.cpp)
        target_link_libraries(toolbar_creation_workflow_tests PRIVATE sevenzip_layout_inspection)
        target_link_libraries(toolbar_creation_workflow_tests PRIVATE sevenzip_window)
        target_include_directories(toolbar_creation_workflow_tests PRIVATE src tests)
        target_link_libraries(toolbar_creation_workflow_tests PRIVATE Qt6::Test)
        target_compile_definitions(toolbar_creation_workflow_tests PRIVATE
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\""
            "SEVENZIP_OPERATION_HOST=\"$<TARGET_FILE:operation_host>\"")
        add_dependencies(toolbar_creation_workflow_tests operation_host)
        add_test(NAME toolbar_creation_workflow COMMAND toolbar_creation_workflow_tests -silent)
        set_tests_properties(toolbar_creation_workflow PROPERTIES TIMEOUT 90
            RESOURCE_LOCK "sevenzip_native_ui;sevenzip_ui_artifacts" ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/toolbar-creation-workflow.ini")
        qt_add_executable(manager_hash_workflow_tests tests/filemanager/manager_hash_workflow_tests.cpp)
        target_link_libraries(manager_hash_workflow_tests PRIVATE sevenzip_layout_inspection)
        target_link_libraries(manager_hash_workflow_tests PRIVATE sevenzip_window)
        target_include_directories(manager_hash_workflow_tests PRIVATE src tests)
        target_link_libraries(manager_hash_workflow_tests PRIVATE Qt6::Test)
        target_compile_definitions(manager_hash_workflow_tests PRIVATE
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\""
            "SEVENZIP_OPERATION_HOST=\"$<TARGET_FILE:operation_host>\"")
        add_dependencies(manager_hash_workflow_tests operation_host)
        add_test(NAME manager_hash_workflow COMMAND manager_hash_workflow_tests -silent)
        set_tests_properties(manager_hash_workflow PROPERTIES TIMEOUT 90
            RESOURCE_LOCK "sevenzip_native_ui;sevenzip_ui_artifacts" ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/manager-hash-workflow.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endif()
    qt_add_executable(address_combo_tests tests/filemanager/address_combo_tests.cpp tests/platform/support/external_editor_fixture.cpp)
    target_link_libraries(address_combo_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(address_combo_tests PRIVATE sevenzip_window)
    target_include_directories(address_combo_tests PRIVATE src tests)
    target_link_libraries(address_combo_tests PRIVATE Qt6::Test)
    add_test(NAME address_combo COMMAND address_combo_tests -silent)
    set_tests_properties(address_combo PROPERTIES TIMEOUT 60 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/address-combo.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(address_lifetime_tests tests/filemanager/address_lifetime_tests.cpp)
    target_link_libraries(address_lifetime_tests PRIVATE sevenzip_window Qt6::Test)
    target_include_directories(address_lifetime_tests PRIVATE src tests)
    add_test(NAME address_lifetime COMMAND address_lifetime_tests -silent)
    set_tests_properties(address_lifetime PROPERTIES TIMEOUT 30 RESOURCE_LOCK sevenzip_native_ui
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(panel_address_keys_tests tests/filemanager/panel_address_keys_tests.cpp)
    target_link_libraries(panel_address_keys_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(panel_address_keys_tests PRIVATE sevenzip_window)
    target_include_directories(panel_address_keys_tests PRIVATE src tests)
    target_link_libraries(panel_address_keys_tests PRIVATE Qt6::Test)
    add_test(NAME panel_address_keys COMMAND panel_address_keys_tests -silent)
    set_tests_properties(panel_address_keys PROPERTIES TIMEOUT 30 RESOURCE_LOCK sevenzip_ui_artifacts ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/panel-address-keys.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(root_command_tests tests/filemanager/root_command_tests.cpp tests/platform/support/external_editor_fixture.cpp)
    target_link_libraries(root_command_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(root_command_tests PRIVATE sevenzip_window)
    target_include_directories(root_command_tests PRIVATE src tests)
    target_link_libraries(root_command_tests PRIVATE Qt6::Test)
    add_test(NAME root_command COMMAND root_command_tests -silent)
    set_tests_properties(root_command PROPERTIES TIMEOUT 60 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/root-command.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(persisted_location_tests
        tests/filemanager/persisted_location_tests.cpp
        tests/platform/support/external_editor_fixture.cpp)
    target_link_libraries(persisted_location_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(persisted_location_tests PRIVATE sevenzip_window)
    target_include_directories(persisted_location_tests PRIVATE src tests)
    target_link_libraries(persisted_location_tests PRIVATE Qt6::Test)
    add_test(NAME persisted_location COMMAND persisted_location_tests -silent)
    set_tests_properties(persisted_location PROPERTIES TIMEOUT 60 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/persisted-location.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    if(WIN32)
        qt_add_executable(native_address_navigation_tests
            tests/filemanager/native_address_navigation_tests.cpp
            tests/support/native_network_share.cpp)
        target_link_libraries(native_address_navigation_tests PRIVATE sevenzip_layout_inspection sevenzip_widget_dialog_fixtures)
        target_link_libraries(native_address_navigation_tests PRIVATE sevenzip_window)
        target_include_directories(native_address_navigation_tests PRIVATE src tests)
        target_link_libraries(native_address_navigation_tests PRIVATE Qt6::Test netapi32 advapi32)
        add_test(NAME native_address_navigation COMMAND native_address_navigation_tests -silent)
        set_tests_properties(native_address_navigation PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE
            RESOURCE_LOCK sevenzip_native_ui ENVIRONMENT
            "QT_QPA_PLATFORM=windows;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native-address-navigation.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
        qt_add_executable(native_persisted_locations_tests
            tests/filemanager/native_persisted_locations_tests.cpp
            tests/support/native_network_share.cpp
            tests/support/native_network_share.h)
        target_link_libraries(native_persisted_locations_tests PRIVATE sevenzip_layout_inspection sevenzip_widget_dialog_fixtures)
        target_link_libraries(native_persisted_locations_tests PRIVATE sevenzip_window)
        target_include_directories(native_persisted_locations_tests PRIVATE src tests)
        target_link_libraries(native_persisted_locations_tests PRIVATE Qt6::Test netapi32 advapi32)
        add_test(NAME native_persisted_locations COMMAND native_persisted_locations_tests -silent)
        set_tests_properties(native_persisted_locations PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native-persisted-locations.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
        qt_add_executable(native_network_navigation_tests
            tests/filemanager/native_network_navigation_tests.cpp
            tests/support/native_network_share.cpp
            tests/support/native_network_share.h)
        target_link_libraries(native_network_navigation_tests PRIVATE sevenzip_layout_inspection)
        target_link_libraries(native_network_navigation_tests PRIVATE sevenzip_window)
        target_include_directories(native_network_navigation_tests PRIVATE src tests)
        target_link_libraries(native_network_navigation_tests PRIVATE Qt6::Test netapi32 advapi32)
        add_test(NAME native_network_navigation COMMAND native_network_navigation_tests -silent)
        set_tests_properties(native_network_navigation PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native-network-navigation.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endif()
    qt_add_executable(namespace_hash_tests tests/filemanager/namespace_hash_tests.cpp)
    target_link_libraries(namespace_hash_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(namespace_hash_tests PRIVATE sevenzip_window)
    target_include_directories(namespace_hash_tests PRIVATE src tests)
    target_link_libraries(namespace_hash_tests PRIVATE Qt6::Test)
    if(WIN32)
        target_sources(namespace_hash_tests PRIVATE tests/support/read_only_virtual_disk.cpp
            tests/support/read_only_virtual_disk.h)
        target_link_libraries(namespace_hash_tests PRIVATE virtdisk)
    endif()
    add_test(NAME namespace_hash COMMAND namespace_hash_tests -silent)
    set_tests_properties(namespace_hash PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/namespace-hash.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    if(WIN32)
        qt_add_executable(fs_drives_device_ui_tests
            tests/devices/fs_drives_device_ui_tests.cpp
            tests/support/read_only_virtual_disk.cpp
            tests/support/read_only_virtual_disk.h)
        target_link_libraries(fs_drives_device_ui_tests PRIVATE sevenzip_layout_inspection)
        target_link_libraries(fs_drives_device_ui_tests PRIVATE sevenzip_window)
        target_include_directories(fs_drives_device_ui_tests PRIVATE src tests)
        target_link_libraries(fs_drives_device_ui_tests PRIVATE Qt6::Test virtdisk)
        add_test(NAME fs_drives_device_ui COMMAND fs_drives_device_ui_tests -silent)
        set_tests_properties(fs_drives_device_ui PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/fs-drives-device-ui.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endif()
    qt_add_executable(help_ui_tests tests/filemanager/help_ui_tests.cpp)
    target_link_libraries(help_ui_tests PRIVATE sevenzip_window)
    target_include_directories(help_ui_tests PRIVATE src tests)
    target_link_libraries(help_ui_tests PRIVATE Qt6::Test)
    add_test(NAME help_ui COMMAND help_ui_tests -silent)
    set_tests_properties(help_ui PROPERTIES TIMEOUT 30 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/help_ui.ini")
    qt_add_executable(options_window_tests tests/settings/options_window_tests.cpp)
    target_link_libraries(options_window_tests PRIVATE sevenzip_window sevenzip_layout_inspection)
    target_include_directories(options_window_tests PRIVATE src tests)
    target_link_libraries(options_window_tests PRIVATE Qt6::Test)
    add_test(NAME options_window COMMAND options_window_tests -silent)
    set_tests_properties(options_window PROPERTIES TIMEOUT 60 RESOURCE_LOCK sevenzip_ui_artifacts
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/options_window.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(language_ui_tests tests/localization/language_ui_tests.cpp)
    target_link_libraries(language_ui_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(language_ui_tests PRIVATE sevenzip_window)
    target_include_directories(language_ui_tests PRIVATE src tests)
    target_link_libraries(language_ui_tests PRIVATE Qt6::Test)
    add_test(NAME language_ui COMMAND language_ui_tests -silent)
    set_tests_properties(language_ui PROPERTIES TIMEOUT 60 RESOURCE_LOCK sevenzip_ui_artifacts
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/language_ui.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(language_main_dialog_tests tests/localization/language_main_dialog_tests.cpp)
    target_link_libraries(language_main_dialog_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(language_main_dialog_tests PRIVATE sevenzip_window)
    target_include_directories(language_main_dialog_tests PRIVATE src tests)
    target_link_libraries(language_main_dialog_tests PRIVATE Qt6::Test)
    add_test(NAME language_main_dialog COMMAND language_main_dialog_tests -silent)
    set_tests_properties(language_main_dialog PROPERTIES TIMEOUT 60 RESOURCE_LOCK sevenzip_ui_artifacts
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/language_main_dialog.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(language_archive_command_tests tests/localization/language_archive_command_tests.cpp)
    target_link_libraries(language_archive_command_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(language_archive_command_tests PRIVATE sevenzip_window)
    target_include_directories(language_archive_command_tests PRIVATE src tests)
    target_link_libraries(language_archive_command_tests PRIVATE Qt6::Test)
    add_test(NAME language_archive_command COMMAND language_archive_command_tests -silent)
    set_tests_properties(language_archive_command PROPERTIES TIMEOUT 60 RESOURCE_LOCK sevenzip_ui_artifacts
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/language_archive_command.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(language_focus_tests tests/localization/language_focus_tests.cpp)
    target_link_libraries(language_focus_tests PRIVATE sevenzip_compression_ui sevenzip_layout_inspection sevenzip_location)
    target_include_directories(language_focus_tests PRIVATE src tests)
    target_link_libraries(language_focus_tests PRIVATE Qt6::Test)
    foreach(catalog_fixture_consumer language_tests language_ui_tests language_focus_tests)
        target_sources(${catalog_fixture_consumer} PRIVATE tests/support/translation_catalog.cpp)
        target_compile_definitions(${catalog_fixture_consumer} PRIVATE TEST_LRELEASE_PATH="$<TARGET_FILE:Qt6::lrelease>")
    endforeach()
    add_test(NAME language_focus COMMAND language_focus_tests -silent)
    set_tests_properties(language_focus PROPERTIES TIMEOUT 30 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/language_focus.ini")
endif()

# Every current window consumer uses the same language presentation adapter.
