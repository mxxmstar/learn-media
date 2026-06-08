option(BUILD_MEDIA_DEMOS "Build media demo executables" ON)

if(NOT BUILD_MEDIA_DEMOS)
    return()
endif()

add_executable(media_runtime_transcode_demo
    runtime_transcode_demo.cpp
)

target_include_directories(media_runtime_transcode_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../puller/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../decoder/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../encoder/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../pusher/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../defines/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../stream/include
)

target_link_libraries(media_runtime_transcode_demo PRIVATE
    media_runtime
    media_puller_lib
    media_decoder_lib
    media_encoder_lib
    media_pusher_lib
    media_defines_lib
    common_lib
    Boost::asio
)

if(MSVC)
    target_compile_options(media_runtime_transcode_demo PRIVATE /utf-8)
endif()
