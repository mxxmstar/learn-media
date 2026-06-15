if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(inference_tensordata LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(WIN32)
        add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    if(NOT TARGET inference_info_lib)
        add_subdirectory(../inferenceinfo inferenceinfo_build)
    endif()
endif()

add_library(inference_tensordata_lib INTERFACE)

target_include_directories(inference_tensordata_lib
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(inference_tensordata_lib
    INTERFACE
        inference_info_lib
)

if(WIN32)
    target_compile_definitions(inference_tensordata_lib INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
