if(NOT BUILD_MEDIA_PUSHER_TESTS)
    return()
endif()

enable_testing()

file(GLOB TEST_SOURCES "*.cpp")

foreach(source ${TEST_SOURCES})
    get_filename_component(test_name ${source} NAME_WE)
    set(test_target "test_media_pusher_${test_name}")

    add_executable(${test_target} ${source})

    target_include_directories(${test_target}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../include
            ${CMAKE_CURRENT_SOURCE_DIR}/../../defines/include
    )

    if(FFMPEG_INCLUDE_DIR)
        target_include_directories(${test_target} PRIVATE "${FFMPEG_INCLUDE_DIR}")
    endif()

    target_link_libraries(${test_target}
        PRIVATE
            media_pusher_lib
            media_defines_lib
            common_lib
    )

    if(AVCODEC_LIBRARY)
        target_link_libraries(${test_target} PRIVATE "${AVCODEC_LIBRARY}")
    endif()
    if(AVFORMAT_LIBRARY)
        target_link_libraries(${test_target} PRIVATE "${AVFORMAT_LIBRARY}")
    endif()
    if(AVUTIL_LIBRARY)
        target_link_libraries(${test_target} PRIVATE "${AVUTIL_LIBRARY}")
    endif()

    if(WIN32)
        target_compile_options(${test_target} PRIVATE /utf-8)
    endif()

    add_test(NAME ${test_target} COMMAND ${test_target})
    message(STATUS "Added pusher test: ${test_target}")
endforeach()
