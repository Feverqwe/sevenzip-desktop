if(BUILD_TESTING)
    qt_add_executable(workspace_archive_tests tests/filemanager/workspace_archive_tests.cpp
        tests/platform/support/external_editor_fixture.cpp)
    target_link_libraries(workspace_archive_tests PRIVATE sevenzip_window sevenzip_layout_inspection
        sevenzip_widget_dialog_fixtures Qt6::Test)
    target_include_directories(workspace_archive_tests PRIVATE src tests)
    add_test(NAME workspace_archive COMMAND workspace_archive_tests -silent)
    set_tests_properties(workspace_archive PROPERTIES TIMEOUT 90 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/workspace-archive-settings.ini")
    qt_add_executable(workspace_navigation_tests tests/filemanager/workspace_navigation_tests.cpp)
    target_link_libraries(workspace_navigation_tests PRIVATE sevenzip_window sevenzip_widget_dialog_fixtures Qt6::Test)
    target_include_directories(workspace_navigation_tests PRIVATE src tests)
    add_test(NAME workspace_navigation COMMAND workspace_navigation_tests -silent)
    set_tests_properties(workspace_navigation PROPERTIES TIMEOUT 90 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/workspace-navigation-settings.ini")
    qt_add_executable(workspace_session_tests tests/filemanager/workspace_session_tests.cpp)
    target_link_libraries(workspace_session_tests PRIVATE sevenzip_window Qt6::Test)
    target_include_directories(workspace_session_tests PRIVATE src tests)
    add_test(NAME workspace_session COMMAND workspace_session_tests -silent)
    set_tests_properties(workspace_session PROPERTIES TIMEOUT 90 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/workspace-session-settings.ini")
    qt_add_executable(rename_lifecycle_tests tests/filemanager/rename_lifecycle_tests.cpp)
    target_link_libraries(rename_lifecycle_tests PRIVATE sevenzip_window Qt6::Test)
    target_include_directories(rename_lifecycle_tests PRIVATE src tests)
    add_test(NAME rename_lifecycle COMMAND rename_lifecycle_tests -silent)
    set_tests_properties(rename_lifecycle PROPERTIES TIMEOUT 90 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/rename-lifecycle.ini")
    qt_add_executable(command_publication_tests tests/filemanager/command_publication_tests.cpp)
    target_link_libraries(command_publication_tests PRIVATE sevenzip_window Qt6::Test)
    target_include_directories(command_publication_tests PRIVATE src tests)
    add_test(NAME command_publication COMMAND command_publication_tests -silent)
    set_tests_properties(command_publication PROPERTIES TIMEOUT 60 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/command-publication.ini")
    qt_add_executable(large_list_window_tests tests/filemanager/large_list_window_tests.cpp)
    target_link_libraries(large_list_window_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(large_list_window_tests PRIVATE sevenzip_window)
    target_include_directories(large_list_window_tests PRIVATE src tests)
    target_link_libraries(large_list_window_tests PRIVATE Qt6::Test)
    add_test(NAME large_list_window COMMAND large_list_window_tests -silent)
    # Four measured projections and their refreshes each have a 15s deadline;
    # leave room for fixture I/O and the separate lifecycle/selection cases.
    # Timing the real GUI heartbeat must not compete with other CTest processes.
    set_tests_properties(large_list_window PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/large-list-window.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(worker_window_progress_tests tests/filemanager/worker_window_progress_tests.cpp)
    target_link_libraries(worker_window_progress_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(worker_window_progress_tests PRIVATE sevenzip_window)
    target_include_directories(worker_window_progress_tests PRIVATE src tests)
    target_link_libraries(worker_window_progress_tests PRIVATE Qt6::Test)
    add_test(NAME worker_window_progress COMMAND worker_window_progress_tests -silent)
    set_tests_properties(worker_window_progress PROPERTIES TIMEOUT 120 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/worker-window-progress.ini")
endif()

if(BUILD_TESTING)
    qt_add_executable(panel_shortcut_tests tests/filemanager/panel_shortcut_tests.cpp)
    target_link_libraries(panel_shortcut_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(panel_shortcut_tests PRIVATE sevenzip_window)
    target_include_directories(panel_shortcut_tests PRIVATE src tests)
    target_link_libraries(panel_shortcut_tests PRIVATE Qt6::Test)
    add_test(NAME panel_shortcut COMMAND panel_shortcut_tests -silent)
    set_tests_properties(panel_shortcut PROPERTIES TIMEOUT 60 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/panel-shortcut.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(panel_folder_sync_tests tests/filemanager/panel_folder_sync_tests.cpp)
    target_link_libraries(panel_folder_sync_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(panel_folder_sync_tests PRIVATE sevenzip_window)
    target_include_directories(panel_folder_sync_tests PRIVATE src tests)
    target_link_libraries(panel_folder_sync_tests PRIVATE Qt6::Test)
    add_test(NAME panel_folder_sync COMMAND panel_folder_sync_tests -silent)
    set_tests_properties(panel_folder_sync PROPERTIES TIMEOUT 90 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/panel-folder-sync.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(panel_key_source_tests tests/filemanager/panel_key_source_tests.cpp)
    target_link_libraries(panel_key_source_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(panel_key_source_tests PRIVATE sevenzip_window)
    target_include_directories(panel_key_source_tests PRIVATE src tests)
    target_link_libraries(panel_key_source_tests PRIVATE Qt6::Test)
    add_test(NAME panel_key_source COMMAND panel_key_source_tests -silent)
    set_tests_properties(panel_key_source PROPERTIES TIMEOUT 90 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/panel-key-source.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(panel_list_key_tests tests/filemanager/panel_list_key_tests.cpp)
    target_link_libraries(panel_list_key_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(panel_list_key_tests PRIVATE sevenzip_window)
    target_include_directories(panel_list_key_tests PRIVATE src tests)
    target_link_libraries(panel_list_key_tests PRIVATE Qt6::Test)
    target_compile_definitions(panel_list_key_tests PRIVATE
        SEVENZIP_CODEC_MODULE="${SEVENZIP_CODEC_MODULE}")
    add_test(NAME panel_list_key COMMAND panel_list_key_tests -silent)
    set_tests_properties(panel_list_key PROPERTIES TIMEOUT 90 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/panel-list-key.ini")
endif()

# Apply startup isolation after registering every consumer, including Language.
# Parallel CTest runs share these generated artifact directories. Keep their
# writers separate while allowing independent tests to run concurrently.
set(SEVENZIP_TEST_SCRATCH_DIR "${CMAKE_BINARY_DIR}/test-workspaces" CACHE PATH
    "Disposable test data, independently of persistent build intermediates")
set(sevenzip_test_workspace_setup "")
get_property(sevenzip_registered_tests DIRECTORY PROPERTY TESTS)
foreach(sevenzip_test IN LISTS sevenzip_registered_tests)
    set(sevenzip_test_workspace "${SEVENZIP_TEST_SCRATCH_DIR}/${sevenzip_test}")
    string(APPEND sevenzip_test_workspace_setup
        "file(MAKE_DIRECTORY [=[${sevenzip_test_workspace}]=])\n")
    # Reference-comparison fixtures require JSON as test input. Ordinary UI
    # suites keep report generation optional.
    get_property(sevenzip_report_fixture TEST ${sevenzip_test} PROPERTY FIXTURES_SETUP)
    if(sevenzip_report_fixture MATCHES "(^|;)ui_reference_")
        set_property(TEST ${sevenzip_test} APPEND PROPERTY ENVIRONMENT "SEVENZIP_UI_REPORTS=ON")
    endif()
    get_property(sevenzip_test_environment TEST ${sevenzip_test} PROPERTY ENVIRONMENT)
    if(sevenzip_test_environment MATCHES "SEVENZIP_SETTINGS_PATH=")
        list(FILTER sevenzip_test_environment EXCLUDE REGEX "^SEVENZIP_SETTINGS_PATH=")
        list(APPEND sevenzip_test_environment
            "SEVENZIP_SETTINGS_PATH=${sevenzip_test_workspace}/settings.ini")
        set_property(TEST ${sevenzip_test} PROPERTY ENVIRONMENT "${sevenzip_test_environment}")
    endif()
    # Reference commands use the client built with this runtime, independently
    # of the native module consumed by the application.
    set_property(TEST ${sevenzip_test} APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${SEVENZIP_CODEC_RUNTIME_DIR}")
    if(WIN32 AND VCPKG_MANIFEST_MODE)
        # Focused tests may be built without deploying the GUI application.
        # Plugins and their DLL dependencies must come from the same Qt config.
        set_property(TEST ${sevenzip_test} APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
            "QT_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::QWindowsIntegrationPlugin>/..")
    endif()
    set_property(TEST ${sevenzip_test} APPEND PROPERTY ENVIRONMENT
        "SEVENZIP_OWNED_TEMP_REGISTRY=${sevenzip_test_workspace}/owned-temp")
    # MainWindow opens the current directory before a test navigates to its
    # fixture. Keep that startup input separate from hundreds of build outputs.
    # Each suite owns its directory so parallel runs cannot populate each other.
    set(sevenzip_test_target ${sevenzip_test}_tests)
    if(sevenzip_test MATCHES "^native_nested_(stream|memory|disk)(_quick)?$")
        set(sevenzip_test_target native_nested_publication_tests)
    elseif(sevenzip_test MATCHES "^(.+)_quick$")
        set(sevenzip_test_target ${CMAKE_MATCH_1}_tests)
    elseif(sevenzip_test STREQUAL "bookmarks")
        set(sevenzip_test_target bookmark_tests)
    elseif(sevenzip_test MATCHES "^drag_drop_add_(independent|temporary)$")
        set(sevenzip_test_target drag_drop_add_ui_tests)
    elseif(sevenzip_test STREQUAL "toolbar_archive_batch")
        set(sevenzip_test_target toolbar_archive_workflow_tests)
    elseif(sevenzip_test STREQUAL "interarchive_recovery_native")
        set(sevenzip_test_target interarchive_recovery_tests)
    elseif(sevenzip_test MATCHES "^native_rename_(2|301)$")
        set(sevenzip_test_target native_rename_publication_tests)
    elseif(sevenzip_test MATCHES "^native_external_(2|301|update)$")
        set(sevenzip_test_target native_external_publication_tests)
    elseif(sevenzip_test MATCHES "^native_association_(handle|disk|browser|workflow)$")
        set(sevenzip_test_target native_association_tests)
    endif()
    if(TARGET ${sevenzip_test_target})
        get_target_property(sevenzip_test_libraries ${sevenzip_test_target} LINK_LIBRARIES)
        if("sevenzip_window" IN_LIST sevenzip_test_libraries)
            set(sevenzip_test_startup_dir "${sevenzip_test_workspace}/startup")
            string(APPEND sevenzip_test_workspace_setup
                "file(MAKE_DIRECTORY [=[${sevenzip_test_startup_dir}]=])\n")
            set_tests_properties(${sevenzip_test} PROPERTIES
                WORKING_DIRECTORY "${sevenzip_test_startup_dir}")
            # Saved panel paths outlive a QTemporaryDir fixture. Upstream
            # navigation then opens its parent, which must also be test-owned.
            set_property(TEST ${sevenzip_test} APPEND PROPERTY ENVIRONMENT
                "TMPDIR=${sevenzip_test_startup_dir}"
                "TMP=${sevenzip_test_startup_dir}"
                "TEMP=${sevenzip_test_startup_dir}")
        endif()
    endif()
    get_property(sevenzip_test_environment TEST ${sevenzip_test} PROPERTY ENVIRONMENT)
    if(sevenzip_test_environment MATCHES "SEVENZIP_UI_REPORT_DIR|SEVENZIP_SCREENSHOT_DIR")
        if(sevenzip_test MATCHES "^native_nested_(stream|memory|disk)(_quick)?$")
            # Report tags include the route. Only its full/quick selections
            # share output names; unrelated routes have separate runtimes too.
            set_property(TEST ${sevenzip_test} APPEND PROPERTY RESOURCE_LOCK
                sevenzip_nested_${CMAKE_MATCH_1}_artifacts)
        else()
            set_property(TEST ${sevenzip_test} APPEND PROPERTY RESOURCE_LOCK sevenzip_ui_artifacts)
        endif()
    endif()
endforeach()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/PrepareTestWorkspace.cmake"
    CONTENT "${sevenzip_test_workspace_setup}")

# Add now reaches the source modal through CPanel and requires its native
# codec metadata even in selection/cancellation-only window tests.
