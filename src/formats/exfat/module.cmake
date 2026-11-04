# The GPL Linux-derived reader is a separate 7-Zip format module.
add_library(sevenzip_exfat MODULE
    src/formats/exfat/exfathandler.cpp src/formats/exfat/exfatstream.cpp
    src/formats/common/moduleexports.cpp src/formats/exfat/exfatreader.c
    src/formats/exfat/dir.c src/formats/exfat/fatent.c src/formats/exfat/super.c
    src/formats/exfat/balloc.c src/formats/exfat/misc.c src/compat/linux/time/time.c src/compat/linux/core/hweight.c
    src/compat/linux/core/string.c
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Archive/ArchiveExports.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/StreamUtils.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/PropId.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/PropVariant.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/TimeUtils.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/MyWindows.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/MyString.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/IntToString.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/StringConvert.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/UTFConvert.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Alloc.c)
target_include_directories(sevenzip_exfat PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
target_link_libraries(sevenzip_exfat PRIVATE Threads::Threads)
set_target_properties(sevenzip_exfat PROPERTIES AUTOMOC OFF AUTOUIC OFF PREFIX ""
    C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(UNIX AND NOT APPLE)
    target_link_options(sevenzip_exfat PRIVATE "-Wl,-Bsymbolic")
endif()
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(exfat_tests tests/formats/exfat/exfat_tests.cpp tests/formats/exfat/support/exfat_fixture.cpp
        ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/MyWindows.cpp
        ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/PropVariant.cpp
        ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/MyString.cpp
        ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/IntToString.cpp
        ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/StringConvert.cpp
        ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/UTFConvert.cpp
        ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Alloc.c)
    target_include_directories(exfat_tests PRIVATE ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    set_target_properties(exfat_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(exfat_tests PRIVATE Qt6::Test)
    target_compile_definitions(exfat_tests PRIVATE
        "SEVENZIP_EXFAT_MODULE=\"$<TARGET_FILE:sevenzip_exfat>\""
        "SEVENZIP_EXFAT_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\"")
    if(WIN32)
        target_link_libraries(exfat_tests PRIVATE oleaut32)
    endif()
    add_dependencies(exfat_tests sevenzip_exfat)
    add_test(NAME exfat COMMAND exfat_tests -silent)
    set_tests_properties(exfat PROPERTIES TIMEOUT 45)
    qt_add_executable(exfat_backend_tests tests/formats/exfat/exfat_backend_tests.cpp tests/formats/exfat/support/exfat_fixture.cpp)
    set_target_properties(exfat_backend_tests PROPERTIES AUTOMOC ON)
    target_include_directories(exfat_backend_tests PRIVATE src)
    target_link_libraries(exfat_backend_tests PRIVATE sevenzip_panel Qt6::Test)
    target_compile_definitions(exfat_backend_tests PRIVATE
        "SEVENZIP_EXFAT_MODULE=\"$<TARGET_FILE:sevenzip_exfat>\""
        "SEVENZIP_EXFAT_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\"")
    add_dependencies(exfat_backend_tests sevenzip_exfat)
    add_test(NAME exfat_backend COMMAND exfat_backend_tests -silent)
    set_tests_properties(exfat_backend PROPERTIES TIMEOUT 45)
endif()
if(WIN32)
    target_sources(sevenzip_exfat PRIVATE src/formats/common/moduleexports.def)
    target_link_libraries(sevenzip_exfat PRIVATE oleaut32)
endif()

function(sevenzip_deploy_exfat)
    add_custom_target(sevenzip_exfat_runtime ALL
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${SEVENZIP_CODEC_RUNTIME_DIR}/Formats"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:sevenzip_exfat>"
            "${SEVENZIP_CODEC_RUNTIME_DIR}/Formats/$<TARGET_FILE_NAME:sevenzip_exfat>"
        DEPENDS sevenzip_exfat VERBATIM)
    add_dependencies(sevenzip_desktop sevenzip_exfat_runtime)
endfunction()
