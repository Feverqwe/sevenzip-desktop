# Regression and security traceability

Use this map to select the affected source owner and direct CTest consumers.
Confirm current test registration, fixtures and platform conditions in code;
the table is a navigation aid, not a test result or exhaustive run list.
[Testing](testing.md) owns validation scope and commands. Slow suites require
[explicit opt-in](development.md#long-suites), including when listed below.

Fixtures use disposable local data and public commands/signals. Do not use
personal archives or live services. Source provenance and dated validation
belong in [the audit](upstream-port-audit.md).

## Scenario and owner

| Scenario | Functional owner and integration boundary | CTest consumers |
| --- | --- | --- |
| encrypted DMG read-only opening, mounting and extraction | Encrypted container stream, native recursive readers, opening/password owner, HFS+ path projection and direct exFAT files | `encrypted_dmg`, `encrypted_dmg_ui`, `exfat`, `exfat_backend`, `hfs_stream`, `native_listing`, `archive_open_options` |
| Apple sparseimage/sparsebundle read-only access | Sparse index/band providers, shared DMG crypto, native/backend extraction, package activation and damaged-file consent | `sparse_image`, `sparse_image_ui`, `encrypted_dmg`, `archive_open_options`, `open_callback` |
| APFS directory browsing | Deferred metadata reads, bounded recursive-read amplification, stable paths, failure/Cancel/retry, explicit subtree size, Unicode/hardlink copy without implicit attribute files, lazy isolated mount view, encrypted SMB source/export | `apfs_directory`, `sparse_image_ui`, `sparse_image_network` (native SMB on macOS) |
| Lazy SMB directory transport | Deferred CREATE and QUERY_DIRECTORY, related compound dependencies, another client during metadata wait, cache/retry and invalid names | `smb_readonly_server`, `archive_mount_transport`; [scope](upstream-port-audit/readonly-smb.md#lazy-apfs-mount-directories--2026-11-04) |
| Read-only mount preparation | Visible stage/count/elapsed time, unknown-work indicator, responsive Cancel and EN/RU normal/compact controls | `archive_readonly_view`, `mount_extraction_prompt` (native Cocoa on macOS) |
| encrypted DMG on an SMB source, repeated forward and random reads | Native macOS source I/O and nested SMB mounts; large file, delayed input, concurrent QuickLook/VLC and seeks, no extraction | `encrypted_dmg_network` (explicit slow opt-in; [command and scope](upstream-port-audit/dmg-encryption.md#network-transport-buffering--2026-11-03)) |
| source window placement | InitInstance record/default/show/refusal/fallback, actual normal/maximized Close/restart, and source splitter coordinate/bounds/ratio/handle/F9 lifecycle | `window_placement`, `window_info`, `app_lifecycle_window`, `file_manager_startup`, `main_shell`, `persisted_location`, `panel_key`, `panel_creation_path`, `panel_folder_sync`, `native_command_dispatch`, `source_context_menu`, `native_toolbar_state`, `toolbar_archive_workflow`, `panel_activation`, `filesystem_selection_runtime`, `panel_sort_commands` |
| source context menu | CPanel OnContextMenu header/list/keyboard/empty/Cancel/numeric routing; captured owning pane, popup peer-focus/Close lifetime and actual source Create | `source_context_menu`, `panel_shell`, `shell_context`, `shell_menu_window`, `native_command_dispatch`, `source_create`, `panel_name_text` |
| PanelOperations/Comment | Complete worker/Create/Delete/Rename/Comment disposition; source-owned guarded publication, Cancel, failed reload/recovery and real ZIP/AES/nested writes | `filesystem_comment`, `native_comment_ui`, `native_comment`, `source_create`, `source_delete`, `panel_rename_state`, `panel_refresh_names`, `panel_refresh_notifications`, `panel_refresh_drop`, `panel_refresh_completion` |
| ViewSettings/Copy history | Source UStringVector/upper-case distinction, K/K persistence and CApp Cancel/accept/reopen; popup completion restores command availability; related binary settings/columns/toolbar consumers | `copy_dialog`, `app_copy_routing`, `view_settings_columns`, `settings`, `native_toolbar_state`, `list_settings_runtime`, `main_shell`, `persisted_location`, `fm_settings`, `toolbar_archive_workflow`, `source_context_menu` |
| six Options pages/Apply errors | Source PSNRET_NOERROR after association error continues later pages; Settings PSNRET_INVALID and MyMessages policy remain | `options_integration`, `options_dialog`, `system_page`, `menu_page`, `language`, `language_ui`, `options_window`, `association_services` |
| source selection lifecycle | CPanel owns mask dialog/default/Cancel/application; source index/mark loops, both panes/modes, inactive owner and independent peer | `source_selection`, `panel_selection`, `ui_state`, `flat_view`, `combo_dialog`, `panel_key`, `panel_item_changed` |
| source Rename activation | Source focused-row RenameFile and parent/read-only OnBeginLabelEdit; editor intent/draft survives repaint, failed editor leaves activation available, queued rename respects navigation/shutdown; table/icon and operated marks independent of focus | `rename_lifecycle`, `deferred_rename`, `panel_rename_state`, `panel_key`, `panel_item_changed`, `panel_name_text`, `ui_operations`, `alt_streams_command` |
| source Delete lifecycle | CPanel confirmation/accepted-result refresh and one source error, No/Cancel early return, partial bytes/provider/peer and deferred guards; Move transaction retains explicit-index seam | `source_delete`, `ui_operations`, `native_fs_operations`, `panel_key`, `alt_streams_command`, `archive_transfer_backup` |
| source Create File/Folder | CPanel owns complete dialog/validation/operation/refresh and first-component focus; alternative marks, collision, Cancel and deferred-scan guards | `source_create`, `large_list_window`, `ui_operations`, `panel_key`, `alt_streams_command` |
| source Split/Combine | CApp selection/peer destination and native plans; UInt64/Trim, contiguous prefix/empty parts, NIO/preallocation, progress and joined transactional failure/cancel | `volume_dialog`, `volume_workflow`, `volume_operations`, `combine_names`, `worker_window_progress` |
| filesystem comparator/cache | Source CFSFolder::CompareItems and IFolderCompare binding; actual FileTable/CPanel sorting, raw versus visible comments, metadata failure/cache/reload, selection and bytes | `panel_filesystem_sort`, `fs_folder`, `panel_sort`, `panel_archive_sort`, `panel_column_sort`, `filesystem_reload` |
| compression method/control states | Source method IDs, Copy/unknown dictionary, empty/Auto controls, TAR memory visibility; Store success/failure/Cancel and exact bytes | `compression_method_state`, `compression_profile_selection`, `source_compress_dialog`, `codec_creation_ui` |
| GUI path/history consumers | Source CompressDialog/ExtractDialog Trim, path resolution, shared UString uniqueness; EN/RU Create/list/Extract, SplitDest, Cancel, settings and exact bytes | `dialog_path_rules`, `compression_path`, `extract_dialog`, `split_destination` |
| archive directory property projection | Source GetProperty, CalculateSizes and CompareItems; list/Properties/error/retry, native ZIP/7z sort/Refresh/selection and byte preservation | `agent_property_projection`, `native_column_workflow`, `source_properties`, `panel_column_sort` |
| Agent provider lifecycle/metadata | Source Close/ReOpen/LoadItems, fresh binding after repair; ZIP/WIM proxy Cancel/retry; source written-count descriptor policy | `agent_lifecycle`, `agent_opening`, `archive_property_catalog` |
| source Agent opening/factory | CArchiveFolderManager/CAgent Open and binding; backend reader/catalog and stream lease; S_FALSE diagnostics, progress/Cancel, Extract/Test/error bytes | `agent_opening`, `agent_folder`, `archive_opening`, `archive_open_options`, `archive_property_catalog` |
| source inventory | Per-module audit and baseline | `upstream_inventory`, `upstream_baseline` |
| independent panels | CApp/CPanel; Qt per-window operation state | `app_state`, `panel_runtime`, `binding_lifecycle` |
| availability and dispatch | CApp/WndProc/PanelMenu; reentrant navigation discards stale action updates and permits subsequent selection | `native_command_dispatch`, `command_policy_runtime`, `menu_policy`, `command_publication` |
| address/parent/nested rollback | PanelFolder/PanelOpen; native folder binding | `native_navigation_window`, `panel_navigation`, `ui_navigation`, `nested_archive` |
| filesystem/flat enumeration | FSFolder/PanelItems; scanner/model projection | `fs_folder`, `filesystem_scanner`, `flat_view` |
| columns/sort/time/refresh | PanelSort/PanelItems; shared Qt column binding and header/viewport adapter | `panel_columns`, `panel_column_schema`, `panel_column_changes`, `native_column_workflow`, `panel_column_menu`, `panel_column_sort`, `panel_sort`, `panel_auto_refresh`, `view_settings_columns` |
| native publication failure and open guard handoff | CPanel::OnReload/OpenItem/OpenAsArc_Index; QtFolderProjection and Copy completion | `native_renderer_failure`, `native_renderer_copy`, `panel_item_icons` |
| native nested stream/memory/disk open, publication failure, confirmed Cancel and retry | Original Agent::GetStream/OpenItemInArchive; Qt native item-order/module lease and deferred guard handoff | `native_nested_stream`, `native_nested_memory`, `native_nested_disk`, `nested_archive`, `nested_edit_chain` |
| native external editor update with publication failure, reentrant close and actual writer retry | PanelItemOpen::OnOpenItemChanged; Agent CopyFromFile/UpdateOneFile/CommonUpdate and Qt terminal publication/focus | `native_external_update` (focused), `native_external_2`, `native_external_301` (slow), `archive_external_edit`, `archive_external_lifetime`, `source_update_backend` |
| focus/marks/keyboard/mouse | PanelSelect/PanelKey; shared hover adapter | `panel_key`, `panel_key_source`, `panel_selection`, `list_settings_runtime`, `list_activation` |
| two-panel focus/persistence | CApp/Panel; MainWindow projection | `panel_focus`, `panel_runtime`, `window_info`, `ui_copy_same` |
| menu/toolbar/context/status | MyLoadMenu/PanelMenu/PanelListNotify; Qt presentation | `main_shell`, `view_menu`, `panel_shell`, `native_toolbar_state`, `file_menu_policy` |
| progress/pause/cancel | CProgressDialog/CProgressSync; native worker and child-operation transport | `progress_dialog`, `progress_cancel`, `progress_sync`, `worker_checkpoint` |
| conflict decisions/rollback | OverwriteDialog/AutoRenamePath/FSFolderCopy; transaction | `extract_prompt`, `file_transfer`, `file_transfer_direct` |
| selected/root/batch Ask | ExtractGUI/ExtractCallback/PanelCopy; ArchiveBackend path guard | `source_extract_backend`, `source_extract_contract`, `extraction_options`, `ui_operations` |
| toolbar Extract/Test in both panes, archive Copy To, one/multiple filesystem archives, Cancel, corrupt archive, caption/language/focus | CApp/CPanel ExtractArchives/TestArchives; CompressCall2/ExtractGUI/ExtractDialog; QtShellArchiveServices backend bridge | `toolbar_archive_workflow`, `toolbar_archive_batch`, `shell_archive_services`, `language_dialog`, `ui_navigation`, `ui_archive_states`, Windows `native_gui_workflow` |
| disk/archive transfer routes | CApp::OnCopy/PanelCopy/Agent; backend snapshot/rollback | `ui_operations`, `ui_copy_same`, `archive_transfer_backup`, `source_update_backend` |
| bound archive destination publication and rollback/reopen recovery | CApp::OnCopy/RefreshListCtrl; CAgent::Close and caller-supplied backup close service; physical ReOpen/fallback | `copy_archive_refresh`, `interarchive_recovery`, `archive_transfer_backup`, `ui_operations` |
| bound filesystem Copy/Move publication, both panes and deferred lists | CApp::OnCopy/OnReload; retained CFSFolder worker and per-pane scan guards | `ui_copy_routing`, `filesystem_scanner`, `filesystem_reload` |
| copy/merge/cross-volume Move | FSFolderCopy/CCopyStateIO; exclusive transaction | `fs_folder_copy`, `file_transfer`, `file_transfer_direct`, `worker_window_progress` |
| create/rename/delete/trash/comments | Panel/FSFolder/FileDir; native trash service | `filesystem_operations`, `native_fs_operations`, `ui_operations`, `filesystem_comment` |
| archive mutation/publish | ArchiveFolder/AgentOut; native writer and publication | `source_update_backend`, `codec_update`, `ui_archive_states` |
| native F2 Rename writer/publication/retry and guarded periodic status | PanelOperations::OnEndLabelEdit; Agent RenameItem/CommonUpdate; Qt rename/timer/status projection | `native_rename_2`, `native_rename_301`, `panel_rename_state`, `panel_processing`, `panel_refresh_completion` |
| Add/update/path modes | CompressDialog/UpdateGUI/UpdatePair; Qt controls/backend | `source_compress_dialog`, `shell_update_window`, `ui_update_modes`, `source_update_backend` |
| archive volumes | CArchiveLink/CMultiOutStream; leased volume publication | `archive_volume_creation`, `volume_workflow`, `ui_archive_volumes` |
| options/memory/deletion lifecycle | COptionsDialog/CompressDialog/Update; Qt control/worker bridge | `compression_advanced`, `compression_memory`, `compression_limit`, `archive_creation_lifecycle` |
| external View/Edit and archive copy updates | System association, process observation and GUI delivery | `external_launch`, `archive_external_edit`, `archive_external_lifetime`, `archive_external_security` |
| nested parent write chain | PanelItemOpen/AgentOut; retained resource/parent publication | `nested_edit_chain`, `nested_update`, `owned_temp_publication` |
| changed native Parent: Yes/No/Cancel, actual writer failure/Cancel, ignored publication error, retry and spool cleanup | OpenParentArchiveFolder/CloseOneLevel/OpenParentFolder; original CopyFromFile and non-owning Qt callback publication | `native_parent_publication`, `panel_navigation`, `nested_edit_chain` |
| properties/raw/folder size | PropertyName/PropIDUtils/Agent/Panel; typed native property projection, retained native levels/catalog diagnostics | `source_properties`, `native_archive_properties`, `properties_runtime`, `property_formatting`, `folder_size_view` |
| root/mount/network/Browse/device image | RootFolder/FSDrives/NetFolder; platform mount service and original device CopyTo | `root_navigation`, `volume_mount`, `network_folder`, `browse_dialog`, `fs_drives_device`, `fs_drives_device_ui`, `native_network_navigation` (Windows) |
| Options Apply/Cancel | OptionsDialog/*Page; Qt page container | `options_dialog`, `options_integration`, `options_window`, `fm_settings` |
| Explorer menu policy | OS registration service | `menu_page`, `context_menu_settings`, `shell_registry` |
| language/fallback/focus | CLang/LangUtils/*Dialog; Qt LanguageChange/bindings | `language`, `language_ui`, `language_dialog`, `language_standard_buttons`, `language_focus`, `language_source_update` |
| Help/About/temp cleanup | HelpUtils/AboutDialog/TempFiles; CHM viewer and owned leases | `help_source`, `help_ui`, `temp_cleanup`, `owned_temp`, `temp_detached_launch` |
| panel/archive drag | PanelDrag/CompressCall2; QDrag/MIME/leased materialization; native bound writer/publication/refusal/retry | `drag_drop`, `drag_archive`, `drag_drop_ui`, `drag_drop_add_ui`, `drag_drop_add_independent`, `drag_drop_add_temporary`, `source_drop_add`, `native_drop_publication` |
| ZIP item comment | ArchiveFolder/Zip writer; retained native writer callback | `native_comment`, `native_comment_ui`, `native_zip_writer` |
| real decoder RAM request | RAR handler/OpenCallback/ExtractCallback; backend callback bridge | `native_rar_memory`, `native_rar_ui`, `open_callback` |
| formats/capabilities/KeepName | LoadCodecs/OpenArchive/Agent; immutable provider leases | `codec_catalog`, `codec_capabilities`, `codec_commands_ui`, `native_open_commands` |
| SFX/external modules | WriteSfxModule/LoadCodecs; official module identity/publication | `sfx_creation`, `external_codecs`, `codec_probe` |
| CRC/Hash/Test | HashCalc/HashGUI/ExtractGUI; native providers and results view | `native_hash_workflow`, `native_hash_ui`, `hash_results`, `shell_archive_services` |
| Benchmark | Bench/BenchmarkDialog; Qt controls and worker lifetime | `native_benchmark`, `benchmark_command`, `benchmark_lifecycle` |
| links/junctions/metadata | FileIO/FileDir/Reparse/LinkDialog; native platform bridge | `filesystem_link`, `link_command`, `source_link_storage`, `source_metadata_storage` |
| ADS/proxy2 and source zone policy | RootFolder/AltStreamsFolder/AgentProxy/CCopyStateIO/PanelCopy; official engine metadata transport | `alt_streams_folder`, `alt_streams_command`, `alt_streams_archive_ui`, Windows `native_zone_copy`, `source_properties`, `native_update_path` |
| absolute/ElimDup/security extraction | ArchiveExtractCallback/FileDir; backend bounded-path/link guard | `source_extract_advanced`, `source_link_extract`, `extraction_options`, `archive_metadata_ui` |
| associations/open events | RegistryAssociations/SystemPage; per-user native APIs; native handler image and pending/Apply/Cancel/conflict preservation | `association_services`, `association_file_open`, `system_page`, Linux `linux_system_page_*`/`linux_association_clear_*`, `file_manager_startup` |
| native shell/menu/GUI integration | ContextMenu/MyLoadMenu/CompressCall; COM/HMENU/process services | `shell_context`, `shell_menu_window`, `shell_update_gui`, `shell_registry`; Windows `native_menu_integration`, `native_shell_data`, `native_system_menu`, `native_gui_workflow` |
| bounded progress/list/lifetime | Source enumeration/copy/hash/split; Qt worker scheduling | `large_list_window`, `worker_progress`, `worker_window_progress`, `archive_volume_creation` |
| one authoritative implementation | Source DTO/properties/volumes/Sync; reviewed integration boundaries | Direct consumers in [the glue register](archive/qt-port/upstream-port-audit.md#explicit-root-file-scope-258-paths-reviewed-component-register); all-target compilation |


## Failure and security behavior

| Critical failure / security property | Concrete public regression |
| --- | --- |
| Wrong password/refusal followed by retry; no partial output or password in output | `archive_backend::encryptedRoundTrip`; `source_update_backend::wrongPasswordAndCancellationKeepProtectedArchive`; `archive_external_security` |
| Invalid/traversal/absolute names cannot publish | `archive_backend::unsafePath/unsafeListing/unsafeDestination`; `codec_update::unsafeNativeMutationDoesNotPublish`; `native_update_path`; `source_update_backend::sourceNamesRequiringCorrectionAreRejected` |
| Linked destination and native links/ADS cannot escape bounded output | `extraction_options::skipRejectsLinkedDestination`; `source_link_extract`; `source_extract_advanced`; `temp_cleanup`; `native_update_path` |
| Case/Unicode equivalence and duplicate names do not prove native indices | `codec_update::canonicallyEquivalentMetadataDoesNotProveNativeIndexes/duplicateNamesDoNotProveNativeIndexes`; `extraction_options::flatNameCollision`; `source_update_backend::implicitFolderRenameAndDelete` |
| Interrupted update/cancel preserves original and permits reuse | `source_update_backend::cancellationInNativeTransactionKeepsOriginalArchive`; `owned_temp_publication`; `archive_workflow` |
| CopyFrom into archived ADS preserves main/other streams; directory/failure/Cancel cannot publish | `agent_stream_update` (official WIM writer, all-platform ordinary input and Windows filesystem ADS prefix); `native_update_path` (malformed ADS paths) |
| Archive proxy replacement cannot query old focused rows during peer publication | `agent_stream_copy_ui` (FS/ZIP→WIM ADS, F5/F6/Cancel, Delete Cancel/rollback, marks/focus, both panes/languages); `copy_archive_refresh`; `interarchive_recovery`; `ui_copy_routing` |
| GUI early errors retain source messages/question suppression before work | `gui_error_policy` (missing writer/recovery Cancel, both invalid Extract paths, EN/RU, source flag on/off, unchanged bytes and fitted message/OK JSON); `shell_update_gui`; `source_extract_backend/advanced/contract` |
| Partial archive read or Move/overwrite failure cannot silently discard sources | `file_transfer::crossVolumeDeleteFailureReportsPartialMove/directoryMergeCancelRestoresOriginalTree`; `ui_operations::archivePanelsCopyAndMoveThroughStaging/damagedArchiveTransferPreservesBothArchives`; `interarchive_recovery_native` (native reopen failure/Cancel/restore); full `interarchive_recovery::rollbackFailureAndRetry` is slow; `archive_transfer_backup` (including refused close/retry) |
| Device image Copy/Cancel/failure preserves its read-only source | Windows `fs_drives_device::sourceCopyUsesReadOnlyPrivateDevice(cancel/failure/skip/move)`; `fs_drives_device_ui::sourceDeviceNamespaceAndF5` |
| Namespace Hash availability; private device SHA256 success/Cancel/refusal/recovery and ADS digest | `namespace_hash` (all-platform capability guard; Windows native read-only VHD/ADS, EN/RU result geometry and unchanged input) |
| Native provider discovery failure/cancellation and independent operation startup/failure | `codec_probe`; `operation_launcher`; `operation_creation`; `agent_opening::readCancellationJoinsInitialCodecLoading` |
| External child/temp lifetime and pending modal close | `archive_external_lifetime`, `archive_external_security`, `temp_detached_launch`, `owned_temp` |
| Window destruction joins workers and rejects obsolete callbacks | `worker_window_progress`, `large_list_window`, `app_lifecycle_window`, `binding_lifecycle` |
| Nested error/wrong-password rollback and read-only refusal | `nested_edit_chain::rejectedInnerPasswordRestoresParent/readOnlyLevelPreventsChainWrites`; `nested_update` |
| Reports omit editable values, including shown passwords | `compression_encryption::methodsAndShowPassword` verifies shown and masked report dumps omit both inputs; `tests/ui/support/layout_inspection.cpp` report schema; current report/log scan in P3-02 evidence |
| Create source validation order, suppressible errors, unconditional memory/small-volume refusal and real writer recovery | `compression_error_policy` (EN/RU, disabled password controls, unknown-RAM fallback, unchanged settings/input and normal/compact message geometry); `source_archive_browse`, `compression_encryption`, `compression_limit`, `gui_error_policy` |
| Saved compression values restore source nearest/clamped dictionary/order/solid and unavailable-thread behavior | `compression_profile_selection` (EN/RU, Cancel/restart, real accepted Create/list/test/extract and exact bytes); `compression_preferences`, `compression_dictionary`, `compression_order`, `compression_solid`, `compression_resources` |


Native desktop, privilege, filesystem and recipient prerequisites must be checked
for the selected platform. A skipped required fixture does not establish acceptance.
Finder/Explorer recipients, OS panels and physical input have separate scopes.
See [geometry](testing/geometry.md) for layout checks and
[acceptance boundaries](ux-port-audit.md) for the completed port.

The [historical matrix](archive/qt-port/regression-matrix.md) retains component
receipts and original platform results.
