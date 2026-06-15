if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(inference_postprocessor LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    find_package(fmt CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_package(yaml-cpp CONFIG REQUIRED)
    find_package(Boost REQUIRED COMPONENTS json system filesystem process lockfree pool)

    if(WIN32)
        add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()

    if(MSVC)
        add_compile_options(/FS /Zm500 /utf-8)
    endif()

    set(BUILD_COMMON_TESTS OFF)
    if(NOT TARGET common_lib)
        add_subdirectory(../../common common_build)
    endif()
    if(NOT TARGET media_defines_lib)
        add_subdirectory(../../media/defines media_defines_build)
    endif()
    if(NOT TARGET inference_info_lib)
        add_subdirectory(../inferenceinfo inferenceinfo_build)
    endif()
    if(NOT TARGET inference_tensordata_lib)
        add_subdirectory(../tensordata tensordata_build)
    endif()
endif()

file(GLOB POSTPROCESSOR_SOURCES "src/*.cpp")

add_library(inference_postprocessor_lib STATIC ${POSTPROCESSOR_SOURCES})

target_include_directories(inference_postprocessor_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(inference_postprocessor_lib
    PUBLIC
        inference_tensordata_lib
        inference_info_lib
        media_defines_lib
        common_lib
)

if(WIN32)
    target_compile_definitions(inference_postprocessor_lib PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(MSVC)
    target_compile_options(inference_postprocessor_lib PRIVATE /utf-8)
endif()
