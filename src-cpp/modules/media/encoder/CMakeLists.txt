# modules/media/encoder/CMakeLists.txt
# encoder submodule: FFmpeg software encoder.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(media_encoder LANGUAGES CXX)
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
    if(NOT TARGET runtime_lib)
        add_subdirectory(../../runtime runtime_build)
    endif()
    if(NOT TARGET pipeline_lib)
        add_subdirectory(../../pipeline pipeline_build)
    endif()
endif()

option(BUILD_ENCODER_TESTS "Build media encoder tests" ON)

file(GLOB ENCODER_SOURCES "src/*.cpp")
add_library(media_encoder_lib STATIC ${ENCODER_SOURCES})

target_include_directories(media_encoder_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_include_directories(media_encoder_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../defines/include>
)

if(FFMPEG_INCLUDE_DIR)
    target_include_directories(media_encoder_lib
        PUBLIC "${FFMPEG_INCLUDE_DIR}"
    )
endif()

target_link_libraries(media_encoder_lib
    PUBLIC
        media_defines_lib
        common_lib
        runtime_lib
        pipeline_lib
)

if(AVCODEC_LIBRARY)
    target_link_libraries(media_encoder_lib PUBLIC "${AVCODEC_LIBRARY}")
endif()
if(AVUTIL_LIBRARY)
    target_link_libraries(media_encoder_lib PUBLIC "${AVUTIL_LIBRARY}")
endif()

if(WIN32)
    target_compile_definitions(media_encoder_lib PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
if(MSVC)
    add_compile_options("/utf-8")
endif()

if(BUILD_ENCODER_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
    add_subdirectory(test)
endif()
