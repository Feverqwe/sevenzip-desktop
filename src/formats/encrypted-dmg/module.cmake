include(src/dependencies/apfscrypto/module.cmake)
add_library(sevenzip_dmg_crypto STATIC
    src/formats/encrypted-dmg/dmgcrypto.cpp
    src/crypto/hmac/HmacSha1.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Crypto/Pbkdf2HmacSha1.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Aes.c ${SEVENZIP_7ZIP_SOURCE_DIR}/C/AesOpt.c
    ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Sha1.c ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Sha1Opt.c
    ${SEVENZIP_7ZIP_SOURCE_DIR}/C/CpuArch.c)
target_include_directories(sevenzip_dmg_crypto PUBLIC src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
set_target_properties(sevenzip_dmg_crypto PROPERTIES AUTOMOC OFF AUTOUIC OFF
    POSITION_INDEPENDENT_CODE ON)
add_library(sevenzip_encrypted_dmg MODULE
    src/formats/encrypted-dmg/encrypteddmghandler.cpp
    src/formats/encrypted-dmg/dmgdecryptstream.cpp
    src/formats/encrypted-dmg/passwordguid.cpp
    src/formats/common/moduleexports.cpp src/formats/common/HandlerCont.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Archive/ArchiveExports.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/StreamUtils.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/StreamObjects.cpp
    src/archive/streams/LimitedStreams.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/ProgressUtils.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/PropId.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Compress/CopyCoder.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/PropVariant.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/MyWindows.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/MyString.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/MyVector.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/IntToString.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/StringConvert.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/UTFConvert.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Alloc.c)
target_include_directories(sevenzip_encrypted_dmg PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
target_compile_definitions(sevenzip_encrypted_dmg PRIVATE SEVENZIP_CONTAINER_ONLY)
target_link_libraries(sevenzip_encrypted_dmg PRIVATE sevenzip_dmg_crypto sevenzip_des_core)
set_target_properties(sevenzip_encrypted_dmg PROPERTIES AUTOMOC OFF AUTOUIC OFF PREFIX "")
if(UNIX AND NOT APPLE)
    target_link_options(sevenzip_encrypted_dmg PRIVATE "-Wl,-Bsymbolic")
endif()
if(WIN32)
    target_sources(sevenzip_encrypted_dmg PRIVATE src/formats/common/moduleexports.def)
    target_link_libraries(sevenzip_encrypted_dmg PRIVATE oleaut32)
endif()
function(sevenzip_deploy_encrypted_dmg)
    add_custom_target(sevenzip_encrypted_dmg_runtime ALL
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SEVENZIP_CODEC_RUNTIME_DIR}/Formats"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:sevenzip_encrypted_dmg>"
            "${SEVENZIP_CODEC_RUNTIME_DIR}/Formats/$<TARGET_FILE_NAME:sevenzip_encrypted_dmg>"
        DEPENDS sevenzip_encrypted_dmg VERBATIM)
    add_dependencies(sevenzip_desktop sevenzip_encrypted_dmg_runtime)
endfunction()

if(BUILD_TESTING)
    qt_add_executable(encrypted_dmg_tests
        tests/formats/encrypted-dmg/encrypted_dmg_tests.cpp
        tests/formats/encrypted-dmg/dmgfixtures.cpp
        tests/formats/exfat/support/exfat_fixture.cpp
        src/formats/encrypted-dmg/dmgdecryptstream.cpp)
    set_target_properties(encrypted_dmg_tests PROPERTIES AUTOMOC ON)
    target_include_directories(encrypted_dmg_tests PRIVATE src tests ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(encrypted_dmg_tests PRIVATE sevenzip_panel Qt6::Test
        sevenzip_dmg_crypto sevenzip_des_core)
    target_compile_definitions(encrypted_dmg_tests PRIVATE
        "SEVENZIP_DMG_MODULE=\"$<TARGET_FILE:sevenzip_encrypted_dmg>\""
        "SEVENZIP_DMG_HFS_MODULE=\"$<TARGET_FILE:sevenzip_hfs>\""
        "SEVENZIP_DMG_EXFAT_MODULE=\"$<TARGET_FILE:sevenzip_exfat>\""
        "SEVENZIP_DMG_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_DMG_FIXTURES=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/encrypted-dmg\"")
    add_dependencies(encrypted_dmg_tests sevenzip_encrypted_dmg_runtime sevenzip_hfs_runtime sevenzip_exfat_runtime)
    add_test(NAME encrypted_dmg COMMAND encrypted_dmg_tests -silent)
    set_tests_properties(encrypted_dmg PROPERTIES TIMEOUT 120)
    qt_add_executable(encrypted_dmg_ui_tests tests/formats/encrypted-dmg/encrypted_dmg_ui_tests.cpp)
    set_target_properties(encrypted_dmg_ui_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(encrypted_dmg_ui_tests PRIVATE sevenzip_window sevenzip_widget_dialog_fixtures Qt6::Test)
    target_include_directories(encrypted_dmg_ui_tests PRIVATE src tests)
    get_target_property(dmg_test_definitions encrypted_dmg_tests COMPILE_DEFINITIONS)
    target_compile_definitions(encrypted_dmg_ui_tests PRIVATE ${dmg_test_definitions})
    add_dependencies(encrypted_dmg_ui_tests sevenzip_encrypted_dmg_runtime sevenzip_hfs_runtime sevenzip_exfat_runtime)
    add_dependencies(encrypted_dmg_ui_tests sevenzip_smb_server)
    add_test(NAME encrypted_dmg_ui COMMAND encrypted_dmg_ui_tests -silent)
    set_tests_properties(encrypted_dmg_ui PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(APPLE)
        set_property(TEST encrypted_dmg_ui PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=cocoa")
        qt_add_executable(encrypted_dmg_network_tests tests/formats/encrypted-dmg/encrypted_dmg_network_tests.cpp)
        set_target_properties(encrypted_dmg_network_tests PROPERTIES AUTOMOC ON)
        target_include_directories(encrypted_dmg_network_tests PRIVATE src tests)
        target_compile_definitions(encrypted_dmg_network_tests PRIVATE ${dmg_test_definitions})
        target_link_libraries(encrypted_dmg_network_tests PRIVATE sevenzip_location Qt6::Test)
        add_dependencies(encrypted_dmg_network_tests sevenzip_smb_server sevenzip_native_mount sevenzip_encrypted_dmg_runtime sevenzip_exfat_runtime)
        add_test(NAME encrypted_dmg_network COMMAND encrypted_dmg_network_tests -silent)
        set_tests_properties(encrypted_dmg_network PROPERTIES TIMEOUT 180)
    endif()
endif()
