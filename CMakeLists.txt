cmake_minimum_required(VERSION 3.20)
project(OpenRipper LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan
    "C:/msys64/ucrt64/include"
)

# MinHook
add_subdirectory(third_party/minhook EXCLUDE_FROM_ALL)

# Vulkan backend
set(VK_SOURCES
    core/ObjExporter.cpp
    core/VertexParser.cpp
    backends/vulkan/VkHooks.cpp
    backends/vulkan/VkLayer.cpp
)

add_library(OpenRipperVk SHARED ${VK_SOURCES})
set_target_properties(OpenRipperVk PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/backends"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/backends"
)

target_compile_definitions(OpenRipperVk PRIVATE
    VK_USE_PLATFORM_WIN32_KHR
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if (MSVC)
    target_compile_options(OpenRipperVk PRIVATE /W3 /O2)
else()
    target_compile_options(OpenRipperVk PRIVATE -Wall -O3)
    target_link_options(OpenRipperVk PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Copy layer manifest on build
add_custom_command(TARGET OpenRipperVk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/VkLayer_openripper.json"
    "${CMAKE_SOURCE_DIR}/bin/backends/VkLayer_openripper.json"
)

# DirectX 9 backend
set(D3D9_SOURCES
    core/ObjExporter.cpp
    backends/d3d9/D3D9Capture.cpp
    backends/d3d9/D3D9Hooks.cpp
)

add_library(OpenRipperDx9 SHARED ${D3D9_SOURCES})
set_target_properties(OpenRipperDx9 PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/backends"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/backends"
)

target_include_directories(OpenRipperDx9 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/d3d9
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dxsdk/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/include
)

target_compile_definitions(OpenRipperDx9 PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(OpenRipperDx9 PRIVATE
    minhook
    d3d9
    user32
    gdi32
)

if (MSVC)
    target_compile_options(OpenRipperDx9 PRIVATE /W3 /O2)
else()
    target_compile_options(OpenRipperDx9 PRIVATE -Wall -O3)
    target_link_options(OpenRipperDx9 PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# DirectX 11 backend
set(D3D11_SOURCES
    core/ObjExporter.cpp
    backends/d3d11/D3D11Capture.cpp
    backends/d3d11/D3D11Hooks.cpp
)

add_library(OpenRipperDx11 SHARED ${D3D11_SOURCES})
set_target_properties(OpenRipperDx11 PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/backends"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/backends"
)

target_include_directories(OpenRipperDx11 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/d3d11
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/include
)

target_compile_definitions(OpenRipperDx11 PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(OpenRipperDx11 PRIVATE
    minhook
    d3d11
    dxgi
    user32
    gdi32
)

if (MSVC)
    target_compile_options(OpenRipperDx11 PRIVATE /W3 /O2)
else()
    target_compile_options(OpenRipperDx11 PRIVATE -Wall -O3)
    target_link_options(OpenRipperDx11 PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# OpenRipper Native Win32 Loader
add_executable(OpenRipperLoader WIN32 loader/OpenRipperLoader.cpp)
set_target_properties(OpenRipperLoader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)
target_link_libraries(OpenRipperLoader PRIVATE comctl32 shell32)
if (NOT MSVC)
    target_link_options(OpenRipperLoader PRIVATE -static -static-libgcc -static-libstdc++ -mwindows)
endif()
