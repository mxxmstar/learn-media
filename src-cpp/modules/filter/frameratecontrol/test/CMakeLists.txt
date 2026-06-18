if(NOT BUILD_FRAMERATECONTROL_TESTS)
    return()
endif()

enable_testing()

file(GLOB FRAMERATECONTROL_TEST_SOURCES "*.cpp")

foreach(test_file ${FRAMERATECONTROL_TEST_SOURCES})
    get_filename_component(test_name ${test_file} NAME_WE)
    set(test_target "test_frameratecontrol_${test_name}")

    add_executable(${test_target} ${test_file})

    target_include_directories(${test_target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../media/defines/include
    )

    target_link_libraries(${test_target} PRIVATE
        filter_frameratecontrol_lib
        media_defines_lib
    )

    add_test(NAME ${test_target} COMMAND ${test_target})
    message(STATUS "Added frame rate control test: ${test_target}")
endforeach()
