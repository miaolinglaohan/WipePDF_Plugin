cmake_minimum_required(VERSION 3.20)
project(WipePDF_Plugin VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(/utf-8)

# Acrobat Plugins on Windows are dynamic libraries (.dll) with .api extension
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".api")

set(ACROBAT_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
message(STATUS "Acrobat SDK Directory: ${ACROBAT_SDK_DIR}")

include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${ACROBAT_SDK_DIR}/Headers/SDK"
    "${ACROBAT_SDK_DIR}/Headers/API"
)

add_definitions(
    -DWIN_PLATFORM=1
    -DWIN_ENV=1
    -DPLATFORM=\"winpltfm.h\"
    -DWIN32=1
    -D_WINDOWS
    -D_USRDLL
    -DWIPEPDF_EXPORTS
    -DPLUGIN=1
    -DPRODUCT="Plugin.h"
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS "Configuring 64-bit Acrobat Plugin")
    add_definitions(-D_WIN64 -DACRO_SDK_64BIT)
else()
    message(STATUS "Configuring 32-bit Acrobat Plugin")
endif()

set(PLUGIN_SOURCES
    src/PluginMain.cpp
    src/MenuHandler.cpp
    src/SelectionTool.cpp
    src/WatermarkService.cpp
    sdk/Headers/API/PIMain.c
    src/WipePDF.def
)

add_library(WipePDF SHARED ${PLUGIN_SOURCES})

set_target_properties(WipePDF PROPERTIES
    OUTPUT_NAME "WipePDF"
    PREFIX ""
    SUFFIX ".api"
)

target_link_libraries(WipePDF PRIVATE
    user32
    gdi32
    shell32
    advapi32
)
