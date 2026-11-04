target_sources(sevenzip_encrypted_dmg PRIVATE
    src/formats/sparse-image/sparseimagestream.cpp
    src/formats/sparse-image/sparsebundlestream.cpp
    src/formats/sparse-image/bandmap.c)

if(BUILD_TESTING)
    qt_add_executable(sparse_image_tests
        tests/formats/sparse-image/sparse_image_tests.cpp
        tests/formats/sparse-image/sparsefixtures.cpp
        tests/formats/encrypted-dmg/dmgfixtures.cpp
        tests/formats/exfat/support/exfat_fixture.cpp
        src/formats/encrypted-dmg/dmgdecryptstream.cpp
        src/formats/sparse-image/sparseimagestream.cpp
        src/formats/sparse-image/sparsebundlestream.cpp
        src/formats/sparse-image/bandmap.c
        ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/MyXml.cpp)
    set_target_properties(sparse_image_tests PROPERTIES AUTOMOC ON)
    target_include_directories(sparse_image_tests PRIVATE src tests ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(sparse_image_tests PRIVATE sevenzip_panel Qt6::Test
        sevenzip_dmg_crypto sevenzip_des_core)
    get_target_property(sparse_test_definitions encrypted_dmg_tests COMPILE_DEFINITIONS)
    target_compile_definitions(sparse_image_tests PRIVATE ${sparse_test_definitions})
    target_compile_definitions(sparse_image_tests PRIVATE
        "SEVENZIP_SPARSE_FIXTURES=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sparse-images\"")
    add_dependencies(sparse_image_tests sevenzip_encrypted_dmg_runtime sevenzip_hfs_runtime sevenzip_exfat_runtime)
    add_test(NAME sparse_image COMMAND sparse_image_tests -silent)
    set_tests_properties(sparse_image PROPERTIES TIMEOUT 120)
    qt_add_executable(apfs_directory_tests tests/formats/apfs/apfs_directory_tests.cpp)
    set_target_properties(apfs_directory_tests PROPERTIES AUTOMOC ON)
    target_include_directories(apfs_directory_tests PRIVATE src tests ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(apfs_directory_tests PRIVATE sevenzip_panel Qt6::Test)
    get_target_property(apfs_directory_definitions sparse_image_tests COMPILE_DEFINITIONS)
    target_compile_definitions(apfs_directory_tests PRIVATE ${apfs_directory_definitions})
    add_test(NAME apfs_directory COMMAND apfs_directory_tests -silent)
    set_tests_properties(apfs_directory PROPERTIES TIMEOUT 120)
    qt_add_executable(sparse_image_ui_tests
        tests/formats/sparse-image/sparse_image_ui_tests.cpp
        tests/formats/sparse-image/sparsefixtures.cpp
        tests/formats/encrypted-dmg/dmgfixtures.cpp
        tests/formats/exfat/support/exfat_fixture.cpp)
    set_target_properties(sparse_image_ui_tests PROPERTIES AUTOMOC ON)
    target_include_directories(sparse_image_ui_tests PRIVATE src tests ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(sparse_image_ui_tests PRIVATE sevenzip_window sevenzip_widget_dialog_fixtures sevenzip_layout_inspection Qt6::Test
        sevenzip_dmg_crypto sevenzip_des_core)
    get_target_property(sparse_ui_definitions sparse_image_tests COMPILE_DEFINITIONS)
    target_compile_definitions(sparse_image_ui_tests PRIVATE ${sparse_ui_definitions})
    add_dependencies(sparse_image_ui_tests sevenzip_encrypted_dmg_runtime sevenzip_hfs_runtime sevenzip_exfat_runtime)
    add_test(NAME sparse_image_ui COMMAND sparse_image_ui_tests -silent)
    set_tests_properties(sparse_image_ui PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(APPLE)
        set_property(TEST sparse_image_ui PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=cocoa")
        add_test(NAME sparse_image_ui_quick COMMAND sparse_image_ui_tests -silent
            browseAndCopy:native-sparse-256
            browseAndCopy:native-sparsebundle-0
            "browseApfsDirectories:en nested"
            "browseApfsDirectories:ru symlink"
            "browseApfsDirectories:en file-with-attributes"
            "browseApfsDirectories:ru deep-file"
            damagedWarning:en-decline damagedWarning:ru-read-only)
        set_tests_properties(sparse_image_ui_quick PROPERTIES TIMEOUT 60 ENVIRONMENT
            "QT_QPA_PLATFORM=cocoa;SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/sparse-image-ui-quick.ini;SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
        qt_add_executable(sparse_image_network_tests
            tests/formats/sparse-image/sparse_image_network_tests.cpp
            tests/formats/sparse-image/sparsefixtures.cpp
            tests/formats/encrypted-dmg/dmgfixtures.cpp
            tests/formats/exfat/support/exfat_fixture.cpp)
        set_target_properties(sparse_image_network_tests PROPERTIES AUTOMOC ON)
        target_include_directories(sparse_image_network_tests PRIVATE src tests)
        target_compile_definitions(sparse_image_network_tests PRIVATE ${sparse_ui_definitions})
        target_link_libraries(sparse_image_network_tests PRIVATE sevenzip_location Qt6::Test
            sevenzip_dmg_crypto sevenzip_des_core)
        add_dependencies(sparse_image_network_tests sevenzip_smb_server sevenzip_native_mount
            sevenzip_encrypted_dmg_runtime sevenzip_hfs_runtime)
        add_test(NAME sparse_image_network COMMAND sparse_image_network_tests -silent)
        set_tests_properties(sparse_image_network PROPERTIES TIMEOUT 120)
    endif()
    set_property(TEST sparse_image_ui APPEND PROPERTY ENVIRONMENT
        "SEVENZIP_SETTINGS_PATH=${CMAKE_BINARY_DIR}/sparse-image-ui.ini"
        "SEVENZIP_UI_REPORT_DIR=${CMAKE_BINARY_DIR}/ui-reports")
endif()
