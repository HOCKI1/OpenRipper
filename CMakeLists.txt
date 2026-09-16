cmake_minimum_required(VERSION 3.20)
project(OpenRipper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan
    "C:/msys64/ucrt64/include"
)

set(SOURCES
    core/ObjExporter.cpp
    core/VertexParser.cpp
    backends/vulkan/VkHooks.cpp
    backends/vulkan/VkLayer.cpp
)

add_library(OpenRipperVk SHARED ${SOURCES})
set_target_properties(OpenRipperVk PROPERTIES PREFIX "")

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
    "$<TARGET_FILE_DIR:OpenRipperVk>/VkLayer_openripper.json"
)

# OpenRipper Native Win32 Loader
add_executable(OpenRipperLoader WIN32 OpenRipperLoader.cpp)
target_link_libraries(OpenRipperLoader PRIVATE comctl32 shell32)
if (NOT MSVC)
    target_link_options(OpenRipperLoader PRIVATE -static -static-libgcc -static-libstdc++ -mwindows)
endif()
