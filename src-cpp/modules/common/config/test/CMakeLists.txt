set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

file(GLOB CONFIG_TEST_SOURCES "*.cpp")

foreach(test_file ${CONFIG_TEST_SOURCES})
    get_filename_component(test_name ${test_file} NAME_WE)
    set(test_target "test_config_${test_name}")
    add_executable(${test_target} ${test_file})
    target_link_libraries(${test_target} PRIVATE common_config common_log)
    add_test(NAME ${test_target} COMMAND ${test_target})
    message(STATUS "Added config test: ${test_target}")
endforeach()
