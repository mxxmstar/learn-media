if(NOT BUILD_RENDER_TESTS)
    return()
endif()

enable_testing()

file(GLOB RENDER_TEST_SOURCES "*.cpp")

foreach(test_file ${RENDER_TEST_SOURCES})
    get_filename_component(test_name ${test_file} NAME_WE)
    set(test_target "test_render_${test_name}")

    add_executable(${test_target} ${test_file})
    target_link_libraries(${test_target} PRIVATE render_lib)

    add_test(NAME ${test_target} COMMAND ${test_target})
    message(STATUS "Added render test: ${test_target}")
endforeach()
