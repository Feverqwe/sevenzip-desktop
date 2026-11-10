cmake_minimum_required(VERSION 3.28)

if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_SYSROOT)
    execute_process(COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE sevenzip_macos_sdk OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    set(CMAKE_OSX_SYSROOT "${sevenzip_macos_sdk}" CACHE PATH "macOS SDK" FORCE)
endif()

project(sevenzip_desktop VERSION 0.1.17 LANGUAGES C CXX)

include(cmake/CxxStandard.cmake)
include(cmake/SourceDependencies.cmake)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <expected>
int main() {
    std::expected<void, int> result;
    return result ? 0 : result.error();
}" sevenzip_cpp23_expected_supported)
if(NOT sevenzip_cpp23_expected_supported)
    message(FATAL_ERROR "C++23 std::expected is required; select a compatible compiler and standard library (Clang 19+ with Ubuntu's libstdc++, GCC 14+, or current MSVC)")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang|GNU)$")
    # These standard-library definitions are inert in C. Keep them uniform so
    # Qt Remote Objects can generate one moc parameter file for mixed targets.
    add_compile_definitions(_GLIBCXX_ASSERTIONS=1 _LIBCPP_ENABLE_HARDENING=1
        _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE)
endif()

if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
    # Match the pinned 7-Zip CPP/Build.mak executable stack reserve. Its
    # bounded recursive readers also run in the application and ABI tests.
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        string(APPEND CMAKE_EXE_LINKER_FLAGS " /STACK:8388608")
    else()
        string(APPEND CMAKE_EXE_LINKER_FLAGS " /STACK:2097152")
    endif()
else()
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
        $<$<COMPILE_LANGUAGE:OBJCXX>:-Wall>
        $<$<COMPILE_LANGUAGE:OBJCXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:OBJCXX>:-Wpedantic>)
endif()

include(cmake/BuildDiagnostics.cmake)
include(cmake/StaticAnalysis.cmake)

set(sevenzip_version_header "${SEVENZIP_7ZIP_SOURCE_DIR}/C/7zVersion.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${sevenzip_version_header}")
file(STRINGS "${sevenzip_version_header}" sevenzip_version_line REGEX "^#define MY_VERSION_NUMBERS ")
if(NOT sevenzip_version_line MATCHES "\"([0-9]+\\.[0-9]+)\"")
    message(FATAL_ERROR "Cannot read the upstream 7-Zip version")
endif()
set(SEVENZIP_UPSTREAM_VERSION "${CMAKE_MATCH_1}")
set(SEVENZIP_DESKTOP_DISPLAY_VERSION "${PROJECT_VERSION} (7-Zip ${SEVENZIP_UPSTREAM_VERSION})")
set_property(SOURCE src/app/main.cpp src/dialogs/about/AboutDialog.cpp
    APPEND PROPERTY COMPILE_DEFINITIONS
    SEVENZIP_DESKTOP_DISPLAY_VERSION="${SEVENZIP_DESKTOP_DISPLAY_VERSION}")

include(CTest)
set(SEVENZIP_CODEC_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/codec-runtime" CACHE PATH
    "Managed native engine and format-plugin deployment for this build tree")
set(SEVENZIP_SFX_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/sfx" CACHE PATH
    "Managed native SFX modules for this build tree")

# Relocated source headers must also resolve from unchanged upstream headers
# spelling MyVector.h, ../Common/MyVector.h or ../../Common/MyVector.h.
include_directories(src ${SEVENZIP_DEPENDENCY_DIR} src/dependencies/grub/include ${SEVENZIP_NATIVE_INCLUDE_DIR}/C ${SEVENZIP_7ZIP_SOURCE_DIR} ${SEVENZIP_7ZIP_SOURCE_DIR}/C ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP/Common
    ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP/Windows ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP/7zip/Common ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP/7zip/UI/Common)
set_source_files_properties(src/archive/streams/LimitedStreams.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common")

find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools Network RemoteObjects)
get_target_property(sevenzip_qt_core_type Qt6::Core TYPE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_GENERATOR MATCHES "^Ninja"
        AND sevenzip_qt_core_type STREQUAL "STATIC_LIBRARY"
        AND SEVENZIP_ENABLE_SANITIZERS AND NOT DEFINED CMAKE_JOB_POOL_LINK)
    # Concurrent static Qt/ASan links can exhaust RAM even with few build jobs.
    get_property(sevenzip_existing_job_pools GLOBAL PROPERTY JOB_POOLS)
    if(NOT sevenzip_existing_job_pools)
        set_property(GLOBAL PROPERTY JOB_POOLS "${CMAKE_JOB_POOLS}")
    endif()
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS sevenzip_static_qt_link=1)
    set(CMAKE_JOB_POOL_LINK sevenzip_static_qt_link)
endif()
if(BUILD_TESTING AND sevenzip_qt_core_type STREQUAL "STATIC_LIBRARY"
        AND TARGET Qt6::QOffscreenIntegrationPlugin)
    # Headless widget checks need their platform plugin embedded in static Qt.
    set_property(TARGET Qt6::QOffscreenIntegrationPlugin PROPERTY QT_DEFAULT_PLUGIN 1)
endif()
find_package(Threads REQUIRED)
include(src/mount/module.cmake)
include(src/dependencies/zstd/module.cmake)
include(src/text/nls/module.cmake)
include(src/formats/exfat/module.cmake)
include(src/formats/xfs/module.cmake)
include(src/formats/fat/module.cmake)
include(src/formats/ext/module.cmake)
include(src/formats/squashfs/module.cmake)
include(src/formats/ntfs/module.cmake)
include(src/formats/hfs/module.cmake)
include(src/formats/iso/module.cmake)
include(src/formats/udf/module.cmake)
include(src/formats/mbr/module.cmake)
include(src/dependencies/lz4/module.cmake)
include(src/formats/erofs/module.cmake)
include(src/formats/f2fs/module.cmake)
include(src/formats/btrfs/module.cmake)
include(src/formats/initramfs/module.cmake)
include(src/formats/brotli/module.cmake)
include(src/formats/lz4/module.cmake)
include(src/formats/lzip/module.cmake)
include(src/formats/encrypted-dmg/module.cmake)
include(cmake/DependencyModules.cmake)

# Retire the former project-built plugin when reusing an existing runtime.
# Otherwise external-module discovery could restore CramFS after its removal.
file(REMOVE "${SEVENZIP_CODEC_RUNTIME_DIR}/Formats/sevenzip_cramfs${CMAKE_SHARED_MODULE_SUFFIX}")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR (APPLE AND BUILD_TESTING))
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    add_library(sevenzip_launcher STATIC src/ui/progress/qtlauncherprogress.cpp src/ui/progress/qtlauncherprogress.h)
    target_include_directories(sevenzip_launcher PUBLIC src ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(sevenzip_launcher PUBLIC Qt6::DBus)
    set_target_properties(sevenzip_launcher PROPERTIES AUTOMOC ON AUTOUIC OFF)
    add_library(sevenzip_linux_taskbar STATIC src/ui/progress/qttaskbarprogress_linux.cpp src/ui/progress/qttaskbarprogress.h)
    target_link_libraries(sevenzip_linux_taskbar PUBLIC sevenzip_launcher Qt6::Widgets)
    set_target_properties(sevenzip_linux_taskbar PROPERTIES AUTOMOC OFF AUTOUIC OFF)
endif()

set(property_name_resource "${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/PropertyName.rc")
set(extract_resource "${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/GUI/Extract.rc")
set(file_manager_resource "${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/resource.rc")
set(progress_resource "${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/ProgressDialog2.rc")
set(update_resource "${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/GUI/resource3.rc")
set(overwrite_resource "${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/OverwriteDialog.rc")
set(gui_resource "${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/resourceGui.rc")
set(explorer_resource "${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/Explorer/resource2.rc")
set(language_resource "${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/LangPage.rc")
set(codec_icon_resource "${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Bundles/Format7zF/resource.rc")
set(resource_strings_header "${CMAKE_CURRENT_BINARY_DIR}/ResourceStrings.h")
add_custom_command(OUTPUT "${resource_strings_header}"
    COMMAND "${CMAKE_COMMAND}" "-DRESOURCE_FILES=${property_name_resource};${extract_resource};${file_manager_resource};${progress_resource};${update_resource};${overwrite_resource};${gui_resource};${explorer_resource};${language_resource};${codec_icon_resource}"
        "-DMIXED_RESOURCE_FILES=${file_manager_resource};${progress_resource};${overwrite_resource};${explorer_resource};${language_resource};${codec_icon_resource}"
        "-DOUTPUT_FILE=${resource_strings_header}" -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/lib/rc_strings.cmake"
    DEPENDS "${property_name_resource}" "${extract_resource}" "${file_manager_resource}"
        "${progress_resource}" "${update_resource}" "${overwrite_resource}" "${gui_resource}" "${explorer_resource}" "${language_resource}" "${codec_icon_resource}" scripts/lib/rc_strings.cmake
    VERBATIM)

qt_standard_project_setup()

include(src/devices/module.cmake)

include(cmake/components/native.cmake)
include(src/fileops/overwrite/module.cmake)

include(src/formats/zip/module.cmake)
include(src/formats/rar/module.cmake)

include(cmake/components/presentation.cmake)

include(cmake/components/resources.cmake)

add_library(sevenzip_folder_loader STATIC
    src/filemanager/providers/filesystemfolderloader.cpp src/filemanager/providers/filesystemfolderloader.h
    src/filemanager/providers/qtfolderprojection.cpp src/filemanager/providers/qtfolderprojection.h)
target_include_directories(sevenzip_folder_loader PUBLIC src)
target_link_libraries(sevenzip_folder_loader PUBLIC sevenzip_location Qt6::Core)
include(cmake/PortComponents.cmake)
include(cmake/WindowComponent.cmake)

qt_add_executable(sevenzip_desktop src/app/main.cpp src/app/desktopidentity.h)
target_include_directories(sevenzip_desktop PRIVATE src)
target_link_libraries(sevenzip_desktop PRIVATE
    Qt6::Widgets sevenzip_window sevenzip_gui_entry sevenzip_file_manager_entry)
if(WIN32)
    # Original resource.rc IDI_ICON entry, with build-local absolute paths.
    add_dependencies(sevenzip_desktop sevenzip_device_reader)
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/application_icon.rc" CONTENT
        "#include \"${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/resourceGui.h\"\nIDI_ICON ICON \"${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/FM.ico\"\n" @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/application_icon.rc" PROPERTIES
        OBJECT_DEPENDS "${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/FM.ico")
    target_sources(sevenzip_desktop PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/application_icon.rc")
    add_custom_command(TARGET sevenzip_desktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:sevenzip_desktop>
            "$<TARGET_FILE_DIR:sevenzip_desktop>/7zG.exe"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:sevenzip_desktop>
            "$<TARGET_FILE_DIR:sevenzip_desktop>/7zFM.exe"
        VERBATIM)
else()
    add_custom_command(TARGET sevenzip_desktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:sevenzip_desktop>
            "$<TARGET_FILE_DIR:sevenzip_desktop>/7zG"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:sevenzip_desktop>
            "$<TARGET_FILE_DIR:sevenzip_desktop>/7zFM"
        VERBATIM)
endif()

set_target_properties(sevenzip_desktop PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)
add_custom_target(sevenzip_mount_runtime
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:sevenzip_smb_server>
            "$<TARGET_FILE_DIR:sevenzip_desktop>/$<TARGET_FILE_NAME:sevenzip_smb_server>"
        DEPENDS sevenzip_smb_server VERBATIM)
add_dependencies(sevenzip_desktop sevenzip_mount_runtime)
if(TARGET sevenzip_native_mount)
    add_custom_command(TARGET sevenzip_mount_runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:sevenzip_native_mount>
            "$<TARGET_FILE_DIR:sevenzip_desktop>/$<TARGET_FILE_NAME:sevenzip_native_mount>" VERBATIM)
    add_dependencies(sevenzip_mount_runtime sevenzip_native_mount)
endif()
sevenzip_deploy_exfat()
sevenzip_deploy_xfs()
sevenzip_deploy_fat()
sevenzip_deploy_ext()
sevenzip_deploy_squashfs()
sevenzip_deploy_ntfs()
sevenzip_deploy_hfs()
sevenzip_deploy_iso()
sevenzip_deploy_udf()
sevenzip_deploy_mbr()
sevenzip_deploy_erofs()
sevenzip_deploy_f2fs()
sevenzip_deploy_btrfs()
sevenzip_deploy_linux_frames()
sevenzip_deploy_brotli()
sevenzip_deploy_lz4_frame()
sevenzip_deploy_lzip()
sevenzip_deploy_encrypted_dmg()
if(APPLE)
    include(cmake/MacAssociationBundle.cmake)
endif()
if(WIN32)
    include(cmake/WindowsInstall.cmake)
else()
    include(cmake/UnixInstall.cmake)
endif()
include(cmake/VcpkgNotices.cmake)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION "${notices_destination}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/PackageInfo.json"
    CONTENT "{\"version\":\"${PROJECT_VERSION}\",\"display_version\":\"${SEVENZIP_DESKTOP_DISPLAY_VERSION}\",\"upstream_version\":\"${SEVENZIP_UPSTREAM_VERSION}\",\"qt_version\":\"${Qt6_VERSION}\"}\n")

if(BUILD_TESTING)
    include(cmake/tests/runtime.cmake)
    include(cmake/tests/streams.cmake)
    include(cmake/tests/filesystems.cmake)
    include(cmake/tests/native_services.cmake)
    include(cmake/tests/native_archive.cmake)
    include(cmake/tests/dialogs.cmake)
    include(cmake/tests/window.cmake)
    include(cmake/tests/compression.cmake)
    include(cmake/tests/columns.cmake)
    include(cmake/tests/panels.cmake)
endif()

include(cmake/tests/integration.cmake)

# Extract every platform's presentation sources, including those omitted by the
# current platform's target graph and sources shared through object libraries.
file(GLOB_RECURSE sevenzip_translation_sources CONFIGURE_DEPENDS
    src/*.cpp src/*.h src/*.mm)
file(GLOB sevenzip_translation_catalogs CONFIGURE_DEPENDS translations/sevenzip_*.ts)
list(REMOVE_ITEM sevenzip_translation_catalogs "${CMAKE_CURRENT_SOURCE_DIR}/translations/sevenzip_en.ts")
qt_add_lupdate(
    SOURCE_TARGETS sevenzip_filesystem
    SOURCES ${sevenzip_translation_sources}
    TS_FILES ${sevenzip_translation_catalogs}
    PLURALS_TS_FILE translations/sevenzip_en.ts
    LUPDATE_TARGET sevenzip_lupdate
    OPTIONS -no-obsolete -locations relative)
# Qt's English source strings are built in; only translated catalogs need
# qtbase merging. Keep missing-catalog warnings enabled for those languages.
qt_add_lrelease(
    TS_FILES ${sevenzip_translation_catalogs}
    MERGE_QT_TRANSLATIONS
    QT_TRANSLATION_CATALOGS qtbase
    LRELEASE_TARGET sevenzip_lrelease
    QM_FILES_OUTPUT_VARIABLE sevenzip_translated_qm_files)
qt_add_lrelease(
    TS_FILES translations/sevenzip_en.ts
    LRELEASE_TARGET sevenzip_en_lrelease
    QM_FILES_OUTPUT_VARIABLE sevenzip_english_qm_files)
add_dependencies(sevenzip_lrelease sevenzip_en_lrelease)
qt_add_resources(sevenzip_filesystem sevenzip_filesystem_translations
    PREFIX "/translations"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${sevenzip_translated_qm_files} ${sevenzip_english_qm_files})

# The example also needs library features beyond module scanning support.
set(sevenzip_cpp23_example_supported ${sevenzip_modules_supported})
if(sevenzip_modules_supported)
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("#include <array>
#include <expected>
#include <print>
#include <ranges>
int main() {
    std::array values{1, 2};
    for (auto part : values | std::views::chunk(1))
        std::println(\"{}\", part.size());
    return std::expected<int, int>(0).transform([](int value) { return value; })
        .and_then([](int value) { return std::expected<int, int>(value); }).value_or(1);
}" sevenzip_cpp23_library_supported)
    if(NOT sevenzip_cpp23_library_supported)
        set(sevenzip_cpp23_example_supported OFF)
    endif()
endif()
option(SEVENZIP_BUILD_CPP23_MODULE_EXAMPLE "Build the C++23 module example"
    ${sevenzip_cpp23_example_supported})
if(SEVENZIP_BUILD_CPP23_MODULE_EXAMPLE)
    if(NOT sevenzip_cpp23_example_supported)
        message(FATAL_ERROR "The module example requires module scanning, std::views::chunk and std::println")
    endif()
    add_executable(sevenzip_cpp23_example examples/cpp23/main.cpp)
    target_sources(sevenzip_cpp23_example PRIVATE
        FILE_SET CXX_MODULES FILES examples/cpp23/foundation.cppm)
    sevenzip_configure_module_cache(sevenzip_cpp23_example)
endif()

include(cmake/SanitizerTests.cmake)
if(BUILD_TESTING)
    sevenzip_set_test_source_roots()
endif()
sevenzip_track_analysis_inputs()
