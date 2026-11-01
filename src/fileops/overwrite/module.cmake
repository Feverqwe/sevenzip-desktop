add_library(sevenzip_overwrite_question STATIC
    src/fileops/overwrite/overwritequestion.cpp
    src/fileops/overwrite/overwritequestion.h)
set_target_properties(sevenzip_overwrite_question PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_compile_features(sevenzip_overwrite_question PUBLIC cxx_std_23)
target_include_directories(sevenzip_overwrite_question PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(sevenzip_overwrite_question PUBLIC Qt6::Core sevenzip_native_types
    PRIVATE sevenzip_common)
