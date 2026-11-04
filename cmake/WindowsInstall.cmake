# Qt and the retained Windows entrypoints share one deployment directory.
# Registration is a separate Menu-page operation; installing files never writes
# associations or registers the extension on the build machine.
if(VCPKG_MANIFEST_MODE)
    if(NOT TARGET Qt6::QWindowsIntegrationPlugin)
        find_package(Qt6QWindowsIntegrationPlugin REQUIRED PATHS "${Qt6Gui_DIR}")
    endif()
    # App-local DLL copying alone misses dynamically loaded Qt plugins and
    # their dependencies. Use Qt's deployment API for the build-tree app too.
    qt6_generate_deploy_script(TARGET sevenzip_desktop NAME sevenzip_build_tree
        OUTPUT_SCRIPT sevenzip_build_deploy_script CONTENT "
set(QT_DEPLOY_PREFIX \"$<TARGET_FILE_DIR:sevenzip_desktop>\")
qt_deploy_runtime_dependencies(
    EXECUTABLE \"$<TARGET_FILE:sevenzip_desktop>\"
    BIN_DIR . LIB_DIR . PLUGINS_DIR . GENERATE_QT_CONF NO_TRANSLATIONS
    INCLUDE_PLUGINS qoffscreen qsvgicon qsvg)
")
    add_custom_command(TARGET sevenzip_desktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -P "${sevenzip_build_deploy_script}"
        VERBATIM)
endif()

set(SEVENZIP_WINDOWS_RUNTIME "${SEVENZIP_CODEC_RUNTIME_DIR}" CACHE PATH
    "Official pinned Windows runtime prepared by scripts/build-codec-runtime.ps1")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/LICENSE"
    DESTINATION "share/licenses/sevenzip-desktop/zlib")
string(JSON windows_runtime_repository GET "${sfx_baseline_json}" repository)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
    set(windows_runtime_architecture arm64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(windows_runtime_architecture x86_64)
else()
    set(windows_runtime_architecture x86)
endif()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyWindowsRuntime.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/VerifyWindowsRuntime.cmake" @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/VerifyWindowsRuntime.cmake")
install(TARGETS sevenzip_desktop sevenzip_shell RUNTIME DESTINATION bin LIBRARY DESTINATION bin)
install(TARGETS sevenzip_device_reader RUNTIME DESTINATION bin)
install(TARGETS sevenzip_smb_server sevenzip_native_mount smb2 RUNTIME DESTINATION bin)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsmb2/COPYING"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsmb2/LICENCE-LGPL-2.1.txt"
    DESTINATION share/licenses/sevenzip-desktop/libsmb2)
install(FILES "$<TARGET_FILE:sevenzip_desktop>" DESTINATION bin RENAME 7zFM.exe)
install(FILES "$<TARGET_FILE:sevenzip_desktop>" DESTINATION bin RENAME 7zG.exe)
install(FILES "${SEVENZIP_WINDOWS_RUNTIME}/7z.exe" "${SEVENZIP_WINDOWS_RUNTIME}/7z.dll"
    DESTINATION bin)
install(FILES "${SEVENZIP_WINDOWS_RUNTIME}/runtime.json"
    DESTINATION share/sevenzip-desktop)
install(FILES "${SEVENZIP_WINDOWS_RUNTIME}/LICENSE.txt"
    DESTINATION share/licenses/sevenzip-desktop/7zip)
install(TARGETS sevenzip_exfat RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(TARGETS sevenzip_xfs RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/grub/COPYING"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-xfs)
install(TARGETS sevenzip_fat RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(TARGETS sevenzip_ext RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(TARGETS sevenzip_squashfs RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(TARGETS sevenzip_ntfs RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(TARGETS sevenzip_hfs RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(TARGETS sevenzip_iso RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(TARGETS sevenzip_udf RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(TARGETS sevenzip_mbr RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/LGPL-2.0"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-udf)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/Unicode-LICENSE.txt"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-iso)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/LICENSE"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-iso RENAME zlib-LICENSE.txt)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/zlib-LICENSE.txt"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-iso RENAME kernel-zlib-LICENSE.txt)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/exceptions/Linux-syscall-note"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-iso)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lzfse/LICENSE"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-hfs)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-ntfs)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/BSD-2-Clause"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-squashfs)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/LICENSE"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-squashfs RENAME zlib-LICENSE.txt)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/LGPL-2.0"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/BSD-2-Clause"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/BSD-3-Clause"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/crypto/asymmetric-NOTICES.txt"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/exceptions/Linux-syscall-note"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-ext)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-fat)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/exceptions/Linux-syscall-note"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-fat)
install(TARGETS sevenzip_erofs RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(TARGETS sevenzip_f2fs RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/grub/COPYING"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/grub/f2fs-tools-COPYING"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-f2fs)
install(TARGETS sevenzip_btrfs RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/exceptions/Linux-syscall-note"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/zlib-LICENSE.txt"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/brotli/7zip-zstd-LICENSE.txt"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-btrfs)
install(TARGETS sevenzip_linux_frames RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/BSD-2-Clause"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/zlib-LICENSE.txt"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/brotli/7zip-zstd-LICENSE.txt"
    DESTINATION "${notices_destination}/sevenzip-linux-frames")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/BSD-2-Clause"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/deprecated/0BSD"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/exceptions/Linux-syscall-note"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-erofs)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/LICENSE"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-erofs RENAME zlib-LICENSE.txt)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xz/COPYING.0BSD"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-erofs RENAME liblzma-COPYING.0BSD)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xz/COPYING.0BSD"
    DESTINATION share/licenses/sevenzip-desktop RENAME liblzma-COPYING.0BSD)
install(TARGETS sevenzip_brotli RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(TARGETS sevenzip_lz4_frame RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(TARGETS sevenzip_lzip RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(TARGETS sevenzip_encrypted_dmg RUNTIME DESTINATION bin/Formats LIBRARY DESTINATION bin/Formats)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/apfs-fuse/LICENSE"
    DESTINATION share/licenses/sevenzip-desktop/apfs-fuse)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lzlib/COPYING"
    DESTINATION share/licenses/sevenzip-desktop/lzlib)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lz4/lib/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/brotli/7zip-zstd-LICENSE.txt"
    DESTINATION share/licenses/sevenzip-desktop/lz4)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/brotli/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/brotli/7zip-zstd-LICENSE.txt"
    DESTINATION share/licenses/sevenzip-desktop/brotli)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd/7zip-zstd-LICENSE.txt"
    DESTINATION share/licenses/sevenzip-desktop/zstd-encoder)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-exfat)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION share/licenses/sevenzip-desktop/sevenzip-desktop)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lucide/LICENSE"
    DESTINATION share/licenses/sevenzip-desktop/lucide)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/qemu/LICENSE"
    DESTINATION share/licenses/sevenzip-desktop/qemu)
install(FILES "${SEVENZIP_SFX_RUNTIME_DIR}/7z.sfx"
    "${SEVENZIP_SFX_RUNTIME_DIR}/7z.sfx.json" DESTINATION bin)
qt_generate_deploy_app_script(TARGET sevenzip_desktop OUTPUT_SCRIPT windows_deploy_script
    NO_TRANSLATIONS)
install(SCRIPT "${windows_deploy_script}")
