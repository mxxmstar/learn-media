cmake_minimum_required(VERSION 3.18)

# ⚠️ vcpkg toolchain must be set before project()
# Check project-root /vcpkg first, then src-cpp/vcpkg/, then VCPKG_ROOT
foreach(_vpkg_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg"
    "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg"
    "$ENV{VCPKG_ROOT}"
)
    set(_tc "${_vpkg_dir}/scripts/buildsystems/vcpkg.cmake")
    if(EXISTS "${_tc}")
        set(CMAKE_TOOLCHAIN_FILE "${_tc}" CACHE FILEPATH "Vcpkg toolchain file")
        message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
        break()
    endif()
endforeach()
unset(_vpkg_dir)
unset(_tc)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(cpp_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories — libraries go to lib/, executables to bin/
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(BUILD_STANDALONE "Build standalone test executable" ON)
option(BUILD_TESTS "Build tests" ON)

if(MSVC)
    add_compile_options(/FS)
endif()

# ==================== Common dependencies ====================
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS asio system uuid json process pool filesystem lockfree beast)
find_package(yaml-cpp CONFIG REQUIRED)

if(WIN32)
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
add_compile_definitions(__STDC_CONSTANT_MACROS __STDC_LIMIT_MACROS)

if(MSVC)
    add_compile_options(/Zm500)
endif()

# ==================== Module libraries ====================
add_subdirectory(modules/common)
add_subdirectory(modules/grpc)
add_subdirectory(modules/runtime)
add_subdirectory(modules/pipeline)
add_subdirectory(modules/media)
add_subdirectory(modules/filter)
add_subdirectory(modules/inference)

# ==================== Standalone executable ====================
if(BUILD_STANDALONE)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
        add_executable(cpp_core_standalone main.cpp)
        target_include_directories(cpp_core_standalone PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(cpp_core_standalone PRIVATE
            runtime_lib
            pipeline_lib
            common_lib
            media_defines_lib
            media_puller_lib
            media_decoder_lib
            media_encoder_lib
            media_pusher_lib
            filter_osd_lib
            inference_lib
            spdlog::spdlog
            fmt::fmt
            Boost::asio
            Boost::system
            Boost::uuid
            Boost::json
            Boost::process
            Boost::pool
            yaml-cpp::yaml-cpp
        )

        if(WIN32)
            if(CMAKE_BUILD_TYPE MATCHES "^[Dd]ebug$")
                set(_openvino_runtime_bin_dir
                    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin")
            else()
                set(_openvino_runtime_bin_dir
                    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
            endif()

            if(EXISTS "${_openvino_runtime_bin_dir}")
                if(CMAKE_BUILD_TYPE MATCHES "^[Dd]ebug$")
                    set(_openvino_runtime_dlls
                        "${_openvino_runtime_bin_dir}/openvinod.dll"
                        "${_openvino_runtime_bin_dir}/openvino_ir_frontendd.dll"
                        "${_openvino_runtime_bin_dir}/openvino_onnx_frontendd.dll"
                        "${_openvino_runtime_bin_dir}/openvino_intel_cpu_plugind.dll"
                    )
                else()
                    set(_openvino_runtime_dlls
                        "${_openvino_runtime_bin_dir}/openvino.dll"
                        "${_openvino_runtime_bin_dir}/openvino_ir_frontend.dll"
                        "${_openvino_runtime_bin_dir}/openvino_onnx_frontend.dll"
                        "${_openvino_runtime_bin_dir}/openvino_intel_cpu_plugin.dll"
                    )
                endif()
                foreach(_openvino_runtime_dll IN LISTS _openvino_runtime_dlls)
                    if(NOT EXISTS "${_openvino_runtime_dll}")
                        list(REMOVE_ITEM _openvino_runtime_dlls "${_openvino_runtime_dll}")
                    endif()
                endforeach()
                if(_openvino_runtime_dlls)
                    add_custom_command(TARGET cpp_core_standalone POST_BUILD
                        COMMAND ${CMAKE_COMMAND}
                            -DDEST_DIR="$<TARGET_FILE_DIR:cpp_core_standalone>"
                            "-DDLLS=${_openvino_runtime_dlls}"
                            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_dlls_if_missing.cmake"
                        COMMENT "Copying required OpenVINO runtime frontend/plugin DLLs"
                        VERBATIM
                    )
                endif()
            endif()
        endif()
    else()
        message(WARNING "BUILD_STANDALONE=ON but main.cpp not found; only libraries will be built")
    endif()
endif()

# ==================== Tests ====================
if(BUILD_TESTS)
    enable_testing()
endif()




