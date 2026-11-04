# Linux HFS+ readers stay in a separate GPL format module.
add_library(sevenzip_hfs MODULE
    src/formats/hfs/HfsDecmpfs.cpp
    src/formats/hfs/HfsDecmpfsStream.cpp
    src/formats/hfs/HfsHandler.cpp src/formats/common/moduleexports.cpp
    src/formats/hfs/hfsreader.c src/compat/linux/filesystem/namei.c src/formats/hfs/plus/unicode.c src/formats/hfs/plus/tables.c src/formats/hfs/plus/dir.c
    src/formats/hfs/plus/inode.c src/formats/hfs/plus/bfind.c src/formats/hfs/plus/xattr.c
    src/formats/hfs/plus/wrapper.c src/formats/hfs/plus/super.c
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Archive/ArchiveExports.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/StreamUtils.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/StreamObjects.cpp
    src/archive/streams/LimitedStreams.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/InBuffer.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/OutBuffer.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/PropId.cpp
    src/codecs/zlib/ZlibDecoder.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Compress/DeflateDecoder.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Compress/BitlDecoder.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Compress/LzOutWindow.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Compress/LzfseDecoder.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/PropVariant.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/PropVariantUtils.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/PropVariantConv.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/TimeUtils.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/MyWindows.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/MyString.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/MyVector.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/IntToString.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/StringConvert.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/UTFConvert.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/C/CpuArch.c ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Alloc.c)
target_include_directories(sevenzip_hfs PRIVATE src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Archive)
target_compile_definitions(sevenzip_hfs PRIVATE Z7_ST)
target_link_libraries(sevenzip_hfs PRIVATE Threads::Threads sevenzip_zisofs_zlib)
target_include_directories(sevenzip_hfs PRIVATE third_party)
set_source_files_properties(src/codecs/zlib/ZlibDecoder.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Compress")
set_target_properties(sevenzip_hfs PROPERTIES AUTOMOC OFF AUTOUIC OFF PREFIX ""
    C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(UNIX AND NOT APPLE)
    target_link_options(sevenzip_hfs PRIVATE "-Wl,-Bsymbolic")
endif()
if(WIN32)
    target_sources(sevenzip_hfs PRIVATE src/formats/common/moduleexports.def)
    target_link_libraries(sevenzip_hfs PRIVATE oleaut32)
endif()
function(sevenzip_deploy_hfs)
    add_custom_target(sevenzip_hfs_runtime ALL
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${SEVENZIP_CODEC_RUNTIME_DIR}/Formats"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:sevenzip_hfs>"
            "${SEVENZIP_CODEC_RUNTIME_DIR}/Formats/$<TARGET_FILE_NAME:sevenzip_hfs>"
        DEPENDS sevenzip_hfs VERBATIM)
    add_dependencies(sevenzip_desktop sevenzip_hfs_runtime)
endfunction()

# The GPL reader sources use their own upstream-shaped include root.
target_include_directories(sevenzip_hfs BEFORE PRIVATE ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP ${SEVENZIP_7ZIP_SOURCE_DIR})
