set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin")

find_package(OpenGL REQUIRED)

# Force MSVC to read source files as UTF-8 (supports #version in raw strings)
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_executable(trig_rect_demo trig_rect.cpp)
target_link_libraries(trig_rect_demo PRIVATE glfw glad::glad OpenGL::GL)

find_package(Stb REQUIRED)
add_executable(texture_demo texture.cpp)
target_link_libraries(texture_demo PRIVATE glfw glad::glad OpenGL::GL)
target_include_directories(texture_demo PRIVATE ${Stb_INCLUDE_DIR})
