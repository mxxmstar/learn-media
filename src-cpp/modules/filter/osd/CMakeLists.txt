if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(filter_osd LANGUAGES CXX)
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
        add_subdirectory(../../common common_build)
    endif()
    if(NOT TARGET media_defines_lib)
        add_subdirectory(../../media/defines media_defines_build)
    endif()
    if(NOT TARGET runtime_lib)
        add_subdirectory(../../runtime runtime_build)
    endif()
    if(NOT TARGET pipeline_lib)
        add_subdirectory(../../pipeline pipeline_build)
    endif()
endif()

if(BUILD_TESTS)
    option(BUILD_OSD_TESTS "Build OSD tests" ON)
else()
    option(BUILD_OSD_TESTS "Build OSD tests" OFF)
endif()

file(GLOB OSD_SOURCES "src/*.cpp")

add_library(filter_osd_lib STATIC ${OSD_SOURCES})

target_include_directories(filter_osd_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../media/defines/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(filter_osd_lib
    PUBLIC
        media_defines_lib
        runtime_lib
        pipeline_lib
)

if(WIN32)
    target_compile_definitions(filter_osd_lib PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(BUILD_OSD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
    add_subdirectory(test)
endif()
