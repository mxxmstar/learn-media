if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(inference_module LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    option(BUILD_TESTS "Build tests" ON)

    if(BUILD_TESTS)
        enable_testing()
    endif()

    find_package(fmt CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_package(yaml-cpp CONFIG REQUIRED)
    find_package(Boost REQUIRED COMPONENTS json system filesystem process lockfree pool)
    if(NOT TARGET openvino::runtime)
        find_package(OpenVINO REQUIRED COMPONENTS Runtime)
    endif()

    if(WIN32)
        add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()

    if(MSVC)
        add_compile_options(/FS /Zm500 /utf-8)
    endif()

    set(BUILD_COMMON_TESTS OFF)
    if(NOT TARGET common_lib)
        add_subdirectory(../common common_build)
    endif()
    if(NOT TARGET media_defines_lib)
        add_subdirectory(../media/defines media_defines_build)
    endif()
endif()

if(NOT TARGET runtime_lib AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/CMakeLists.txt")
    add_subdirectory(../runtime runtime_build)
endif()

if(NOT TARGET pipeline_lib AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../pipeline/CMakeLists.txt")
    add_subdirectory(../pipeline pipeline_build)
endif()

if(NOT TARGET openvino::runtime)
    find_package(OpenVINO REQUIRED COMPONENTS Runtime)
endif()

set(INFERENCE_SUBMODULES
    inferenceinfo
    tensordata
    preprocessor
    postprocessor
    model
    inferenceengine
    inferencesession
    infernode
)

foreach(submodule ${INFERENCE_SUBMODULES})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${submodule}/CMakeLists.txt")
        add_subdirectory(${submodule})
    endif()
endforeach()

add_library(inference_lib INTERFACE)

target_link_libraries(inference_lib
    INTERFACE
        inference_session_lib
        inference_engine_lib
        inference_model_lib
        inference_preprocessor_lib
        inference_postprocessor_lib
        inference_tensordata_lib
        inference_info_lib
        inference_node_lib
)

if(WIN32)
    target_compile_definitions(inference_lib INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(BUILD_TESTS)
    option(BUILD_INFERENCE_TESTS "Build inference tests and demos" ON)
else()
    option(BUILD_INFERENCE_TESTS "Build inference tests and demos" OFF)
endif()

if(BUILD_INFERENCE_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
    add_subdirectory(test)
endif()
