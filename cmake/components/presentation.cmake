add_library(sevenzip_progress_sync STATIC
    src/ui/progress/progresssync.cpp src/ui/progress/progresssync.h
    src/ui/progress/qtworkerprogress.cpp src/ui/progress/qtworkerprogress.h)
target_include_directories(sevenzip_progress_sync PUBLIC src)
target_link_libraries(sevenzip_progress_sync PUBLIC Qt6::Core sevenzip_common)

add_library(sevenzip_ui_theme STATIC src/ui/theme/qtuitheme.cpp src/ui/theme/qtuitheme.h)
target_include_directories(sevenzip_ui_theme PUBLIC src)
target_link_libraries(sevenzip_ui_theme PUBLIC Qt6::Widgets)
set_target_properties(sevenzip_ui_theme PROPERTIES AUTOMOC OFF AUTOUIC OFF)

add_library(sevenzip_system_icons STATIC
    src/filemanager/SysIconUtils.cpp src/filemanager/SysIconUtils.h)
set_target_properties(sevenzip_system_icons PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_link_libraries(sevenzip_system_icons PUBLIC Qt6::Widgets sevenzip_common)
if(WIN32)
    target_link_libraries(sevenzip_system_icons PUBLIC shell32 comctl32)
endif()

add_library(sevenzip_name_sort STATIC
    src/filemanager/sorting/filenamesort.cpp src/filemanager/sorting/filenamesort.h)
set_target_properties(sevenzip_name_sort PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_include_directories(sevenzip_name_sort PUBLIC src)
target_link_libraries(sevenzip_name_sort PUBLIC Qt6::Core sevenzip_common)

# The private API dependency is intentional; require the matching Qt build below.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6GuiPrivate ${Qt6Gui_VERSION} EXACT QUIET CONFIG
        HINTS "${Qt6Gui_DIR}/../Qt6GuiPrivate")
endif()
if(NOT TARGET Qt6::GuiPrivate)
    message(FATAL_ERROR "Qt GUI private development headers are required for native keyboard decoding")
endif()
add_library(sevenzip_native_keys STATIC src/filemanager/panels/qtnativepanelkeys.cpp src/filemanager/panels/qtnativepanelkeys.h)
target_include_directories(sevenzip_native_keys PUBLIC src)
target_link_libraries(sevenzip_native_keys PRIVATE Qt6::GuiPrivate)
if(APPLE)
    target_link_libraries(sevenzip_native_keys PRIVATE "-framework Carbon")
endif()
set_target_properties(sevenzip_native_keys PROPERTIES AUTOMOC OFF AUTOUIC OFF)

add_library(sevenzip_text_report STATIC
    src/ui/reports/qttextreport.cpp src/ui/reports/qttextreport.h)
target_include_directories(sevenzip_text_report PUBLIC src)
target_link_libraries(sevenzip_text_report PUBLIC Qt6::Widgets)
set_target_properties(sevenzip_text_report PROPERTIES AUTOMOC OFF AUTOUIC OFF)

include(${PROJECT_SOURCE_DIR}/src/platform/process/module.cmake)

add_library(sevenzip_panel
    STATIC
    src/filemanager/SplitUtils.cpp
    src/filemanager/App.cpp
    src/filemanager/AppCopy.cpp
    src/filemanager/AppDiff.cpp
    src/filemanager/AppLink.cpp
    src/ui/theme/qtsemanticcolors.cpp
    src/ui/theme/qtsemanticcolors.h
    src/platform/memory/qtmemorylockservices.cpp
    src/platform/memory/qtmemorylockservices.h
    src/platform/memory/qtmemorylimitinfo.cpp
    src/platform/memory/qtmemorylimitinfo.h
    src/filemanager/panels/PanelCrc.cpp
    src/archive/hashing/qtfilecrc.cpp
    src/archive/hashing/qtfilecrc.h
    src/common/memory/midallocbuffer.h
    src/archive/operations/ArchiveName.cpp
    src/filemanager/panels/qtpaneldragmodel.h
    src/filemanager/panels/qtpaneldragtransport.cpp
    src/filemanager/panels/qtpaneldragtransport.h
    src/dialogs/about/AboutDialog.cpp
    src/dialogs/about/AboutDialog.h
    src/dialogs/browse/BrowseDialog2.cpp
    src/dialogs/browse/BrowseDialog2.h
    src/dialogs/browse/browseiteminspection.cpp
    src/dialogs/browse/browseiteminspection.h
    src/dialogs/browse/qttempbrowseinput.cpp
    src/dialogs/browse/qttempbrowsepresentation.cpp
    src/filemanager/views/qtfilelisthover.cpp
    src/filemanager/views/qtfilelisthover.h
    src/filemanager/HelpUtils.cpp
    src/filemanager/HelpUtils.h
    src/help/qthelpbrowser.cpp
    src/help/qthelpbrowser_p.h
    src/help/qthelpservices.cpp
    src/help/qthelpservices.h
    src/dialogs/compress/qtcompressoptionscontrols.cpp
    src/dialogs/compress/qtcompressoptionscontrols.h
    src/dialogs/compress/qtcompressoptionsdialog.h
    src/gui/UpdateGUI.cpp
    src/gui/UpdateGUI.h
    src/filemanager/SplitUtils.h
    src/dialogs/copy/CopyDialog.cpp
    src/dialogs/copy/CopyDialogDestination.cpp
    src/dialogs/copy/CopyDialog.h
    src/fileops/native/Dialog.cpp
    src/fileops/native/Dialog.h
    src/platform/filepicker/CommonDialog.cpp
    src/platform/filepicker/CommonDialog.h
    src/filemanager/MyLoadMenu.cpp
    src/filemanager/nativecommanddispatch.cpp
    src/filemanager/MyLoadMenu.h
    src/filemanager/qtfilemenuservices.h
    src/filemanager/qtfilemanagercommandservices.h
    src/filemanager/FM.cpp
    src/filemanager/qtfilemanagerwindowservices.h
    src/filemanager/state/qtappoperationstate.cpp
    src/filemanager/state/qtappoperationstate.h
    src/filemanager/panels/PanelDrag.cpp
    src/filemanager/panels/PanelDragSource.cpp
    src/gui/HashGUI.cpp
    src/gui/HashGUI.h
    src/benchmark/BenchmarkDialog.cpp
    src/benchmark/BenchmarkDialog.h
    src/operations/CompressCall.cpp
    src/operations/CompressCall2.cpp
    src/operations/CompressCall.h
    src/dialogs/compress/qtcompresscallservices.h
    src/gui/qtupdateguiservices.h
    src/benchmark/qtbenchmarkcontrols.cpp
    src/benchmark/qtbenchmarkcontrols.h
    src/benchmark/qtbenchmarkpresentation.cpp
    src/benchmark/qtbenchmarkreport.cpp
    src/dialogs/link/LinkDialog.cpp
    src/dialogs/link/LinkDialog.h
    src/dialogs/link/qtlinkcontrols.cpp
    src/dialogs/link/qtlinkpresentation.cpp
    src/dialogs/link/qtlinkcontrols.h
    src/filemanager/ExtractCallback.cpp
    src/filemanager/ExtractCallbackOutput.cpp
    src/archive/resources/virtualextractfilesystem.cpp
    src/archive/resources/virtualextractfilesystem.h
    src/filemanager/ExtractCallback.h
    src/archive/diagnostics/archiveerrormessages.cpp
    src/archive/diagnostics/archiveerrormessages.h
    src/archive/callbacks/qtcallbackdialogs.cpp
    src/archive/callbacks/qtcallbackdialogs.h
    src/ui/dialogs/qtmodalresult.h
    src/dialogs/overwrite/OverwriteDialog.cpp
    src/dialogs/overwrite/qtoverwritepresentation.cpp
    src/dialogs/overwrite/OverwriteDialog.h
    src/dialogs/mem/MemDialog.cpp
    src/dialogs/mem/qtmemoryrequestpresentation.cpp
    src/dialogs/mem/MemDialog.h
    src/filemanager/OpenCallback.cpp
    src/filemanager/OpenCallback.h
    src/filemanager/providers/FileFolderPluginOpen.cpp
    src/filemanager/providers/FileFolderPluginOpen.h
    src/filemanager/panels/PanelItemOpen.cpp
    src/filemanager/panels/PanelArchiveLifecycle.cpp
    src/filemanager/panels/PanelArchiveOpen.cpp
    src/filemanager/external/qtexternalapplications.cpp
    src/filemanager/external/qtexternalapplications.h
    src/filemanager/external/qtexternalarchivetarget.cpp
    src/filemanager/external/qtexternalarchivetarget.h
    src/filemanager/external/qtexternalfilesession.cpp
    src/filemanager/external/qtexternalfilesession.h
    src/filemanager/external/qtexternalfilerecord.cpp
    src/filemanager/external/qtexternalfilerecord.h
    src/filemanager/external/qtexternalsessionowner.h
    src/archive/resources/qtarchiveitemopen.cpp
    src/archive/resources/qtarchiveitemopen.h
    src/filemanager/VerCtrl.cpp
    src/fileops/links/qtverctrlfileservices.cpp
    src/fileops/links/qtverctrlfileservices.h
    src/platform/associations/qtassociatedfileservices.cpp
    src/platform/associations/qtassociatedfileservices.h
    ${sevenzip_external_process_source}
    src/platform/process/qtexternalprocess.h
    src/operations/qtexternalguicall.cpp
    src/operations/qtexternalguicall.h
    src/filemanager/panels/PanelCopy.cpp
    src/platform/shell/ContextMenu.cpp
    src/platform/shell/ContextMenu.h
    src/platform/shell/RegistryContextMenu.cpp
    src/platform/shell/RegistryContextMenu.h
    src/platform/shell/qtshellregistryservices.h
    src/platform/shell/qtmenupageservices.h
    src/platform/shell/qtnativemenupageservices.cpp
    src/platform/shell/qtnativemenupageservices.h
    src/platform/shell/qtshellmenupresentation.cpp
    src/platform/shell/qtshellmenupresentation.h
    src/platform/shell/qtcontextmenuservices.h
    src/filemanager/providers/RootFolder.cpp
    src/filemanager/providers/RootFolder.h
    src/filemanager/panels/PanelSort.cpp
    src/ui/progress/archiveprogresstransport.cpp
    src/ui/progress/archiveprogresstransport.h
    src/ui/progress/qtprogresstransport.h
    src/ui/progress/qtcallbackprogresstransport.cpp
    src/ui/progress/qtcallbackprogresstransport.h
    src/filemanager/FormatUtils.cpp
    src/filemanager/FormatUtils.h
    src/ui/progress/ProgressDialog2.cpp
    src/ui/progress/ProgressDisplay.cpp
    src/ui/progress/ProgressDialog2.h
    src/dialogs/messages/MessagesDialog.cpp
    src/dialogs/messages/MessagesDialog.h
    src/dialogs/messages/MessagesDialogRes.h
    src/gui/UpdateCallbackGUI2.cpp
    src/gui/UpdateCallbackGUI2.h
    src/gui/UpdateCallbackGUI.cpp
    src/gui/UpdateCallbackGUI.h
    src/gui/ExtractGUI.cpp
    src/gui/ExtractGUI.h
    src/dialogs/extract/ExtractDialog.cpp
    src/dialogs/extract/qtextractpresentation.cpp
    src/dialogs/extract/ExtractDialog.h
    src/filemanager/UpdateCallback100.cpp
    src/filemanager/UpdateCallback100.h
    src/archive/callbacks/qtcallbackpassword.cpp
    src/archive/callbacks/qtcallbackpassword.h
    src/crypto/fscrypt/fscryptkeydialog.cpp
    src/crypto/fscrypt/fscryptkeydialog.h
    src/ui/progress/qtprogresscontrols.cpp
    src/ui/progress/qtprogresspresentation.cpp
    src/ui/progress/qtprogresscontrols.h
    src/ui/dialogs/qtdialogunits.cpp
    src/ui/dialogs/qtdialogunits.h
    src/ui/dialogs/qtdialoglayout.cpp
    src/ui/dialogs/qtdialoglayout.h
    src/ui/dialogs/qtdialogsurface.cpp src/ui/dialogs/qtdialogsurface.h
    src/ui/controls/qtdisclosure.cpp
    src/ui/controls/qtdisclosure.h
    src/ui/controls/qtrecentpathbutton.cpp
    src/ui/controls/qtrecentpathbutton.h
    src/ui/controls/qtresponsiverow.cpp
    src/ui/controls/qtresponsiverow.h
    src/ui/controls/qtwrappingcheckbox.cpp
    src/ui/controls/qtwrappingcheckbox.h
    src/ui/controls/qtsecretedit.cpp
    src/ui/controls/qtsecretedit.h
    src/ui/controls/qtsourcesummary.cpp
    src/ui/controls/qtsourcesummary.h
    src/ui/controls/qtmetadatarows.cpp
    src/ui/controls/qtmetadatarows.h
    src/filemanager/panels/qtpanellist.cpp
    src/filemanager/panels/qtpanellist.h
    src/filemanager/panels/qtpanelfolder.cpp
    src/filemanager/panels/qtpanelfolderraw.cpp
    src/filemanager/panels/qtpanelfolder.h
    src/filemanager/App.h
    src/filemanager/panels/Panel.cpp
    src/settings/SettingsPage.cpp
    src/settings/qtsettingspagepresentation.cpp
    src/settings/SettingsPage.h
    src/ui/controls/qtmemorylimitspin.cpp
    src/ui/controls/qtmemorylimitspin.h
    src/settings/FoldersPage.cpp
    src/settings/FoldersPage.h
    src/filemanager/panels/PanelFolderChange.cpp
    src/filemanager/panels/PanelAddress.cpp
    src/filemanager/panels/PanelMenu.cpp
    src/filemanager/panels/panelmenuids.h
    src/filemanager/panels/PanelProperties.cpp
    src/filemanager/PropertyName.cpp
    src/filemanager/PropertyName.h
    src/localization/qtlangcontrols.cpp
    src/localization/qtlangcontrols.h
    src/ui/theme/qtcommandicons.cpp
    src/ui/theme/qtcommandicons.h
    src/filemanager/window/qtmainwindowchrome.cpp
    src/filemanager/window/qtmainwindowchrome.h
    src/filemanager/panels/qtpaneladdresscombo.cpp
    src/filemanager/panels/qtpaneladdresscombo.h
    src/filemanager/panels/qtaddresssegment.h
    src/filemanager/panels/qtpanellocationtransaction.cpp
    src/filemanager/panels/qtpanellocationtransaction.h
    src/localization/qtlangbuttonbox.cpp
    src/localization/qtlangbuttonbox.h
    src/ui/controls/qtstatuslabel.cpp
    src/ui/controls/qtstatuslabel.h
    src/filemanager/panels/PanelColumns.cpp
    src/filemanager/panels/PanelInfo.cpp
    src/filemanager/panels/PanelInfo.h
    src/filemanager/panels/PanelItems.cpp
    src/filemanager/panels/PanelListLoad.cpp
    src/filemanager/panels/PanelSelectionState.cpp
    src/filemanager/panels/PanelOperations.cpp
    src/filemanager/panels/PanelNameOperations.cpp
    src/filemanager/panels/PanelFolderOperations.cpp
    src/filemanager/panels/PanelFolderOperations.h
    src/filemanager/views/qtlistview.cpp
    src/filemanager/views/qtlistview.h
    src/filemanager/AppState.h
    src/dialogs/listview/ListViewDialog.cpp
    src/dialogs/listview/ListViewDialog.h
    src/dialogs/listview/qtlistviewdialogpresentation.cpp
    src/dialogs/edit/EditDialog.cpp
    src/dialogs/edit/EditDialog.h
    src/dialogs/combo/ComboDialog.cpp
    src/dialogs/combo/ComboDialog.h
    src/dialogs/password/PasswordDialog.cpp
    src/dialogs/password/PasswordDialog.h
    src/dialogs/browse/BrowseDialog.cpp
    src/dialogs/browse/BrowseDialog.h
    src/dialogs/browse/qtsourcebrowsedialog_p.h
    src/dialogs/browse/qtsourcebrowsepresentation.cpp
    src/filemanager/providers/NetFolder.cpp
    src/filemanager/providers/NetFolder.h
    src/filemanager/panels/PanelListNotify.cpp
    src/filemanager/panels/Panel.h)
target_link_libraries(sevenzip_panel PRIVATE sevenzip_dialog_screen sevenzip_location sevenzip_panel_selection sevenzip_text_report
    PUBLIC sevenzip_ui_theme)
# Resource callbacks can pull the panel's archive progress transport in after
# the first two scans of this existing static-library cycle.
set_property(TARGET sevenzip_panel PROPERTY LINK_INTERFACE_MULTIPLICITY 3)
target_link_libraries(sevenzip_panel PUBLIC sevenzip_overwrite_question)
set_target_properties(sevenzip_panel PROPERTIES AUTOUIC OFF)
target_link_libraries(sevenzip_panel PUBLIC sevenzip_progress_sync)
qt_add_resources(sevenzip_panel upstream_progress_icons
    PREFIX "/upstream/7zip/UI/FileManager"
    BASE "${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager"
    FILES ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/FM.ico
          ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/Add.bmp
          ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/Add2.bmp
          ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/Extract.bmp
          ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/Extract2.bmp
          ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/Test.bmp
          ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/Test2.bmp
          ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/Copy.bmp
          ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/Copy2.bmp
          ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/Move.bmp
          ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/Move2.bmp
          ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/Delete.bmp
          ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/Delete2.bmp
          ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/Info.bmp
          ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/Info2.bmp)
qt_add_resources(sevenzip_panel upstream_shell_icons
    PREFIX "/upstream/7zip/UI/Explorer"
    BASE "${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/Explorer"
    FILES ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/Explorer/MenuLogo.bmp)
target_include_directories(sevenzip_panel PUBLIC src)
target_include_directories(sevenzip_panel PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(sevenzip_panel PUBLIC Qt6::Widgets sevenzip_common sevenzip_system_icons
    sevenzip_native_keys)
target_sources(sevenzip_panel PRIVATE src/platform/shell/MyMessages.cpp src/archive/callbacks/qtguimessages.h)
if(WIN32)
    target_sources(sevenzip_panel PRIVATE
        src/filemanager/panels/PanelShellMenu_win.cpp
        src/filemanager/panels/PanelDrag_win.cpp
        src/platform/associations/qtassociatedfilelaunch_win.cpp
        src/platform/associations/qtassociatedfilelaunch_win.h
        src/filemanager/panels/qtpaneldragnative.cpp src/filemanager/panels/qtpaneldragnative.h
        ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/EnumFormatEtc.cpp
        src/filemanager/providers/AltStreamsFolder.cpp
        src/filemanager/providers/AltStreamsFolder.h
        src/platform/shell/wincontextmenuservices.cpp src/platform/shell/wincontextmenuservices.h
        src/platform/shell/winshellmenupresentation.cpp src/platform/shell/winshellmenupresentation.h)
    set_source_files_properties(src/platform/associations/qtassociatedfilelaunch_win.cpp PROPERTIES
        COMPILE_DEFINITIONS "_UNICODE;UNICODE")
    target_link_libraries(sevenzip_panel PUBLIC shell32)
endif()
target_sources(sevenzip_panel PRIVATE src/ui/progress/qtprogresswait.cpp src/ui/progress/qtprogresswait.h)
if(WIN32)
    add_library(sevenzip_shell SHARED src/platform/shell/DllExportsExplorer.cpp
        ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/Explorer/Explorer.def)
    set_target_properties(sevenzip_shell PROPERTIES OUTPUT_NAME "7-zip" AUTOUIC OFF AUTOMOC OFF)
    target_compile_definitions(sevenzip_shell PRIVATE _UNICODE UNICODE)
    target_link_libraries(sevenzip_shell PRIVATE sevenzip_panel)
    file(READ "${explorer_resource}" sevenzip_native_menu_resource)
    string(REPLACE "\"resource.h\""
        "\"${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/Explorer/resource.h\""
        sevenzip_native_menu_resource "${sevenzip_native_menu_resource}")
    string(REPLACE "\"../../UI/Explorer/MenuLogo.bmp\""
        "\"${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/Explorer/MenuLogo.bmp\""
        sevenzip_native_menu_resource "${sevenzip_native_menu_resource}")
    # Original Explorer resource.rc icon entry, with a build-only absolute path.
    string(APPEND sevenzip_native_menu_resource
        "\nIDI_ICON ICON \"${SEVENZIP_7ZIP_SOURCE_DIR}/CPP/7zip/UI/FileManager/FM.ico\"\n")
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/native_context_menu.rc" CONTENT
        "${sevenzip_native_menu_resource}" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${explorer_resource}")
    target_sources(sevenzip_shell PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/native_context_menu.rc")
endif()
if(APPLE)
    target_sources(sevenzip_panel PRIVATE src/ui/progress/qttaskbarprogress_mac.mm src/ui/progress/qttaskbarprogress.h)
    target_link_libraries(sevenzip_panel PUBLIC "-framework AppKit")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(sevenzip_panel PUBLIC sevenzip_linux_taskbar)
endif()
