# ── Standalone build ──
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(common_config LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    option(BUILD_TESTS "Build tests" ON)
    if(BUILD_TESTS)
        enable_testing()
    endif()
    if(WIN32)
        add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    find_package(yaml-cpp CONFIG REQUIRED)
    find_package(Boost REQUIRED COMPONENTS json system filesystem process)
    find_package(spdlog CONFIG REQUIRED)
    find_package(fmt CONFIG REQUIRED)
endif()

# ── Library target ──
file(GLOB CONFIG_SOURCES "src/*.cpp")

add_library(common_config STATIC ${CONFIG_SOURCES})

target_include_directories(common_config
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../log/include
)

target_link_libraries(common_config
    PUBLIC
        yaml-cpp::yaml-cpp
        Boost::json
        Boost::system
        Boost::filesystem
        Boost::process
    PRIVATE
        spdlog::spdlog
        fmt::fmt
)

if(WIN32)
    target_compile_definitions(common_config PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# ── Tests ──
if(BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
    add_subdirectory(test)
endif()
