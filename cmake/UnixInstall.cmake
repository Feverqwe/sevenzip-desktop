# Install an explicit runtime payload; never copy the checkout or build tree.
if(APPLE)
    set(runtime_destination Sevenzip.app/Contents/Resources/7zip)
    set(notices_destination Sevenzip.app/Contents/Resources/Notices)
    install(TARGETS sevenzip_desktop BUNDLE DESTINATION .)
else()
    set(runtime_destination bin)
    set(notices_destination share/licenses/sevenzip-desktop)
    install(TARGETS sevenzip_desktop RUNTIME DESTINATION bin)
    install(PROGRAMS "$<TARGET_FILE_DIR:sevenzip_desktop>/7zFM"
        "$<TARGET_FILE_DIR:sevenzip_desktop>/7zG" DESTINATION bin)
endif()
if(APPLE)
    install(TARGETS sevenzip_smb_server sevenzip_native_mount
        RUNTIME DESTINATION Sevenzip.app/Contents/MacOS)
    install(TARGETS smb2 LIBRARY DESTINATION Sevenzip.app/Contents/Frameworks)
else()
    install(TARGETS sevenzip_smb_server sevenzip_native_mount RUNTIME DESTINATION bin)
    install(TARGETS smb2 LIBRARY DESTINATION lib)
endif()
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsmb2/COPYING"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsmb2/LICENCE-LGPL-2.1.txt"
    DESTINATION "${notices_destination}/libsmb2")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/LICENSE"
    DESTINATION "${notices_destination}/zlib")
install(PROGRAMS "${SEVENZIP_CODEC_RUNTIME_DIR}/7z"
    DESTINATION "${runtime_destination}")
install(FILES "${SEVENZIP_CODEC_RUNTIME_DIR}/7z.so"
    "${SEVENZIP_SFX_RUNTIME_DIR}/7zCon.sfx"
    "${SEVENZIP_SFX_RUNTIME_DIR}/7zCon.sfx.json"
    DESTINATION "${runtime_destination}")
install(FILES "${SEVENZIP_CODEC_RUNTIME_DIR}/LICENSE.txt"
    DESTINATION "${notices_destination}/7zip")
install(TARGETS sevenzip_exfat LIBRARY DESTINATION "${runtime_destination}/Formats")
install(TARGETS sevenzip_xfs LIBRARY DESTINATION "${runtime_destination}/Formats")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/grub/COPYING"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    DESTINATION "${notices_destination}/sevenzip-xfs")
install(TARGETS sevenzip_fat LIBRARY DESTINATION "${runtime_destination}/Formats")
install(TARGETS sevenzip_ext LIBRARY DESTINATION "${runtime_destination}/Formats")
install(TARGETS sevenzip_squashfs LIBRARY DESTINATION "${runtime_destination}/Formats")
install(TARGETS sevenzip_ntfs LIBRARY DESTINATION "${runtime_destination}/Formats")
install(TARGETS sevenzip_hfs LIBRARY DESTINATION "${runtime_destination}/Formats")
install(TARGETS sevenzip_iso LIBRARY DESTINATION "${runtime_destination}/Formats")
install(TARGETS sevenzip_udf LIBRARY DESTINATION "${runtime_destination}/Formats")
install(TARGETS sevenzip_mbr LIBRARY DESTINATION "${runtime_destination}/Formats")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/LGPL-2.0"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    DESTINATION "${notices_destination}/sevenzip-udf")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/Unicode-LICENSE.txt"
    DESTINATION "${notices_destination}/sevenzip-iso")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/LICENSE"
    DESTINATION "${notices_destination}/sevenzip-iso" RENAME zlib-LICENSE.txt)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/zlib-LICENSE.txt"
    DESTINATION "${notices_destination}/sevenzip-iso" RENAME kernel-zlib-LICENSE.txt)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/exceptions/Linux-syscall-note"
    DESTINATION "${notices_destination}/sevenzip-iso")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lzfse/LICENSE"
    DESTINATION "${notices_destination}/sevenzip-hfs")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    DESTINATION "${notices_destination}/sevenzip-ntfs")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/BSD-2-Clause"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    DESTINATION "${notices_destination}/sevenzip-squashfs")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/LICENSE"
    DESTINATION "${notices_destination}/sevenzip-squashfs" RENAME zlib-LICENSE.txt)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/LGPL-2.0"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/BSD-2-Clause"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/BSD-3-Clause"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/crypto/asymmetric-NOTICES.txt"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/exceptions/Linux-syscall-note"
    DESTINATION "${notices_destination}/sevenzip-ext")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    DESTINATION "${notices_destination}/sevenzip-fat")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/exceptions/Linux-syscall-note"
    DESTINATION "${notices_destination}/sevenzip-fat")
install(TARGETS sevenzip_erofs LIBRARY DESTINATION "${runtime_destination}/Formats")
install(TARGETS sevenzip_f2fs LIBRARY DESTINATION "${runtime_destination}/Formats")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/grub/COPYING"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/grub/f2fs-tools-COPYING"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    DESTINATION "${notices_destination}/sevenzip-f2fs")
install(TARGETS sevenzip_btrfs LIBRARY DESTINATION "${runtime_destination}/Formats")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/exceptions/Linux-syscall-note"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/zlib-LICENSE.txt"
    "${SEVENZIP_7ZIP_SOURCE_DIR}/DOC/copying.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/brotli/7zip-zstd-LICENSE.txt"
    DESTINATION "${notices_destination}/sevenzip-btrfs")
install(TARGETS sevenzip_linux_frames LIBRARY DESTINATION "${runtime_destination}/Formats")
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
    DESTINATION "${notices_destination}/sevenzip-erofs")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/LICENSE"
    DESTINATION "${notices_destination}/sevenzip-erofs" RENAME zlib-LICENSE.txt)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xz/COPYING.0BSD"
    DESTINATION "${notices_destination}/sevenzip-erofs" RENAME liblzma-COPYING.0BSD)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xz/COPYING.0BSD"
    DESTINATION "${notices_destination}" RENAME liblzma-COPYING.0BSD)
install(TARGETS sevenzip_brotli LIBRARY DESTINATION "${runtime_destination}/Formats")
install(TARGETS sevenzip_lz4_frame LIBRARY DESTINATION "${runtime_destination}/Formats")
install(TARGETS sevenzip_lzip LIBRARY DESTINATION "${runtime_destination}/Formats")
install(TARGETS sevenzip_encrypted_dmg LIBRARY DESTINATION "${runtime_destination}/Formats")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/apfs-fuse/LICENSE"
    DESTINATION "${notices_destination}/apfs-fuse")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lzlib/COPYING"
    DESTINATION "${notices_destination}/lzlib")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lz4/lib/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/brotli/7zip-zstd-LICENSE.txt"
    DESTINATION "${notices_destination}/lz4")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/brotli/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/brotli/7zip-zstd-LICENSE.txt"
    DESTINATION "${notices_destination}/brotli")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd/7zip-zstd-LICENSE.txt"
    DESTINATION "${notices_destination}/zstd-encoder")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/LICENSES/preferred/GPL-2.0"
    DESTINATION "${notices_destination}/sevenzip-exfat")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION "${notices_destination}/sevenzip-desktop")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lucide/LICENSE"
    DESTINATION "${notices_destination}/lucide")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/qemu/LICENSE"
    DESTINATION "${notices_destination}/qemu")
get_target_property(sevenzip_qt_core_type Qt6::Core TYPE)
# Static Qt embeds its libraries and plugins; Qt's runtime deployment API only
# supports shared builds. The native payload above is installed in either case.
if(NOT sevenzip_qt_core_type STREQUAL "STATIC_LIBRARY")
    if(APPLE)
        qt_generate_deploy_app_script(TARGET sevenzip_desktop OUTPUT_SCRIPT unix_deploy_script
            NO_TRANSLATIONS DEPLOY_TOOL_OPTIONS
            "-executable=Sevenzip.app/Contents/MacOS/SevenzipDeviceReader"
            "-executable=Sevenzip.app/Contents/MacOS/SevenzipSmb"
            "-executable=Sevenzip.app/Contents/MacOS/SevenzipMount")
    else()
        qt_generate_deploy_app_script(TARGET sevenzip_desktop OUTPUT_SCRIPT unix_deploy_script
            NO_TRANSLATIONS)
    endif()
    install(SCRIPT "${unix_deploy_script}")
endif()
