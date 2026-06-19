add_library(pipeline_lib INTERFACE)

target_include_directories(pipeline_lib
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../media/defines/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../media/stream/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../media/encoder/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../inference/inferenceinfo/include>
)

if(WIN32)
    target_compile_definitions(pipeline_lib INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
