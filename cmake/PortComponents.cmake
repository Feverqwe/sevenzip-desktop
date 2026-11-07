# Production sources have one owner. Consumers link component targets directly.
function(sevenzip_add_port_component owner header)
    add_library(${owner} OBJECT ${ARGN} ${header})
    target_compile_features(${owner} PUBLIC cxx_std_23)
    target_include_directories(${owner} PRIVATE src ${SEVENZIP_NATIVE_INCLUDE_DIR}/CPP ${SEVENZIP_7ZIP_SOURCE_DIR}/CPP)
    target_link_libraries(${owner} PRIVATE Qt6::Widgets sevenzip_common)
    set_target_properties(${owner} PROPERTIES AUTOUIC OFF)
endfunction()

sevenzip_add_port_component(sevenzip_archive_backend src/archive/backend/archivebackend.h
    src/archive/backend/archivebackend.cpp
    src/archive/backend/archivebackend_codecs.cpp
    src/archive/backend/archivebackend_compression.cpp
    src/archive/backend/archivebackend_hash.cpp
    src/archive/backend/archivebackend_sourceextract.cpp
    src/archive/backend/archivebackend_sourceupdate.cpp
    src/archive/backend/archivebackend_validation.cpp
    src/archive/backend/archivebackend_workerprogress.cpp src/archive/backend/archivebackend_transfer.cpp)
target_link_libraries(sevenzip_archive_backend PRIVATE sevenzip_archive_transfer_backup sevenzip_overwrite_question)
target_sources(sevenzip_archive_backend PRIVATE src/archive/codecs/archivecodecpublication.h)

sevenzip_add_port_component(sevenzip_compress_presentation src/dialogs/compress/createarchivedialog.h
    src/dialogs/compress/createarchivedialog.cpp
    src/dialogs/compress/createarchivedialog_presentation.cpp
    src/dialogs/compress/createarchivedialog_advanced.cpp
    src/dialogs/compress/createarchivedialog_compression.cpp
    src/dialogs/compress/createarchivedialog_encryption.cpp
    src/dialogs/compress/createarchivedialog_expert.cpp
    src/dialogs/compress/createarchivedialog_formats.cpp
    src/dialogs/compress/createarchivedialog_language.cpp
    src/dialogs/compress/createarchivedialog_preferences.cpp
    src/dialogs/compress/createarchivedialog_resources.cpp
    src/dialogs/compress/createarchivedialog_sfx.cpp
    src/dialogs/compress/createarchivedialog_source.cpp
    src/dialogs/compress/createarchivedialog_volumes.cpp)

sevenzip_add_port_component(sevenzip_dialog_screen src/ui/dialogs/qtdialogscreenservices.h
    src/ui/dialogs/qtdialogscreenservices.cpp)

if(APPLE OR WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(sevenzip_window_mount_sources src/filemanager/qtfilemanagermountcommands.cpp)
endif()
sevenzip_add_port_component(sevenzip_main_window src/filemanager/window/mainwindow.h
    src/filemanager/window/qtworkspacetheme.cpp
    src/filemanager/window/qtworkspacetheme.h
    src/filemanager/window/qtnavigationbar.cpp
    src/filemanager/window/qtwindowworkspace.cpp
    src/filemanager/window/qtworkspaceapplication.cpp
    src/filemanager/window/qtworkspacequit.cpp
    src/filemanager/window/qtworkspacetaskresults.cpp
    src/filemanager/window/qtoperationsbutton.cpp src/filemanager/window/qtoperationsbutton.h
    src/filemanager/window/qtlocalfiletask.cpp src/filemanager/window/qtlocalfiletask.h
    src/filemanager/window/qtexternalfiletask.cpp src/filemanager/window/qtexternalfiletask.h
    src/filemanager/window/qtexternalfiletask_p.h
    src/filemanager/window/qtexternalfiletask_presentation.cpp
    src/filemanager/window/qtexternalfiletask_record.cpp
    src/filemanager/window/qtfavoritestore.cpp
    src/filemanager/window/qtfastfoldersstore.cpp src/filemanager/window/qtfastfoldersstore.h
    src/filemanager/window/qtnativeplaces.cpp src/filemanager/window/qtnativeplaces.h
    src/filemanager/window/qtfileclipboard.cpp
    src/filemanager/window/qtworkspacechrome.cpp
    src/filemanager/window/qtworkspacefocus.cpp
    src/filemanager/window/qtworkspacelifecycle.cpp
    src/filemanager/window/qtworkspacesession.cpp
    src/filemanager/window/qtworkspacelocation.cpp
    src/filemanager/window/qtworkspacearchivelocation.cpp
    src/filemanager/window/qtworkspacearchivepublication.cpp
    src/filemanager/window/qtworkspacenavigation.cpp
    src/filemanager/window/qtworkspacerecentplaces.cpp
    src/filemanager/window/qtrecentplacesstore.cpp
    src/filemanager/window/qtrecentplacesstore.h
    src/dialogs/recentplaces/qtrecentplacesdialog.cpp
    src/dialogs/recentplaces/qtrecentplacesdialog.h
    src/dialogs/taskresult/qttaskresultdialog.cpp
    src/dialogs/taskresult/qttaskresultdialog.h
    src/filemanager/window/qtsavedlocation.cpp
    src/filemanager/window/mainwindow.cpp
    src/filemanager/window/mainwindow_archive_transfer.cpp
    src/filemanager/window/mainwindow_archivecommands.cpp
    src/filemanager/window/mainwindow_extraction.cpp
    src/filemanager/window/mainwindow_guioperations.cpp
    src/filemanager/window/qtguioutputrefresh.cpp
    src/filemanager/window/mainwindow_archivelist.cpp
    src/filemanager/window/mainwindow_state.cpp
    src/filemanager/window/mainwindow_archiveopening.cpp
    src/filemanager/window/mainwindow_backendevents.cpp
    src/filemanager/window/mainwindow_close.cpp
    src/filemanager/window/mainwindow_startup.cpp src/filemanager/qtfilemanagersplitter.cpp
    src/filemanager/qtfilemanagerdirectorycommands.cpp
    src/filemanager/window/mainwindow_columns.cpp
    src/filemanager/window/mainwindow_refresh.cpp
    src/filemanager/window/mainwindow_dialogs.cpp
    src/filemanager/window/mainwindow_filelist.cpp
    src/filemanager/window/mainwindow_fileoperations.cpp
    src/filemanager/window/mainwindow_filesystemlisting.cpp
    src/filemanager/window/mainwindow_filesystemscan.cpp
    src/filemanager/window/mainwindow_language.cpp
    src/filemanager/window/mainwindow_menus.cpp
    src/filemanager/window/mainwindow_panel.cpp
    src/filemanager/window/mainwindow_panelkey.cpp
    src/filemanager/window/mainwindow_shortcuts.cpp
    src/filemanager/window/mainwindow_preferences.cpp
    src/filemanager/window/mainwindow_persistednavigation.cpp
    src/filemanager/window/mainwindow_propertyinvalidation.cpp
    src/filemanager/window/mainwindow_root.cpp
    src/filemanager/window/mainwindow_selection.cpp
    src/filemanager/window/mainwindow_selectionpaths.cpp
    src/filemanager/window/mainwindow_twopanel.cpp
    src/filemanager/panels/PanelKey.cpp
    src/filemanager/views/filepanelmodel.cpp src/filemanager/views/filepanelmodel.h
    src/filemanager/views/filerowdelegate.cpp
    src/filemanager/views/qtfileselectiongesture.cpp src/filemanager/views/qtfileselectiongesture.h
    src/filemanager/views/qtworkspacefiletable.cpp src/filemanager/views/qtworkspacefiletable.h
    src/filemanager/views/filetable.cpp src/filemanager/views/filetable.h src/filemanager/views/filetable_columns.cpp
    src/filemanager/window/qtapppresentation.cpp
    src/filemanager/window/qtappcopypresentation.cpp
    src/filemanager/rename/qtrenamecontroller.cpp src/filemanager/rename/qtrenamecontroller.h
    src/filemanager/qtfilemanagercommands.cpp
    src/filemanager/qtcommandavailability.cpp
    src/filemanager/qtcommandpublication.cpp
    src/filemanager/qtfilemanagercommandworkflows.cpp
    src/filemanager/qtfilemanagershellmenu.cpp
    src/filemanager/qtfilemanagershellmenupresentation.cpp
    src/platform/shell/qtnativeshellmenu.cpp
    src/filemanager/qtprogramfilemenu.cpp
    src/filemanager/qtfilemanagerdrag.cpp
    src/filemanager/qtfilemanagerdragpresentation.cpp
    src/filemanager/qtfilemanagerdraginput.cpp
    src/filemanager/qtfilemenuprojection.cpp
    ${sevenzip_window_mount_sources})
if(APPLE OR WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(sevenzip_main_window PRIVATE sevenzip_mount_ui)
endif()

sevenzip_add_port_component(sevenzip_native_menu_presentation src/platform/shell/qtnativeshellmenupresentation.h
    src/platform/shell/qtnativeshellmenupresentation.cpp)

sevenzip_add_port_component(sevenzip_gui_entry src/gui/qtguicommand.h src/gui/qtguimain.cpp)
sevenzip_add_port_component(sevenzip_file_manager_entry src/filemanager/qtfilemanagerstartup.h
    src/filemanager/qtfilemanagermain.cpp)
if(APPLE)
    target_sources(sevenzip_file_manager_entry PRIVATE
        src/app/qtapplicationstartup.h src/app/qtapplicationstartup_mac.mm)
endif()

sevenzip_add_port_component(sevenzip_compress_source src/dialogs/compress/CompressDialog.h
    src/dialogs/compress/CompressInfo.h
    src/dialogs/compress/CompressDialog.cpp
    src/dialogs/compress/CompressMethods.cpp
    src/dialogs/compress/CompressOptionsDialog.cpp
    src/dialogs/compress/CompressResources.cpp src/dialogs/compress/CompressResources.h)

sevenzip_add_port_component(sevenzip_agent src/archive/agent/Agent.h
    src/archive/agent/Agent.cpp src/archive/agent/AgentProxy.cpp src/archive/agent/AgentDirectory.cpp)
sevenzip_add_port_component(sevenzip_panel_selection src/filemanager/panels/PanelSelect.h
    src/filemanager/panels/PanelSelect.cpp)
sevenzip_add_port_component(sevenzip_file_transfer src/fileops/transfer/filetransferoperation.h
    src/fileops/transfer/filetransferoperation.cpp src/fileops/transfer/qtfiletransfertransaction.cpp
    src/fileops/transfer/qtfiletransfertransaction.h src/fileops/transfer/qtfiletransfercopy.cpp)
target_link_libraries(sevenzip_file_transfer PRIVATE sevenzip_overwrite_question)
sevenzip_add_port_component(sevenzip_filesystem_scan src/fileops/scanner/filesystemscanner.h
    src/fileops/scanner/filesystemscanner.cpp)

# Archive resources in this library call the typed backend even when a consumer
# only opens an options page. Carry that implementation in the resource target
# instead of requiring each executable to repeat the backend source closure.
target_link_libraries(sevenzip_location PRIVATE
    sevenzip_archive_backend sevenzip_compress_source sevenzip_agent)
target_sources(sevenzip_location PRIVATE
    src/archive/backend/archivebackend_guioperations.cpp
    src/archive/backend/archivebackend_test.cpp)

sevenzip_add_port_component(sevenzip_panel_context_menu src/filemanager/panels/qtpanelcontextmenu.h
    src/filemanager/panels/qtpanelcontextmenu.cpp)
set_target_properties(sevenzip_panel_context_menu PROPERTIES AUTOMOC OFF)

# Dialog-only processes do not need the File Manager window composition.
add_library(sevenzip_compression_ui STATIC)
set_target_properties(sevenzip_compression_ui PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_compile_features(sevenzip_compression_ui PUBLIC cxx_std_23)
target_link_libraries(sevenzip_compression_ui
    PUBLIC Qt6::Widgets sevenzip_location
    PRIVATE sevenzip_compress_presentation)
target_link_libraries(sevenzip_gui_entry PRIVATE sevenzip_compression_ui)
target_link_libraries(sevenzip_file_manager_entry PRIVATE sevenzip_window)
