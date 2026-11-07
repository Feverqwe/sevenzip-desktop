add_library(sevenzip_regional_settings STATIC
    src/platform/locale/qtregionalsettings.cpp src/platform/locale/qtregionalsettings.h)
target_include_directories(sevenzip_regional_settings PUBLIC src)
target_link_libraries(sevenzip_regional_settings PUBLIC Qt6::Core)
if(APPLE)
    target_sources(sevenzip_regional_settings PRIVATE src/platform/locale/qtregionalsettings_mac.mm)
    set_source_files_properties(src/platform/locale/qtregionalsettings_mac.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(sevenzip_regional_settings PRIVATE "-framework Foundation")
elseif(WIN32)
    target_sources(sevenzip_regional_settings PRIVATE src/platform/locale/qtregionalsettings_win.cpp)
endif()
