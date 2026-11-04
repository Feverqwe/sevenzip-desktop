add_library(sevenzip_hash_registry OBJECT ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/CrcReg.cpp
    src/formats/rar/Rar5Handler.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/CRC.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/CksumReg.cpp ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/Md5Reg.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/Sha1Reg.cpp ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/Sha1Prepare.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/Sha256Reg.cpp ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/Sha256Prepare.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/Sha512Reg.cpp ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/Sha512Prepare.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/Sha3Reg.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/XzCrc64Reg.cpp ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/XzCrc64Init.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/Xxh64Reg.cpp)
set_target_properties(sevenzip_hash_registry PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_include_directories(sevenzip_hash_registry PRIVATE ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
target_link_libraries(sevenzip_hash_registry PRIVATE Qt6::Core)
add_library(sevenzip_hash_registration INTERFACE)
target_link_libraries(sevenzip_hash_registration INTERFACE sevenzip_hash_registry
    $<TARGET_OBJECTS:sevenzip_hash_registry>)

add_library(sevenzip_native_types INTERFACE)
target_compile_features(sevenzip_native_types INTERFACE cxx_std_23)
target_include_directories(sevenzip_native_types INTERFACE ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
target_compile_definitions(sevenzip_native_types INTERFACE Z7_EXTERNAL_CODECS)
if(WIN32)
    target_compile_definitions(sevenzip_native_types INTERFACE Z7_DEVICE_FILE)
endif()

# Native temp creation calls the registry, whose Windows identity adapter uses
# native file handles. Compose these objects into the same archive as FileDir/
# FileIO instead of introducing mutually dependent static libraries.
add_library(sevenzip_owned_temp OBJECT
    src/platform/temp/qtownedtempidentity.cpp src/platform/temp/qtownedtempidentity.h
    src/platform/temp/qtownedtempregistry.cpp src/platform/temp/qtownedtempregistry.h
    src/platform/temp/qtownedtempdir.cpp src/platform/temp/qtownedtempdir.h)
set_target_properties(sevenzip_owned_temp PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_include_directories(sevenzip_owned_temp PUBLIC src)
target_link_libraries(sevenzip_owned_temp PUBLIC Qt6::Core PRIVATE sevenzip_native_types)

add_library(sevenzip_common STATIC ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Sort.c ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Alloc.c
    src/ui/diagnostics/qtdiagnostictext.cpp src/ui/diagnostics/qtdiagnostictext.h
    src/localization/qttranslationcatalog.cpp src/localization/qttranslationcatalog.h
    ${SEVENZIP_7ZIP_SOURCE_DIR}/C/7zCrc.c ${SEVENZIP_7ZIP_SOURCE_DIR}/C/7zCrcOpt.c ${SEVENZIP_7ZIP_SOURCE_DIR}/C/CpuArch.c
    ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Sha1.c ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Sha1Opt.c
    ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Sha256.c ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Sha256Opt.c
    ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Sha512.c ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Sha512Opt.c ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Sha3.c
    ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Md5.c src/checksums/crc64/XzCrc64.c
    src/checksums/xxhash/Xxh64.c ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Compress/BZip2Crc.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Blake2s.c
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/CreateCoder.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/FilterCoder.cpp
    src/archive/hashing/HashCalc.cpp
    src/benchmark/Bench.cpp src/benchmark/Bench.h
    src/benchmark/qtbenchmarkservices.cpp src/benchmark/qtbenchmarkservices.h
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/SystemInfo.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/DynLimBuf.cpp
    src/archive/operations/EnumDirItems.cpp src/archive/operations/EnumDirItems.h
    src/archive/operations/SortUtils.cpp
    src/fileops/native/FileLink.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/UniqBlocks.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/ProgressUtils.cpp
    src/archive/codecs/LoadCodecs.cpp src/archive/codecs/LoadCodecs.h
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/Common/DefaultName.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/C/Threads.c src/platform/errors/ErrorMsg.cpp
    src/platform/loader/DLL.cpp src/platform/loader/DLL.h
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/StringToInt.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/MyString.cpp ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/IntToString.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/MyWindows.cpp
    src/fileops/native/FileDir.cpp src/fileops/native/FileDir.h
    src/fileops/native/FilePublish.cpp
    src/fileops/native/FileName.cpp
    src/fileops/native/correctfspath.h
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/FileFind.cpp
    src/fileops/native/FileIO.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/TimeUtils.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/FileStreams.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/StreamUtils.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/StreamObjects.cpp
    src/archive/streams/LimitedStreams.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Compress/CopyCoder.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Archive/Common/ItemNameUtils.cpp
    src/archive/operations/ExtractingFilePath.cpp src/archive/operations/ExtractingFilePath.h
    src/fileops/patterns/Wildcard.cpp src/fileops/patterns/Wildcard.h
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/StringConvert.cpp ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/UTFConvert.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/System.cpp src/archive/compression/MethodProps.cpp src/archive/operations/ZipRegistry.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/PropVariant.cpp ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/PropVariantConv.cpp
    src/filemanager/ClassDefs.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/PropId.cpp
    src/settings/ViewSettings.cpp src/settings/ViewSettings.h
    src/settings/RegistryUtils.cpp src/settings/RegistryUtils.h
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/StringUtils.cpp)
set_target_properties(sevenzip_common PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_include_directories(sevenzip_common PRIVATE src)
target_include_directories(sevenzip_common PRIVATE ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows)
target_link_libraries(sevenzip_common PUBLIC sevenzip_native_types Qt6::Core
    PRIVATE sevenzip_owned_temp)
target_compile_definitions(sevenzip_hash_registry PRIVATE Z7_EXTERNAL_CODECS)
target_link_libraries(sevenzip_common PUBLIC sevenzip_hash_registration)
target_link_libraries(sevenzip_common PUBLIC ${CMAKE_DL_LIBS})
target_link_libraries(sevenzip_common PUBLIC Threads::Threads)
target_link_libraries(sevenzip_common PUBLIC sevenzip_zstd)
include(src/checksums/liblzma/module.cmake)
target_sources(sevenzip_common PRIVATE
    src/app/commands/CommandLineParser.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/ListFileUtils.cpp
    src/archive/operations/ArchiveCommandLine.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/Common/UpdateAction.cpp)
target_include_directories(sevenzip_common PRIVATE
    ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP/Common ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP/7zip/UI/Common)
if(WIN32)
    target_sources(sevenzip_common PRIVATE src/platform/loader/DllSecur.c)
    target_sources(sevenzip_common PRIVATE ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/FileSystem.cpp
        ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/FileSystem.h)
    target_sources(sevenzip_common PRIVATE ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/Random.cpp)
    set_source_files_properties(${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Common/ListFileUtils.cpp
        src/archive/operations/ArchiveCommandLine.cpp PROPERTIES
        COMPILE_DEFINITIONS "_UNICODE;UNICODE")
    set_source_files_properties(src/operations/CompressCall.cpp PROPERTIES
        COMPILE_DEFINITIONS "_UNICODE;UNICODE")
    target_sources(sevenzip_common PRIVATE src/platform/shell/Shell.cpp
        ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/MemoryGlobal.cpp)
    set_source_files_properties(src/platform/shell/Shell.cpp
        src/dialogs/browse/BrowseDialog2.cpp
        ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/MemoryGlobal.cpp PROPERTIES
        COMPILE_DEFINITIONS "_UNICODE;UNICODE")
    target_link_libraries(sevenzip_common PUBLIC shell32)
    target_sources(sevenzip_common PRIVATE ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/Registry.cpp)
    target_sources(sevenzip_common PRIVATE ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/Menu.cpp)
    set_source_files_properties(${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/Menu.cpp PROPERTIES
        COMPILE_DEFINITIONS "_UNICODE;UNICODE")
    target_link_libraries(sevenzip_common PUBLIC user32)
    target_sources(sevenzip_common PRIVATE ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/ProcessUtils.cpp)
    set_source_files_properties(${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/ProcessUtils.cpp
        ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/Registry.cpp src/archive/codecs/LoadCodecs.cpp PROPERTIES
        COMPILE_DEFINITIONS "_UNICODE;UNICODE")
    target_sources(sevenzip_common PRIVATE ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/SecurityUtils.cpp)
    target_sources(sevenzip_common PRIVATE ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/MemoryLock.cpp)
    set_source_files_properties(src/platform/shell/qtnativemenupageservices.cpp PROPERTIES
        COMPILE_DEFINITIONS "_UNICODE;UNICODE")
    set_source_files_properties(${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/MemoryLock.cpp
        src/filemanager/FM.cpp src/platform/memory/qtmemorylockservices.cpp
        PROPERTIES COMPILE_DEFINITIONS "_UNICODE;UNICODE")
    target_link_libraries(sevenzip_common PUBLIC ole32 oleaut32 uuid advapi32)
endif()
