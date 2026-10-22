cmake_minimum_required(VERSION 3.24)
project(HandyViewer3D VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# OpenXR headers only (SOURCE_SUBDIR points nowhere so the loader is not built).
FetchContent_Declare(openxr
    URL https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.65.tar.gz
    SOURCE_SUBDIR do-not-build)
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

# Lay out the install folder exactly as the installer expects it.
install(TARGETS HandyViewer3D RUNTIME DESTINATION .)
install(FILES manifest/XR_APILAYER_SYMYOULATER_handy_viewer.json DESTINATION .)
install(DIRECTORY shapes DESTINATION .)
