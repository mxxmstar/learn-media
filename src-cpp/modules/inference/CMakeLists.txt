if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(inference_module LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    option(BUILD_TESTS "Build tests" ON)

    find_package(fmt CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_package(yaml-cpp CONFIG REQUIRED)
    find_package(Boost REQUIRED COMPONENTS json system filesystem process lockfree pool)
    find_package(OpenVINO REQUIRED COMPONENTS Runtime)

    if(WIN32)
        add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()

    if(MSVC)
        add_compile_options(/FS /Zm500 /utf-8)
    endif()

    if(NOT TARGET common_lib)
        add_subdirectory(../common common_build)
    endif()
    if(NOT TARGET media_defines_lib)
        add_subdirectory(../media/defines media_defines_build)
    endif()
endif()

find_package(OpenVINO REQUIRED COMPONENTS Runtime)

file(GLOB_RECURSE INFERENCE_SOURCES CONFIGURE_DEPENDS
    "model/src/*.cpp"
    "inferenceengine/src/*.cpp"
    "inferencesession/src/*.cpp"
    "preprocessor/src/*.cpp"
    "postprocessor/src/*.cpp"
)

add_library(inference_lib STATIC ${INFERENCE_SOURCES})

target_include_directories(inference_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tensordata/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inferenceinfo/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/model/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/preprocessor/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/postprocessor/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inferenceengine/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inferencesession/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(inference_lib
    PUBLIC
        common_lib
        media_defines_lib
        openvino::runtime
)

if(WIN32)
    target_compile_definitions(inference_lib PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(MSVC)
    target_compile_options(inference_lib PRIVATE /utf-8)
endif()

if(BUILD_TESTS)
    option(BUILD_INFERENCE_TESTS "Build inference tests and demos" ON)
else()
    option(BUILD_INFERENCE_TESTS "Build inference tests and demos" OFF)
endif()

if(BUILD_INFERENCE_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
    add_subdirectory(test)
endif()
