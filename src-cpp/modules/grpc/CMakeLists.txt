# grpc 模块的 CMake 构建配置
# 编译 .proto 文件生成 C++ 源码，构建 proto 库和核心库，以及可选的演示程序

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    project(learn_media_grpc_module LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# 是否构建演示可执行文件
option(BUILD_GRPC_DEMOS "Build grpc demo executables" ON)

# 查找依赖
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# .proto 文件和生成文件的路径配置
set(GRPC_DEMO_PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(GRPC_DEMO_PROTO_FILES
    "${GRPC_DEMO_PROTO_DIR}/hello.proto"
    "${GRPC_DEMO_PROTO_DIR}/video_frame.proto"
)
set(GRPC_DEMO_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# protoc 生成的全部源文件
set(GRPC_DEMO_GENERATED_SOURCES
    "${GRPC_DEMO_GENERATED_DIR}/hello.pb.cc"
    "${GRPC_DEMO_GENERATED_DIR}/hello.grpc.pb.cc"
    "${GRPC_DEMO_GENERATED_DIR}/video_frame.pb.cc"
    "${GRPC_DEMO_GENERATED_DIR}/video_frame.grpc.pb.cc"
)

# protoc 生成的全部头文件
set(GRPC_DEMO_GENERATED_HEADERS
    "${GRPC_DEMO_GENERATED_DIR}/hello.pb.h"
    "${GRPC_DEMO_GENERATED_DIR}/hello.grpc.pb.h"
    "${GRPC_DEMO_GENERATED_DIR}/video_frame.pb.h"
    "${GRPC_DEMO_GENERATED_DIR}/video_frame.grpc.pb.h"
)

# 自定义命令：运行 protoc 生成 C++ gRPC 源码
add_custom_command(
    OUTPUT
        ${GRPC_DEMO_GENERATED_SOURCES}
        ${GRPC_DEMO_GENERATED_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GRPC_DEMO_GENERATED_DIR}"
    COMMAND protobuf::protoc
    ARGS
        --proto_path=${GRPC_DEMO_PROTO_DIR}
        --cpp_out=${GRPC_DEMO_GENERATED_DIR}
        --grpc_out=${GRPC_DEMO_GENERATED_DIR}
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        ${GRPC_DEMO_PROTO_FILES}
    DEPENDS ${GRPC_DEMO_PROTO_FILES}
    COMMENT "生成 gRPC 演示源码"
    VERBATIM
)

# 静态库：protoc 生成的 proto 代码
add_library(grpc_demo_proto STATIC
    ${GRPC_DEMO_GENERATED_SOURCES}
    ${GRPC_DEMO_GENERATED_HEADERS}
)

target_include_directories(grpc_demo_proto
    PUBLIC
        "${GRPC_DEMO_GENERATED_DIR}"
)

target_link_libraries(grpc_demo_proto
    PUBLIC
        protobuf::libprotobuf
        gRPC::grpc++
)

if(MSVC)
    target_compile_options(grpc_demo_proto PRIVATE /utf-8)
endif()

# 静态库：gRPC 核心封装（客户端、服务端、帧抓取客户端）
add_library(grpc_core STATIC
    src/grpc_client.cpp
    src/grpc_server.cpp
    src/frame_ingest_client.cpp
)

target_include_directories(grpc_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(grpc_core
    PUBLIC
        grpc_demo_proto
        gRPC::grpc++
)

if(MSVC)
    target_compile_options(grpc_core PRIVATE /utf-8)
endif()

if(MSVC)
    target_compile_options(grpc_core PRIVATE /utf-8)
endif()

# 演示程序（可选）
if(BUILD_GRPC_DEMOS)
    # 帧发送演示
    add_executable(grpc_frame_sender_demo
        demo/frame_sender_demo.cpp
    )

    target_link_libraries(grpc_frame_sender_demo
        PRIVATE
            grpc_core
    )

    # Hello 服务演示
    add_executable(grpc_hello_demo
        demo/hello_demo.cpp
        demo/hello_grpc_service.cpp
    )

    target_include_directories(grpc_hello_demo
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/demo"
    )

    target_link_libraries(grpc_hello_demo
        PRIVATE
            grpc_core
    )

    if(MSVC)
        target_compile_options(grpc_frame_sender_demo PRIVATE /utf-8)
        target_compile_options(grpc_hello_demo PRIVATE /utf-8)
    endif()
endif()

if(MSVC)
    target_compile_options(grpc_demo_proto PRIVATE /utf-8)
    target_compile_options(grpc_core PRIVATE /utf-8)
endif()


if(MSVC)
    target_compile_options(grpc_demo_proto PRIVATE /utf-8)
    target_compile_options(grpc_core PRIVATE /utf-8)
endif()