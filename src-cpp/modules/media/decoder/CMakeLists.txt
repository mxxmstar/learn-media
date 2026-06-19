# modules/media/decoder/CMakeLists.txt
# decoder 子模块：基于 FFmpeg 的软件解码器。

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(media_decoder LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    option(BUILD_TESTS "Build tests" ON)
    if(BUILD_TESTS)
        enable_testing()
    endif()
    find_package(fmt CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_package(Boost REQUIRED COMPONENTS asio system json filesystem process lockfree pool)
    find_package(yaml-cpp CONFIG REQUIRED)
    if(MSVC)
        add_compile_options(/FS /Zm500 /utf-8)
    endif()
    if(WIN32)
        add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    set(BUILD_COMMON_TESTS OFF)
    if(NOT TARGET common_lib)
        add_subdirectory(../../common common_build)
    endif()
    if(NOT TARGET media_defines_lib)
        add_subdirectory(../defines defines_build)
    endif()
    if(NOT TARGET media_stream_lib)
        add_subdirectory(../stream stream_build)
    endif()
    if(NOT TARGET media_puller_lib)
        set(BUILD_PULLER_TESTS OFF)
        add_subdirectory(../puller puller_build)
    endif()
    if(NOT TARGET runtime_lib)
        add_subdirectory(../../runtime runtime_build)
    endif()
    if(NOT TARGET pipeline_lib)
        add_subdirectory(../../pipeline pipeline_build)
    endif()
endif()

option(BUILD_DECODER_TESTS "Build media decoder tests" ON)

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
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../stream/include>
)

target_link_libraries(media_decoder_lib
    PUBLIC
        media_defines_lib
        media_stream_lib
        common_lib
        runtime_lib
        pipeline_lib
)

if(WIN32)
    target_compile_definitions(media_decoder_lib PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
if(MSVC)
    add_compile_options("/utf-8")
endif()

# ==================== 测试子目录 ====================
if(BUILD_DECODER_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
    add_subdirectory(test)
endif()
