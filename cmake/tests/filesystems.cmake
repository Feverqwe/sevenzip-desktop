    add_test(NAME mbr COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/mbr/mbr_tests.py
        ${SEVENZIP_CODEC_RUNTIME_DIR} "$<TARGET_FILE:sevenzip_mbr>"
        "$<TARGET_FILE:sevenzip_fat>")
    set_tests_properties(mbr PROPERTIES TIMEOUT 45)
    qt_add_executable(mbr_stream_tests tests/formats/mbr/mbr_stream_tests.cpp)
    target_include_directories(mbr_stream_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(mbr_stream_tests PRIVATE sevenzip_panel Qt6::Test)
    if(WIN32)
        target_link_libraries(mbr_stream_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(mbr_stream_tests PRIVATE
        "SEVENZIP_MBR_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_MBR_MODULE=\"$<TARGET_FILE:sevenzip_mbr>\""
        "SEVENZIP_MBR_PYTHON=\"${Python3_EXECUTABLE}\""
        "SEVENZIP_MBR_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/mbr/support/mbr_fixture.py\"")
    target_compile_definitions(mbr_stream_tests PRIVATE
        "SEVENZIP_MBR_SUBTYPE_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/mbr/support/mbr_subtypes_fixture.py\"")
    target_compile_definitions(mbr_stream_tests PRIVATE
        "SEVENZIP_MBR_CLASSIFICATION_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/mbr/support/mbr_classification_fixture.py\"")
    add_test(NAME mbr_stream COMMAND mbr_stream_tests -silent)
    set_tests_properties(mbr_stream PROPERTIES TIMEOUT 45)
    add_dependencies(mbr_stream_tests sevenzip_mbr_runtime sevenzip_fat_runtime)
    add_test(NAME apm COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/apm/apm_tests.py
        ${SEVENZIP_CODEC_RUNTIME_DIR})
    set_tests_properties(apm PROPERTIES TIMEOUT 45)
    qt_add_executable(apm_stream_tests tests/formats/apm/apm_stream_tests.cpp)
    target_include_directories(apm_stream_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(apm_stream_tests PRIVATE sevenzip_panel Qt6::Test)
    if(WIN32)
        target_link_libraries(apm_stream_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(apm_stream_tests PRIVATE
        "SEVENZIP_APM_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_APM_PYTHON=\"${Python3_EXECUTABLE}\""
        "SEVENZIP_APM_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/apm/support/apm_fixture.py\"")
    add_test(NAME apm_stream COMMAND apm_stream_tests -silent)
    set_tests_properties(apm_stream PROPERTIES TIMEOUT 30)
    add_test(NAME gpt COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/gpt/gpt_tests.py
        ${SEVENZIP_CODEC_RUNTIME_DIR})
    set_tests_properties(gpt PROPERTIES TIMEOUT 45)
    qt_add_executable(gpt_stream_tests tests/formats/gpt/gpt_stream_tests.cpp)
    target_include_directories(gpt_stream_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(gpt_stream_tests PRIVATE sevenzip_panel Qt6::Test)
    if(WIN32)
        target_link_libraries(gpt_stream_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(gpt_stream_tests PRIVATE
        "SEVENZIP_GPT_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_GPT_PYTHON=\"${Python3_EXECUTABLE}\""
        "SEVENZIP_GPT_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/gpt/support/gpt_fixture.py\"")
    add_test(NAME gpt_stream COMMAND gpt_stream_tests -silent)
    set_tests_properties(gpt_stream PROPERTIES TIMEOUT 30)
    add_test(NAME squashfs COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/squashfs/squashfs_tests.py
        ${SEVENZIP_CODEC_RUNTIME_DIR} "$<TARGET_FILE:sevenzip_squashfs>")
    set_tests_properties(squashfs PROPERTIES TIMEOUT 60)
    qt_add_executable(squashfs_stream_tests tests/formats/squashfs/squashfs_stream_tests.cpp)
    target_include_directories(squashfs_stream_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(squashfs_stream_tests PRIVATE sevenzip_panel Qt6::Test)
    if(WIN32)
        target_link_libraries(squashfs_stream_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(squashfs_stream_tests PRIVATE
        "SEVENZIP_SQUASHFS_CORE=\"$<TARGET_FILE:sevenzip_squashfs>\""
        "SEVENZIP_SQUASHFS_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_SQUASHFS_NATIVE_FIXTURES=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/squashfs\""
        "SEVENZIP_SQUASHFS_PYTHON=\"${Python3_EXECUTABLE}\""
        "SEVENZIP_SQUASHFS_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/squashfs/support/squashfs_fixture.py\"")
    add_test(NAME squashfs_stream COMMAND squashfs_stream_tests -silent)
    add_dependencies(squashfs_stream_tests sevenzip_squashfs_runtime)
    set_tests_properties(squashfs_stream PROPERTIES TIMEOUT 45)
    qt_add_executable(ntfs_stream_tests tests/formats/ntfs/ntfs_stream_tests.cpp)
    target_include_directories(ntfs_stream_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(ntfs_stream_tests PRIVATE sevenzip_panel Qt6::Test)
    if(WIN32)
        target_link_libraries(ntfs_stream_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(ntfs_stream_tests PRIVATE
        "SEVENZIP_NTFS_CORE=\"$<TARGET_FILE:sevenzip_ntfs>\""
        "SEVENZIP_NTFS_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_NTFS_PYTHON=\"${Python3_EXECUTABLE}\""
        "SEVENZIP_NTFS_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/ntfs/support/ntfs_fixture.py\"")
    add_test(NAME ntfs_stream COMMAND ntfs_stream_tests -silent)
    add_dependencies(ntfs_stream_tests sevenzip_ntfs_runtime)
    set_tests_properties(ntfs_stream PROPERTIES TIMEOUT 60)
    add_test(NAME ntfs_stream_quick COMMAND ntfs_stream_tests -silent
        reads
        errorsAndRetry
        malformed
        backendSelectedExtraction
        reparseMetadataIoAndCancellation
        volumeListFailureAndCancellation
        dataListIoFailureAndReopening)
    set_tests_properties(ntfs_stream_quick PROPERTIES TIMEOUT 45)
    qt_add_executable(ntfs_directory_tests tests/formats/ntfs/ntfs_directory_tests.cpp tests/formats/ntfs/support/ntfs_test_support.h)
    target_include_directories(ntfs_directory_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(ntfs_directory_tests PRIVATE sevenzip_filesystem Qt6::Test)
    if(WIN32)
        target_link_libraries(ntfs_directory_tests PRIVATE oleaut32)
    endif()
    get_target_property(ntfs_test_definitions ntfs_stream_tests COMPILE_DEFINITIONS)
    target_compile_definitions(ntfs_directory_tests PRIVATE ${ntfs_test_definitions})
    add_test(NAME ntfs_directory COMMAND ntfs_directory_tests -silent)
    add_dependencies(ntfs_directory_tests sevenzip_ntfs_runtime)
    set_tests_properties(ntfs_directory PROPERTIES TIMEOUT 45)
    qt_add_executable(ntfs_record_tests tests/formats/ntfs/ntfs_record_tests.cpp tests/formats/ntfs/support/ntfs_test_support.h)
    target_include_directories(ntfs_record_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(ntfs_record_tests PRIVATE sevenzip_filesystem Qt6::Test)
    if(WIN32)
        target_link_libraries(ntfs_record_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(ntfs_record_tests PRIVATE ${ntfs_test_definitions})
    add_test(NAME ntfs_record COMMAND ntfs_record_tests -silent)
    add_dependencies(ntfs_record_tests sevenzip_ntfs_runtime)
    set_tests_properties(ntfs_record PROPERTIES TIMEOUT 45)
    qt_add_executable(ntfs_recovery_tests tests/formats/ntfs/ntfs_recovery_tests.cpp tests/formats/ntfs/support/ntfs_test_support.h)
    target_include_directories(ntfs_recovery_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(ntfs_recovery_tests PRIVATE sevenzip_filesystem Qt6::Test)
    if(WIN32)
        target_link_libraries(ntfs_recovery_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(ntfs_recovery_tests PRIVATE ${ntfs_test_definitions})
    add_test(NAME ntfs_recovery COMMAND ntfs_recovery_tests -silent)
    add_dependencies(ntfs_recovery_tests sevenzip_ntfs_runtime)
    set_tests_properties(ntfs_recovery PROPERTIES TIMEOUT 45)
    qt_add_executable(ntfs_deleted_tests tests/formats/ntfs/ntfs_deleted_tests.cpp tests/formats/ntfs/support/ntfs_test_support.h)
    target_include_directories(ntfs_deleted_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(ntfs_deleted_tests PRIVATE sevenzip_filesystem Qt6::Test)
    if(WIN32)
        target_link_libraries(ntfs_deleted_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(ntfs_deleted_tests PRIVATE ${ntfs_test_definitions})
    add_test(NAME ntfs_deleted COMMAND ntfs_deleted_tests -silent)
    add_dependencies(ntfs_deleted_tests sevenzip_ntfs_runtime)
    set_tests_properties(ntfs_deleted PROPERTIES TIMEOUT 45)
    qt_add_executable(ntfs_security_list_tests tests/formats/ntfs/ntfs_security_list_tests.cpp tests/formats/ntfs/support/ntfs_test_support.h)
    target_include_directories(ntfs_security_list_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(ntfs_security_list_tests PRIVATE sevenzip_filesystem Qt6::Test)
    if(WIN32)
        target_link_libraries(ntfs_security_list_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(ntfs_security_list_tests PRIVATE ${ntfs_test_definitions})
    add_test(NAME ntfs_security_list COMMAND ntfs_security_list_tests -silent)
    add_dependencies(ntfs_security_list_tests sevenzip_ntfs_runtime)
    set_tests_properties(ntfs_security_list PROPERTIES TIMEOUT 45)
    qt_add_executable(ntfs_security_tests tests/formats/ntfs/ntfs_security_tests.cpp)
    target_include_directories(ntfs_security_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(ntfs_security_tests PRIVATE sevenzip_panel Qt6::Test)
    if(WIN32)
        target_link_libraries(ntfs_security_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(ntfs_security_tests PRIVATE
        "SEVENZIP_NTFS_MODULE=\"$<TARGET_FILE:sevenzip_ntfs>\""
        "SEVENZIP_NTFS_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_NTFS_PYTHON=\"${Python3_EXECUTABLE}\""
        "SEVENZIP_NTFS_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/ntfs/support/ntfs_fixture.py\"")
    add_dependencies(ntfs_security_tests sevenzip_ntfs_runtime)
    add_test(NAME ntfs_security COMMAND ntfs_security_tests -silent)
    set_tests_properties(ntfs_security PROPERTIES TIMEOUT 60)
    add_test(NAME hfs COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/hfs/hfs_tests.py
        ${SEVENZIP_CODEC_RUNTIME_DIR} "$<TARGET_FILE:sevenzip_hfs>")
    set_tests_properties(hfs PROPERTIES TIMEOUT 60)
    qt_add_executable(hfs_stream_tests tests/formats/hfs/hfs_stream_tests.cpp)
    target_include_directories(hfs_stream_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(hfs_stream_tests PRIVATE sevenzip_panel Qt6::Test)
    if(WIN32)
        target_link_libraries(hfs_stream_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(hfs_stream_tests PRIVATE
        "SEVENZIP_HFS_CORE=\"$<TARGET_FILE:sevenzip_hfs>\""
        "SEVENZIP_HFS_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_HFS_PYTHON=\"${Python3_EXECUTABLE}\""
        "SEVENZIP_HFS_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/hfs/support/hfs_fixture.py\"")
    add_test(NAME hfs_stream COMMAND hfs_stream_tests -silent)
    add_dependencies(hfs_stream_tests sevenzip_hfs_runtime)
    set_tests_properties(hfs_stream PROPERTIES TIMEOUT 45)
    add_test(NAME udf COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/udf/udf_tests.py
        ${SEVENZIP_CODEC_RUNTIME_DIR} "$<TARGET_FILE:sevenzip_udf>")
    set_tests_properties(udf PROPERTIES TIMEOUT 60)
    qt_add_executable(udf_stream_tests tests/formats/udf/udf_stream_tests.cpp)
    target_include_directories(udf_stream_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(udf_stream_tests PRIVATE sevenzip_panel Qt6::Test)
    if(WIN32)
        target_link_libraries(udf_stream_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(udf_stream_tests PRIVATE
        "SEVENZIP_UDF_CORE=\"$<TARGET_FILE:sevenzip_udf>\""
        "SEVENZIP_UDF_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_UDF_PYTHON=\"${Python3_EXECUTABLE}\""
        "SEVENZIP_UDF_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/udf/support/udf_fixture.py\"")
    add_test(NAME udf_stream COMMAND udf_stream_tests -silent)
    add_dependencies(udf_stream_tests sevenzip_udf_runtime)
    # All 289 stream/backend cases passed in 51.7s on native macOS after the
    # 45s aggregate cap expired. Reserve twice that time, rounded up; retain
    # every scenario and its individual operation deadlines.
    set_tests_properties(udf_stream PROPERTIES TIMEOUT 120)
    add_test(NAME iso COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/iso/iso_tests.py
        ${SEVENZIP_CODEC_RUNTIME_DIR} "$<TARGET_FILE:sevenzip_iso>"
        "$<TARGET_FILE:filesystem_charset_tests>")
    set_tests_properties(iso PROPERTIES TIMEOUT 60)
    qt_add_executable(iso_stream_tests tests/formats/iso/iso_stream_tests.cpp)
    target_include_directories(iso_stream_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(iso_stream_tests PRIVATE sevenzip_panel Qt6::Test sevenzip_nls_system)
    if(WIN32)
        target_link_libraries(iso_stream_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(iso_stream_tests PRIVATE
        "SEVENZIP_ISO_CORE=\"$<TARGET_FILE:sevenzip_iso>\""
        "SEVENZIP_ISO_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_ISO_PYTHON=\"${Python3_EXECUTABLE}\""
        "SEVENZIP_ISO_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/iso/support/iso_fixture.py\"")
    add_test(NAME iso_stream COMMAND iso_stream_tests -silent)
    add_dependencies(iso_stream_tests sevenzip_iso_runtime)
    set_tests_properties(iso_stream PROPERTIES TIMEOUT 60)
    add_test(NAME ntfs COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/ntfs/ntfs_tests.py
        ${SEVENZIP_CODEC_RUNTIME_DIR} "$<TARGET_FILE:sevenzip_ntfs>")
    set_tests_properties(ntfs PROPERTIES TIMEOUT 60)
    qt_add_executable(squashfs_metadata_tests tests/formats/squashfs/squashfs_metadata_tests.cpp)
    target_include_directories(squashfs_metadata_tests PRIVATE src)
    target_link_libraries(squashfs_metadata_tests PRIVATE sevenzip_squashfs_metadata Qt6::Test)
    target_compile_definitions(squashfs_metadata_tests PRIVATE
        "SEVENZIP_SQUASHFS_PYTHON=\"${Python3_EXECUTABLE}\""
        "SEVENZIP_SQUASHFS_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/squashfs/support/squashfs_fixture.py\"")
    add_test(NAME squashfs_metadata COMMAND squashfs_metadata_tests -silent)
    set_tests_properties(squashfs_metadata PROPERTIES TIMEOUT 30)
    add_test(NAME fat COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/fat/fat_tests.py
        ${SEVENZIP_CODEC_RUNTIME_DIR} "$<TARGET_FILE:sevenzip_fat>")
    set_tests_properties(fat PROPERTIES TIMEOUT 60)
    add_test(NAME ext COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/ext/ext_tests.py
        ${SEVENZIP_CODEC_RUNTIME_DIR} "$<TARGET_FILE:sevenzip_ext>")
    set_tests_properties(ext PROPERTIES TIMEOUT 45)
    if(MSVC AND SEVENZIP_ENABLE_SANITIZERS AND
            CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo)$")
        # The full CLI matrix exceeds the ordinary deadline with Windows ASan.
        # Keep it opt-in and retain focused malformed-inode coverage by default.
        set_property(TEST ext APPEND PROPERTY LABELS slow)
        add_test(NAME ext_inode_safety COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/ext/ext_tests.py
            ${SEVENZIP_CODEC_RUNTIME_DIR} "$<TARGET_FILE:sevenzip_ext>"
            ExtTests.test_named_inodes_require_linux_read_acceptance)
        set_tests_properties(ext_inode_safety PROPERTIES TIMEOUT 30)
    endif()
    qt_add_executable(ext_stream_tests tests/formats/ext/ext_stream_tests.cpp)
    target_include_directories(ext_stream_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(ext_stream_tests PRIVATE sevenzip_filesystem Qt6::Test)
    if(WIN32)
        target_link_libraries(ext_stream_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(ext_stream_tests PRIVATE
        "SEVENZIP_EXT_CORE=\"$<TARGET_FILE:sevenzip_ext>\""
        "SEVENZIP_EXT_NATIVE_FIXTURES=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ext4\""
        "SEVENZIP_EXT_PYTHON=\"${Python3_EXECUTABLE}\""
        "SEVENZIP_EXT_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/ext/support/ext_fixture.py\"")
    add_test(NAME ext_stream COMMAND ext_stream_tests -silent)
    add_dependencies(ext_stream_tests sevenzip_ext_runtime)
    set_tests_properties(ext_stream PROPERTIES TIMEOUT 45)
    qt_add_executable(ext_recovery_tests tests/formats/ext/ext_recovery_tests.cpp)
    target_include_directories(ext_recovery_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(ext_recovery_tests PRIVATE sevenzip_filesystem Qt6::Test)
    if(WIN32)
        target_link_libraries(ext_recovery_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(ext_recovery_tests PRIVATE
        "SEVENZIP_EXT_CORE=\"$<TARGET_FILE:sevenzip_ext>\""
        "SEVENZIP_EXT_PYTHON=\"${Python3_EXECUTABLE}\""
        "SEVENZIP_EXT_RECOVERY_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/ext/support/ext_recovery_fixture.py\"")
    add_dependencies(ext_recovery_tests sevenzip_ext_runtime)
    add_test(NAME ext_recovery COMMAND ext_recovery_tests -silent)
    set_tests_properties(ext_recovery PROPERTIES TIMEOUT 45)
    qt_add_executable(ext_fscrypt_tests tests/formats/ext/ext_fscrypt_tests.cpp)
    target_include_directories(ext_fscrypt_tests PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(ext_fscrypt_tests PRIVATE sevenzip_filesystem Qt6::Test)
    if(WIN32)
        target_link_libraries(ext_fscrypt_tests PRIVATE oleaut32)
    endif()
    target_compile_definitions(ext_fscrypt_tests PRIVATE
        "SEVENZIP_EXT_CORE=\"$<TARGET_FILE:sevenzip_ext>\""
        "SEVENZIP_FSCRYPT_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fscrypt\"")
    add_dependencies(ext_fscrypt_tests sevenzip_ext_runtime)
    add_test(NAME ext_fscrypt COMMAND ext_fscrypt_tests -silent)
    set_tests_properties(ext_fscrypt PROPERTIES TIMEOUT 30)
    add_test(NAME ext_fscrypt_cli COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/ext/ext_fscrypt_cli_tests.py
        ${SEVENZIP_CODEC_RUNTIME_DIR} "$<TARGET_FILE:sevenzip_ext>")
    set_tests_properties(ext_fscrypt_cli PROPERTIES TIMEOUT 30)
    qt_add_executable(ext_fscrypt_backend_tests tests/formats/ext/ext_fscrypt_backend_tests.cpp)
    target_include_directories(ext_fscrypt_backend_tests PRIVATE src)
    target_link_libraries(ext_fscrypt_backend_tests PRIVATE sevenzip_panel Qt6::Test)
    target_compile_definitions(ext_fscrypt_backend_tests PRIVATE
        "SEVENZIP_EXT_CORE=\"$<TARGET_FILE:sevenzip_ext>\""
        "SEVENZIP_EXT_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_FSCRYPT_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fscrypt\"")
    add_dependencies(ext_fscrypt_backend_tests sevenzip_ext_runtime)
    add_test(NAME ext_fscrypt_backend COMMAND ext_fscrypt_backend_tests -silent)
    set_tests_properties(ext_fscrypt_backend PROPERTIES TIMEOUT 45)
    qt_add_executable(ext_xattr_backend_tests tests/formats/ext/ext_xattr_backend_tests.cpp)
    target_include_directories(ext_xattr_backend_tests PRIVATE src)
    target_link_libraries(ext_xattr_backend_tests PRIVATE sevenzip_location Qt6::Test)
    target_compile_definitions(ext_xattr_backend_tests PRIVATE
        "SEVENZIP_EXT_CORE=\"$<TARGET_FILE:sevenzip_ext>\""
        "SEVENZIP_EXT_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\""
        "SEVENZIP_EXT_PYTHON=\"${Python3_EXECUTABLE}\""
        "SEVENZIP_EXT_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/ext/support/ext_fixture.py\"")
    add_dependencies(ext_xattr_backend_tests sevenzip_ext_runtime)
    add_test(NAME ext_xattr_backend COMMAND ext_xattr_backend_tests -silent)
    set_tests_properties(ext_xattr_backend PROPERTIES TIMEOUT 45)
    add_test(NAME erofs COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/formats/erofs/erofs_tests.py
        ${SEVENZIP_CODEC_RUNTIME_DIR} "$<TARGET_FILE:sevenzip_erofs>")
    set_tests_properties(erofs PROPERTIES TIMEOUT 90)
    add_test(NAME runtime_package COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/package_tests.py)
    add_test(NAME windows_package_verification COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/windows_package_tests.py)
    add_test(NAME upstream_baseline COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/upstream_baseline_tests.py)
    set_tests_properties(upstream_baseline PROPERTIES TIMEOUT 120)
    add_test(NAME upstream_inventory COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/upstream_baseline.py inventory)
