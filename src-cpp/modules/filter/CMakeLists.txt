if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(filter_module LANGUAGES CXX)
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

set(FILTER_SUBMODULES
    frameratecontrol
    osd
)

foreach(submodule ${FILTER_SUBMODULES})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${submodule}/CMakeLists.txt")
        add_subdirectory(${submodule})
    endif()
endforeach()
