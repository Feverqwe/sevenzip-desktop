if(BUILD_TESTING)
    qt_add_executable(startup_path_tests tests/filemanager/startup_path_tests.cpp)
    target_link_libraries(startup_path_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(startup_path_tests PRIVATE sevenzip_window)
    target_include_directories(startup_path_tests PRIVATE src tests)
    target_link_libraries(startup_path_tests PRIVATE Qt6::Test)
    add_test(NAME startup_path COMMAND startup_path_tests -silent)
    set_tests_properties(startup_path PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/startup-path-settings.ini"
        TIMEOUT 30)
endif()

if(BUILD_TESTING)
    qt_add_executable(archive_recovery_ui_tests tests/filemanager/archive_recovery_ui_tests.cpp)
    target_link_libraries(archive_recovery_ui_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(archive_recovery_ui_tests PRIVATE sevenzip_window)
    target_include_directories(archive_recovery_ui_tests PRIVATE src tests)
    target_link_libraries(archive_recovery_ui_tests PRIVATE Qt6::Test)
    if(SEVENZIP_CODEC_MODULE)
        target_compile_definitions(archive_recovery_ui_tests PRIVATE
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\"")
    endif()
    add_test(NAME archive_recovery_ui COMMAND archive_recovery_ui_tests -silent)
    set_tests_properties(archive_recovery_ui PROPERTIES TIMEOUT 45 RESOURCE_LOCK sevenzip_ui_artifacts
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    if(APPLE)
        set_property(TEST archive_recovery_ui PROPERTY ENVIRONMENT
            "QT_QPA_PLATFORM=cocoa;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endif()
    qt_add_executable(filesystem_read_ui_tests tests/filemanager/filesystem_read_ui_tests.cpp tests/formats/exfat/support/exfat_fixture.cpp)
    target_link_libraries(filesystem_read_ui_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(filesystem_read_ui_tests PRIVATE sevenzip_window)
    target_include_directories(filesystem_read_ui_tests PRIVATE src tests)
    target_link_libraries(filesystem_read_ui_tests PRIVATE Qt6::Test)
    target_compile_definitions(filesystem_read_ui_tests PRIVATE
        "SEVENZIP_SQUASHFS_MODULE=\"$<TARGET_FILE:sevenzip_squashfs>\""
        "SEVENZIP_SQUASHFS_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/squashfs/support/squashfs_fixture.py\""
        "SEVENZIP_EXFAT_MODULE=\"$<TARGET_FILE:sevenzip_exfat>\""
        "SEVENZIP_FAT_MODULE=\"$<TARGET_FILE:sevenzip_fat>\""
        "SEVENZIP_FAT_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/fat/support/fat_fixture.py\""
        "SEVENZIP_EXFAT_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_HFS_MODULE=\"$<TARGET_FILE:sevenzip_hfs>\""
        "SEVENZIP_NTFS_MODULE=\"$<TARGET_FILE:sevenzip_ntfs>\""
        "SEVENZIP_NTFS_CORE=\"$<TARGET_FILE:sevenzip_ntfs>\""
        "SEVENZIP_NTFS_PYTHON=\"${Python3_EXECUTABLE}\""
        "SEVENZIP_NTFS_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/ntfs/support/ntfs_fixture.py\""
        "SEVENZIP_EXT_MODULE=\"$<TARGET_FILE:sevenzip_ext>\""
        "SEVENZIP_EXT_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/ext/support/ext_fixture.py\""
        "SEVENZIP_HFS_PYTHON=\"${Python3_EXECUTABLE}\""
        "SEVENZIP_EXT_RECOVERY_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/ext/support/ext_recovery_fixture.py\""
        "SEVENZIP_HFS_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/hfs/support/hfs_fixture.py\"")
    add_dependencies(filesystem_read_ui_tests sevenzip_exfat sevenzip_fat_runtime sevenzip_hfs_runtime sevenzip_ntfs_runtime sevenzip_ext_runtime sevenzip_squashfs_runtime)
    add_test(NAME filesystem_squashfs_read_ui COMMAND filesystem_read_ui_tests -silent confirmsPartialSquashfsAttributes)
    add_test(NAME filesystem_squashfs_directory_ui COMMAND filesystem_read_ui_tests -silent confirmsPartialSquashfsDirectory)
    add_test(NAME filesystem_squashfs_inode_ui COMMAND filesystem_read_ui_tests -silent confirmsPartialSquashfsInodes)
    add_test(NAME filesystem_squashfs_index_ui COMMAND filesystem_read_ui_tests -silent confirmsPartialSquashfsIndexes)
    add_test(NAME filesystem_read_ui COMMAND filesystem_read_ui_tests -silent deletedRecoveryConsent confirmsAndCopiesFlaggedVolume)
    add_test(NAME filesystem_read_ui_ru COMMAND filesystem_read_ui_tests -silent confirmsAndCopiesFlaggedVolumeRu)
    add_test(NAME filesystem_hfs_read_ui COMMAND filesystem_read_ui_tests -silent
        confirmsAndCopiesFlaggedVolume:en-cancel-unclean
        confirmsAndCopiesFlaggedVolume:en-accept-wrapper-no-slock
        confirmsAndCopiesFlaggedVolume:en-accept-unclean
        confirmsAndCopiesFlaggedVolume:en-accept-free-overflow
        confirmsAndCopiesFlaggedVolumeRu:ru-cancel-unclean
        confirmsAndCopiesFlaggedVolumeRu:ru-accept-wrapper-no-slock
        confirmsAndCopiesFlaggedVolumeRu:ru-accept-unclean
        confirmsAndCopiesFlaggedVolumeRu:ru-accept-free-overflow)
    add_test(NAME filesystem_ext_read_ui COMMAND filesystem_read_ui_tests -silent confirmsLimitedExt)
    add_test(NAME filesystem_ext_read_ui_ru COMMAND filesystem_read_ui_tests -silent confirmsLimitedExtRu)
    add_test(NAME filesystem_security_read_ui COMMAND filesystem_read_ui_tests -silent confirmsPartialSecurityStream)
    add_test(NAME filesystem_fat_read_ui COMMAND filesystem_read_ui_tests -silent confirmsLimitedFat)
    add_test(NAME filesystem_fat_directory_ui COMMAND filesystem_read_ui_tests -silent confirmsPartialFatDirectory)
    add_test(NAME filesystem_fat_file_ui COMMAND filesystem_read_ui_tests -silent confirmsPartialFatFile)
    add_test(NAME filesystem_exfat_directory_ui COMMAND filesystem_read_ui_tests -silent confirmsPartialExfatDirectories)
    add_test(NAME filesystem_exfat_recovery_ui COMMAND filesystem_read_ui_tests -silent confirmsExfatRecovery)
    add_test(NAME filesystem_ext_recovery_ui COMMAND filesystem_read_ui_tests -silent confirmsExtRecovery)
    add_test(NAME filesystem_ntfs_recovery_ui COMMAND filesystem_read_ui_tests -silent confirmsNtfsRecovery)
    add_test(NAME filesystem_ntfs_failure_ui COMMAND filesystem_read_ui_tests -silent explainsNtfsFailure)
    set_tests_properties(filesystem_exfat_recovery_ui filesystem_ext_recovery_ui filesystem_ntfs_recovery_ui filesystem_ntfs_failure_ui PROPERTIES TIMEOUT 60 RESOURCE_LOCK sevenzip_ui_artifacts
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    if(APPLE)
        set_property(TEST filesystem_exfat_recovery_ui filesystem_ext_recovery_ui filesystem_ntfs_recovery_ui filesystem_ntfs_failure_ui PROPERTY ENVIRONMENT
            "QT_QPA_PLATFORM=cocoa;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endif()
    set_tests_properties(filesystem_exfat_directory_ui filesystem_squashfs_index_ui filesystem_squashfs_inode_ui filesystem_squashfs_directory_ui filesystem_squashfs_read_ui filesystem_read_ui filesystem_read_ui_ru filesystem_hfs_read_ui filesystem_ext_read_ui filesystem_ext_read_ui_ru filesystem_security_read_ui filesystem_fat_read_ui filesystem_fat_directory_ui filesystem_fat_file_ui PROPERTIES TIMEOUT 60 RESOURCE_LOCK sevenzip_ui_artifacts
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    if(APPLE)
        set_property(TEST filesystem_exfat_directory_ui filesystem_squashfs_index_ui filesystem_squashfs_inode_ui filesystem_squashfs_directory_ui filesystem_squashfs_read_ui filesystem_read_ui filesystem_read_ui_ru filesystem_hfs_read_ui filesystem_ext_read_ui filesystem_ext_read_ui_ru filesystem_security_read_ui filesystem_fat_read_ui filesystem_fat_directory_ui filesystem_fat_file_ui PROPERTY ENVIRONMENT
            "QT_QPA_PLATFORM=cocoa;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endif()
    qt_add_executable(associated_window_tests tests/filemanager/associated_window_tests.cpp)
    target_link_libraries(associated_window_tests PRIVATE sevenzip_widget_dialog_fixtures)
    target_link_libraries(associated_window_tests PRIVATE sevenzip_window sevenzip_file_manager_entry)
    target_include_directories(associated_window_tests PRIVATE src tests)
    target_link_libraries(associated_window_tests PRIVATE Qt6::Test)
    target_compile_definitions(associated_window_tests PRIVATE
        "SLOW_CODEC_FIXTURE=\"$<TARGET_FILE:slow_codec_fixture>\"")
    add_dependencies(associated_window_tests slow_codec_fixture)
    add_test(NAME associated_window COMMAND associated_window_tests -silent)
    set_tests_properties(associated_window PROPERTIES TIMEOUT 30 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/associated-window.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    if(APPLE)
        add_library(mac_document_main OBJECT src/app/main.cpp)
        target_compile_definitions(mac_document_main PRIVATE main=SevenzipDesktopMain)
        target_include_directories(mac_document_main PRIVATE src)
        target_link_libraries(mac_document_main PRIVATE sevenzip_window Qt6::Test)
        qt_add_executable(mac_document_entry_probe
            tests/support/mac_document_entry_probe.cpp
            $<TARGET_OBJECTS:mac_document_main>)
        target_link_libraries(mac_document_entry_probe PRIVATE sevenzip_window sevenzip_gui_entry sevenzip_file_manager_entry)
        target_include_directories(mac_document_entry_probe PRIVATE src tests)
        target_link_libraries(mac_document_entry_probe PRIVATE Qt6::Test)
        add_executable(mac_document_launcher tests/support/mac_document_launcher.mm)
        target_link_libraries(mac_document_launcher PRIVATE "-framework AppKit")
        add_test(NAME macos_document_startup COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/mac_document_startup_test.py"
            --probe "$<TARGET_FILE:mac_document_entry_probe>"
            --launcher "$<TARGET_FILE:mac_document_launcher>")
        set_tests_properties(macos_document_startup PROPERTIES TIMEOUT 90
            RESOURCE_LOCK sevenzip_native_ui)
    endif()
    qt_add_executable(archive_metadata_ui_tests tests/filemanager/archive_metadata_ui_tests.cpp)
    target_link_libraries(archive_metadata_ui_tests PRIVATE sevenzip_window)
    qt_add_executable(device_ui_tests tests/devices/device_ui_tests.cpp tests/formats/exfat/support/exfat_fixture.cpp)
    target_link_libraries(device_ui_tests PRIVATE sevenzip_layout_inspection)
    target_link_libraries(device_ui_tests PRIVATE sevenzip_window)
    target_include_directories(device_ui_tests PRIVATE src tests)
    target_link_libraries(device_ui_tests PRIVATE Qt6::Test)
    add_dependencies(device_ui_tests sevenzip_exfat_runtime)
    target_compile_definitions(device_ui_tests PRIVATE
        "DEVICE_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/devices\"")
    if(SEVENZIP_CODEC_MODULE)
        target_compile_definitions(device_ui_tests PRIVATE
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\"")
    endif()
    add_test(NAME device_ui COMMAND device_ui_tests -silent)
    qt_add_executable(fscrypt_key_ui_tests tests/crypto/fscrypt_key_ui_tests.cpp)
    target_link_libraries(fscrypt_key_ui_tests PRIVATE sevenzip_layout_inspection sevenzip_widget_dialog_fixtures)
    target_link_libraries(fscrypt_key_ui_tests PRIVATE sevenzip_window)
    target_include_directories(fscrypt_key_ui_tests PRIVATE src tests)
    target_link_libraries(fscrypt_key_ui_tests PRIVATE Qt6::Test)
    target_compile_definitions(fscrypt_key_ui_tests PRIVATE
        "FSCRYPT_FIXTURES=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fscrypt\""
        "FSCRYPT_MODULE=\"$<TARGET_FILE:sevenzip_ext>\"")
    if(SEVENZIP_CODEC_MODULE)
        target_compile_definitions(fscrypt_key_ui_tests PRIVATE
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\"")
    endif()
    add_dependencies(fscrypt_key_ui_tests sevenzip_ext_runtime)
    add_test(NAME fscrypt_key_ui COMMAND fscrypt_key_ui_tests -silent)
    set_tests_properties(fscrypt_key_ui PROPERTIES TIMEOUT 90 RESOURCE_LOCK sevenzip_ui_artifacts
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/fscrypt-key-ui.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    if(APPLE)
        set_property(TEST fscrypt_key_ui PROPERTY ENVIRONMENT
            "QT_QPA_PLATFORM=cocoa;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/fscrypt-key-ui.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endif()
    foreach(ext_consumer archive_device_tests device_folder_tests device_ui_tests)
        add_dependencies(${ext_consumer} sevenzip_ext_runtime)
    endforeach()
    set_tests_properties(device_ui PROPERTIES TIMEOUT 90 RESOURCE_LOCK sevenzip_ui_artifacts
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/device-ui.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    if(APPLE)
        set_property(TEST device_ui PROPERTY ENVIRONMENT
            "QT_QPA_PLATFORM=cocoa;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/device-ui.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endif()
    target_include_directories(archive_metadata_ui_tests PRIVATE src tests)
    target_link_libraries(archive_metadata_ui_tests PRIVATE Qt6::Test)
    if(SEVENZIP_CODEC_MODULE)
        if(WIN32)
            add_executable(native_association_process tests/support/native_association_process.cpp)
            qt_add_executable(native_association_tests tests/filemanager/native_association_tests.cpp)
            target_link_libraries(native_association_tests PRIVATE sevenzip_window)
            target_include_directories(native_association_tests PRIVATE src tests)
            target_link_libraries(native_association_tests PRIVATE Qt6::Test)
            target_compile_definitions(native_association_tests PRIVATE
                "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\""
                "SEVENZIP_ASSOC_HELPER=\"$<TARGET_FILE:native_association_process>\"")
            add_dependencies(native_association_tests native_association_process)
            foreach(association_case IN ITEMS handle disk browser workflow)
                if(association_case STREQUAL "handle")
                    set(association_function nativeShellReturnsWaitableProcess)
                elseif(association_case STREQUAL "disk")
                    set(association_function diskOpenKeepsOriginalAndDoesNotWait)
                elseif(association_case STREQUAL "browser")
                    set(association_function temporaryBrowserClosesHandleAndRetainsProtection)
                else()
                    set(association_function archiveOpenCleansCopyAfterNativeCompletion)
                endif()
                add_test(NAME native_association_${association_case}
                    COMMAND native_association_tests ${association_function} -silent)
                set_tests_properties(native_association_${association_case} PROPERTIES TIMEOUT 90
                    RESOURCE_LOCK sevenzip_native_ui
                    ENVIRONMENT "QT_QPA_PLATFORM=windows;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native-association-${association_case}.ini")
            endforeach()
        endif()
        qt_add_executable(file_manager_entry_probe tests/support/file_manager_entry_probe.cpp)
        target_link_libraries(file_manager_entry_probe PRIVATE sevenzip_window sevenzip_file_manager_entry)
        target_include_directories(file_manager_entry_probe PRIVATE src tests)
        target_link_libraries(file_manager_entry_probe PRIVATE Qt6::Test)
        add_library(file_manager_ram_source_fixture OBJECT
            src/filemanager/FM.cpp src/filemanager/panels/PanelItemOpen.cpp)
        target_include_directories(file_manager_ram_source_fixture PRIVATE src tests)
        target_link_libraries(file_manager_ram_source_fixture PRIVATE sevenzip_window Qt6::Test)
        target_compile_definitions(file_manager_ram_source_fixture PRIVATE
            GetRamSize=FixtureGetRamSize)
        qt_add_executable(file_manager_ram_probe
            tests/support/file_manager_ram_probe.cpp
            $<TARGET_OBJECTS:file_manager_ram_source_fixture>)
        target_link_libraries(file_manager_ram_probe PRIVATE sevenzip_window sevenzip_file_manager_entry)
        target_include_directories(file_manager_ram_probe PRIVATE src tests)
        target_link_libraries(file_manager_ram_probe PRIVATE Qt6::Test)
        qt_add_executable(file_manager_ram_tests tests/ui/file_manager_ram_tests.cpp)
        target_include_directories(file_manager_ram_tests PRIVATE src tests)
        target_link_libraries(file_manager_ram_tests PRIVATE sevenzip_window Qt6::Test)
        target_compile_definitions(file_manager_ram_tests PRIVATE
            "SEVENZIP_RAM_PROBE=\"$<TARGET_FILE:file_manager_ram_probe>\"")
        add_dependencies(file_manager_ram_tests file_manager_ram_probe)
        add_test(NAME file_manager_ram COMMAND file_manager_ram_tests -silent)
        set_tests_properties(file_manager_ram PROPERTIES TIMEOUT 120 ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/file-manager-ram.ini")
        if(NOT WIN32)
            qt_add_executable(unix_gui_entry_tests tests/ui/unix_gui_entry_tests.cpp)
            target_include_directories(unix_gui_entry_tests PRIVATE src tests)
            target_link_libraries(unix_gui_entry_tests PRIVATE sevenzip_common Qt6::Test)
            target_compile_definitions(unix_gui_entry_tests PRIVATE
                "SEVENZIP_NATIVE_GUI=\"$<TARGET_FILE_DIR:sevenzip_desktop>/7zG\"")
            if(SEVENZIP_CODEC_MODULE)
                target_compile_definitions(unix_gui_entry_tests PRIVATE
                    "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\"")
            endif()
            add_dependencies(unix_gui_entry_tests sevenzip_desktop)
            add_test(NAME unix_gui_entry COMMAND unix_gui_entry_tests -silent)
            set_tests_properties(unix_gui_entry PROPERTIES TIMEOUT 180 ENVIRONMENT
                "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/unix-gui-entry.ini")
        endif()
        qt_add_executable(file_manager_startup_tests tests/filemanager/file_manager_startup_tests.cpp)
        target_link_libraries(file_manager_startup_tests PRIVATE sevenzip_window)
        target_include_directories(file_manager_startup_tests PRIVATE src tests)
        target_link_libraries(file_manager_startup_tests PRIVATE Qt6::Test)
        target_compile_definitions(file_manager_startup_tests PRIVATE
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\""
            "SEVENZIP_FM_PROBE=\"$<TARGET_FILE:file_manager_entry_probe>\"")
        add_dependencies(file_manager_startup_tests file_manager_entry_probe)
        if(WIN32)
            target_compile_definitions(file_manager_startup_tests PRIVATE
                "SEVENZIP_NATIVE_FM=\"$<TARGET_FILE_DIR:sevenzip_desktop>/7zFM.exe\"")
            add_dependencies(file_manager_startup_tests sevenzip_desktop)
        endif()
        add_test(NAME file_manager_startup COMMAND file_manager_startup_tests -silent)
        set_tests_properties(file_manager_startup PROPERTIES TIMEOUT 90 ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/file-manager-startup.ini")
    endif()
    if(WIN32 AND SEVENZIP_CODEC_MODULE)
        qt_add_executable(native_shell_email_tests tests/filemanager/native_shell_email_tests.cpp)
        target_link_libraries(native_shell_email_tests PRIVATE sevenzip_layout_inspection)
        target_link_libraries(native_shell_email_tests PRIVATE sevenzip_window)
        target_include_directories(native_shell_email_tests PRIVATE src tests)
        target_link_libraries(native_shell_email_tests PRIVATE Qt6::Test)
        target_compile_definitions(native_shell_email_tests PRIVATE
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\""
            "NATIVE_MAPI_HOST=\"$<TARGET_FILE:native_mapi_host>\""
            "NATIVE_MAPI_WIDE=\"$<TARGET_FILE:native_mapi_wide>\""
            "NATIVE_MAPI_MISSING=\"$<TARGET_FILE:native_mapi_missing>\"")
        add_dependencies(native_shell_email_tests native_mapi_host native_mapi_wide native_mapi_missing)
        add_test(NAME native_shell_email COMMAND native_shell_email_tests -silent)
        set_tests_properties(native_shell_email PROPERTIES TIMEOUT 180 RESOURCE_LOCK sevenzip_ui_artifacts
            ENVIRONMENT "SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/native-shell-email.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
    endif()
    if(SEVENZIP_CODEC_MODULE)
        target_compile_definitions(archive_metadata_ui_tests PRIVATE
            "SEVENZIP_CODEC_MODULE=\"${SEVENZIP_CODEC_MODULE}\"")
    endif()
    add_test(NAME archive_metadata_ui COMMAND archive_metadata_ui_tests -silent)
    set_tests_properties(archive_metadata_ui PROPERTIES TIMEOUT 60 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/archive-metadata-ui.ini")
endif()
