if(NOT BUILD_INFERENCE_TESTS)
    return()
endif()

set(OpenCV_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/opencv4")
find_package(OpenCV REQUIRED)

add_executable(inference_yolo_image_demo
    yolo_image_demo.cpp
)

target_link_libraries(inference_yolo_image_demo
    PRIVATE
        inference_lib
        ${OpenCV_LIBS}
)

target_include_directories(inference_yolo_image_demo
    PRIVATE
        ${OpenCV_INCLUDE_DIRS}
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(OPENVINO_RUNTIME_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin")
else()
    set(OPENVINO_RUNTIME_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
endif()

file(GLOB OPENVINO_RUNTIME_DLLS
    "${OPENVINO_RUNTIME_BIN_DIR}/openvino*.dll"
)

if(OPENVINO_RUNTIME_DLLS)
    add_custom_command(TARGET inference_yolo_image_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${OPENVINO_RUNTIME_DLLS}
            $<TARGET_FILE_DIR:inference_yolo_image_demo>
        COMMENT "Copying OpenVINO frontend and plugin DLLs"
    )
endif()

message(STATUS "Added inference demo: inference_yolo_image_demo")
