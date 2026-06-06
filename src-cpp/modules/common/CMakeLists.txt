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
    find_package(Boost REQUIRED COMPONENTS json system filesystem process lockfree pool)
    if(WIN32)
        add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
endif()

# ── Test gate (media sets BUILD_COMMON_TESTS=OFF) ──
if(DEFINED BUILD_COMMON_TESTS AND NOT BUILD_COMMON_TESTS)
    set(BUILD_TESTS OFF)
endif()

# ── Submodules ──
add_subdirectory(config)
add_subdirectory(log)
add_subdirectory(pool)
add_subdirectory(queue)

# ── Meta-target for backward compatibility ──
add_library(common_lib INTERFACE)
target_link_libraries(common_lib INTERFACE
    common_config
    common_log
    common_pool
    common_queue
)
