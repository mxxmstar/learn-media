# ── Standalone build ──
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(media_decoder LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    option(BUILD_TESTS "Build tests" ON)
    option(BUILD_DECODER_TESTS "Build media decoder tests" ON)
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
    add_subdirectory(../defines defines_build)
    add_subdirectory(../puller puller_build)
endif()

# ── Library target ──
file(GLOB DECODER_SOURCES "src/*.cpp")
add_library(media_decoder_lib STATIC ${DECODER_SOURCES})

target_include_directories(media_decoder_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_include_directories(media_decoder_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../defines/include>
)

target_link_libraries(media_decoder_lib
    PUBLIC
        media_defines_lib
        common_lib
)

if(WIN32)
    target_compile_definitions(media_decoder_lib PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
if(MSVC)
    add_compile_options("/utf-8")
endif()

# ── Tests ──
if(BUILD_DECODER_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
    add_subdirectory(test)
endif()
