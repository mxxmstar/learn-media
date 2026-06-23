if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(render_lib LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    option(BUILD_TESTS "Build tests" ON)
    if(BUILD_TESTS)
        enable_testing()
    endif()
    find_package(glfw3 CONFIG REQUIRED)
    find_package(glad CONFIG REQUIRED)
    find_package(OpenGL REQUIRED)
    if(WIN32)
        add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    if(MSVC)
        add_compile_options(/FS /Zm500 /utf-8)
    endif()
endif()

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

if(BUILD_TESTS)
    option(BUILD_RENDER_TESTS "Build render tests" ON)
else()
    option(BUILD_RENDER_TESTS "Build render tests" OFF)
endif()
option(BUILD_RENDER_DEMOS "Build render demos" ON)

file(GLOB RENDER_SOURCES "src/*.cpp")

add_library(render_lib STATIC ${RENDER_SOURCES})

target_include_directories(render_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../media/defines/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(render_lib
    PUBLIC
        glfw
        glad::glad
        OpenGL::GL
)

if(WIN32)
    target_compile_definitions(render_lib PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(MSVC)
    target_compile_options(render_lib PRIVATE /utf-8)
endif()

if(BUILD_RENDER_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
    add_subdirectory(test)
endif()

if(BUILD_RENDER_DEMOS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demo")
    add_subdirectory(demo ${CMAKE_CURRENT_BINARY_DIR}/demo)
endif()
