cmake_minimum_required(VERSION 3.24)
project(HandyViewer3D VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# OpenXR SDK: headers for the layer, plus the loader for the Check tool.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(openxr
    URL https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.65.tar.gz)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    SOURCE_SUBDIR do-not-build)
FetchContent_MakeAvailable(openxr nlohmann_json)

add_library(HandyViewer3D SHARED
    src/layer.cpp
    src/config.cpp
    src/steering.cpp
    src/hotkeys.cpp
    src/log.cpp)

target_include_directories(HandyViewer3D PRIVATE
    ${openxr_SOURCE_DIR}/include
    ${nlohmann_json_SOURCE_DIR}/single_include
    ${nlohmann_json_SOURCE_DIR}/include)

target_compile_definitions(HandyViewer3D PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(HandyViewer3D PRIVATE d3d11 d3d12 hid setupapi shell32 ole32 user32)

if(MSVC)
    target_compile_options(HandyViewer3D PRIVATE /W3 /EHsc /utf-8)
    # Static runtime: no Visual C++ redistributable needed on the PC.
    set_property(TARGET HandyViewer3D PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Check tool: reports runtime, registered layers and whether the layer loads.
add_executable(HandyViewer3DCheck WIN32 tools/check.cpp)
target_link_libraries(HandyViewer3DCheck PRIVATE OpenXR::openxr_loader advapi32 user32)
target_compile_definitions(HandyViewer3DCheck PRIVATE UNICODE _UNICODE)
if(MSVC)
    set_property(TARGET HandyViewer3DCheck PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET openxr_loader PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Lay out the install folder exactly as the installer expects it.
install(TARGETS HandyViewer3D HandyViewer3DCheck RUNTIME DESTINATION .)
install(FILES manifest/XR_APILAYER_SYMYOULATER_handy_viewer.json DESTINATION .)
install(DIRECTORY shapes DESTINATION .)
