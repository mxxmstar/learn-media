if(NOT DEFINED DEST_DIR)
    message(FATAL_ERROR "DEST_DIR is required")
endif()
string(REPLACE "\"" "" DEST_DIR "${DEST_DIR}")

if(NOT DEFINED DLLS)
    return()
endif()

foreach(src IN LISTS DLLS)
    if(NOT EXISTS "${src}")
        continue()
    endif()

    get_filename_component(name "${src}" NAME)
    set(dst "${DEST_DIR}/${name}")
    if(EXISTS "${dst}")
        continue()
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy "${src}" "${dst}"
        RESULT_VARIABLE copy_result
    )
    if(NOT copy_result EQUAL 0)
        message(FATAL_ERROR "Failed to copy ${src} to ${dst}")
    endif()
endforeach()
