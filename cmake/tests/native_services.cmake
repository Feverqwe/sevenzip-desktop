    qt_add_executable(app_state_tests tests/ui/app_state_tests.cpp)
    target_link_libraries(app_state_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME app_state COMMAND app_state_tests -silent)
    qt_add_executable(view_settings_columns_tests tests/ui/view_settings_columns_tests.cpp)
    target_include_directories(view_settings_columns_tests PRIVATE src)
    target_link_libraries(view_settings_columns_tests PRIVATE sevenzip_common Qt6::Test)
    add_test(NAME view_settings_columns COMMAND view_settings_columns_tests -silent)
    qt_add_executable(panel_folder_tests tests/filemanager/panel_folder_tests.cpp)
    target_link_libraries(panel_folder_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME panel_folder COMMAND panel_folder_tests -silent)
    set_tests_properties(panel_folder PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME ui_parity_tool COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/ui_parity_tests.py)
    add_test(NAME ui_runtime_comparison COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/ui_runtime_comparison_tests.py)
    add_test(NAME ui_parity_copy COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_ui_parity.py compare
        --reference ${CMAKE_CURRENT_SOURCE_DIR}/docs/ui-reference/copy-dialog.reference.json
        --contract ${CMAKE_CURRENT_SOURCE_DIR}/docs/ui-reference/copy-dialog.contract.json
        --reports ${CMAKE_BINARY_DIR}/ui-reports
        --output ${CMAKE_BINARY_DIR}/ui-reports/copy-parity.json)
    set_tests_properties(ui_parity_copy PROPERTIES FIXTURES_REQUIRED ui_reference_copy)
    add_test(NAME ui_parity_password COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_ui_parity.py compare
        --reference ${CMAKE_CURRENT_SOURCE_DIR}/docs/ui-reference/password-dialog.reference.json
        --contract ${CMAKE_CURRENT_SOURCE_DIR}/docs/ui-reference/password-dialog.contract.json
        --reports ${CMAKE_BINARY_DIR}/ui-reports
        --output ${CMAKE_BINARY_DIR}/ui-reports/password-parity.json)
    set_tests_properties(password_dialog PROPERTIES FIXTURES_SETUP ui_reference_password)
    set_tests_properties(ui_parity_password PROPERTIES FIXTURES_REQUIRED ui_reference_password)
    foreach(prompt IN ITEMS overwrite mem)
        add_test(NAME ui_parity_${prompt} COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_ui_parity.py compare
            --reference ${CMAKE_CURRENT_SOURCE_DIR}/docs/ui-reference/${prompt}-dialog.reference.json
            --contract ${CMAKE_CURRENT_SOURCE_DIR}/docs/ui-reference/${prompt}-dialog.contract.json
            --reports ${CMAKE_BINARY_DIR}/ui-reports
            --output ${CMAKE_BINARY_DIR}/ui-reports/${prompt}-parity.json)
        set_tests_properties(ui_parity_${prompt} PROPERTIES
            FIXTURES_REQUIRED ui_reference_extract_prompts)
    endforeach()
    qt_add_executable(combo_dialog_tests tests/ui/combo_dialog_tests.cpp)
    target_link_libraries(combo_dialog_tests PRIVATE sevenzip_layout_inspection)
    target_include_directories(combo_dialog_tests PRIVATE tests)
    target_link_libraries(combo_dialog_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME combo_dialog COMMAND combo_dialog_tests -silent)
    qt_add_executable(disclosure_accessibility_tests tests/ui/disclosure_accessibility_tests.cpp)
    target_link_libraries(disclosure_accessibility_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME disclosure_accessibility COMMAND disclosure_accessibility_tests -silent)
    set_tests_properties(disclosure_accessibility PROPERTIES TIMEOUT 15
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(context_dialog_tests tests/ui/context_dialog_tests.cpp)
    target_link_libraries(context_dialog_tests PRIVATE sevenzip_layout_inspection sevenzip_split_dialog)
    target_include_directories(context_dialog_tests PRIVATE tests)
    target_link_libraries(context_dialog_tests PRIVATE sevenzip_panel Qt6::Test)
    if(APPLE)
        target_sources(context_dialog_tests PRIVATE tests/support/native_sheet_mac.mm)
    endif()
    add_test(NAME context_dialog COMMAND context_dialog_tests -silent)
    set_tests_properties(context_dialog PROPERTIES TIMEOUT 60 RESOURCE_LOCK sevenzip_native_ui
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    if(APPLE)
        set_property(TEST context_dialog PROPERTY ENVIRONMENT
            "QT_QPA_PLATFORM=cocoa;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endif()
    qt_add_executable(browse_dialog_tests tests/ui/browse_dialog_tests.cpp)
    target_link_libraries(browse_dialog_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME browse_dialog COMMAND browse_dialog_tests -silent)
    set_tests_properties(browse_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
    qt_add_executable(source_browse_dialog_tests tests/archive/source_browse_dialog_tests.cpp)
    target_link_libraries(source_browse_dialog_tests PRIVATE sevenzip_layout_inspection)
    target_include_directories(source_browse_dialog_tests PRIVATE tests)
    target_link_libraries(source_browse_dialog_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME source_browse_dialog COMMAND source_browse_dialog_tests -silent)
    set_tests_properties(source_browse_dialog PROPERTIES TIMEOUT 30 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    set_tests_properties(combo_dialog PROPERTIES TIMEOUT 30 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(volume_dialog_tests
        tests/fileops/volume_dialog_tests.cpp
        tests/support/split_dialog_prompt.cpp)
    target_link_libraries(volume_dialog_tests PRIVATE sevenzip_layout_inspection sevenzip_panel sevenzip_panel_split sevenzip_split_dialog)
    target_include_directories(volume_dialog_tests PRIVATE src tests)
    target_link_libraries(volume_dialog_tests PRIVATE Qt6::Test)
    add_test(NAME volume_dialog COMMAND volume_dialog_tests -silent)
    set_tests_properties(volume_dialog PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    qt_add_executable(filesystem_operations_tests tests/ui/filesystem_operations_tests.cpp
        tests/fileops/support/filedeleteoperation.cpp tests/fileops/support/filedeleteoperation.h)
    target_link_libraries(filesystem_operations_tests PRIVATE sevenzip_filesystem Qt6::Test)
    add_test(NAME filesystem_operations COMMAND filesystem_operations_tests -silent)
    qt_add_executable(filesystem_cleanup_tests tests/ui/filesystem_cleanup_tests.cpp)
    target_link_libraries(filesystem_cleanup_tests PRIVATE sevenzip_filesystem Qt6::Test)
    add_test(NAME filesystem_cleanup COMMAND filesystem_cleanup_tests -silent)
    add_library(native_library_fixture SHARED tests/support/native_library_fixture.c)
    set_target_properties(native_library_fixture PROPERTIES AUTOMOC OFF AUTOUIC OFF)
    qt_add_executable(native_library_tests tests/platform/native_library_tests.cpp)
    target_include_directories(native_library_tests PRIVATE src)
    target_link_libraries(native_library_tests PRIVATE sevenzip_common Qt6::Test)
    target_compile_definitions(native_library_tests PRIVATE
        "NATIVE_LIBRARY_FIXTURE=\"$<TARGET_FILE:native_library_fixture>\"")
    add_dependencies(native_library_tests native_library_fixture)
    if(WIN32)
        add_library(native_module_path_fixture SHARED tests/support/native_module_path_fixture.cpp)
        target_include_directories(native_module_path_fixture PRIVATE src)
        target_link_libraries(native_module_path_fixture PRIVATE sevenzip_common)
        target_compile_definitions(native_library_tests PRIVATE
            "NATIVE_MODULE_PATH_FIXTURE=\"$<TARGET_FILE:native_module_path_fixture>\"")
        add_dependencies(native_library_tests native_module_path_fixture)
    endif()
    add_test(NAME native_library COMMAND native_library_tests -silent)
    find_file(SEVENZIP_CODEC_MODULE NAMES 7z.so 7z.dll
        HINTS "${SEVENZIP_CODEC_RUNTIME_DIR}" ENV SEVENZIP_CODEC_MODULE_DIR
        PATHS /opt/homebrew/opt/sevenzip/lib /usr/lib/7zip /usr/lib/p7zip
        DOC "Official 7-Zip module for native capability regression checks")
    qt_add_executable(codec_capabilities_tests tests/archive/codec_capabilities_tests.cpp)
    target_include_directories(codec_capabilities_tests PRIVATE src)
    target_link_libraries(codec_capabilities_tests PRIVATE sevenzip_filesystem Qt6::Test)
    target_compile_definitions(codec_capabilities_tests PRIVATE
        "NATIVE_LIBRARY_FIXTURE=\"$<TARGET_FILE:native_library_fixture>\"")
    if(SEVENZIP_CODEC_MODULE)
        target_compile_definitions(codec_capabilities_tests PRIVATE
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\"")
    endif()
    add_dependencies(codec_capabilities_tests native_library_fixture)
    add_test(NAME codec_capabilities COMMAND codec_capabilities_tests -silent)
    qt_add_executable(folder_link_tests tests/filemanager/folder_link_tests.cpp)
    target_link_libraries(folder_link_tests PRIVATE sevenzip_panel Qt6::Test)
    target_compile_definitions(folder_link_tests PRIVATE
        "NATIVE_LIBRARY_FIXTURE=\"$<TARGET_FILE:native_library_fixture>\"")
    add_dependencies(folder_link_tests native_library_fixture)
    add_test(NAME folder_link COMMAND folder_link_tests -silent)
    qt_add_executable(panel_processing_tests tests/filemanager/panel_processing_tests.cpp)
    target_link_libraries(panel_processing_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME panel_processing COMMAND panel_processing_tests -silent)
    set_tests_properties(panel_processing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(format_utils_tests tests/ui/format_utils_tests.cpp)
    target_link_libraries(format_utils_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME format_utils COMMAND format_utils_tests -silent)
    qt_add_executable(native_services_tests tests/platform/native_services_tests.cpp)
    target_link_libraries(native_services_tests PRIVATE sevenzip_common Qt6::Test)
    add_test(NAME native_services COMMAND native_services_tests -silent)
    set_tests_properties(native_services PROPERTIES TIMEOUT 10)
    if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        qt_add_executable(process_snapshot_tests tests/operations/process_snapshot_tests.cpp
            ${sevenzip_external_process_source})
        target_include_directories(process_snapshot_tests PRIVATE src)
        target_link_libraries(process_snapshot_tests PRIVATE sevenzip_common Qt6::Test)
        add_test(NAME process_snapshot COMMAND process_snapshot_tests -silent)
        set_tests_properties(process_snapshot PROPERTIES TIMEOUT 10)
    endif()
    qt_add_executable(progress_sync_tests tests/ui/progress_sync_tests.cpp)
    target_link_libraries(progress_sync_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME progress_sync COMMAND progress_sync_tests -silent)
    qt_add_executable(worker_checkpoint_tests tests/ui/worker_checkpoint_tests.cpp)
    target_link_libraries(worker_checkpoint_tests PRIVATE sevenzip_progress_sync Qt6::Test)
    add_test(NAME worker_checkpoint COMMAND worker_checkpoint_tests -silent)
    set_tests_properties(worker_checkpoint PROPERTIES TIMEOUT 10)
    qt_add_executable(progress_wait_tests tests/ui/progress_wait_tests.cpp)
    target_link_libraries(progress_wait_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME progress_wait COMMAND progress_wait_tests -silent)
    set_tests_properties(progress_wait PROPERTIES TIMEOUT 10)
    qt_add_executable(progress_worker_tests tests/ui/progress_worker_tests.cpp)
    target_link_libraries(progress_worker_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(progress_worker_tests PRIVATE sevenzip_panel Qt6::Test)
    add_test(NAME progress_worker COMMAND progress_worker_tests -silent)
    set_tests_properties(progress_worker PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports"
        FIXTURES_SETUP ui_reference_progress_worker TIMEOUT 30)
    qt_add_executable(update_callback_tests tests/ui/update_callback_tests.cpp)
    qt_add_executable(open_callback_tests tests/ui/open_callback_tests.cpp)
    qt_add_executable(work_dir_tests tests/ui/work_dir_tests.cpp)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(volume_registration_allocation_tests tests/archive/volume_registration_allocation_tests.cpp)
        target_include_directories(volume_registration_allocation_tests PRIVATE src)
        target_link_libraries(volume_registration_allocation_tests PRIVATE sevenzip_panel)
        set_target_properties(volume_registration_allocation_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF)
        add_test(NAME volume_registration_allocation COMMAND volume_registration_allocation_tests)
        set_tests_properties(volume_registration_allocation PROPERTIES TIMEOUT 15)
        add_executable(temp_directory_allocation_tests tests/platform/temp_directory_allocation_tests.cpp)
        target_include_directories(temp_directory_allocation_tests PRIVATE src)
        target_link_libraries(temp_directory_allocation_tests PRIVATE sevenzip_common)
        set_target_properties(temp_directory_allocation_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF)
        add_test(NAME temp_directory_allocation COMMAND temp_directory_allocation_tests)
        set_tests_properties(temp_directory_allocation PROPERTIES TIMEOUT 10)
    endif()
    qt_add_executable(source_update_backend_tests tests/archive/source_update_backend_tests.cpp)
    target_link_libraries(source_update_backend_tests PRIVATE sevenzip_location)
    qt_add_executable(source_extract_backend_tests tests/filemanager/source_extract_backend_tests.cpp)
    target_link_libraries(source_extract_backend_tests PRIVATE sevenzip_location)
    target_include_directories(source_extract_backend_tests PRIVATE src)
    target_link_libraries(source_extract_backend_tests PRIVATE Qt6::Test)
    add_test(NAME source_extract_backend COMMAND source_extract_backend_tests -silent)
    set_tests_properties(source_extract_backend PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    qt_add_executable(backend_request_tests tests/archive/backend_request_tests.cpp)
    target_link_libraries(backend_request_tests PRIVATE sevenzip_location Qt6::Test)
    target_include_directories(backend_request_tests PRIVATE src tests)
    add_test(NAME backend_request COMMAND backend_request_tests -silent)
    set_tests_properties(backend_request PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
    qt_add_executable(operation_host tests/support/operation_host.cpp tests/support/operation_dialogs.cpp)
    target_link_libraries(operation_host PRIVATE sevenzip_compression_ui sevenzip_gui_entry sevenzip_layout_inspection sevenzip_location)
    target_include_directories(operation_host PRIVATE src tests)
    target_link_libraries(operation_host PRIVATE Qt6::Widgets)
    target_compile_definitions(operation_launcher_tests PRIVATE
        "SEVENZIP_OPERATION_HOST=\"$<TARGET_FILE:operation_host>\"")
    add_dependencies(operation_launcher_tests operation_host)
    target_compile_definitions(operation_extraction_tests PRIVATE
        "SEVENZIP_OPERATION_HOST=\"$<TARGET_FILE:operation_host>\"")
    add_dependencies(operation_extraction_tests operation_host)
    target_compile_definitions(operation_creation_tests PRIVATE
        "SEVENZIP_OPERATION_HOST=\"$<TARGET_FILE:operation_host>\"")
    add_dependencies(operation_creation_tests operation_host)
    target_compile_definitions(operation_hash_tests PRIVATE
        "SEVENZIP_OPERATION_HOST=\"$<TARGET_FILE:operation_host>\"")
    add_dependencies(operation_hash_tests operation_host)
    qt_add_executable(operation_benchmark_tests tests/operations/operation_benchmark_tests.cpp tests/support/operation_parent.cpp)
    target_include_directories(operation_benchmark_tests PRIVATE src tests)
    target_link_libraries(operation_benchmark_tests PRIVATE sevenzip_filesystem Qt6::Test)
    if(SEVENZIP_CODEC_MODULE)
        target_compile_definitions(operation_benchmark_tests PRIVATE
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\"")
    endif()
    target_compile_definitions(operation_benchmark_tests PRIVATE
        "SEVENZIP_OPERATION_HOST=\"$<TARGET_FILE:operation_host>\"")
    add_dependencies(operation_benchmark_tests operation_host)
    add_test(NAME operation_benchmark COMMAND operation_benchmark_tests -silent)
    set_tests_properties(operation_benchmark PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        qt_add_executable(native_mapi_host
            tests/support/native_mapi_host.cpp
            tests/support/native_mapi_loader.cpp)
        target_link_libraries(native_mapi_host PRIVATE sevenzip_compression_ui sevenzip_gui_entry sevenzip_location)
        target_include_directories(native_mapi_host PRIVATE src)
        target_link_libraries(native_mapi_host PRIVATE Qt6::Widgets)
        foreach(mapi_variant IN ITEMS wide ansi missing)
            add_library(native_mapi_${mapi_variant} SHARED tests/support/native_mapi_provider.cpp)
            target_link_libraries(native_mapi_${mapi_variant} PRIVATE Qt6::Core)
            set_target_properties(native_mapi_${mapi_variant} PROPERTIES
                OUTPUT_NAME Mapi32 AUTOMOC OFF AUTOUIC OFF
                PDB_NAME "native_mapi_${mapi_variant}"
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mapi-fixtures/${mapi_variant}"
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mapi-fixtures/${mapi_variant}"
                ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mapi-fixtures/${mapi_variant}")
        endforeach()
        target_compile_definitions(native_mapi_wide PRIVATE SEVENZIP_MAPI_WIDE SEVENZIP_MAPI_ANSI)
        target_compile_definitions(native_mapi_ansi PRIVATE SEVENZIP_MAPI_ANSI)
        target_sources(native_mapi_wide PRIVATE tests/support/native_mapi_wide.def)
        target_sources(native_mapi_ansi PRIVATE tests/support/native_mapi_ansi.def)
        qt_add_executable(native_mapi_tests tests/platform/native_mapi_tests.cpp)
        target_include_directories(native_mapi_tests PRIVATE tests)
        target_link_libraries(native_mapi_tests PRIVATE Qt6::Test)
        target_compile_definitions(native_mapi_tests PRIVATE
            "NATIVE_MAPI_HOST=\"$<TARGET_FILE:native_mapi_host>\""
            "NATIVE_MAPI_WIDE=\"$<TARGET_FILE:native_mapi_wide>\""
            "NATIVE_MAPI_ANSI=\"$<TARGET_FILE:native_mapi_ansi>\""
            "NATIVE_MAPI_MISSING=\"$<TARGET_FILE:native_mapi_missing>\"")
        add_dependencies(native_mapi_tests native_mapi_host native_mapi_wide native_mapi_ansi native_mapi_missing)
        add_test(NAME native_mapi COMMAND native_mapi_tests -silent)
        set_tests_properties(native_mapi PROPERTIES TIMEOUT 180 ENVIRONMENT
            "SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native_mapi.ini")
    endif()
    qt_add_executable(archive_open_options_tests tests/archive/archive_open_options_tests.cpp)
    target_link_libraries(archive_open_options_tests PRIVATE sevenzip_location)
    target_include_directories(archive_open_options_tests PRIVATE src tests)
    target_link_libraries(archive_open_options_tests PRIVATE Qt6::Test)
    if(SEVENZIP_CODEC_MODULE)
        target_compile_definitions(archive_open_options_tests PRIVATE
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\"")
    endif()
    add_test(NAME archive_open_options COMMAND archive_open_options_tests -silent)
    qt_add_executable(archive_layer_chain_tests tests/archive/archive_layer_chain_tests.cpp)
    target_link_libraries(archive_layer_chain_tests PRIVATE sevenzip_location)
    target_include_directories(archive_layer_chain_tests PRIVATE src tests)
    target_link_libraries(archive_layer_chain_tests PRIVATE Qt6::Test)
    add_dependencies(archive_layer_chain_tests sevenzip_ext_runtime sevenzip_mbr_runtime)
    target_compile_definitions(archive_layer_chain_tests PRIVATE
        LAYER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/layers"
        SEVENZIP_EXT_PYTHON="${Python3_EXECUTABLE}"
        SEVENZIP_EXT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/ext/support/ext_fixture.py")
    if(SEVENZIP_CODEC_MODULE)
        target_compile_definitions(archive_layer_chain_tests PRIVATE
            SEVENZIP_CODEC_MODULE="${SEVENZIP_CODEC_MODULE}")
    endif()
    add_test(NAME archive_layer_chain COMMAND archive_layer_chain_tests -silent)
    set_tests_properties(archive_layer_chain PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(archive_layer_lifetime_tests tests/archive/archive_layer_lifetime_tests.cpp)
    target_link_libraries(archive_layer_lifetime_tests PRIVATE sevenzip_location)
    target_include_directories(archive_layer_lifetime_tests PRIVATE src tests)
    target_link_libraries(archive_layer_lifetime_tests PRIVATE Qt6::Test)
    if(SEVENZIP_CODEC_MODULE)
        target_compile_definitions(archive_layer_lifetime_tests PRIVATE
            SEVENZIP_CODEC_MODULE="${SEVENZIP_CODEC_MODULE}")
    endif()
    add_test(NAME archive_layer_lifetime COMMAND archive_layer_lifetime_tests -silent)
    set_tests_properties(archive_layer_lifetime PROPERTIES TIMEOUT 45
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(archive_readonly_view_tests tests/archive/archive_readonly_view_tests.cpp)
    target_link_libraries(archive_readonly_view_tests PRIVATE sevenzip_location)
    target_include_directories(archive_readonly_view_tests PRIVATE src tests)
    target_link_libraries(archive_readonly_view_tests PRIVATE Qt6::Test)
    add_dependencies(archive_readonly_view_tests sevenzip_linux_frames_runtime)
    add_test(NAME archive_readonly_view COMMAND archive_readonly_view_tests -silent)
    qt_add_executable(archive_mount_device_tests tests/mount/archive_mount_device_tests.cpp tests/formats/exfat/support/exfat_fixture.cpp)
    target_link_libraries(archive_mount_device_tests PRIVATE sevenzip_location)
    target_include_directories(archive_mount_device_tests PRIVATE src tests)
    target_link_libraries(archive_mount_device_tests PRIVATE Qt6::Test)
    target_compile_definitions(archive_mount_device_tests PRIVATE
        SEVENZIP_SMB_SERVER="$<TARGET_FILE:sevenzip_smb_server>")
    add_dependencies(archive_mount_device_tests sevenzip_exfat_runtime sevenzip_smb_server)
    add_test(NAME archive_mount_device COMMAND archive_mount_device_tests -silent)
    set_tests_properties(archive_mount_device PROPERTIES TIMEOUT 120)
    qt_add_executable(native_mount_client_tests tests/mount/native_mount_client_tests.cpp)
    target_link_libraries(native_mount_client_tests PRIVATE sevenzip_native_client Qt6::Test)
    target_compile_definitions(native_mount_client_tests PRIVATE
        SEVENZIP_SMB_PROBE="$<TARGET_FILE:smb2_mount_probe>")
    add_dependencies(native_mount_client_tests smb2_mount_probe)
    add_test(NAME native_mount_client COMMAND native_mount_client_tests -silent)
    set_tests_properties(native_mount_client PROPERTIES TIMEOUT 150)
    qt_add_executable(smb_readonly_server_tests tests/mount/smb_readonly_server_tests.cpp)
    target_link_libraries(smb_readonly_server_tests PRIVATE sevenzip_smb sevenzip_smb2_facade smb2 Qt6::Test Qt6::Network)
    if(SEVENZIP_ENABLE_DEPENDENCY_MODULES)
        sevenzip_configure_module_cache(smb_readonly_server_tests)
    endif()
    target_compile_definitions(smb_readonly_server_tests PRIVATE
        SEVENZIP_SMB_PROBE="$<TARGET_FILE:smb2_mount_probe>")
    add_dependencies(smb_readonly_server_tests smb2_mount_probe)
    add_test(NAME smb_readonly_server COMMAND smb_readonly_server_tests -silent)
    set_tests_properties(smb_readonly_server PROPERTIES TIMEOUT 60)
    if(WIN32)
        add_dependencies(smb_readonly_server_tests sevenzip_smb_server_smb_runtime)
    endif()
    qt_add_executable(mount_extraction_prompt_tests tests/mount/mount_extraction_prompt_tests.cpp)
    target_link_libraries(mount_extraction_prompt_tests PRIVATE sevenzip_layout_inspection sevenzip_location)
    target_include_directories(mount_extraction_prompt_tests PRIVATE tests)
    target_link_libraries(mount_extraction_prompt_tests PRIVATE sevenzip_mount_ui Qt6::Test)
    add_dependencies(mount_extraction_prompt_tests sevenzip_smb_server)
    add_test(NAME mount_extraction_prompt COMMAND mount_extraction_prompt_tests -silent)
    set_tests_properties(mount_extraction_prompt PROPERTIES TIMEOUT 20
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    if(APPLE)
        set_property(TEST mount_extraction_prompt PROPERTY ENVIRONMENT
            "QT_QPA_PLATFORM=cocoa;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endif()
    qt_add_executable(mount_session_tests tests/mount/mount_session_tests.cpp)
    target_include_directories(mount_session_tests PRIVATE tests)
    target_link_libraries(mount_session_tests PRIVATE sevenzip_location sevenzip_mount_ui Qt6::Test)
    add_test(NAME mount_session COMMAND mount_session_tests -silent)
    set_tests_properties(mount_session PROPERTIES TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(archive_readonly_view PROPERTIES TIMEOUT 45
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(UNIX)
        qt_add_executable(archive_mount_transport_tests tests/mount/archive_mount_transport_tests.cpp)
        target_link_libraries(archive_mount_transport_tests PRIVATE sevenzip_location sevenzip_widget_dialog_fixtures)
        target_include_directories(archive_mount_transport_tests PRIVATE src tests)
        target_link_libraries(archive_mount_transport_tests PRIVATE smb2 Qt6::Test)
        if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_libraries(archive_mount_transport_tests PRIVATE sevenzip_mount_ui)
            target_link_libraries(archive_mount_transport_tests PRIVATE sevenzip_layout_inspection)
        endif()
        target_compile_definitions(archive_mount_transport_tests PRIVATE
            SEVENZIP_SMB_SERVER="$<TARGET_FILE:sevenzip_smb_server>")
        add_dependencies(archive_mount_transport_tests sevenzip_smb_server sevenzip_linux_frames_runtime)
        add_test(NAME archive_mount_transport COMMAND archive_mount_transport_tests -silent)
        set_tests_properties(archive_mount_transport PROPERTIES TIMEOUT 60
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endif()
    if(APPLE)
        qt_add_executable(archive_mount_media_tests
            tests/mount/archive_mount_media_tests.cpp
            tests/support/native_media_consumer.mm)
        target_link_libraries(archive_mount_media_tests PRIVATE sevenzip_location)
        set_source_files_properties(tests/support/native_media_consumer.mm PROPERTIES
            COMPILE_OPTIONS "-fobjc-arc")
        target_include_directories(archive_mount_media_tests PRIVATE src tests)
        target_link_libraries(archive_mount_media_tests PRIVATE Qt6::Test "-framework PDFKit" "-framework AVFoundation" "-framework CoreVideo" "-framework CoreMedia")
        target_compile_definitions(archive_mount_media_tests PRIVATE
            SEVENZIP_SMB_SERVER="$<TARGET_FILE:sevenzip_smb_server>")
        add_dependencies(archive_mount_media_tests sevenzip_smb_server)
        add_test(NAME archive_mount_media COMMAND archive_mount_media_tests -silent)
        set_tests_properties(archive_mount_media PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=cocoa")
    endif()
