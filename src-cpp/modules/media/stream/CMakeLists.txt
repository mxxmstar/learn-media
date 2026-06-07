if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(media_stream LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    option(BUILD_TESTS "Build tests" ON)
    option(BUILD_STREAM_TESTS "Build media stream tests" ON)
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
endif()

file(GLOB_RECURSE STREAM_SOURCES "src/*.cpp")

add_library(media_stream_lib STATIC ${STREAM_SOURCES})

target_include_directories(media_stream_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../puller/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(media_stream_lib
    PUBLIC
        media_defines_lib
        common_lib
        Boost::asio
)

if(WIN32)
    target_compile_definitions(media_stream_lib PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
endif()
if(MSVC)
    target_compile_options(media_stream_lib PRIVATE /utf-8)
endif()

if(BUILD_STREAM_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
    add_subdirectory(test)
endif()
