# ── Standalone build ──
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(common_lib LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    option(BUILD_TESTS "Build tests" ON)
    if(BUILD_TESTS)
        enable_testing()
    endif()
    find_package(spdlog CONFIG REQUIRED)
    find_package(fmt CONFIG REQUIRED)
    find_package(yaml-cpp CONFIG REQUIRED)
    find_package(Boost REQUIRED COMPONENTS json system filesystem process lockfree)
    if(WIN32)
        add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
endif()

# ── Library target ──
if(NOT DEFINED BUILD_COMMON_TESTS)
    set(BUILD_COMMON_TESTS ${BUILD_TESTS})
endif()

file(GLOB_RECURSE COMMON_SOURCES
    "src/*.cpp"
    "include/common/log/*.h"
    "include/common/config/*.h"
    "include/common/pool/*.hpp"
)

add_library(common_lib ${COMMON_SOURCES})

target_include_directories(common_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(common_lib
    PUBLIC
        spdlog::spdlog
        fmt::fmt
        yaml-cpp::yaml-cpp
        Boost::json
        Boost::system
        Boost::filesystem
        Boost::process
        Boost::lockfree
)

if(WIN32)
    target_compile_definitions(common_lib PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# ── Tests ──
if(BUILD_TESTS AND BUILD_COMMON_TESTS)
    file(GLOB TEST_SOURCES "test/*.cpp")
    if(TEST_SOURCES)
        foreach(TEST_FILE ${TEST_SOURCES})
            get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
            add_executable(test_${TEST_NAME} ${TEST_FILE})
            target_link_libraries(test_${TEST_NAME} PRIVATE common_lib)
            add_test(NAME test_${TEST_NAME} COMMAND test_${TEST_NAME})
        endforeach()
    endif()
endif()
