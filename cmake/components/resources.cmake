add_library(sevenzip_operation_contract STATIC src/operations/qtoperationprotocol.cpp src/operations/qtoperationprotocol.h)
target_compile_features(sevenzip_operation_contract PUBLIC cxx_std_23)
target_include_directories(sevenzip_operation_contract PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(sevenzip_operation_contract PUBLIC Qt6::Core Qt6::Network Qt6::RemoteObjects)
qt_add_repc_merged(sevenzip_operation_contract src/operations/operation.rep)

add_library(sevenzip_operation_process STATIC src/operations/qtoperationprocess.cpp src/operations/qtoperationprocess.h)
target_compile_features(sevenzip_operation_process PUBLIC cxx_std_23)
target_include_directories(sevenzip_operation_process PUBLIC src)
target_link_libraries(sevenzip_operation_process PUBLIC Qt6::Core)
if(WIN32)
    target_link_libraries(sevenzip_operation_process PRIVATE sevenzip_common)
elseif(UNIX)
    add_library(sevenzip_child_reaper STATIC
        src/platform/process/qtchildprocessreaper.cpp src/platform/process/qtchildprocessreaper.h)
    set_target_properties(sevenzip_child_reaper PROPERTIES AUTOMOC OFF AUTOUIC OFF)
    target_compile_features(sevenzip_child_reaper PUBLIC cxx_std_23)
    target_include_directories(sevenzip_child_reaper PUBLIC src)
    target_link_libraries(sevenzip_child_reaper PRIVATE Threads::Threads)
    target_link_libraries(sevenzip_operation_process PRIVATE sevenzip_child_reaper)
    target_link_libraries(sevenzip_panel PRIVATE sevenzip_child_reaper)
endif()

add_library(sevenzip_operation_client STATIC src/operations/qtoperationlauncher.cpp src/operations/qtoperationlauncher.h)
target_link_libraries(sevenzip_operation_client PUBLIC sevenzip_operation_contract
    PRIVATE sevenzip_operation_process)

add_library(sevenzip_archive_write_lease STATIC src/archive/resources/qtarchivewritelease.cpp src/archive/resources/qtarchivewritelease.h)
target_compile_features(sevenzip_archive_write_lease PUBLIC cxx_std_23)
target_include_directories(sevenzip_archive_write_lease PUBLIC src)
target_link_libraries(sevenzip_archive_write_lease PUBLIC Qt6::Core)

add_library(sevenzip_codec_image STATIC src/archive/codecs/archivecodecimage.cpp src/archive/codecs/archivecodecimage.h)
set_target_properties(sevenzip_codec_image PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_compile_features(sevenzip_codec_image PUBLIC cxx_std_23)
target_include_directories(sevenzip_codec_image PUBLIC src)
target_link_libraries(sevenzip_codec_image PUBLIC Qt6::Core PRIVATE sevenzip_common)

add_library(sevenzip_archive_input STATIC
    src/archive/resources/archiveinputlayer.cpp src/archive/resources/archiveinputlayer.h src/archive/resources/archivelayercontext.h src/archive/resources/archivereadstream.h)
set_target_properties(sevenzip_archive_input PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_include_directories(sevenzip_archive_input PUBLIC src)
target_link_libraries(sevenzip_archive_input PUBLIC sevenzip_native_types PRIVATE sevenzip_common)

add_library(sevenzip_archive_transfer_backup STATIC
    src/archive/resources/qtarchivetransferbackup.cpp src/archive/resources/qtarchivetransferbackup.h)
set_target_properties(sevenzip_archive_transfer_backup PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_include_directories(sevenzip_archive_transfer_backup PUBLIC src)
target_link_libraries(sevenzip_archive_transfer_backup PUBLIC Qt6::Core
    PRIVATE sevenzip_common)
target_compile_features(sevenzip_archive_transfer_backup PUBLIC cxx_std_23)

add_library(sevenzip_filesystem STATIC
    src/operations/qtoperationendpoint.cpp src/operations/qtoperationendpoint.h
    src/archive/sfx/archivesfxmodule.cpp src/archive/sfx/archivesfxmodule.h
    src/archive/resources/qtarchivehashoperation.cpp src/archive/resources/qtarchivehashoperation.h
    src/archive/resources/qtarchivereadoperation.cpp src/archive/resources/qtarchivereadoperation.h
    src/archive/resources/qtarchivedirectoryextract.cpp src/archive/resources/qtarchivedirectoryextract.h
    src/archive/backend/archivebackend_nativeentries.cpp src/archive/backend/archivebackend_nativeentries.h
    src/console/List.cpp src/console/List.h

    src/archive/codecs/archivecodelocation.cpp src/archive/codecs/archivecodelocation.h
    src/localization/LangUtils.cpp src/localization/LangUtils.h
    src/localization/qtlangdialogcontrols.h
    src/localization/qtlanguageservices.cpp src/localization/qtlanguageservices.h
    src/localization/qtupstreamtranslations.h
    src/localization/ResourceString.cpp src/localization/ResourceString.h
    "${resource_strings_header}"
    src/platform/temp/WorkDir.cpp src/platform/temp/WorkDir.h
    src/archive/operations/Update.cpp src/archive/operations/Update.h
    src/platform/temp/TempFiles.cpp src/platform/temp/TempFiles.h
    src/archive/callbacks/UpdateCallback.cpp src/archive/callbacks/UpdateCallback.h
    src/archive/operations/UpdatePair.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/Common/UpdateProduce.cpp
    src/archive/resources/qtarchivedeleteinputs.cpp src/archive/resources/qtarchivedeleteinputs.h
    src/archive/resources/qtarchivetransferinputs.cpp src/archive/resources/qtarchivetransferinputs.h

    src/archive/streams/MultiOutStream.cpp src/archive/streams/MultiOutStream.h

    src/archive/callbacks/ArchiveExtractCallback.cpp src/archive/callbacks/ArchiveExtractCallback.h
    src/archive/callbacks/qtarchiveoutputidentity.cpp src/archive/callbacks/qtarchiveoutputidentity.h
    src/archive/operations/Extract.cpp src/archive/operations/Extract.h
    src/archive/opening/OpenArchive.cpp src/archive/opening/OpenArchive.h
    src/archive/codecs/archivecodeccatalog.cpp src/archive/codecs/archivecodeccatalog.h
    src/archive/resources/archivenativeopen.cpp src/archive/resources/archivenativeopen.h
    src/archive/resources/archivedevice.cpp src/archive/resources/archivedevice.h
    src/archive/backend/archivebackend_device.cpp
    src/devices/deviceinput.cpp src/devices/deviceaccess.h
    src/archive/codecs/archivecodecprobe.cpp src/archive/codecs/archivecodecprobe.h
    src/archive/callbacks/ArchiveOpenCallback.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/Common/SetProperties.cpp
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/Common/FilePathAutoRename.cpp
    src/filemanager/providers/FSFolder.cpp
    src/filemanager/providers/FSFolderProperties.cpp
    src/filemanager/providers/FSFolderCopy.cpp
    src/fileops/links/qtlinkservices.cpp src/fileops/links/qtlinkservices.h
    src/fileops/transfer/qtfiletransfercopycallback.cpp src/fileops/transfer/qtfiletransfercopycallback.h
    src/localization/TextPairs.cpp
    src/filemanager/providers/FSDrives.cpp src/filemanager/providers/FSDrives.h
    src/fileops/volumes/qtvolumemounts.cpp src/fileops/volumes/qtvolumemounts.h
    ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/Common/PropIDUtils.cpp)
file(GLOB_RECURSE sevenzip_help_assets CONFIGURE_DEPENDS
    "${SEVENZIP_DEPENDENCY_DIR}/7zip-help/*.htm"
    "${SEVENZIP_DEPENDENCY_DIR}/7zip-help/*.css")
qt_add_resources(sevenzip_panel upstream_help PREFIX "/upstream/7zip/Help"
    BASE "${SEVENZIP_DEPENDENCY_DIR}/7zip-help" FILES ${sevenzip_help_assets})
target_compile_definitions(sevenzip_panel PRIVATE SEVENZIP_DESKTOP_VERSION="${PROJECT_VERSION}")
if(WIN32)
    target_sources(sevenzip_filesystem PRIVATE ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/Net.cpp
        ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows/Net.h)
    target_link_libraries(sevenzip_filesystem PUBLIC mpr)
endif()
target_include_directories(sevenzip_filesystem PUBLIC src)
target_include_directories(sevenzip_filesystem PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(sevenzip_filesystem PRIVATE ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/Windows)
target_link_libraries(sevenzip_filesystem PUBLIC sevenzip_operation_client sevenzip_archive_write_lease
    sevenzip_archive_input sevenzip_common sevenzip_zip sevenzip_progress_sync sevenzip_overwrite_question)
target_link_libraries(sevenzip_filesystem PUBLIC sevenzip_system_icons sevenzip_name_sort)
target_link_libraries(sevenzip_filesystem PRIVATE sevenzip_codec_image)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/upstream-baseline.json")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/docs/upstream-baseline.json" sfx_baseline_json)
string(JSON sfx_upstream_revision GET "${sfx_baseline_json}" revision)
target_compile_definitions(sevenzip_filesystem PRIVATE
    "SEVENZIP_UPSTREAM_REVISION=\"${sfx_upstream_revision}\"")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_property(SOURCE src/archive/sfx/archivesfxmodule.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        "SEVENZIP_BUILD_SFX_DIR=\"${SEVENZIP_SFX_RUNTIME_DIR}\"")
    set_property(SOURCE src/archive/codecs/archivecodelocation.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        "SEVENZIP_BUILD_CODEC_RUNTIME=\"${SEVENZIP_CODEC_RUNTIME_DIR}\"")
endif()
target_link_libraries(sevenzip_panel PUBLIC sevenzip_filesystem)

add_library(sevenzip_location
    STATIC
    src/filemanager/providers/qtlocationfolder.cpp
    src/filemanager/providers/qtlocationfolder.h
    src/archive/resources/archivereadonlyview.cpp
    src/archive/resources/archivereadonlytree.cpp
    src/archive/resources/archivereadonlyview.h
    src/archive/resources/archivereadonlyview_p.h
    src/archive/resources/archivereadonlycache.cpp
    src/mount/qtarchivemountsource.cpp
    src/mount/qtarchivemountsource.h
    src/platform/associations/qtassociationfileopen.cpp
    src/platform/associations/qtassociationfileopen.h
    src/archive/agent/ArchiveFolderOpen.cpp
    src/settings/LangPage.cpp
    src/settings/LangPage.h
    src/settings/EditPage.cpp
    src/settings/EditPage.h
    src/settings/MenuPage.cpp
    src/settings/MenuPage.h
    src/settings/SystemPage.cpp
    src/settings/SystemPage.h
    src/filemanager/providers/FilePlugins.cpp
    src/filemanager/providers/FilePlugins.h
    src/settings/RegistryAssociations.cpp
    src/settings/RegistryAssociations.h
    src/platform/associations/qtassociationservices.cpp
    src/platform/associations/qtassociationservices.h
    src/platform/associations/qtassociationplatform.h
    src/settings/OptionsDialog.cpp
    src/settings/qtfmoptionsdialog.h
    src/settings/qtfmoptionslayout.cpp
    src/settings/SystemPagePresentation.cpp
    src/archive/resources/qtarchivebatchopen.cpp
    src/archive/resources/qtarchivebatchopen.h
    src/archive/resources/qtarchivebatchextract.cpp
    src/archive/resources/qtarchivebatchextract.h
    src/archive/resources/qtextractpathpolicy.h
    src/platform/shell/qtshellarchiveservices.cpp
    src/platform/shell/qtshellarchiveservices.h
    src/archive/resources/qtarchivebackendrequest.cpp
    src/archive/resources/qtarchivebackendrequest.h
    src/archive/resources/qtarchivesourceupdate.cpp
    src/archive/resources/qtarchivereopen.cpp
    src/archive/resources/qtarchivereopen.h
    src/archive/resources/qtarchivesourceextract.cpp
    src/archive/agent/AgentOut.cpp
    src/archive/agent/ArchiveFolderOut.cpp
    src/archive/agent/ArchiveFolder.cpp
    src/archive/agent/UpdateCallbackAgent.cpp
    src/archive/agent/UpdateCallbackAgent.h
    src/archive/resources/qtarchiveproperties.cpp
    src/archive/resources/qtarchiveproperties.h
    src/archive/resources/archivepropertysource.cpp
    src/archive/resources/archivepropertysource.h
    src/archive/resources/qtarchiveitemstream.cpp
    src/archive/resources/qtarchiveitemstream.h
    src/archive/resources/qtarchivefoldermanager.cpp
    src/archive/resources/qtarchivefoldermanager.h
    src/archive/resources/qtarchiveagentopen.h
    src/archive/resources/qtarchivenativeopenprogress.cpp
    src/archive/resources/qtarchivenativeopenprogress.h
    src/archive/resources/qtarchivestreamopen.h
    src/archive/resources/qtarchivefileresource.cpp
    src/archive/resources/qtarchivefileresource.h
    src/archive/resources/qtarchivedevicefolder.cpp
    src/devices/deviceselectiondialog.cpp
    src/devices/deviceselectiondialog.h
    src/devices/qtdevicecommands.cpp
    src/archive/resources/qtfilesystemreadprompt.cpp
    src/archive/resources/qtfilesystemreadprompt.h
    src/archive/resources/qtarchivenativeproperties.cpp
    src/filemanager/providers/qtfolderpropertycapabilities.h)
target_include_directories(sevenzip_location PUBLIC src)
target_link_libraries(sevenzip_location PUBLIC sevenzip_filesystem sevenzip_panel)
target_link_libraries(sevenzip_location PUBLIC sevenzip_mount_protocol)
add_library(sevenzip_mount_ui STATIC src/mount/qtmountextractionprompt.cpp src/mount/qtmountextractionprompt.h)
target_sources(sevenzip_mount_ui PRIVATE src/mount/qtmountsettings.cpp src/mount/qtmountsettings.h)
set_target_properties(sevenzip_mount_ui PROPERTIES AUTOMOC ON AUTOUIC OFF)
target_link_libraries(sevenzip_mount_ui PUBLIC sevenzip_mount_protocol Qt6::Widgets)
target_link_libraries(sevenzip_mount_ui PRIVATE sevenzip_panel)
target_include_directories(sevenzip_mount_ui PUBLIC src)
if(APPLE OR WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(sevenzip_mount_ui PRIVATE src/mount/qtarchivemountmanager.h
        src/mount/qtarchivemountmanager.cpp src/mount/qtarchivemountmanagerdialog.cpp)
    target_link_libraries(sevenzip_mount_ui PRIVATE sevenzip_location)
endif()
add_library(sevenzip_native_client STATIC src/mount/qtsmbmountplatform.h)
target_include_directories(sevenzip_native_client PUBLIC src)
target_link_libraries(sevenzip_native_client PUBLIC sevenzip_mount_protocol)
set_target_properties(sevenzip_native_client PROPERTIES AUTOMOC ON AUTOUIC OFF)
target_sources(sevenzip_location PRIVATE src/mount/qtarchivemountsession.h src/mount/qtarchivemountsession.cpp)
target_link_libraries(sevenzip_location PUBLIC sevenzip_native_client)
if(APPLE OR WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(sevenzip_native_client PRIVATE src/mount/qtsmbmountplatform_remote.cpp)
    add_dependencies(sevenzip_native_client sevenzip_native_mount)
    if(WIN32)
        target_link_libraries(sevenzip_native_client PRIVATE shell32)
    endif()
endif()
target_link_libraries(sevenzip_location PRIVATE sevenzip_device_access)
target_sources(sevenzip_location PRIVATE src/gui/GUI.cpp src/gui/qtguicommand.h)
target_include_directories(sevenzip_location PRIVATE ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP/Common ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP/7zip/UI/Common)
if(WIN32)
    set_source_files_properties(src/gui/GUI.cpp PROPERTIES
        COMPILE_DEFINITIONS "_UNICODE;UNICODE")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-unix-2.0)
    target_sources(sevenzip_panel PRIVATE src/platform/associations/qtfileassociationprobe.cpp src/platform/associations/qtfileassociationprobe.h)
    target_link_libraries(sevenzip_panel PRIVATE PkgConfig::GIO)
    target_sources(sevenzip_location PRIVATE src/platform/associations/qtassociationplatform_linux.cpp)
    target_link_libraries(sevenzip_location PRIVATE PkgConfig::GIO)
endif()
if(WIN32)
    # Source association records expose CSysString. Keep the Windows UTF-16
    # definition consistent between the library and its page/service consumers.
    target_compile_definitions(sevenzip_location PUBLIC _UNICODE UNICODE)
    target_sources(sevenzip_location PRIVATE src/platform/associations/qtassociationplatform_win.cpp)
    target_sources(sevenzip_panel PRIVATE src/platform/associations/qtfileassociationprobe.cpp src/platform/associations/qtfileassociationprobe.h)
    target_link_libraries(sevenzip_panel PRIVATE shlwapi)
elseif(APPLE)
    target_sources(sevenzip_panel PRIVATE src/platform/associations/qtfileassociationprobe_mac.mm src/platform/associations/qtfileassociationprobe.h)
    target_sources(sevenzip_location PRIVATE src/platform/associations/qtassociationplatform_mac.cpp src/platform/associations/qtassociationplatform_mac.mm
        src/platform/associations/qtmacassociationnative.h)
    target_link_libraries(sevenzip_location PRIVATE "-framework AppKit" "-framework UniformTypeIdentifiers")
endif()
