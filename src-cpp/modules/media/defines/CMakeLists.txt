# ── Standalone build ──
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(media_defines LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    option(BUILD_TESTS "Build tests" ON)
    option(BUILD_MEDIA_TESTS "Build media defines tests" ON)
    if(BUILD_TESTS)
        enable_testing()
    endif()
    if(MSVC)
        add_compile_options(/utf-8)
    endif()
    if(WIN32)
        add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    find_package(spdlog CONFIG REQUIRED)
    find_package(fmt CONFIG REQUIRED)
    find_package(yaml-cpp CONFIG REQUIRED)
    find_package(Boost REQUIRED COMPONENTS json system filesystem process lockfree)
    set(BUILD_COMMON_TESTS OFF)
    add_subdirectory(../../common common_build)
endif()

# ── Library target ──
file(GLOB DEFINES_SOURCES "src/*.cpp")

add_library(media_defines_lib STATIC ${DEFINES_SOURCES})

target_include_directories(media_defines_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ── FFmpeg ──
find_path(FFMPEG_INCLUDE_DIR
    NAMES libavcodec/avcodec.h
    PATHS
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
        "${CMAKE_PREFIX_PATH}/include"
        "/vcpkg/packages/ffmpeg_x64-windows/include"
        "/vcpkg_installed/x64-windows/include"
        /usr/include
        /usr/local/include
)

find_library(AVCODEC_LIBRARY
    NAMES avcodec
    PATHS
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
        "${CMAKE_PREFIX_PATH}/debug/lib"
        "${CMAKE_PREFIX_PATH}/lib"
        "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/debug/lib"
        "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/lib"
)

find_library(AVFORMAT_LIBRARY
    NAMES avformat
    PATHS
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
        "${CMAKE_PREFIX_PATH}/debug/lib"
        "${CMAKE_PREFIX_PATH}/lib"
        "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/debug/lib"
        "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/lib"
)

find_library(AVUTIL_LIBRARY
    NAMES avutil
    PATHS
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
        "${CMAKE_PREFIX_PATH}/debug/lib"
        "${CMAKE_PREFIX_PATH}/lib"
        "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/debug/lib"
        "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/lib"
)

if(FFMPEG_INCLUDE_DIR)
    target_include_directories(media_defines_lib PUBLIC ${FFMPEG_INCLUDE_DIR})
endif()

target_link_libraries(media_defines_lib PUBLIC common_lib)

if(AVCODEC_LIBRARY)
    target_link_libraries(media_defines_lib PUBLIC ${AVCODEC_LIBRARY})
endif()
if(AVFORMAT_LIBRARY)
    target_link_libraries(media_defines_lib PUBLIC ${AVFORMAT_LIBRARY})
endif()
if(AVUTIL_LIBRARY)
    target_link_libraries(media_defines_lib PUBLIC ${AVUTIL_LIBRARY})
endif()

if(WIN32)
    target_compile_definitions(media_defines_lib PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
if(MSVC)
    add_compile_options("/utf-8")
endif()

# ── Tests ──
if(BUILD_MEDIA_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
    add_subdirectory(test)
endif()
