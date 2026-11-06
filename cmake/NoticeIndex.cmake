# Both platform install owners populate this directory before indexing it.
if(APPLE)
    set(notices_destination Sevenzip.app/Contents/Resources/Notices)
else()
    set(notices_destination share/licenses/sevenzip-desktop)
endif()
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION "${notices_destination}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallNoticeIndex.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/InstallNoticeIndex.cmake" @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/InstallNoticeIndex.cmake")
