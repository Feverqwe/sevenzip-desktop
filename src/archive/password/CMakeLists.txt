add_library(sevenzip_archive_password OBJECT archivepassword.cpp archivepassword.h)
set_target_properties(sevenzip_archive_password PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_include_directories(sevenzip_archive_password PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(sevenzip_archive_password PRIVATE sevenzip_native_types Qt6::Core)
