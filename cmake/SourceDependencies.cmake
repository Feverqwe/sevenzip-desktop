# Unchanged dependency sources are expanded only inside this build tree.
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(SEVENZIP_7ZIP_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/dependencies/7zip")
set(SEVENZIP_NATIVE_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/compatibility/7zip")
set(SEVENZIP_DEPENDENCY_DIR "${CMAKE_CURRENT_BINARY_DIR}/dependencies")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/upstream-baseline.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/7zip/source.tar.xz"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/upstream_baseline.py")
file(GLOB dependency_archives CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/*/source.tar.xz")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${dependency_archives}
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dependency-sources.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/dependencies/source_archives.py")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dependency-sources.py"
        --destination "${SEVENZIP_DEPENDENCY_DIR}"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/upstream_baseline.py"
        dependency-snapshot --snapshot "${SEVENZIP_7ZIP_SOURCE_DIR}"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/upstream_baseline.py"
        header-snapshot --snapshot "${SEVENZIP_NATIVE_INCLUDE_DIR}"
    COMMAND_ERROR_IS_FATAL ANY)
