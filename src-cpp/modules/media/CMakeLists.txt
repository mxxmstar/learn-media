if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(media_module LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(BUILD_TESTS "Build tests" ON)
option(BUILD_MEDIA_TESTS "Build media defines tests" ON)
option(BUILD_STREAM_TESTS "Build media stream tests" ON)
option(BUILD_PULLER_TESTS "Build media puller tests" ON)
option(BUILD_DECODER_TESTS "Build media decoder tests" ON)
option(BUILD_ENCODER_TESTS "Build media encoder tests" ON)
option(BUILD_MEDIA_PUSHER_TESTS "Build media pusher tests" ON)
    if(BUILD_TESTS)
        enable_testing()
    endif()
    find_package(fmt CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_package(Boost REQUIRED COMPONENTS asio system uuid json process pool filesystem lockfree beast)
    find_package(yaml-cpp CONFIG REQUIRED)
    if(MSVC)
        add_compile_options(/FS /Zm500)
    endif()
    if(WIN32)
        add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    add_compile_definitions(__STDC_CONSTANT_MACROS __STDC_LIMIT_MACROS)
    set(BUILD_COMMON_TESTS OFF)
    add_subdirectory(../common common_build)
endif()

set(MEDIA_SUBMODULES
    runtime
    defines
    stream
    puller
    decoder
    encoder
    pusher
    demo
)

foreach(submodule ${MEDIA_SUBMODULES})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${submodule}/CMakeLists.txt")
        add_subdirectory(${submodule})
    endif()
endforeach()
